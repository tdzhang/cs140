#include "vm/page.h"
#include "threads/vaddr.h"
#include <hash.h>

bool load_file(struct supplemental_pte *spte);

bool try_load_page(void* fault_addr){
	ASSERT (fault_addr < PHYS_BASE);
	/* Round down to nearest page boundary. */
	uint8_t* target_addr = (uint8_t*)pg_round_down (fault_addr);
	bool result = false;
	struct thread* cur= thread_current();

	/*creat key elem for searching*/
	struct supplemental_pte key;
	key.uaddr=target_addr;

	lock_acquire(&cur->supplemental_pt_lock);
	struct hash_elem *e = hash_find (&cur->supplemental_pt, &key.elem);

	if(e==NULL){
		/*if not found, return false*/
		lock_release(&cur->supplemental_pt_lock);
		return false;
	}

	/*get the entry and release lock*/
	struct supplemental_pte *spte = hash_entry (e, struct supplemental_pte, elem);
	lock_acquire(&spte->lock);
	lock_release(&cur->supplemental_pt_lock);

	//TODO: actually try to load/swap according to the type_code
	if (spte->type_code == SPTE_FILE) {
		/* load file from disk into frame */
		result = load_file(spte);
	} else if (spte->type_code == SPTE_IN_SWAP) {
		//TODO: swap into memory
	} else {
		PANIC("invalid spte type_code!");
	}

	lock_release(&spte->lock);

	return result;
}

bool load_file(struct supplemental_pte *spte) {
	ASSERT(spte != NULL);

	struct frame_table_entry *fte = get_frame(spte);
	if (fte == NULL) {
		return false;
	}

	struct file *f = spte->f;
	off_t offset = spte->offset;
	size_t zero_bytes = spte->zero_bytes;
	size_t read_bytes = PGSIZE - zero_bytes;

	if (file_read (f, fte->frame_addr, read_bytes) != read_bytes) {
		palloc_free_page (fte->frame_addr);
		return false;
	}
	memset(fte->frame_addr+read_bytes, 0, read_bytes);

}



/* Get a page of memory. */
     uint8_t *kpage = palloc_get_page (PAL_USER);
     if (kpage == NULL)
       return false;

     /* Load this page. */
     if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
       {
         palloc_free_page (kpage);
         return false;
       }
     memset (kpage + page_read_bytes, 0, page_zero_bytes);

     /* Add the page to the process's address space. */
     if (!install_page (upage, kpage, writable))
       {
         palloc_free_page (kpage);
         return false;
       }


struct supplemental_pte {
	  uint8_t type_code;		/* type of this spte entry to find the content */
	  //TODO: load_segment need to set type code to file_type(which need to be defined)
	  uint8_t *uaddr;		/* virtual address of the page */
	  bool writable;		    /* if the page is writable */

	  struct file *f;		/* file pointer to the file in filesystem */
	  off_t offset;			/* current offset in the file */
	  size_t zero_bytes;		/* zero bytes number in this page */

	  struct hash_elem elem;	/* hash elem for the spte in thread's hash table */

	  struct lock lock; /*lock for this struct*/

};


