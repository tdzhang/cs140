#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "filesys/filesys.h"
#include "devices/block.h"

#define INVALID_SECTOR_ID (block_sector_t)(-1)

bool buffer_cache_init(void);
off_t cache_read(block_sector_t sector, block_sector_t next_sector,
		void *buffer, off_t sector_offset, off_t read_bytes);
off_t cache_write(block_sector_t sector, void *buffer,
		off_t sector_offset, off_t write_bytes);


#endif
