#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include <stdint.h>
#include <list.h>

struct list swap_space_pool;  /* swap table */
struct lock swap_space_pool_lock; /* the lock of swap table */
struct swap_page_block {
	block_sector_t block_sector_head;
	struct list_elem elem;
};

void swap_pool_init();
void swap_out(struct frame_table_entry *fte);
void swap_in(struct frame_table_entry *fte, struct swap_page_block *spb);

#endif /* vm/swap.h */
