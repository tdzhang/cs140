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


struct frame_table_entry *
create_fte(struct thread* t,uint8_t *frame_addr,struct supplemental_pte* spte);
struct frame_table_entry *
evict_frame(struct supplemental_pte *spte);


/*init frame table*/
void frame_table_init(){
	  lock_init (&frame_table_lock);
	  list_init (&frame_table);
}

/*get a frame, and generate a correspinding frame_table_entry*/
struct frame_table_entry*
get_frame (struct supplemental_pte *spte)
{
	ASSERT(spte != NULL);
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
	/*
	for (e = list_begin (&frame_table); e != list_end (&frame_table);
						  e = list_next (e)) {
				fte = list_entry (e, struct frame_table_entry, elem);
				if(!fte->pinned)break;
	}
	*/

	//TODO: clock algorithm
	for (e = frame_table.tail.prev; e != &frame_table.head;
								  e = list_prev (e)) {
		fte = list_entry (e, struct frame_table_entry, elem);
		if(!fte->pinned && fte->spte->type_code != SPTE_CODE_SEG)break;
	}

	if(fte!=NULL && !fte->pinned){
		fte->spte->fte=NULL;

		/*pin the fte to avoid IO conflict, need to unpin outside*/
		fte->pinned=true;
		/* swap out the frame swap pool */
		swap_out(fte);

		/*if the block is dirty, write it back to disk*/
		struct supplemental_pte *old_spte=fte->spte;
		struct file* file;
		bool is_dirty = pagedir_is_dirty (fte->t->pagedir, old_spte->uaddr);
		if (is_dirty && old_spte->writable && old_spte->type_code == SPTE_MMAP) {
			file = old_spte->f;
			off_t ofs = old_spte->offset;
			off_t page_write_bytes = PGSIZE-old_spte->zero_bytes;
			lock_acquire(&filesys_lock);
			file_seek(file, ofs);
			file_write(file, fte->frame_addr, page_write_bytes);
			lock_release(&filesys_lock);
		}

		/*update the page table*/
		pagedir_clear_page(fte->t->pagedir,fte->spte->uaddr);

		/*put the fte into the tail*/
		list_remove(&fte->elem);
		list_push_back(&frame_table,&fte->elem);
		fte->spte=spte;
		fte->t=thread_current();
		spte->fte=fte;

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
	ASSERT(t != NULL);
	struct frame_table_entry *fte=malloc(sizeof(struct frame_table_entry));
	ASSERT(fte != NULL);
	fte->t=t;
	fte->frame_addr=frame_addr;
	fte->spte=spte;
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
	ASSERT(fte != NULL);
  lock_acquire (&frame_table_lock);
  if(fte->pinned){
	  /*cannot deallocate when it is pinned*/
	  lock_release (&frame_table_lock);
	  return false;
  }

  /*clean up*/
  list_remove (&fte->elem);
  palloc_free_page(fte->frame_addr);
  free(fte);

  lock_release (&frame_table_lock);

  return true;
}
