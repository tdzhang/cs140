#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static void free_map_release_all_direct(struct inode_disk *disk_inode);
static void free_map_release_all_single_indirect(struct indirect_block *ib);
static void free_map_release_direct(struct inode_disk *disk_inode, int end_idx);
static void free_map_release_single_indirect(struct indirect_block *ib, int end_idx);
static void free_map_release_double_indirect (struct indirect_block *db, int double_level_end_idx, int single_level_end_idx);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector_no_check (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

	/* sector_pos starts from 0 */
	off_t sector_pos = pos/BLOCK_SECTOR_SIZE;

	struct inode_disk id;
	cache_read(inode->sector, INVALID_SECTOR_ID, &id, 0, BLOCK_SECTOR_SIZE);

	/*sector_pos in the range of direct index*/
	if (sector_pos < DIRECT_INDEX_NUM) {
		return id.direct_idx[sector_pos];
	}

	/*sector_pos in the range of single indirect index*/
	if (sector_pos < DIRECT_INDEX_NUM+INDEX_PER_SECTOR) {
		static struct indirect_block ib;
		cache_read(id.single_idx, INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
		return ib.sectors[sector_pos-DIRECT_INDEX_NUM];
	}

	/*sector_pos in the range of double indirect index*/
	off_t double_level_idx = (sector_pos-(DIRECT_INDEX_NUM+INDEX_PER_SECTOR)) / INDEX_PER_SECTOR;
	off_t single_level_idx = (sector_pos-(DIRECT_INDEX_NUM+INDEX_PER_SECTOR)) % INDEX_PER_SECTOR;
	static struct indirect_block db;
	cache_read(id.double_idx, INVALID_SECTOR_ID, &db, 0, BLOCK_SECTOR_SIZE);
	static struct indirect_block ib;
	cache_read(db.sectors[double_level_idx], INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
	return ib.sectors[single_level_idx];
}

static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  if (pos < inode->readable_length) {
	  return byte_to_sector_no_check (inode, pos);
  }
  else
    return INVALID_SECTOR_ID;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock; /*lock for open_inodes*/

/* Initializes the inode module. */
void
inode_init (void) 
{
  lock_init (&open_inodes_lock);
  list_init (&open_inodes);
}


/* set the new_sector to the first non-allocated sector in the inode
 * must acquire inode lock before calling it */
bool append_sector_to_inode(struct inode_disk *id, block_sector_t new_sector) {
	int sectors = (int)bytes_to_sectors(id->length);
	static struct indirect_block ib;
	static struct indirect_block db;
	if (sectors <= DIRECT_INDEX_NUM) {
		if (sectors < DIRECT_INDEX_NUM) {
			id->direct_idx[sectors] = new_sector;
		} else {
			if (!free_map_allocate (1, &id->single_idx)) {
				return false;
			}
			ib.sectors[0] = new_sector;
			cache_write(id->single_idx, &ib, 0, BLOCK_SECTOR_SIZE);
		}
	} else if (sectors <= DIRECT_INDEX_NUM+INDEX_PER_SECTOR) {
		if (sectors < DIRECT_INDEX_NUM+INDEX_PER_SECTOR) {
			cache_read(id->single_idx, INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
			ib.sectors[sectors-DIRECT_INDEX_NUM] = new_sector;
			cache_write(id->single_idx, &ib, 0, BLOCK_SECTOR_SIZE);
		} else {
			if (!free_map_allocate (1, &id->double_idx)) {
				return false;
			}
			static struct indirect_block single_ib;
			if (!free_map_allocate (1, &db.sectors[0])) {
				free_map_release (id->double_idx, 1);
				return false;
			}
			single_ib.sectors[0] = new_sector;
			cache_write(db.sectors[0], &single_ib, 0, BLOCK_SECTOR_SIZE);
			cache_write(id->double_idx, &db, 0, BLOCK_SECTOR_SIZE);
		}
	} else {
		size_t sectors_left=sectors - DIRECT_INDEX_NUM - INDEX_PER_SECTOR;
		if(sectors_left%INDEX_PER_SECTOR ==0){
			/*on the edge*/
			cache_read(id->double_idx, INVALID_SECTOR_ID, &db, 0, BLOCK_SECTOR_SIZE);
			if (!free_map_allocate (1, &db.sectors[sectors_left/INDEX_PER_SECTOR])) {
				return false;
			}
			ib.sectors[0]=new_sector;
			cache_write(db.sectors[sectors_left/INDEX_PER_SECTOR], &ib, 0, BLOCK_SECTOR_SIZE);
		}else{
			cache_read(id->double_idx, INVALID_SECTOR_ID, &db, 0, BLOCK_SECTOR_SIZE);
			cache_read(db.sectors[sectors_left/INDEX_PER_SECTOR],INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
			ib.sectors[sectors_left%INDEX_PER_SECTOR]=new_sector;
			cache_write(db.sectors[sectors_left/INDEX_PER_SECTOR], &ib, 0, BLOCK_SECTOR_SIZE);
		}

	}
	return true;
}


/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;

  ASSERT (length >= 0);
  ASSERT (length <= 251*512);
  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      int sectors = (int)bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;

      int i;
      block_sector_t sector_idx = 0;
      static char zeros[BLOCK_SECTOR_SIZE];
      bool allocate_failed = false;

      /* allocate sectors for data and write all zeros to sectors*/
      int direct_sector_num = sectors < DIRECT_INDEX_NUM ? sectors : DIRECT_INDEX_NUM;
      int indirect_sector_num = (sectors-DIRECT_INDEX_NUM) < INDEX_PER_SECTOR ? (sectors - DIRECT_INDEX_NUM) : INDEX_PER_SECTOR;
      int double_indirect_sector_num = sectors - DIRECT_INDEX_NUM - INDEX_PER_SECTOR;

      /* allocate direct sectors */
      for (i = 0; i < direct_sector_num; i++) {
    	  	  if (free_map_allocate (1, &sector_idx)) {
    	  		  disk_inode->direct_idx[i] = sector_idx;
    	  		  cache_write(sector_idx, zeros, 0, BLOCK_SECTOR_SIZE);
    	  	  } else {
    	  		  allocate_failed = true;
    	  		  break;
    	  	  }
      }
      /* release allocated direct sectors when failed to allocate */
      if (allocate_failed) {
    	  	  free_map_release_direct(disk_inode, i);
    	  	  free (disk_inode);
    	  	  return false;
      }

      static struct indirect_block ib;
      /* allocate single indirect sectors */
      if(indirect_sector_num > 0){
			if (!free_map_allocate (1, &disk_inode->single_idx)) {
				free_map_release_all_direct(disk_inode);
				free (disk_inode);
				return false;
			}

			for (i = 0; i < indirect_sector_num; i++) {
			  if (free_map_allocate (1, &sector_idx)) {
				  ib.sectors[i] = sector_idx;
				  cache_write(sector_idx, zeros, 0, BLOCK_SECTOR_SIZE);
			  } else {
				  allocate_failed = true;
				  break;
			  }
			}

			/* release all direct sectors and allocated single indirect sectors
			 * when failed to allocate */
			if (allocate_failed) {
				free_map_release_all_direct(disk_inode);
				free_map_release_single_indirect(&ib, i);
				free_map_release(disk_inode->single_idx, 1);
				free (disk_inode);
				return false;
			}

			cache_write(disk_inode->single_idx, &ib, 0, BLOCK_SECTOR_SIZE);
      }



      /* allocate double indirect sectors */
      if(double_indirect_sector_num > 0){
    	  	  if (!free_map_allocate (1, &disk_inode->double_idx)) {
    	  		  free_map_release_all_direct(disk_inode);
    	  		  free_map_release_all_single_indirect(&ib);
    	  		  free_map_release (disk_inode->single_idx, 1);
    	  		  free (disk_inode);
    	  		  return false;
		  }

    	  	  off_t double_level_end_idx = (double_indirect_sector_num-1) / INDEX_PER_SECTOR;
    	      off_t single_level_end_idx = (double_indirect_sector_num-1) % INDEX_PER_SECTOR;
    	      int i, j;
    	      /*double indirect index block*/
    	      static struct indirect_block db;
    	      /*buffer the single indirect index block in double indirect index block*/
    	      static struct indirect_block single_ib;
    	      /* allocate all full single indirect block */
    	      for (i = 0; i < double_level_end_idx; i++) {
			  if (!free_map_allocate (1, &db.sectors[i])){
				  free_map_release_all_direct(disk_inode);
				  free_map_release_all_single_indirect(&ib);
				  free_map_release (disk_inode->single_idx, 1);
				  free_map_release_double_indirect (&db, i, 0);
				  free_map_release (disk_inode->double_idx, 1);
				  free (disk_inode);
				  return false;
			  }

			  /* fully allocate the whole single indirect block */
    	    	  	  for (j = 0; j < INDEX_PER_SECTOR; j++) {
    	    	  		  if (free_map_allocate (1, &sector_idx)) {
    	    	  			  single_ib.sectors[j] = sector_idx;
    	    	  			  cache_write(sector_idx, zeros, 0, BLOCK_SECTOR_SIZE);
    	    	  		  } else {
    	    	  			  allocate_failed = true;
    	    	  			  break;
    	    	  		  }
    	    	  	  }


    	    	  	  if (allocate_failed) {
    	    	  		  free_map_release_all_direct(disk_inode);
    	    	  		  free_map_release_all_single_indirect(&ib);
				  free_map_release (disk_inode->single_idx, 1);
				  free_map_release_double_indirect (&db, i, j);
				  free_map_release (disk_inode->double_idx, 1);
				  free (disk_inode);
				  return false;
    	    	  	  }

    	    	  	  cache_write(db.sectors[i], &single_ib, 0, BLOCK_SECTOR_SIZE);
    	      }

    	      /* allocate the last partial/full single indirect block */
    	      if (!free_map_allocate (1, &db.sectors[double_level_end_idx])){
			  free_map_release_all_direct(disk_inode);
			  free_map_release_all_single_indirect(&ib);
			  free_map_release (disk_inode->single_idx, 1);
			  free_map_release_double_indirect (&db, double_level_end_idx, 0);
			  free_map_release (disk_inode->double_idx, 1);
			  free (disk_inode);
			  return false;
    	      }
    	      /* partially or fully (depend on single_level_end_idx) allocate the last single indirect block */
		  for (j = 0; j <= single_level_end_idx; j++) {
			  if (free_map_allocate (1, &sector_idx)) {
				  single_ib.sectors[j] = sector_idx;
				  cache_write(sector_idx, zeros, 0, BLOCK_SECTOR_SIZE);
			  } else {
				  allocate_failed = true;
				  break;
			  }
		  }


		  if (allocate_failed) {
			  free_map_release_all_direct(disk_inode);
			  free_map_release_all_single_indirect(&ib);
			  free_map_release (disk_inode->single_idx, 1);
			  free_map_release_double_indirect (&db, double_level_end_idx, j);
			  free_map_release (disk_inode->double_idx, 1);
			  free (disk_inode);
			  return false;
		  }

		  cache_write(db.sectors[double_level_end_idx], &single_ib, 0, BLOCK_SECTOR_SIZE);
		  /* update inode_disk(metadata) after successfully allocate all necessary sectors */
		  cache_write(disk_inode->double_idx, &db, 0, BLOCK_SECTOR_SIZE);
      }


      /* write inode_disk(metadata) to sector */
      cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
      free (disk_inode);
      return true;
    }
  return false;
}



/* help to free_map_release double indirect index sectors, from the beginning
 * to db[double_level_idx][single_level_idx] (exclusive) */
static void free_map_release_double_indirect (struct indirect_block *db, int double_level_end_idx, int single_level_end_idx) {
	int i;
	static struct indirect_block ib;

	for (i = 0; i < double_level_end_idx; i++) {
		cache_read(db->sectors[i], INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
		free_map_release_all_single_indirect(&ib);
	}

	for (i = 0; i < single_level_end_idx; i++) {
		cache_read(db->sectors[double_level_end_idx], INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
		free_map_release_single_indirect(&ib, single_level_end_idx);
	}

	free_map_release_single_indirect(db, (single_level_end_idx>0)?(double_level_end_idx+1):double_level_end_idx);
}

/* help to free_map_release all direct index sectors */
static void free_map_release_all_direct(struct inode_disk *disk_inode) {
	free_map_release_direct(disk_inode, DIRECT_INDEX_NUM);
}

/* help to free_map_release all single indirect index sectors */
static void free_map_release_all_single_indirect(struct indirect_block *ib) {
	free_map_release_single_indirect(ib, INDEX_PER_SECTOR);
}



/* help to free_map_release direct index sectors from beginning to end_idx (exclusive) */
static void free_map_release_direct(struct inode_disk *disk_inode, int end_idx) {
	int i;
	for (i = 0; i < end_idx; i++) {
		free_map_release(disk_inode->direct_idx[i], 1);
	}
}

/* help to free_map_release single indirect index sectors from beginning to end_idx (exclusive) */
static void free_map_release_single_indirect(struct indirect_block *ib, int end_idx){
	int i;
	for (i = 0; i < end_idx; i++) {
		free_map_release (ib->sectors[i], 1);
	}
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  ASSERT(!lock_held_by_current_thread (&open_inodes_lock));
  lock_acquire(&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_release(&open_inodes_lock);
          return inode; 
        }
    }
  lock_release(&open_inodes_lock);
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  ASSERT(!lock_held_by_current_thread (&open_inodes_lock));
  lock_acquire(&open_inodes_lock);
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->dir_lock);
  lock_init(&inode->inode_lock);
  /* retrieve inode_disk from sector */
  struct inode_disk id;
  cache_read(inode->sector, INVALID_SECTOR_ID, &id, 0, BLOCK_SECTOR_SIZE);
  inode->readable_length = id.length;
  inode->is_dir = id.is_dir;
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
	  ASSERT(!lock_held_by_current_thread (&inode->inode_lock));
	  lock_acquire(&inode->inode_lock);
	  inode->open_cnt++;
	  lock_release(&inode->inode_lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  ASSERT(!lock_held_by_current_thread (&open_inodes_lock));
  lock_acquire(&open_inodes_lock);
  bool holding_inode_lock = lock_held_by_current_thread (&inode->inode_lock);
  if (!holding_inode_lock) {
	  lock_acquire(&inode->inode_lock);
  }
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
      lock_release(&open_inodes_lock);

      /* Deallocate blocks if removed. */
      if (inode->removed) 
      {
    	  	  /* retrieve inode_disk(metadata) from sector */
          struct inode_disk id;
          cache_read(inode->sector, INVALID_SECTOR_ID, &id, 0, BLOCK_SECTOR_SIZE);

          int sectors = (int)bytes_to_sectors (id.length);

          int direct_sector_num = sectors < DIRECT_INDEX_NUM ? sectors : DIRECT_INDEX_NUM;
          int indirect_sector_num = (sectors - DIRECT_INDEX_NUM) < INDEX_PER_SECTOR ? (sectors - DIRECT_INDEX_NUM) : INDEX_PER_SECTOR;
          int double_indirect_sector_num = sectors - DIRECT_INDEX_NUM - INDEX_PER_SECTOR;

          int i;
          /* release data sectors */
          free_map_release_direct(&id, direct_sector_num);

          if (indirect_sector_num > 0){
        	  	  static struct indirect_block ib;
        	  	  cache_read(id.single_idx, INVALID_SECTOR_ID, &ib, 0, BLOCK_SECTOR_SIZE);
        	  	  free_map_release_single_indirect(&ib, indirect_sector_num);
        	  	  free_map_release (id.single_idx, 1);
          }

          if (double_indirect_sector_num > 0) {
        	  	  static struct indirect_block db;
        	  	  cache_read(id.double_idx, INVALID_SECTOR_ID, &db, 0, BLOCK_SECTOR_SIZE);
        	  	  off_t double_level_end_idx = (double_indirect_sector_num-1) / INDEX_PER_SECTOR;
        	  	  off_t single_level_end_idx = (double_indirect_sector_num-1) % INDEX_PER_SECTOR;
        	  	  free_map_release_double_indirect(&db, double_level_end_idx, single_level_end_idx+1);
        	  	  free_map_release (id.double_idx, 1);
          }


          /* release inode_disk(metadata) sector */
          free_map_release (inode->sector, 1);
      }

      if (lock_held_by_current_thread (&inode->inode_lock)) {
    	  	  lock_release(&inode->inode_lock);
      }
      free (inode);
  }

  if (lock_held_by_current_thread (&open_inodes_lock)) {
  	  lock_release(&open_inodes_lock);
  }
  if (inode != NULL) {
	  if (!holding_inode_lock && lock_held_by_current_thread (&inode->inode_lock)) {
		  lock_release(&inode->inode_lock);
	  }
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  ASSERT(!lock_held_by_current_thread (&inode->inode_lock));
  lock_acquire(&inode->inode_lock);
  inode->removed = true;
  lock_release(&inode->inode_lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if (size+offset > inode->readable_length) {
    	  return 0;
  }
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      //TODO: pass next_sector_id
      cache_read(sector_idx, INVALID_SECTOR_ID, buffer+bytes_read, sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}


/* padding zeros from start_pos (inclusive) to end_pos (exclusive) */
bool zero_padding(struct inode *inode, struct inode_disk *id, off_t start_pos, off_t end_pos) {
	ASSERT(lock_held_by_current_thread (&inode->inode_lock));
	static char zeros[BLOCK_SECTOR_SIZE];
	/* padding the first partial sector */
	if (start_pos % BLOCK_SECTOR_SIZE != 0) {
		block_sector_t eof_sector = byte_to_sector(inode, start_pos-1);
		off_t sector_ofs = start_pos % BLOCK_SECTOR_SIZE;
		size_t zero_bytes = BLOCK_SECTOR_SIZE - sector_ofs;
		cache_write(eof_sector, zeros, sector_ofs, zero_bytes);
	}

	/* padding full sectors until end_pos-1 */
	int extra_sectors = (int)bytes_to_sectors(end_pos)-(int)bytes_to_sectors(start_pos);
	off_t* record_sectors=malloc(sizeof(off_t) * extra_sectors);
	off_t i,j;
	block_sector_t new_sector=-1;
	for(i=0;i<extra_sectors;i++){
		if (!free_map_allocate (1, &new_sector)) {
			for(j=0;j<i;j++){
				free_map_release(record_sectors[i],1);
			}
			free(record_sectors);
			return false;
		}
		if(!append_sector_to_inode(id,new_sector)){
			for(j=0;j<i;j++){
				free_map_release(record_sectors[i],1);
			}
			free(record_sectors);
			return false;
		}
		cache_write(new_sector, zeros, 0, BLOCK_SECTOR_SIZE);
		record_sectors[i]=new_sector;
		id->length += BLOCK_SECTOR_SIZE;
	}
	/*update the physical length info*/
	id->length=end_pos;
	cache_write(inode->sector, id, 0, BLOCK_SECTOR_SIZE);
	free(record_sectors);
	return true;

}




/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  off_t len_within, len_extend;
  struct inode_disk id;
  lock_acquire(&inode->inode_lock);
  cache_read(inode->sector, INVALID_SECTOR_ID, &id, 0, BLOCK_SECTOR_SIZE);
  off_t phy_length = id.length;
  if (offset + size > phy_length) {
	  if(!zero_padding(inode, &id, phy_length, offset+size)){
		  lock_release(&inode->inode_lock);
		  return 0;
	  }
  }


  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector_no_check (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = id.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_write(sector_idx, (void *)(buffer+bytes_written), sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  inode->readable_length=id.length;
  lock_release(&inode->inode_lock);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  ASSERT(!lock_held_by_current_thread (&inode->inode_lock));
  lock_acquire(&inode->inode_lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->inode_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT(!lock_held_by_current_thread (&inode->inode_lock));
  lock_acquire(&inode->inode_lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->inode_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->readable_length;
}


void inode_flush_cache(void) {
	force_flush_all_cache();
}
