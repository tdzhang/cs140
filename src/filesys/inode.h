#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include <list.h>

struct bitmap;

#define DIRECT_INDEX_NUM 123    /* 122 direct indices in one sector*/
#define INDEX_PER_SECTOR 128    /* 128 indices stored in one sector */

/* structure for indirect index block */
struct indirect_block {
    block_sector_t sectors[INDEX_PER_SECTOR];
};


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    int is_dir;                         /* 1 if this inode is a dir,
                                                   0 otherwise. */
	block_sector_t direct_idx [DIRECT_INDEX_NUM];/* Direct index. */
	block_sector_t single_idx;                   /* Single indirect index. */
	block_sector_t double_idx;                   /* Double indirect index. */
  };



/* In-memory inode. */
struct inode {
     block_sector_t sector;           /* sector id */
     int open_cnt;                       /* Number of openers. */
     bool removed;                 		/* True if deleted, false otherwise. */
     int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
     off_t readable_length;              /* file size in bytes */
     bool is_dir;                        /* whether the inode is for a dir */
     struct lock inode_lock;             /* lock for the inode */
     struct lock dir_lock;               /* lock for the corresponding dir */
     struct list_elem elem;              /* Element in inode list. */
};





void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
