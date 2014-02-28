#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include <stdint.h>
#include <list.h>
#include "vm/page.h"
#include "vm/frame.h"

void swap_pool_init(void);
void swap_out(struct frame_table_entry *fte);
void swap_in(struct frame_table_entry *fte, struct swap_page_block *spb);

struct swap_page_block {
	block_sector_t block_sector_head;
	struct list_elem elem;
};



#endif /* vm/swap.h */
