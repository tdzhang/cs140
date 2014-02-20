#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

/*info used for process waiting*/
struct wait_info_block {
	tid_t tid;               /*thread's tid*/
	struct thread *t;        /*pointer to thread*/
	int exit_code;           /*code for exit status*/
	struct list_elem elem;   /*list elem for children list of its parent*/
	struct lock l;           /*lock for this struct itself*/
	struct condition c;      /*cond for waiting by the parent*/
};

/*load info struct used for process_execute*/
struct load_info_block {
	char * full_line;               /*full command line string*/
	struct semaphore sema_loaded;   /*semaphore used to load thread*/
	bool success;              /*whether the thread is loaded successfully*/
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/*self defined*/
#define MAX_FILE_NAME 14
void get_cmd(const char *full_line, char* cmd);
bool init_wait_info_block(struct thread *t);

/* load() helpers. */
bool install_page (void *upage, void *kpage, bool writable);

#endif /* userprog/process.h */
