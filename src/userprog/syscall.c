#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "lib/user/syscall.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "devices/block.h"
#include "threads/malloc.h"
#include "filesys/inode.h"
#include "lib/string.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/cache.h"



static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);


/*info for opened files in the entire system*/
struct global_file_block {
	block_sector_t inode_block_num;/*identification for file*/
	int ref_num;                   /*the number of threads holding this file*/
	bool is_deleted;               /*indicates the file is to be removed*/
	struct list_elem elem;         /*list elem for global_file_list*/
	struct lock lock;
};

struct list global_file_list;      /*List of all opened files*/


static bool is_user_address(const void *pointer, int size);
static bool is_string_address_valid(const void *pointer);
static bool is_page_mapped (const void *uaddr_);
static struct file_info_block* find_fib(struct list* l, int fd);
static struct global_file_block *find_opened_file(
		struct list *l, block_sector_t s);
static int write_to_file(struct file *file, char *buffer, size_t size);
static int read_from_file(struct file* f, void *buffer, int size);
static void sys_exit_handler(struct intr_frame *f);
static void sys_halt_handler(struct intr_frame *f);
static void sys_exec_handler(struct intr_frame *f);
static void sys_wait_handler(struct intr_frame *f);
static void sys_create_handler(struct intr_frame *f);
static void sys_open_handler(struct intr_frame *f);
static void sys_write_handler(struct intr_frame *f);
static void sys_remove_handler(struct intr_frame *f);
static void sys_close_handler(struct intr_frame *f);
static void sys_read_handler(struct intr_frame *f);
static void sys_filesize_handler(struct intr_frame *f);
static void sys_seek_handler(struct intr_frame *f);
static void sys_tell_handler(struct intr_frame *f);
static void sys_chdir_handler(struct intr_frame *f);
static void sys_mkdir_handler(struct intr_frame *f);
static void sys_readdir_handler(struct intr_frame *f);
static void sys_isdir_handler(struct intr_frame *f);
static void sys_inumber_handler(struct intr_frame *f);


void
syscall_init (void) 
{
  /*init the global file list and filesys locl*/
  lock_init(&global_file_list_lock);
  list_init(&global_file_list);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
 uint32_t* esp=f->esp;
 if(!is_user_address((void*)esp, sizeof(void *))){
	 user_exit(-1);
 }

 /*get system call number*/
 int *sys_call_num = (int*)esp;

 /*switch to specfic system call handler*/
 switch(*sys_call_num){
	case SYS_HALT:
		sys_halt_handler(f);
		break;
	case SYS_EXIT:
		sys_exit_handler(f);
		break;
	case SYS_EXEC:
		sys_exec_handler(f);
		break;
	case SYS_WAIT:
		sys_wait_handler(f);
		break;
	case SYS_CREATE:
		sys_create_handler(f);
		break;
	case SYS_REMOVE:
		sys_remove_handler(f);
		break;
	case SYS_OPEN:
		sys_open_handler(f);
		break;
	case SYS_FILESIZE:
		sys_filesize_handler(f);
		break;
	case SYS_READ:
		sys_read_handler(f);
		break;
	case SYS_WRITE:
		sys_write_handler(f);
		break;
	case SYS_SEEK:
		sys_seek_handler(f);
		break;
	case SYS_TELL:
		sys_tell_handler(f);
		break;
	case SYS_CLOSE:
		sys_close_handler(f);
		break;
	case SYS_CHDIR:
		sys_chdir_handler(f);
		break;
	case SYS_MKDIR:
		sys_mkdir_handler(f);
		break;
	case SYS_READDIR:
		sys_readdir_handler(f);
		break;
	case SYS_ISDIR:
		sys_isdir_handler(f);
		break;
	case SYS_INUMBER:
		sys_inumber_handler(f);
		break;
	default:break;
 }

}


/*handle sys_inumber*/
static void sys_inumber_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *fd_ptr=(int *)(esp+1);

	if(*fd_ptr==0 || *fd_ptr==1){
		/*return invalid inumber value in case of console*/
		f->eax = INVALID_SECTOR_ID;
	} else {
		/*read from regular file*/
		struct file_info_block *fib =
				find_fib(&thread_current()->opened_file_list, *fd_ptr);
		if (fib == NULL) {
			f->eax = INVALID_SECTOR_ID;
		} else {
			f->eax = fib->f->inode->sector;
		}
	}
}



