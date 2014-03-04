#include "filesys/cache.h"
#include <stdbool.h>
#include "devices/block.h"
#include <list.h>
#include "threads/synch.h"

#define CACHE_SIZE 64          /* the buffer cache size */
#define WRITE_BEHIND_CYCLE 30 * 1000    /* write-behind happens every 30 sec */
#define INVALID_SECTOR_ID (block_sector_t)-1
#define INVALID_ENTRY_INDEX -1

/* structure for cache entry */
struct cache_entry
{
  block_sector_t sector_id;        /* sector id */
  block_sector_t next_sector_id;   /* keep track of sector id to be loaded after flush */
  bool dirty;                      /* whether the cache entry is dirty */
  bool accessed;                   /* whether the cache entry is accessed recently */
  bool flushing_out;               /* whether the cache entry is being flushed out */
  bool loading_in;                 /* whether the cache entry is being loaded in */
  uint32_t writing_num;            /* the number of processes writing data */
  uint32_t reading_num;            /* the number of processes reading data */
  uint32_t wait_writing_num;       /* the number of processes waiting to write data */
  uint32_t wait_reading_num;       /* the number of processes waiting to read data */
  struct lock lock;                /* lock for the cache entry */
  struct condition ready;          /* condition var to indicate whether the cache entry is ready for read/write */
  uint8_t sector_data[BLOCK_SECTOR_SIZE]; /* the data in this sector */
};

static struct cache_entry buffer_cache[CACHE_SIZE];  /* the buffer cache */
static int clock_hand;                    /* clock hand for clock algorithm */
static struct lock buffer_cache_lock;  /* the global lock for buffer cache */

static struct list read_ahead_list;     /* the queue for read-ahead */
static struct lock read_ahead_lock;     /* the lock for read_ahead_list */
static struct condition read_ahead_list_ready; /* condition var to indicate whether read_ahead_list is ready */

/* element structure in read_ahead_list */
struct read_ahead_elem
{
  block_sector_t sector_id;
  struct list_elem elem;
};



void flush_cache_entry(int entry_index, bool need_wait);
int get_entry_index(block_sector_t searching_sector_id);

/* initialize buffer cache */
void buffer_cache_init(void) {
	clock_hand = 0;

	lock_init(&buffer_cache_lock);

	int i;
	/* init each cache entry */
	for (i = 0; i < CACHE_SIZE; i++) {
		buffer_cache[i].sector_id = INVALID_SECTOR_ID;
		buffer_cache[i].next_sector_id = INVALID_SECTOR_ID;
		buffer_cache[i].dirty = false;
		buffer_cache[i].accessed = false;
		buffer_cache[i].flushing_out = false;
		buffer_cache[i].loading_in = false;
		buffer_cache[i].writing_num = 0;
		buffer_cache[i].reading_num = 0;
		buffer_cache[i].wait_reading_num = 0;
		buffer_cache[i].wait_writing_num = 0;
		lock_init(&buffer_cache[i].lock);
		cond_init(&buffer_cache[i].ready);
		//TODO: init sector_data ?
	}


	lock_init(&read_ahead_lock);
	list_init(&read_ahead_list);
	cond_init(&read_ahead_list_ready);
	//TODO: create write behind thread
	//TODO: create read ahead thread

}

/*search a given sector_id in buffer cache,
 * return the index in the buffer cache if found*/
int get_entry_index(block_sector_t searching_sector_id) {

	int i;
	for (i = 0; i < CACHE_SIZE; i++) {
		/*find a cache entry's sector_id matching the searching_sector_id*/
		if (buffer_cache[i].sector_id == searching_sector_id) {
			lock_acquire(&buffer_cache[i].lock);
			/*wait until flushing is done*/
			while (buffer_cache[i].flushing_out) {
				cond_wait(&buffer_cache[i].ready, &buffer_cache[i].lock);
			}
			if (buffer_cache[i].sector_id == searching_sector_id) {
				lock_release(&buffer_cache[i].lock);
				return i;
			} else {
				lock_release(&buffer_cache[i].lock);
				return INVALID_ENTRY_INDEX;
			}

		} else if (buffer_cache[i].next_sector_id == searching_sector_id) {
			/*find a cache entry's next_sector_id matching the searching_sector_id*/
			lock_acquire(&buffer_cache[i].lock);
			/*wait until flushing and loading are done*/
			while (buffer_cache[i].flushing_out || buffer_cache[i].loading_in) {
				cond_wait(&buffer_cache[i].ready, &buffer_cache[i].lock);
			}
			if (buffer_cache[i].next_sector_id == searching_sector_id) {
				lock_release(&buffer_cache[i].lock);
				return i;
			} else {
				lock_release(&buffer_cache[i].lock);
				return INVALID_ENTRY_INDEX;
			}
		}
	}
	return INVALID_ENTRY_INDEX;
}

/* flush the data in the cache entry to disk */
void flush_cache_entry(int entry_index, bool need_wait) {
	bool holding_global_lock = false;

	ASSERT (entry_index >= 0 && entry_index < CACHE_SIZE);
	lock_acquire(&buffer_cache[entry_index].lock);

	ASSERT(buffer_cache[entry_index].dirty);
	/*if some process is writing or waiting to write into this entry
	 * or the entry is being loaded or flushed,
	 * and no need to wait, return immediately*/
	if ((buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num > 0
			|| buffer_cache[entry_index].flushing_out
			|| buffer_cache[entry_index].loading_in) && !need_wait) {
		lock_release(&buffer_cache[entry_index].lock);
		return;
	}

	/* if no process is writing or waiting to write and not in loading or
	 * flushing, call block_write and return*/
	if (buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num == 0
			&& !buffer_cache[entry_index].flushing_out
			&& !buffer_cache[entry_index].loading_in) {
		if (lock_held_by_current_thread(&buffer_cache_lock)) {
			holding_global_lock = true;
			lock_release(&buffer_cache_lock);
		}
		lock_release(&buffer_cache[entry_index].lock);
		block_write(fs_device, buffer_cache[entry_index]->sector_id, buffer_cache[entry_index].sector_data);
---------------->
	}


}







