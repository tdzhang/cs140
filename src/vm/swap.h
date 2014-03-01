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
void put_back_spb(struct swap_page_block *spb);

struct swap_page_block {
	block_sector_t block_sector_head;  /*starting block sector in disk*/
	struct list_elem elem;             /*list elem of list swap_space_pool*/
};



#endif /* vm/swap.h */
