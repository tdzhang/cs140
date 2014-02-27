#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include <stdint.h>
#include <list.h>



#include "vm/page.h"
#include "vm/frame.h"
#include <debug.h>

struct swap_page_block {
	block_sector_t block_sector_head;
	struct list_elem elem;
};

void swap_pool_init();
void swap_out(struct frame_table_entry *fte);
void swap_in(struct frame_table_entry *fte, struct swap_page_block *spb);


#define BLOCKS_UNIT_NUMBER 8

struct list swap_space_pool;  /* swap table */
struct lock swap_space_pool_lock; /* the lock of swap table */

struct block *swap_block;
struct swap_page_block *get_free_spb();
void put_back_spb(struct swap_page_block *spb);


/* swap pool init */
void swap_pool_init() {
	lock_init(&swap_space_pool_lock);
	list_init(&swap_space_pool);

	swap_block = block_get_role (BLOCK_SWAP);
	uint32_t swap_pool_size = block_size(swap_block);
	int i;
	struct swap_page_block *spb = NULL;
	/* populate the whole swap pool */
	lock_acquire(&swap_space_pool_lock);
	for (i = 0; i < swap_pool_size; i = i+BLOCKS_UNIT_NUMBER) {
		spb = malloc(sizeof(struct swap_page_block));
		spb->block_sector_head = (block_sector_t)i;
		list_push_back(&swap_space_pool, &spb->elem);
	}
	lock_release(&swap_space_pool_lock);
}

/* retrieve a free swap_page_block from the swap pool*/
struct swap_page_block *get_free_spb() {
	lock_acquire(&swap_space_pool_lock);
	if (list_empty(&swap_space_pool)) {
		lock_release(&swap_space_pool_lock);
		PANIC("NO FREE SWAP BLOCK");
	}
	struct swap_page_block *result = list_pop_front(&swap_space_pool);
	lock_release(&swap_space_pool_lock);
	return result;
}

/* put the swap_page_block back into the swap pool */
void put_back_spb(struct swap_page_block *spb) {
	ASSERT(spb != NULL);
	lock_acquire(&swap_space_pool_lock);
	list_push_back(&swap_space_pool, &spb->elem);
	lock_release(&swap_space_pool_lock);
}


/* swap in */
void swap_in(struct frame_table_entry *fte, struct swap_page_block *spb) {
	ASSERT(fte != NULL);
	ASSERT(spb != NULL);

	struct supplemental_pte *spte = fte->spte;
	ASSERT(spte != NULL);
	/* indicate the spte is not swapped */
	spte->spb = NULL;

	int i;
	for (i = 0; i < BLOCKS_UNIT_NUMBER; i++) {
		block_read(swap_block, i+spb->block_sector_head, (void *)(fte->frame_addr+i*BLOCK_SECTOR_SIZE));
	}
	/* put the swap_page_block back into the swap pool */
	put_back_spb(spb);
}





/* swap out */
void swap_out(struct frame_table_entry *fte) {
	ASSERT(fte != NULL);
	/* retrieve a free swap_page_block from the swap pool*/
	struct swap_page_block *spb = get_free_spb();
	struct supplemental_pte *spte = fte->spte;
	ASSERT(spte != NULL);
	/* indicate the spte is swapped */
	spte->spb = spb;
	int i;
	for (i = 0; i < BLOCKS_UNIT_NUMBER; i++) {
		block_write(swap_block, i+spb->block_sector_head, (void *)(fte->frame_addr+i*BLOCK_SECTOR_SIZE));
	}
}


#endif /* vm/swap.h */
