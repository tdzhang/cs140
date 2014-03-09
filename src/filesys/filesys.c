#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"


#define MAX_ENTRIES_PER_DIR 16

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

    /*init buffer cache*/
    if(!buffer_cache_init()) {
  	  PANIC ("fail to init buffer cache, can't initialize file system.");
    }

  if (format) 
    do_format ();

  free_map_open ();


}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* mkdir a directory for a given dir. */
bool filesys_mkdir (const char* dir) {
	block_sector_t inode_sector = 0;
	static char tmp[MAX_DIR_PATH];
	relative_path_to_absolute(dir, tmp);
	struct dir *d = path_to_dir(tmp);
	char name_to_create[NAME_MAX + 1];
	get_file_name_from_path(tmp, name_to_create);
	bool success = (d != NULL
	                  && free_map_allocate (1, &inode_sector)
	                  && dir_create (inode_sector, MAX_ENTRIES_PER_DIR)
	                  && dir_add (d, name_to_create, inode_sector, true));
	if (!success && inode_sector != 0) {
	    free_map_release (inode_sector, 1);
	}
	dir_close (d);

	return success;
}


/* get file name from a given path, i.e. the substring after the last '/'
 * if no slash in path, copy path into file_name directly*/
void get_file_name_from_path(char *path, char *file_name) {
	char *last_slash = strrchr(path, '/');
	if (last_slash == NULL) {
		strlcpy(file_name, path, NAME_MAX + 1);
	} else {
		strlcpy(file_name, (last_slash+1), NAME_MAX + 1);
	}
}


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;
  static char tmp[MAX_DIR_PATH];
  relative_path_to_absolute(name, tmp);
  printf(">>>>>>>>>>name=%s\n",name);
  printf(">>>>>>>>>>temp=%s\n",tmp);
  struct dir *dir = path_to_dir(tmp);
  printf(">>>>>>>>>>dir=%d\n",dir->inode->sector);
  char name_to_create[NAME_MAX + 1];
  get_file_name_from_path(tmp, name_to_create);
  printf(">>>>>>>>>>name_to_create=%s\n",name_to_create);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, name_to_create, inode_sector, is_dir));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  static char tmp[MAX_DIR_PATH];
  relative_path_to_absolute(name, tmp);
  struct dir *dir = path_to_dir(tmp);
  char name_to_open[NAME_MAX + 1];
  get_file_name_from_path(tmp, name_to_open);

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name_to_open, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
	static char tmp[MAX_DIR_PATH];
	relative_path_to_absolute(name, tmp);
	struct dir *dir = path_to_dir(tmp);
	char name_to_remove[NAME_MAX + 1];
	get_file_name_from_path(tmp, name_to_remove);

	bool success = dir != NULL && dir_remove (dir, name_to_remove);
	dir_close (dir);

	return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, MAX_ENTRIES_PER_DIR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}


