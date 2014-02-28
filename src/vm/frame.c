#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "userprog/syscall.h"
#include <list.h>

/* List of all frame_table_entry. */
static struct list frame_table;
static struct lock frame_table_lock;
struct list_elem *clock_hand;  /*current frame_table_entry the clock algorithm is pointing to*/


struct frame_table_entry *
create_fte(struct thread* t,uint8_t *frame_addr,struct supplemental_pte* spte);
struct frame_table_entry *
evict_frame(struct supplemental_pte *spte);


/*init frame table*/
void frame_table_init(){
	  clock_hand = NULL;
	  lock_init (&frame_table_lock);
	  list_init (&frame_table);
}

/*get a frame, and generate a correspinding frame_table_entry*/
struct frame_table_entry*
get_frame (struct supplemental_pte *spte)
{
  /* get a physical address of a free frame*/
  uint8_t *frame_addr = palloc_get_page (PAL_USER);

  if (frame_addr != NULL)
  {
    /* create a frame_table_entry and return */
    return create_fte (thread_current (), frame_addr, spte);
  } else {
    /* no available frame, need to evict one  */
	  return evict_frame(spte);
  }
}


struct frame_table_entry *
evict_frame(struct supplemental_pte *spte){
	lock_acquire (&frame_table_lock);
	struct list_elem *e;
	struct frame_table_entry *fte;
	struct thread* cur=thread_current();

	if (clock_hand == NULL) {
		clock_hand = list_begin (&frame_table);
	}

	/* choose the frame to evict using "second-chance" algorithm */
	while(true) {
		if (clock_hand == list_end (&frame_table)) {
			clock_hand = list_begin (&frame_table);
		}
		fte = list_entry (clock_hand, struct frame_table_entry, elem);
		if(fte->accessed || fte->pinned || fte->spte->type_code == SPTE_CODE_SEG) {
			fte->accessed = false;
			clock_hand = list_next (clock_hand);
		} else {

			fte->accessed = true;
			clock_hand = list_next (clock_hand);
			break;
		}
	}

	bool already_hold_lock_old=false;
	bool already_hold_lock=false;
	if(fte!=NULL && !fte->pinned){
		if(!lock_held_by_current_thread (&fte->spte->lock)){
			lock_acquire(&fte->spte->lock);
		}else{
			already_hold_lock_old=true;
		}

		if(!lock_held_by_current_thread (&spte->lock)){
			lock_acquire(&spte->lock);
		}else{
			already_hold_lock=true;
		}


		fte->spte->fte=NULL;

		/*pin the fte to avoid IO conflict, need to unpin outside*/
		fte->pinned=true;
		/* swap out the frame swap pool */
		if(fte->spte->type_code != SPTE_MMAP){
			swap_out(fte);
		}

		/*if the block is dirty, write it back to disk*/
		struct supplemental_pte *old_spte=fte->spte;
		struct file* file;
		bool is_dirty = pagedir_is_dirty (fte->t->pagedir, old_spte->uaddr);
		if (is_dirty && old_spte->writable && old_spte->type_code == SPTE_MMAP) {
			file = old_spte->f;
			off_t ofs = old_spte->offset;
			off_t page_write_bytes = PGSIZE-old_spte->zero_bytes;
			lock_acquire(&filesys_lock);
			fte->accessed = true;
			file_seek(file, ofs);
			file_write(file, fte->frame_addr, page_write_bytes);
			lock_release(&filesys_lock);
		}

		/*update the page table*/
		pagedir_clear_page(fte->t->pagedir,fte->spte->uaddr);

		/*put the fte into the tail*/
		list_remove(&fte->elem);
		list_push_back(&frame_table,&fte->elem);
		fte->accessed = true;
		fte->spte=spte;
		fte->t=thread_current();
		spte->fte=fte;
		if(!already_hold_lock_old){
			lock_release(&old_spte->lock);
		}
		if(!already_hold_lock){
			lock_release(&spte->lock);
		}

	}
	else{
		/*cannot find a frame to evict*/
		fte=NULL;
	}
	lock_release (&frame_table_lock);
	return fte;
}

struct frame_table_entry *
create_fte(struct thread* t,uint8_t *frame_addr,struct supplemental_pte* spte){
	struct frame_table_entry *fte=malloc(sizeof(struct frame_table_entry));
	fte->t=t;
	fte->frame_addr=frame_addr;
	fte->spte=spte;
	fte->accessed = false;
	spte->fte=fte;
	/*pin the fte to avoid IO conflict, need to unpin outside*/

	fte->pinned=true;
	/*add the new entry into frame_table */
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table,&fte->elem);
	lock_release(&frame_table_lock);

	return fte;
}

/*free a frame_table_entry*/
bool
free_fte (struct frame_table_entry *fte)
{
  lock_acquire (&frame_table_lock);
  if(fte->pinned){
	  /*cannot deallocate when it is pinned*/
	  lock_release (&frame_table_lock);
	  return false;
  }

  /* update clock hand */
  if (&fte->elem == clock_hand) {
	  clock_hand = list_next (clock_hand);
	  if (clock_hand == list_end(&frame_table)) {
		  clock_hand = list_begin(&frame_table);
	  }
  }

  /*clean up*/
  list_remove (&fte->elem);
  palloc_free_page(fte->frame_addr);
  free(fte);

  lock_release (&frame_table_lock);

  return true;
}
