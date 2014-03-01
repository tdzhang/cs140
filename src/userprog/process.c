#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/page.h"

static thread_func start_process NO_RETURN;
static bool load (void *lib_, void (**eip) (void), void **esp);

/*self defined*/
static void push_args2stack(void **esp, char *full_line);
static void push_stack(void **esp, void *arg, int size,int esp_limit_);



/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /*dynamically allocate a load_info_block to help loading new process*/
  struct load_info_block *lib = malloc(sizeof(struct load_info_block));
  if(lib==NULL){
	  palloc_free_page (fn_copy);
	  return TID_ERROR;
  }
  /*initialize load_info_block*/
  lib->full_line = fn_copy;
  sema_init(&lib->sema_loaded, 0);
  lib->success = false;

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (fn_copy, PRI_DEFAULT, start_process, lib);

  /*wait for load() finishes*/
  sema_down(&lib->sema_loaded);

  if (!lib->success) {
	  tid = TID_ERROR;
  }

  /*clean up*/
  if(lib->full_line!=NULL){
	  palloc_free_page(lib->full_line);
  }
  free(lib);

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *lib_)
{
  struct load_info_block *lib = (struct load_info_block *) lib_;
  struct intr_frame if_;
  struct thread *cur=thread_current();
  bool success;

  /*update is_user flag to true: it is a user process*/
  cur->is_user=true;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (lib, &if_.eip, &if_.esp);
  lib->success = success;
  /*notice the waiting parent thread*/
  sema_up(&lib->sema_loaded);

  /* If load failed, quit. */
  if (!success) {
	cur->exit_code = -1;
    thread_exit ();
  }
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
	int exit_code = 0;
	struct thread * cur=thread_current();
	struct list_elem *e=NULL;
	struct wait_info_block *wib=NULL;
	bool element_found=false;
	enum intr_level old_level = intr_disable ();

	/*check if it is the child of current process*/
	for (e = list_begin (&cur->child_wait_block_list); e != list_end
		 (&cur->child_wait_block_list); e = list_next (e)) {
	    wib = list_entry (e, struct wait_info_block, elem);
	    if (wib->tid == child_tid){
	    		element_found=true;
	    		break;
	    }
	}

	/*return -1 if the tid is not a child of cur*/
	if(!element_found){
		return -1;
	}

	/*return -1 if terminated by kernel*/
	if (wib->exit_code == -1) {
		return -1;
	}

	lock_acquire(&wib->l);
	/*wait for child process to die*/
	while(wib->t != NULL) {
		cond_wait(&wib->c, &wib->l);
	}

	exit_code = wib->exit_code;

	/*free wait_info_block*/
	list_remove(&wib->elem);
	ASSERT(wib->t == NULL);
	lock_release(&wib->l);

	free(wib);

	intr_set_level (old_level);

	return exit_code;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  struct wait_info_block *wib = cur->wait_info;
  uint32_t *pd;
  char cmd[MAX_FILE_NAME+2];

  /*close exec_file and allow to write it again*/
  if(cur->exec_file_ptr!=NULL){
	  file_allow_write(cur->exec_file_ptr);
	  file_close (cur->exec_file_ptr);
	  cur->exec_file_ptr=NULL;
  }

  /*close all opened files of this thread*/
  struct list *opened_files = &cur->opened_file_list;
  struct list_elem *e = list_begin (opened_files);
  struct list_elem *temp = NULL;
  struct file_info_block *fib;

  while (e != list_end (opened_files)){
	  temp = list_next (e);
	  fib = list_entry (e, struct file_info_block, elem);
	  close_file_by_fib(fib);
	  e = temp;
  }

  /*print out termination msg for grading use*/
  if (cur->is_user){
	get_cmd(cur->name, cmd);
	printf ("%s: exit(%d)\n", cmd, cur->exit_code);
  }


  /*update wait_info_block if its parent process still exists*/
  if(wib != NULL){
	  lock_acquire(&wib->l);
	  wib->exit_code = cur->exit_code;
	  wib->t = NULL;
	  cond_signal(&wib->c, &wib->l);
	  lock_release(&wib->l);
  }


  /*clean up children's wait_info_block*/
  struct list *child_list = &cur->child_wait_block_list;
  while(!list_empty(child_list)) {
	  wib = list_entry (list_pop_front(child_list),
			  struct wait_info_block, elem);

	  lock_acquire(&wib->l);
	  list_remove(&wib->elem);
	  if (wib->t != NULL) {
		  wib->t->wait_info = NULL;
	  }
	  lock_release(&wib->l);
	  free(wib);
  }


  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /*if this about to exit process is holding the filesys_lock, release it*/
  if(lock_held_by_current_thread (&filesys_lock)){
	  lock_release(&filesys_lock);
  }

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (void *lib_, void (**eip) (void), void **esp)
{
  struct load_info_block *lib = lib_;
  char *fn_copy=lib->full_line;
  char file_name[MAX_FILE_NAME+1];
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /*parse out file name to file_name*/
  get_cmd(fn_copy,file_name);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /*add exec_file_ptr and deny write operation to it*/
  t->exec_file_ptr=file;
  file_deny_write (file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /*copy args into stack*/
  push_args2stack(esp,fn_copy);

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  bool success = false;
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* populate spte in supplemental page table */
      if (read_bytes > 0) {
    	  	  success = populate_spte(file, ofs, upage,
    	  			  age_zero_bytes, writable, SPTE_CODE_SEG);
      } else {
    	  	  success = populate_spte(file, ofs, upage,
    	  			  page_zero_bytes, writable, SPTE_DATA_SEG);
      }

      if (!success) {
    	  	  return false;
      }
      /* Advance. */
      ofs += page_read_bytes;
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  if(!populate_spte(NULL, NULL, (uint8_t *)PHYS_BASE-PGSIZE,
		  PGSIZE, true, SPTE_STACK_INIT)) {
	  return false;
  }

  if(!try_load_page((void *)((uint8_t *)PHYS_BASE-PGSIZE))) {
	  return false;
  }
  *esp = PHYS_BASE;
  return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


/*self defined function*/

/*parse string full_line to get the first word to string cmd*/
void get_cmd(const char *full_line, char* cmd){
	int i;
	for(i=0;i<MAX_FILE_NAME+1;i++){
		if(full_line[i]=='\0'||full_line[i]==' '){
			cmd[i]='\0';
			break;
		}
		cmd[i]=full_line[i];
	}
	ASSERT(i<=MAX_FILE_NAME);
}


/*push args into stack*/
static void push_args2stack(void **esp, char *full_line){
	char *token, *save_ptr;
	int argc=0;
	int i=0;
	int j=0;
	uint8_t zero=0;
	int zero_int=0;

	int esp_limit=(int)(*esp)-PGSIZE;

	/*find out how many args are there*/
	for (token = strtok_r (full_line, " ", &save_ptr); token != NULL;
		token = strtok_r (NULL, " ", &save_ptr)){
		argc++;
	}

	/*updates argv according to argc*/
	char* argv[argc];
	for(i=0;i<argc;i++){
		while(full_line[j]=='\0'||full_line[j]==' '){j++;}
		argv[i]=&full_line[j];
		while(full_line[j]!='\0'){j++;}
	}

	/*push back the argv[i] content into stack*/
	for(i=argc-1;i>=0;i--){
		push_stack(esp, argv[i], strlen(argv[i])+1,esp_limit);
		/*update argv[i]*/
		argv[i]=*esp;
	}

	/*if the esp is not multiple of 4, push 0 into the stack*/
	while((int)(*esp) % 4 !=0){
		push_stack(esp, &zero, sizeof(uint8_t),esp_limit);
	}

	/*add a marker 0*/
	push_stack(esp, &zero_int, sizeof(int),esp_limit);

	/*push back the addresses of argv[i] into stack*/
	for(i=argc-1;i>=0;i--){
		push_stack(esp, &argv[i], sizeof(char*),esp_limit);
	}

	/*push argv*/
	void *cur_esp=*esp;
	push_stack(esp, &cur_esp, sizeof(void*),esp_limit);

	/*push argc*/
	push_stack(esp, &argc, sizeof(int),esp_limit);

	/*push return address*/
	push_stack(esp, &zero_int, sizeof(int),esp_limit);
}

/*push data into esp*/
static void push_stack(void **esp, void *arg, int size, int esp_limit_){
	*esp=(*esp)-size;
	ASSERT((int)*(esp)>esp_limit_);
	memcpy(*esp,arg,size);
}

/*malloc init wait_info_block*/
bool init_wait_info_block(struct thread *t) {
	struct wait_info_block *wib = malloc(sizeof(struct wait_info_block));
	/*if malloc failed, return false*/
	if (wib == NULL) return false;
	t->wait_info = wib;
	/*init wib*/
	wib->t = t;
	wib->tid = t->tid;
	lock_init(&wib->l);
	cond_init(&wib->c);
	wib->exit_code = 0;
	/*add wib in current thread's child_wait_block_list*/
	struct thread *cur = thread_current();
	if (cur != t) {
		list_push_back(&cur->child_wait_block_list, &wib->elem);
	}
	return true;
}


bool populate_spte(struct file *file, off_t ofs, uint8_t *upage,
		uint32_t zero_bytes, bool writable, uint8_t type) {
	struct supplemental_pte *spte = malloc(sizeof(struct supplemental_pte));

	void * vs_addr=pg_round_down((const void *)upage);
	if (spte == NULL) {
		return false;
	}

	spte->type_code = type;
	spte->uaddr = (uint8_t*)vs_addr;
	spte->writable = writable;
	spte->f = file;
	spte->offset = ofs;
	spte->zero_bytes = zero_bytes;
	lock_init(&spte->lock);
	spte->spb = NULL;

	struct thread * cur=thread_current();
	ASSERT(cur != NULL);
#ifdef VM
	lock_acquire(&cur->supplemental_pt_lock);
	hash_insert(&cur->supplemental_pt, &spte->elem);
	lock_release(&cur->supplemental_pt_lock);
#endif
	return true;
}




