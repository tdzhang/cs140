#include "vm/frame.h"

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

  /* get a physical address of a free frame*/
  uint8_t *frame_addr = palloc_get_page (PAL_USER);

  if (frame_addr != NULL)
  {
    /* create a frame_table_entry and return */
    return create_fte (thread_current (), spte, frame_addr);
  } else {
    /* no available frame, need to evict one  */
   //TODO: evict a frame, and return the address
	  return NULL;
  }
}

struct frame_table_entry *
create_fte(struct thread* t,uint8_t *frame_addr,struct supplemental_pte* spte){
	struct frame_table_entry *fte=malloc(sizeof(struct frame_table_entry));
	fte->t=t;
	fte->frame_addr=frame_addr;
	fte->spte=spte;

	/*add the new entry into frame_table */
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table,&fte->elem);
	lock_release(&frame_table_lock);

	return fte;
}

struct frame_table_entry
{
  struct thread *t;		/* the thread who own this entry */
  uint8_t *frame_addr;		/* the actual frame address */
  struct list_elem elem;	/* Linked list of frame entries */
  bool pinned;			/* whether this frame is pinned  */
  struct supplemental_pte* spte; /*the corresponding spte*/
};
