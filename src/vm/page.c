#include "vm/page.h"
#include "threads/vaddr.h"
#include <hash.h>


bool try_load_page(void* fault_addr){
	ASSERT (fault_addr < PHYS_BASE);
	/* Round down to nearest page boundary. */
	uint8_t* target_addr = (uint8_t*)pg_round_down (fault_addr);

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

	lock_release(&spte->lock);




	return true;
}