/*handle sys_isdir*/
static void sys_isdir_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *fd_ptr=(int *)(esp+1);

	if(*fd_ptr==0 || *fd_ptr==1){
		/*return invalid inumber value in case of console*/
		f->eax = false;
	} else {
		/*read from regular file*/
		struct file_info_block *fib =
				find_fib(&thread_current()->opened_file_list, *fd_ptr);
		if (fib == NULL) {
			f->eax = false;
		} else {
			f->eax = fib->f->inode->is_dir;
		}
	}
}


/*handle sys_readdir*/
static void sys_readdir_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	int *fd_ptr=(int *)(esp+1);

	/*validate the 2nd argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the dir name*/
	char *dir=*(char **)(esp+1);

	/*verify string address*/
	if(!is_string_address_valid(dir)){
		user_exit(-1);
		return;
	}

	if(*fd_ptr==0 || *fd_ptr==1){
		/*return invalid inumber value in case of console*/
		f->eax = false;
	} else {
		/*read from regular file*/
		struct file_info_block *fib =
				find_fib(&thread_current()->opened_file_list, *fd_ptr);
		if (fib == NULL) {
			f->eax = false;
		} else {
			if (!fib->f->inode->is_dir) {
				f->eax = false;
			} else {
				strlcpy(dir, fib->file_name, READDIR_MAX_LEN + 1);
			}
		}
	}


}


/*handle sys_mkdir*/
static void sys_mkdir_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the dir name*/
	char *dir=*(char **)(esp+1);

	/*verify string address*/
	if(!is_string_address_valid(dir)){
		user_exit(-1);
		return;
	}

	f->eax = filesys_mkdir(dir);
}



/*handle sys_chdir*/
static void sys_chdir_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the dir name*/
	char *dir=*(char **)(esp+1);

	/*verify string address*/
	if(!is_string_address_valid(dir)){
		user_exit(-1);
		return;
	}

	static char tmp[MAX_DIR_PATH];
	relative_path_to_absolute(dir, tmp);
	int len=strlen(tmp);
	ASSERT(len<MAX_DIR_PATH-1);
	if(len>0 && tmp[len-1]!='/'){
		tmp[len]='/';
		tmp[len+1]=0;
	}
	struct dir *d = path_to_dir(tmp);
	if (d != NULL) {
		strlcpy(thread_current()->cwd, tmp, strlen(tmp)+1);
		dir_close(d);
		f->eax = true;
		return;
	}
	f->eax = false;
}

/*handle sys_exec*/
static void sys_exec_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	/*get the full_line command*/
	char *full_line=*(char **)(esp+1);

	/*verify string address*/
	if(!is_string_address_valid(full_line)){
		user_exit(-1);
		return;
	}

	/*execute*/
	tid_t tid=process_execute(full_line);
	/*handle return value*/
	if(tid==TID_ERROR){
		f->eax=-1;
	}
	else{
		f->eax=tid;
	}
}

/*handle sys_remove*/
static void sys_remove_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the full_line command*/
	char *file_name=*(char **)(esp+1);

	/*verify string address*/
	if(!is_string_address_valid(file_name)){
		user_exit(-1);
		return;
	}

	struct file *file = filesys_open(file_name);
	/*return false if failed to open the file*/
	if (file == NULL) {
		f->eax = false;
		return;
	}

	/*update the global_file_block*/
	ASSERT(!lock_held_by_current_thread (&global_file_list_lock));
	lock_acquire(&global_file_list_lock);
	struct global_file_block *gfb = find_opened_file(&global_file_list,
			file->inode->sector);
	if (gfb != NULL) {
		ASSERT (!lock_held_by_current_thread (&gfb->lock));
		lock_acquire(&gfb->lock);
	}
	lock_release(&global_file_list_lock);

	/*if not find, return, since this file is not opened*/
	if (gfb == NULL) {
		f->eax =filesys_remove(file_name);
	} else {
		ASSERT (lock_held_by_current_thread (&gfb->lock));
		/*mark as deleted*/
		gfb->is_deleted=true;
		lock_release(&gfb->lock);
		f->eax = true;
	}
}

