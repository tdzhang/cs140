#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include <hash.h>
#include "filesys/file.h"
#include "userprog/syscall.h"

bool load_file(struct supplemental_pte *spte);
bool extend_stack(struct supplemental_pte *spte);

bool try_load_page(void* fault_addr){
	ASSERT (fault_addr < PHYS_BASE);
	ASSERT(!lock_held_by_current_thread (&frame_table_lock_dummy));
	lock_acquire (&frame_table_lock_dummy);
	lock_release (&frame_table_lock_dummy);
	/* Round down to nearest page boundary. */
	uint8_t* target_addr = (uint8_t*)pg_round_down (fault_addr);
	bool result = false;
	struct thread* cur= thread_current();
	ASSERT(cur != NULL);
	/*creat key elem for searching*/
	struct supplemental_pte key;
	key.uaddr=target_addr;
	ASSERT (!lock_held_by_current_thread (&cur->supplemental_pt_lock) && 6==6 );
	lock_acquire(&cur->supplemental_pt_lock);
	struct hash_elem *e = hash_find (&cur->supplemental_pt, &key.elem);

	if(e==NULL){
		/*if not found, return false*/
		lock_release(&cur->supplemental_pt_lock);
		return false;
	}

	/*get the entry and release lock*/
	struct supplemental_pte *spte = hash_entry (e, struct supplemental_pte, elem);
	ASSERT(spte != NULL);
	ASSERT (!lock_held_by_current_thread (&spte->lock) && 1==1 );
	lock_acquire(&spte->lock);
	lock_release(&cur->supplemental_pt_lock);

	if (spte->spb != NULL) {
		/* swap in the frame from swap pool */
		struct frame_table_entry *fte = get_frame(spte);
		if (fte == NULL) {
			lock_release(&spte->lock);
			return false;
		}
		swap_in(fte, spte->spb);
		bool success = install_page (spte->uaddr, fte->frame_addr, spte->writable);
		if (!success) {
			free_fte(fte);
			lock_release(&spte->lock);
			return false;
		}
		fte->pinned = false;
		result = true;
	} else {
		if (spte->type_code == SPTE_CODE_SEG || spte->type_code == SPTE_DATA_SEG || spte->type_code == SPTE_MMAP) {
				/* load file from disk into frame */
				result = load_file(spte);
		} else if (spte->type_code ==SPTE_STACK_INIT){
			/*deal with stack extension*/
			result = extend_stack(spte);
		}
		else {
			PANIC("invalid spte type_code!");
		}
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
	ASSERT(f != NULL);
	off_t offset = spte->offset;
	size_t zero_bytes = spte->zero_bytes;
	size_t read_bytes = PGSIZE - zero_bytes;

	off_t old_pos = file_tell (f);
	ASSERT (!lock_held_by_current_thread (&filesys_lock) && 9==9 );
	lock_acquire(&filesys_lock);
	file_seek(f, offset);
	fte->accessed = true;
	if (file_read (f, fte->frame_addr, read_bytes) != read_bytes) {
		file_seek (f, old_pos);
		free_fte (fte);
		lock_release(&filesys_lock);
		return false;
	}
	file_seek (f, old_pos);
	lock_release(&filesys_lock);
	memset(fte->frame_addr+read_bytes, 0, zero_bytes);

	bool success = install_page (spte->uaddr, fte->frame_addr, spte->writable);

	/*finished the memset, unpin the frame*/
	fte->pinned=false;
	if (!success) {
		free_fte (fte);
		return false;
	}
	return true;
}


bool extend_stack(struct supplemental_pte *spte) {
	ASSERT(spte != NULL);
	ASSERT(spte->type_code == SPTE_STACK_INIT);

	struct frame_table_entry *fte = get_frame(spte);
	if (fte == NULL) {
		return false;
	}

	size_t zero_bytes = spte->zero_bytes;
	fte->accessed = true;
	memset(fte->frame_addr, 0, zero_bytes);

	bool success = install_page (spte->uaddr, fte->frame_addr, spte->writable);
	/*finished the memset, unpin the frame*/
	fte->pinned=false;
	if (!success) {
		free_fte (fte);
		return false;
	}
	return true;
}


