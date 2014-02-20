#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include <list.h>

/* List of all frame_table_entry. */
static struct list frame_table;
static struct lock frame_table_lock;


struct frame_table_entry *
create_fte(struct thread* t,uint8_t *frame_addr,struct supplemental_pte* spte);


/*init frame table*/
void frame_table_init(){
	  lock_init (&frame_table_lock);
	  list_init (&frame_table);
}

/**/
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
   //TODO: evict a frame, and return the address
	  return NULL;
  }
}

struct frame_table_entry *
create_fte(struct thread* t,uint8_t *frame_addr,struct supplemental_pte* spte){
	ASSERT(t != NULL);
	struct frame_table_entry *fte=malloc(sizeof(struct frame_table_entry));
	ASSERT(fte != NULL);
	fte->t=t;
	fte->frame_addr=frame_addr;
	fte->spte=spte;

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