/*handle sys_close*/
static void sys_close_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the full_line command*/
	int *fd_ptr=(int *)(esp+1);

	struct thread * cur=thread_current();
	struct file_info_block*fib = find_fib(&cur->opened_file_list, *fd_ptr);
	if(fib==NULL){
		/*if cur did not hold this file, return with false*/
		return;
	}

	/*close file corresponding to *fd_ptr*/
	close_file_by_fib(fib);

}


/*close the file given by file_info_block*/
void close_file_by_fib(struct file_info_block *fib) {
	ASSERT(fib != NULL);
	ASSERT(!lock_held_by_current_thread (&global_file_list_lock));
	lock_acquire(&global_file_list_lock);
	struct global_file_block *gfb = find_opened_file(&global_file_list,
			fib->f->inode->sector);
	if (gfb != NULL) {
		ASSERT (!lock_held_by_current_thread (&gfb->lock));
		lock_acquire(&gfb->lock);
		/*remove it from the global_file_list if it's the last opener*/
		if (gfb->ref_num <= 1) {
			list_remove(&gfb->elem);
		}
	}
	lock_release(&global_file_list_lock);

	/*if not find, return, since this file is not opened*/
	if (gfb == NULL) {
		file_close(fib->f);
		return;
	} else {
		ASSERT (lock_held_by_current_thread (&gfb->lock));
		/*check the reference number*/
		if (gfb->ref_num>1) {
			/*if reference number>1, other thread also holding the file
			 * keep the file, but marked it as is_delete*/
			gfb->ref_num--;
			lock_release(&gfb->lock);
		}
		else{
			bool need_remove = gfb->is_deleted;
			lock_release(&gfb->lock);

			if(need_remove){
				filesys_remove(fib->file_name);
			}

			/*free the memory*/
			free(gfb);
		}

		/*close the file*/
		file_close(fib->f);
		/*delete the file_info_block from opened_file_list in current thread*/
		list_remove(&fib->elem);
		free(fib->file_name);
		free(fib);
	}
}

/*handle sys_open*/
static void sys_open_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the full_line command*/
	char *file_name=*(char **)(esp+1);

	/*verify string address*/
	if(!is_string_address_valid(file_name)){
		user_exit(-1);
		return;
	}

	struct file *file = filesys_open(file_name);
	/*return -1 if failed to open the file*/
	if (file == NULL) {
		f->eax = -1;
		return;
	}

	/*update current thread's opened_file_list*/
	struct thread *cur = thread_current();
	struct file_info_block *fib = malloc(sizeof(struct file_info_block));
	fib->f = file;
	fib->fd = cur->next_fd_num++;
	char *file_name_copy = malloc(NAME_MAX + 1);
	if (file_name_copy == NULL) {
		f->eax = -1;
		free(fib);
		file_close(file);
		return;
	}

	get_file_name_from_path(file_name, file_name_copy);
	fib->file_name = file_name_copy;
	/*add file_info_block of the opened file into current thread's
	  opened_file_list*/
	list_push_back(&cur->opened_file_list, &fib->elem);

	/*update global_file_block*/
	ASSERT(!lock_held_by_current_thread (&global_file_list_lock));
	lock_acquire(&global_file_list_lock);
	struct global_file_block *gfb = find_opened_file(&global_file_list,
			file->inode->sector);
	if (gfb != NULL) {
		ASSERT (!lock_held_by_current_thread (&gfb->lock));
		lock_acquire(&gfb->lock);
	}
	lock_release(&global_file_list_lock);

	if (gfb == NULL) {
		/*open a new file*/
		gfb = malloc(sizeof(struct global_file_block));
		gfb->inode_block_num = file->inode->sector;
		gfb->is_deleted = false;
		gfb->ref_num = 1;
		lock_init(&gfb->lock);
		list_push_back(&global_file_list, &gfb->elem);
	} else {
		ASSERT (lock_held_by_current_thread (&gfb->lock));
		/*the file is opened already*/
		if (gfb->is_deleted) {
			f->eax = -1;
			lock_release(&gfb->lock);
			file_close(file);
			return;
		}
		/*increase file reference number*/
		gfb->ref_num++;
		lock_release(&gfb->lock);
	}


	f->eax=fib->fd;
}

