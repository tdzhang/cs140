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
  inode_flush_cache();
  force_close_all_open_inodes();
  free_map_close ();
}

/* mkdir a directory for a given dir. */
bool filesys_mkdir (const char* dir) {
	if (dir == NULL || strlen(dir) == 0) {
		return false;
	}
	block_sector_t inode_sector = 0;

	char name_to_create[NAME_MAX + 1];
	struct dir *d = path_to_dir(dir,name_to_create);

	if(d==NULL||strlen(name_to_create)==0){
		  dir_close(d);
		  return false;
	 }

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


/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  if (name == NULL || strlen(name) == 0) {
	  return false;
  }
  block_sector_t inode_sector = 0;
  char name_to_create[NAME_MAX + 1];
  struct dir *dir = path_to_dir(name,name_to_create);

  if(dir==NULL||strlen(name_to_create)==0){
	  dir_close(dir);
	  return false;
  }

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, name_to_create, inode_sector, false));
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
  if (name == NULL || strlen(name) == 0) {
	  return NULL;
  }
  struct inode *inode = NULL;
  struct dir *dir = NULL;
  char name_to_open[NAME_MAX + 1];

  /*validate cwd if the name is a relative path*/
  if(name[0]!='/'){
	  struct dir *dummy_dir = path_to_dir(".",name_to_open);

	  if(dummy_dir==NULL){
		  /*fail, return */
		  return NULL;
	  }
	  else{
		  dir_close(dummy_dir);
	  }
  }

  dir = path_to_dir(name,name_to_open);

  if(dir==NULL){
	  return NULL;
  }

  /*if open root, file open the root*/
  if(strlen(name_to_open)==0){
	inode = inode_open (ROOT_DIR_SECTOR);
	return file_open (inode);
  }else{
	  ASSERT (name_to_open != NULL && strlen(name_to_open) > 0);
	  dir_lookup (dir, name_to_open, &inode);
	  if (inode == NULL) {
		  dir_close (dir);
		  return NULL;
	  }
	  if (inode->removed) {
		  inode_close(inode);
		  dir_close (dir);
		  return NULL;
	  }
	  dir_close (dir);
	  return file_open (inode);
  }
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
	if (name == NULL || strlen(name) == 0) {
	  return false;
	}

	struct inode *inode = NULL;
	struct dir *dir = NULL;
	char name_to_remove[NAME_MAX + 1];
	dir = path_to_dir(name,name_to_remove);

	if(dir==NULL||strlen(name_to_remove)==0){
	  dir_close(dir);
	  return  false;
	}

	ASSERT (name_to_remove != NULL && strlen(name_to_remove) > 0);

	bool success = dir != NULL && dir_remove (dir, name_to_remove);
	dir_close (dir);

	return success;
}

/*init the 2 entries of root dir*/
void root_dir_init(){
   /*create .. and . for the root directory*/
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  struct inode *new_inode = inode_open(ROOT_DIR_SECTOR);
    /*create ..*/
    for (ofs = 0; inode_read_at (new_inode, &e, sizeof e, ofs) == sizeof e;
           ofs += sizeof e)
        if (!e.in_use)
          break;
    e.in_use = true;
    e.is_dir = true;
    strlcpy (e.name, "..", 3);
    e.inode_sector = ROOT_DIR_SECTOR;
    success = inode_write_at (new_inode, &e, sizeof e, ofs) == sizeof e;
    if(!success) {
      inode_close(new_inode);
     PANIC ("root directory creation failed at .. entry");
    }

    /*create .*/
    for (ofs = 0; inode_read_at (new_inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
      if (!e.in_use)
      break;
    e.in_use = true;
    e.is_dir = true;
    strlcpy (e.name, ".", 3);
    e.inode_sector = ROOT_DIR_SECTOR;
    success = inode_write_at (new_inode, &e, sizeof e, ofs) == sizeof e;

    if(!success) {
          inode_close(new_inode);
         PANIC ("root directory creation failed at . entry");
    }
    inode_close(new_inode);
}
/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, MAX_ENTRIES_PER_DIR))
    PANIC ("root directory creation failed");

  root_dir_init();

  free_map_close ();
  printf ("done.\n");
}


