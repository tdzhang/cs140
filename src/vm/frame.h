#ifndef VM_FRAME_H
#define VM_FRAME_H
#include <stdbool.h>
#include <stdint.h>
#include <debug.h>
#include <hash.h>
#include "threads/thread.h"
#include "vm/page.h"

void frame_table_init();
struct frame_table_entry* get_frame(struct supplemental_pte *spte);
bool free_fte (struct frame_table_entry *fte);

struct frame_table_entry
{
  struct thread *t;		/* the thread who own this entry */
  uint8_t *frame_addr;		/* the actual frame address */
  struct list_elem elem;	/* Linked list of frame entries */
  bool pinned;			/* whether this frame is pinned  */
  struct supplemental_pte* spte; /*the corresponding spte*/

};

#endif
