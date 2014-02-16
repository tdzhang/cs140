#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <debug.h>
#include <hash.h>
#include "threads/thread.h"
#include "filesys/off_t.h"


struct supplemental_pte {
	  uint8_t type_code;		/* type of this spte entry to find the content */
	  uint8_t *uaddr;		/* virtual address of the page */
	  bool writable;		    /* if the page is writable */

	  struct file *f;		/* file pointer to the file in filesystem */
	  off_t offset;			/* current offset in the file */
	  size_t zero_bytes;		/* zero bytes number in this page */

	  struct hash_elem elem;	/* hash elem for the spte in thread's hash table */

	  //TODO: need a lock to handle pin etc.
	  /*-->need init in populate_spte in process.c  */
};

#endif /* vm/page.h */
