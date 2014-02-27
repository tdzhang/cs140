#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <debug.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/off_t.h"

#define SPTE_FILE 1
#define SPTE_IN_SWAP 2
#define SPTE_STACK_INIT 3
#define SPTE_MMAP 4

bool try_load_page(void* fault_addr);


struct supplemental_pte {
	  uint8_t type_code;		/* type of this spte entry to find the content */
	  //TODO: load_segment need to set type code to file_type(which need to be defined)
	  uint8_t *uaddr;		/* virtual address of the page */
	  bool writable;		    /* if the page is writable */

	  struct file *f;		/* file pointer to the file in filesystem */
	  off_t offset;			/* current offset in the file */
	  size_t zero_bytes;		/* zero bytes number in this page */

	  struct hash_elem elem;	/* hash elem for the spte in thread's hash table */
	  struct frame_table_entry* fte;
	  struct lock lock; /*lock for this struct*/

};


#endif /* vm/page.h */
