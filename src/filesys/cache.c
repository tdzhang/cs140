#include "filesys/cache.h"
#include <stdbool.h>
#include <list.h>
#include <string.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "devices/timer.h"
#include <debug.h>

#define CACHE_SIZE 64          /* the buffer cache size */
#define WRITE_BEHIND_CYCLE (int64_t)(30 * 1000)    /* write-behind happens every 30 sec */
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



bool flush_cache_entry(int entry_index, bool need_wait);
int get_entry_index(block_sector_t searching_sector_id);
int evict_cache_entry(void);
bool load_cache_entry(int entry_index, block_sector_t sector_id, bool need_wait);
int switch_cache_entry(block_sector_t new_sector, bool need_wait);
static inline void clock_next(void);
static void read_ahead_daemon(void *aux UNUSED);
static void write_behind_daemon(void *aux UNUSED);
static void trigger_read_ahead(block_sector_t sector_id);

/* initialize buffer cache */
bool buffer_cache_init(void) {
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
	}


	/*create write-behind daemon thread*/
	/*
	tid_t write_t = thread_create ("write_behind_daemon", PRI_DEFAULT,
			write_behind_daemon, NULL);
	if (write_t == TID_ERROR) return false;
	*/
	lock_init(&read_ahead_lock);
	list_init(&read_ahead_list);
	cond_init(&read_ahead_list_ready);
	/*create read-ahead daemon thread*/
	tid_t reader_t = thread_create ("read_ahead_daemon", PRI_DEFAULT,
            read_ahead_daemon, NULL);
	if (reader_t == TID_ERROR) return false;

	return true;
}

/*search a given sector_id in buffer cache,
 * return the index in the buffer cache if found
 * must holding buffer_cache_lock before calling it*/
