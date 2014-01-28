#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

/*self defined */
static bool is_user_address(const void *pointer, int size);
void user_exit(int exit_code);
void sys_exit_handler(struct intr_frame *f);
void sys_halt_handler(struct intr_frame *f);
void sys_exec_handler(struct intr_frame *f);
void sys_wait_handler(struct intr_frame *f);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
//TODO:may need to check string address
 void* esp=f->esp;
 if(!is_user_address(esp, 1)){
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
		//TODO: handler
		sys_wait_handler(f);
		break;
	case SYS_CREATE:break;
	case SYS_REMOVE:break;
	case SYS_OPEN:break;
	case SYS_FILESIZE:break;
	case SYS_READ:break;
	case SYS_WRITE:
		sys_write_handler(f);
		break;
	case SYS_SEEK:break;
	case SYS_TELL:break;
	case SYS_CLOSE:break;
	default:break;
 }

  printf ("system call!\n");
  thread_exit ();
}

/*self defined*/

/*handle sys_exec*/
void sys_exec_handler(struct intr_frame *f){
	void* esp=f->esp;
	/*validat the 1st argument*/
	if(!is_user_address(esp+1, 1)){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	/*get the full_line command*/
	char *full_line=(char *)esp+1;
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

/*handle sys_halt*/
void sys_halt_handler(struct intr_frame *f){
	/*shutdown pintos*/
	shutdown_power_off();
}

/*handle sys_exit*/
void sys_exit_handler(struct intr_frame *f){
	void* esp=f->esp;
	/*validat the 1st argument*/
	if(!is_user_address(esp+1, 1)){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *exit_code=((int *)esp)+1;
	/*exit with exit code*/
	user_exit(*exit_code);
}

/*handle sys_wait*/
void sys_wait_handler(struct intr_frame *f){
	void* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, 1)){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *arg=((int *)esp)+1;
	/*call process wait and update return value*/
	f->eax=process_wait(*arg);
}

/*handle sys_write*/
void sys_write_handler(struct intr_frame *f){
	void* esp=f->esp;
	/*validate the 1st argument*/
	if(!is_user_address(esp+1, 1)){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }
	/*validate the 2nd argument*/
	if(!is_user_address(esp+2, 1)){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }
	/*validate the 3rd argument*/
	if(!is_user_address(esp+3, 1)){
		 /* exit with -1*/
		 user_exit(-1);
		 return;
	 }

	int *fd_ptr=(int *)(esp+1);
	char *buffer=*(char **)(esp+2);
	int *size_ptr=(int *)(esp+3);

	/*verify whole buffer*/
	if(!is_user_address(buffer, *size_ptr)){
			 /* exit with -1*/
			 user_exit(-1);
			 return;
	}

	/*handle if fd==1, which is write to console*/
	if(*fd_ptr==1){
		putbuf(buffer,*size_ptr);
		/*handle return value*/
		f->eax= *size_ptr;
	}

	//TODO: other normal file write

}

/*user process exit with exit_code*/
void user_exit(int exit_code){
	struct thread* cur=thread_current();

	cur->exit_code = exit_code;

	//TODO: handling user process's file handler

	thread_exit();
}

/*judge if the pointer point to a valid space*/
static bool is_user_address(const void *pointer, int size){
	struct thread *cur=thread_current();
	uint32_t address=pointer;
	const void *end_pointer=pointer+size-1;
	int page_range=(address+size-1)/PGSIZE-address/PGSIZE;
	int i;
	bool result=false;
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
		result=pagedir_get_page (cur->pagedir, pointer+i*PGSIZE)!=NULL;
		if(!result){
			/*if unmapped, return false*/
			return false;
		}
	}
	result=pagedir_get_page (cur->pagedir, end_pointer)!=NULL;

    return result;
}
