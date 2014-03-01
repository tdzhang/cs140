#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <debug.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/off_t.h"

/*these are the flag values stand for code 
 * segment, data segment, stack part and mmap part*/
#define SPTE_CODE_SEG 1     /*spte for code segment*/
#define SPTE_DATA_SEG 2     /*spte for data segment*/
#define SPTE_STACK_INIT 3   /*spte for stack*/
#define SPTE_MMAP 4         /*spte for mmap file*/

bool try_load_page(void* fault_addr);


struct supplemental_pte {
	  uint8_t type_code;		/* type of this spte entry to find the content */

	  uint8_t *uaddr;		/* virtual address of the page */
	  bool writable;		    /* if the page is writable */

	  struct file *f;		/* file pointer to the file in filesystem */
	  off_t offset;			/* current offset in the file */
	  size_t zero_bytes;		/* zero bytes number in this page */

	  struct hash_elem elem;	/* hash elem for the spte in thread's hash table */
	  struct frame_table_entry* fte;  /*corresponding frame in memory*/
	  struct lock lock;     /*lock for this struct*/
	  struct swap_page_block *spb;  /*swap location*/
};


#endif /* vm/page.h */