/*handle sys_halt*/
static void sys_halt_handler(struct intr_frame *f UNUSED){
	/*shutdown pintos*/
	shutdown_power_off();
}

/*handle sys_exit*/
static void sys_exit_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validat the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *exit_code=(int *)(esp+1);
	/*exit with exit code*/
	user_exit(*exit_code);
}

/*handle sys_wait*/
static void sys_wait_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *arg=(int *)(esp+1);
	/*call process wait and update return value*/
	f->eax=process_wait(*arg);
}


/*handle sys_create*/
static void sys_create_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*validate the 2nd argument*/
	if(!is_user_address(esp+2, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	char *file_name=*(char **)(esp+1);
	int *file_size=(int *)(esp+2);

	/* verify file_name string */
	if(!is_string_address_valid(file_name)){
		user_exit(-1);
		return;
	}

	bool success = filesys_create(file_name, *file_size, false);
	/*return the value returned by filesys_create*/
	f->eax=success;
}


/*handle sys_seek*/
static void sys_seek_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*validate the 2nd argument*/
	if(!is_user_address(esp+2, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	int *fd_ptr=(int *)(esp+1);
	int *pos_ptr=(int *)(esp+2);

	/*find the file_info_block corresponding to the input fd*/
	struct thread *cur=thread_current();
	struct file_info_block *fib = find_fib(&cur->opened_file_list, *fd_ptr);
	if(fib==NULL){
		return;
	}

	file_seek(fib->f, *pos_ptr);
}

/*handle sys_write*/
static void sys_write_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }
	/*validate the 2nd argument*/
	if(!is_user_address(esp+2, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }
	/*validate the 3rd argument*/
	if(!is_user_address(esp+3, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *fd_ptr=(int *)(esp+1);
	char *buffer=*(char **)(esp+2);
	int *size_ptr=(int *)(esp+3);

	/*verify whole buffer*/
	if(!is_user_address((void *)buffer, *size_ptr)){
			 user_exit(-1);
			 return;
	}

	/*handle if fd==1, which is write to console*/
	if(*fd_ptr==1){
		putbuf(buffer,*size_ptr);
		/*handle return value*/
		f->eax= *size_ptr;
		return;
	}

	/*write to regular file*/
	struct thread * cur=thread_current();
	struct file_info_block*fib = find_fib(&cur->opened_file_list, *fd_ptr);
	if(fib==NULL){
		/*if cur didnot hold this file, exit*/
		user_exit(-1);
		return;
	}

	/* if fd is corresponding to a dir, fail the write */
	if (fib->f->inode->is_dir) {
		f->eax = -1;
		return;
	}
	/*write to file*/
	int result = write_to_file(fib->f, buffer, *size_ptr);
	f->eax = result;
}

/*handle sys_read*/
static void sys_read_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }
	/*validate the 2nd argument*/
	if(!is_user_address(esp+2, sizeof(void **))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }
	/*validate the 3rd argument*/
	if(!is_user_address(esp+3, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *fd_ptr=(int *)(esp+1);
	char *buffer=*(char **)(esp+2);
	int *size_ptr=(int *)(esp+3);

	/*verify whole buffer*/
	if(!is_user_address((void *)buffer, *size_ptr)){
		 user_exit(-1);
		 return;
	}

	/*handle if fd==0, which is read from console*/
	if(*fd_ptr==0){
		int already_read = 0;
		while(already_read < *size_ptr) {
			buffer[already_read++] = input_getc();
		}
		/*handle return value*/
		f->eax = *size_ptr;
	} else {
	/*read from regular file*/
		struct file_info_block *fib =
				find_fib(&thread_current()->opened_file_list, *fd_ptr);
		if (fib == NULL) {
			f->eax = -1;
		} else {
			f->eax = read_from_file(fib->f, buffer, *size_ptr);
		}
	}
}

/*user process exit with exit_code*/
void user_exit(int exit_code){
	struct thread* cur=thread_current();
	cur->exit_code = exit_code;
	thread_exit();
}

/*judge if the pointer points to a valid space*/
static bool is_user_address(const void *pointer, int size){
	uint32_t address=(uint32_t)pointer;
	void *end_pointer=(void *)(address+size-1);
	int page_range=(address+size-1)/PGSIZE-address/PGSIZE;
	int i;
	bool mapped=false;
	/*check if pointer is null*/
	if(pointer==NULL){
		return false;
	}

	/*check if pointer and end_pointer inside the user's space*/
	if(!(is_user_vaddr(pointer)&&is_user_vaddr(end_pointer))){
		return false;
	}

	/*check if the address is mapped*/
	for(i=0;i<page_range;i++){
		mapped = is_page_mapped(pointer+i*PGSIZE);
		if(!mapped){
			/*if unmapped, return false*/
			return false;
		}
	}

	return is_page_mapped(end_pointer);
}


/*judge if the a string's address is valid*/
static bool is_string_address_valid(const void *pointer){
	uint8_t *str=(uint8_t *)pointer;
	int byte_value;
	/*check if pointer is null*/
	if(str==NULL){
		return false;
	}

	/*1 by 1 check each char's address*/
	while(true){
		/*if over the user's space return*/
		if(!is_user_vaddr(str)){
			return false;
		}

		/*if unmmaped return*/
		byte_value= get_user(str);
		if(byte_value==-1){
			return false;
		}

		/*if reached the end of the string, return true*/
		if(byte_value==0){
			return true;
		}
		str++;
	}

	/*dummy return, never called*/
	return true;
}

/*check if page mapped*/
static bool is_page_mapped (const void *uaddr_){
	uint8_t *uaddr = (uint8_t *)uaddr_;
	int byte_value= get_user(uaddr);
	return  byte_value!= -1;
}


/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/*search global file list for the given block_sector_t*/
static struct global_file_block *find_opened_file(
		struct list* l, block_sector_t s) {
	struct global_file_block *gf = NULL;
	struct list_elem *e = NULL;

	for (e = list_begin (l); e != list_end (l); e = list_next (e)) {
		gf = list_entry (e, struct global_file_block, elem);
		if (gf->inode_block_num == s) {
			return gf;
		}
	}

	return NULL;
}

/*in opened_file_list, search for file_info_block using fd*/
static struct file_info_block* find_fib(struct list* l, int fd) {
	struct file_info_block *fib = NULL;
	struct list_elem *e = NULL;

	for (e = list_begin (l); e != list_end (l); e = list_next (e)) {
		fib = list_entry (e, struct file_info_block, elem);
		if (fib->fd == fd) {
			return fib;
		}
	}

	return NULL;
}

/*handle sys_filesize*/
static void sys_filesize_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the full_line command*/
	int *fd_ptr=(int *)(esp+1);

	struct thread * cur=thread_current();
	struct file_info_block*fib = find_fib(&cur->opened_file_list, *fd_ptr);
	if(fib==NULL){
		/*if cur did not hold this file, return with -1*/
		f->eax=-1;
		return;
	}

	f->eax= file_length (fib->f);

}


/*handle sys_tell*/
static void sys_tell_handler(struct intr_frame *f){
	uint32_t* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, sizeof(int))){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	}

	/*get the full_line command*/
	int *fd_ptr=(int *)(esp+1);

	struct thread * cur=thread_current();
	struct file_info_block*fib = find_fib(&cur->opened_file_list, *fd_ptr);
	if(fib==NULL){
		/*if cur did not hold this file, return with -1*/
		f->eax=-1;
		return;
	}

	f->eax = file_tell (fib->f);

}


/*read from file with buffer of size*/
static int read_from_file(struct file* f, void *buffer, int size) {
	return file_read(f, buffer, size);
}

/*write to file with buffer of size*/
static int write_to_file(struct file *file, char *buffer, size_t size){
	return file_write (file, buffer, size);
}



