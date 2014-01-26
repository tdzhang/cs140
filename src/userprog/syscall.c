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

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
 void* esp=f->esp;
 if(!is_user_address(esp, sizeof(void *))){
	 //TODO: exit with -1, may call process_exit and thread_exit with printf
	 user_exit(-1);
 }

 /*get system call number*/
 int *sys_call_num = (int*)esp;

 /*switch to specfic system call handler*/
 switch(*sys_call_num){
	case SYS_HALT:break;
	case SYS_EXIT:break;
	case SYS_EXEC:break;
	case SYS_WAIT:break;
	case SYS_CREATE:break;
	case SYS_REMOVE:break;
	case SYS_OPEN:break;
	case SYS_FILESIZE:break;
	case SYS_READ:break;
	case SYS_WRITE:break;
	case SYS_SEEK:break;
	case SYS_TELL:break;
	case SYS_CLOSE:break;
	default:break;
 }

  printf ("system call!\n");
  thread_exit ();
}

/*self defined*/

/*user process exit with exit_code*/
void user_exit(int exit_code){
	struct thread* cur=thread_current();
	char cmd[MAX_FILE_NAME+2];
	cur->exit_code = exit_code;
	get_cmd(cur->name, cmd);

	//TODO: handling user process's file handler

	/*print out termination msg for grading use*/
	if (cur->is_user){
		printf ("%s: exit(%d)\n", cmd, cur->exit_code);
	}

	thread_exit();
}

/*judge if the pointer point to a valid space*/
static bool is_user_address(const void *pointer, int size){
	struct thread *cur=thread_current();
	const void *end_pointer=pointer+size-1;
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
	result=pagedir_get_page (cur->pagedir, pointer)!=NULL;

    return result;
}
