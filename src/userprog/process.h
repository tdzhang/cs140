#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/*self defined*/
#define MAX_FILE_NAME 14
void get_cmd(const char *full_line, char* cmd);


#endif /* userprog/process.h */