int get_entry_index(block_sector_t searching_sector_id) {
	ASSERT (searching_sector_id != INVALID_SECTOR_ID);
	ASSERT(lock_held_by_current_thread(&buffer_cache_lock));
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
			if (buffer_cache[i].sector_id == searching_sector_id) {
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

/* flush the data in the cache entry to disk, return whether really flushed */
bool flush_cache_entry(int entry_index, bool need_wait) {
	ASSERT(buffer_cache[entry_index].sector_id != INVALID_SECTOR_ID);
	bool holding_global_lock = false;

	ASSERT (entry_index >= 0 && entry_index < CACHE_SIZE);
	ASSERT (lock_held_by_current_thread(&buffer_cache[entry_index].lock));

	ASSERT(buffer_cache[entry_index].dirty);
	/*if some process is writing or waiting to write into this entry
	 * and no need to wait, return immediately*/
	if ((buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num > 0
			|| buffer_cache[entry_index].flushing_out
			|| buffer_cache[entry_index].loading_in) && !need_wait) {
		return false;
	}

	/* if no process is writing or waiting to write, call block_write and return*/
	if (buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num == 0
			&& !buffer_cache[entry_index].flushing_out
			&& !buffer_cache[entry_index].loading_in) {
		/*release all locks it's holding in I/O period*/
		if (lock_held_by_current_thread(&buffer_cache_lock)) {
			holding_global_lock = true;
			lock_release(&buffer_cache_lock);
		}
		buffer_cache[entry_index].flushing_out = true;
		lock_release(&buffer_cache[entry_index].lock);
		block_write(fs_device, buffer_cache[entry_index].sector_id, buffer_cache[entry_index].sector_data);
		lock_acquire(&buffer_cache[entry_index].lock);
		buffer_cache[entry_index].dirty = false;
		buffer_cache[entry_index].flushing_out = false;
		cond_broadcast(&buffer_cache[entry_index].ready, &buffer_cache[entry_index].lock);
		if (holding_global_lock) {
			lock_acquire(&buffer_cache_lock);
		}
		return true;
	}

	/*if some process is writing or waiting to write into this entry
	 * and need to wait, wait until ready */
	if ((buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num > 0
			|| buffer_cache[entry_index].flushing_out
			|| buffer_cache[entry_index].loading_in) && need_wait) {
		while(buffer_cache[entry_index].wait_writing_num
				+buffer_cache[entry_index].writing_num > 0
				|| buffer_cache[entry_index].flushing_out
				|| buffer_cache[entry_index].loading_in) {
			cond_wait(&buffer_cache[entry_index].ready, &buffer_cache[entry_index].lock);
		}
		/* after waiting period, it is possible that the entry is not dirty at more */
		if (!buffer_cache[entry_index].dirty) {
			return true;
		}

		/*release all locks it's holding in I/O period*/
		if (lock_held_by_current_thread(&buffer_cache_lock)) {
			holding_global_lock = true;
			lock_release(&buffer_cache_lock);
		}
		buffer_cache[entry_index].flushing_out = true;
		lock_release(&buffer_cache[entry_index].lock);
		block_write(fs_device, buffer_cache[entry_index].sector_id, buffer_cache[entry_index].sector_data);
		lock_acquire(&buffer_cache[entry_index].lock);
		buffer_cache[entry_index].dirty = false;
		buffer_cache[entry_index].flushing_out = false;
		cond_broadcast(&buffer_cache[entry_index].ready, &buffer_cache[entry_index].lock);
		if (holding_global_lock) {
			lock_acquire(&buffer_cache_lock);
		}
		return true;
	}
	return false;
}

/* load data from disk to cache entry */
bool load_cache_entry(int entry_index, block_sector_t sector_id, bool need_wait) {
	bool holding_global_lock = false;

	ASSERT (entry_index >= 0 && entry_index < CACHE_SIZE);
	ASSERT (sector_id != INVALID_SECTOR_ID);
	ASSERT (lock_held_by_current_thread(&buffer_cache[entry_index].lock));

	ASSERT(!buffer_cache[entry_index].dirty);
	/*if some process is reading/writing or waiting to read/write from/to this entry
	 * and no need to wait, return immediately*/
	if ((buffer_cache[entry_index].wait_reading_num
			+buffer_cache[entry_index].reading_num
			+buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num > 0
			|| buffer_cache[entry_index].flushing_out
			|| buffer_cache[entry_index].loading_in) && !need_wait) {
		return false;
	}

	/* if no process is reading/writing or waiting to read/write call block_write and return*/
	if (buffer_cache[entry_index].wait_reading_num
			+buffer_cache[entry_index].reading_num
			+buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num == 0
			&& !buffer_cache[entry_index].flushing_out
			&& !buffer_cache[entry_index].loading_in) {
		/*release all locks it's holding in I/O period*/
		if (lock_held_by_current_thread(&buffer_cache_lock)) {
			holding_global_lock = true;
			lock_release(&buffer_cache_lock);
		}
		buffer_cache[entry_index].loading_in = true;
		lock_release(&buffer_cache[entry_index].lock);
		block_read(fs_device, buffer_cache[entry_index].next_sector_id, buffer_cache[entry_index].sector_data);
		lock_acquire(&buffer_cache[entry_index].lock);
		buffer_cache[entry_index].dirty = false;
		buffer_cache[entry_index].loading_in = false;
		cond_broadcast(&buffer_cache[entry_index].ready, &buffer_cache[entry_index].lock);
		if (holding_global_lock) {
			lock_acquire(&buffer_cache_lock);
		}
		return true;
	}

	/*if some process is reading/writing or waiting to read/write into this entry
	 * and need to wait, wait until ready */
	if ((buffer_cache[entry_index].wait_reading_num
			+buffer_cache[entry_index].reading_num
			+buffer_cache[entry_index].wait_writing_num
			+buffer_cache[entry_index].writing_num > 0
			|| buffer_cache[entry_index].flushing_out
			|| buffer_cache[entry_index].loading_in) && need_wait) {
		while(buffer_cache[entry_index].wait_reading_num
				+buffer_cache[entry_index].reading_num
				+buffer_cache[entry_index].wait_writing_num
				+buffer_cache[entry_index].writing_num > 0
				|| buffer_cache[entry_index].flushing_out
				|| buffer_cache[entry_index].loading_in) {
			cond_wait(&buffer_cache[entry_index].ready, &buffer_cache[entry_index].lock);
		}

		/*release all locks it's holding in I/O period*/
		if (lock_held_by_current_thread(&buffer_cache_lock)) {
			holding_global_lock = true;
			lock_release(&buffer_cache_lock);
		}
		buffer_cache[entry_index].loading_in = true;
		lock_release(&buffer_cache[entry_index].lock);
		block_write(fs_device, buffer_cache[entry_index].next_sector_id, buffer_cache[entry_index].sector_data);
		lock_acquire(&buffer_cache[entry_index].lock);
		buffer_cache[entry_index].dirty = false;
		buffer_cache[entry_index].loading_in = false;
		cond_broadcast(&buffer_cache[entry_index].ready, &buffer_cache[entry_index].lock);
		if (holding_global_lock) {
			lock_acquire(&buffer_cache_lock);
		}
		return true;
	}
	return false;
}

/* switch cache entry */
int switch_cache_entry(block_sector_t new_sector, bool need_wait) {
	ASSERT(lock_held_by_current_thread(&buffer_cache_lock));
/*
	if (!lock_held_by_current_thread(&buffer_cache_lock)) {
		lock_acquire(&buffer_cache_lock);
	}
*/
	bool need_flush = false;
	bool did_flushed = false;
	bool did_loaded = false;
	int slot = evict_cache_entry();
	ASSERT (slot >= 0 && slot < CACHE_SIZE);
	lock_acquire(&buffer_cache[slot].lock);
	buffer_cache[slot].next_sector_id = new_sector;
	if (buffer_cache[slot].dirty) {
		need_flush = true;
	}


	if (need_wait) {
		/*if need to wait, flush and load are both blocking call*/
		if (need_flush) {
			did_flushed = flush_cache_entry(slot, true);
			ASSERT(did_flushed);
		}
		did_loaded = load_cache_entry(slot, new_sector, true);
		ASSERT(did_loaded);
	} else {
		/*no need to wait, this may only happen in read-ahead*/
		if (need_flush) {
			did_flushed = flush_cache_entry(slot, false);
			if (!did_flushed) {
				/*not able to flush the dirty cache,
				 * return INVALID_ENTRY_INDEX*/
				buffer_cache[slot].next_sector_id = INVALID_SECTOR_ID;
				lock_release(&buffer_cache[slot].lock);
				return INVALID_ENTRY_INDEX;
			}
		}
		did_loaded = load_cache_entry(slot, new_sector, false);
		if (!did_loaded) {
			/*not able to load the cache, return INVALID_ENTRY_INDEX*/
			buffer_cache[slot].next_sector_id = INVALID_SECTOR_ID;
			lock_release(&buffer_cache[slot].lock);
			return INVALID_ENTRY_INDEX;
		}
	}

	/*succeeded to flush-load, update the cache and return its index*/
	buffer_cache[slot].sector_id = new_sector;
	buffer_cache[slot].next_sector_id = INVALID_SECTOR_ID;
	lock_release(&buffer_cache[slot].lock);

	return slot;
}

/* find a cache entry to be evict, return the index of the entry */
int evict_cache_entry(void) {
	int result;
	while(true) {
		/* move to next if the current entry if not ready */
		if (buffer_cache[clock_hand].wait_reading_num
			+buffer_cache[clock_hand].reading_num
			+buffer_cache[clock_hand].wait_writing_num
			+buffer_cache[clock_hand].writing_num > 0
			|| buffer_cache[clock_hand].flushing_out
			|| buffer_cache[clock_hand].loading_in) {
			clock_next();
			continue;
		}
		/* move to next if the current entry is accessed recently */
		if (buffer_cache[clock_hand].accessed) {
			buffer_cache[clock_hand].accessed = false;
			clock_next();
			continue;
		}

		/* find an entry to evict */
		result = clock_hand;
		clock_next();
		return result;
	}
	return INVALID_ENTRY_INDEX;
}

static inline void clock_next(void) {
	clock_hand = (clock_hand + 1) % CACHE_SIZE;
}


/* cache read */
off_t cache_read(block_sector_t sector, block_sector_t next_sector,
		void *buffer, off_t sector_offset, off_t read_bytes) {
	ASSERT (sector != INVALID_SECTOR_ID);
	lock_acquire(&buffer_cache_lock);

	int slot = get_entry_index(sector);
	if (slot == INVALID_ENTRY_INDEX) {
		slot = switch_cache_entry(sector, true);
	}
	if (slot == INVALID_ENTRY_INDEX) {
		lock_release(&buffer_cache_lock);
		return -1;
	}
	ASSERT (slot >= 0 && slot < CACHE_SIZE);
	lock_acquire(&buffer_cache[slot].lock);
	lock_release(&buffer_cache_lock);

	buffer_cache[slot].wait_reading_num ++;

	while (buffer_cache[slot].wait_writing_num
			+buffer_cache[slot].writing_num > 0
			|| buffer_cache[slot].loading_in) {
		cond_wait(&buffer_cache[slot].ready, &buffer_cache[slot].lock);
	}

	buffer_cache[slot].wait_reading_num --;
	buffer_cache[slot].reading_num ++;

	lock_release(&buffer_cache[slot].lock);
	/*memory copy from cached data to buffer*/
	memcpy (buffer, buffer_cache[slot].sector_data+sector_offset, read_bytes);

	lock_acquire(&buffer_cache[slot].lock);
	buffer_cache[slot].reading_num --;
	buffer_cache[slot].accessed = true;
	cond_broadcast(&buffer_cache[slot].ready, &buffer_cache[slot].lock);
	lock_release(&buffer_cache[slot].lock);

	/* trigger read-ahead, next_sector cannot be INVALID_SECTOR_ID
	 * or 0 (freemap sector) */
	if (next_sector != INVALID_SECTOR_ID
			&& next_sector != 0) {
		trigger_read_ahead(next_sector);
	}
	return read_bytes;
}




/* cache write */
off_t cache_write(block_sector_t sector, void *buffer,
		off_t sector_offset, off_t write_bytes) {
	ASSERT (sector != INVALID_SECTOR_ID);
	lock_acquire(&buffer_cache_lock);

	int slot = get_entry_index(sector);
	if (slot == INVALID_ENTRY_INDEX) {
		slot = switch_cache_entry(sector, true);
	}
	if (slot == INVALID_ENTRY_INDEX) {
		lock_release(&buffer_cache_lock);
		return -1;
	}
	ASSERT (slot >= 0 && slot < CACHE_SIZE);
	lock_acquire(&buffer_cache[slot].lock);
	lock_release(&buffer_cache_lock);

	buffer_cache[slot].wait_writing_num ++;

	while (buffer_cache[slot].writing_num
			+buffer_cache[slot].reading_num > 0
			|| buffer_cache[slot].flushing_out
			|| buffer_cache[slot].loading_in) {
		cond_wait(&buffer_cache[slot].ready, &buffer_cache[slot].lock);
	}

	buffer_cache[slot].wait_writing_num --;
	buffer_cache[slot].writing_num ++;

	lock_release(&buffer_cache[slot].lock);
	/*memory copy from cached data to buffer*/
	memcpy (buffer_cache[slot].sector_data+sector_offset, buffer, write_bytes);

	lock_acquire(&buffer_cache[slot].lock);
	buffer_cache[slot].writing_num --;
	buffer_cache[slot].accessed = true;
	buffer_cache[slot].dirty = true;
	cond_broadcast(&buffer_cache[slot].ready, &buffer_cache[slot].lock);
	lock_release(&buffer_cache[slot].lock);

	return write_bytes;
}


/* push sector to be loaded into read_ahead_list and trigger read-ahead */
static void trigger_read_ahead(block_sector_t sector_id) {
	struct read_ahead_elem *e = malloc (sizeof (struct read_ahead_elem));
	if (e == NULL) {
		return;
	}
	e->sector_id = sector_id;
	lock_acquire (&read_ahead_lock);
	list_push_back (&read_ahead_list, &e->elem);
	cond_signal (&read_ahead_list_ready, &read_ahead_lock);
	lock_release (&read_ahead_lock);
}



/* read ahead daemon for asynchronously read from disk to cache */
static void read_ahead_daemon(void *aux UNUSED) {
	struct read_ahead_elem *e;
	int slot = INVALID_ENTRY_INDEX;
	while(true) {
		lock_acquire(&read_ahead_lock);
		while(list_empty(&read_ahead_list)) {
			cond_wait(&read_ahead_list_ready, &read_ahead_lock);
		}

		e = list_entry (list_pop_front (&read_ahead_list),
				struct read_ahead_elem, elem);
		lock_release(&read_ahead_lock);

		/* try to load sector into cache */
		lock_acquire(&buffer_cache_lock);
		slot = get_entry_index(e->sector_id);
		if (slot != INVALID_ENTRY_INDEX) {
			/* the next sector is already in cache, no need to load again */
			ASSERT (slot >= 0 && slot < CACHE_SIZE);
			lock_acquire(&buffer_cache[slot].lock);
			lock_release(&buffer_cache_lock);
			buffer_cache[slot].accessed = true;
			lock_release(&buffer_cache[slot].lock);
			continue;
		}

		/* try to flush and load */
		slot = switch_cache_entry(e->sector_id, false);
		if (slot == INVALID_ENTRY_INDEX) {
			/*flush-load is not really done*/
			/*push back to the list to read-ahead later*/
			list_push_back(&read_ahead_list, &e->elem);
			lock_release(&buffer_cache_lock);
			continue;
		}
		/*read-ahead succeeded, set accessed flag and
		 * free the read_ahead_elem*/
		ASSERT (slot >= 0 && slot < CACHE_SIZE);
		lock_acquire(&buffer_cache[slot].lock);
		lock_release(&buffer_cache_lock);
		buffer_cache[slot].accessed = true;
		lock_release(&buffer_cache[slot].lock);
		free(e);
	}
}

/* write behind daemon for asynchronously flush dirty cache to disk */
static void write_behind_daemon(void *aux UNUSED) {
	int i;
	bool holding_lock;
	while(true) {
		timer_msleep(WRITE_BEHIND_CYCLE);
		for (i = 0; i < CACHE_SIZE; i++) {
			holding_lock = lock_held_by_current_thread(&buffer_cache[i].lock);
			if (!holding_lock)
				lock_acquire (&buffer_cache[i].lock);
			if (buffer_cache[i].dirty) {
				flush_cache_entry (i, false);
			}
			if (!holding_lock)
				lock_release (&buffer_cache[i].lock);
		}
	}
}


/* flush all cache entries to disk, used when filesys is done*/
void force_flush_all_cache(void) {
	int i;
	bool holding_lock;
	for (i = 0; i < CACHE_SIZE; i++) {
		holding_lock = lock_held_by_current_thread(&buffer_cache[i].lock);
		if (!holding_lock)
			lock_acquire (&buffer_cache[i].lock);
		if (buffer_cache[i].dirty) {
			flush_cache_entry (i, true);
		}
		if (!holding_lock)
			lock_release (&buffer_cache[i].lock);
	}
}







