#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
    bool is_dir;                        /* whether the entry is a dir*/
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  if (inode != NULL) {
	  ASSERT (inode->is_dir);
  }
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  e.is_dir = is_dir;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
	//TODO: only remove empty dir if name is a dir
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

/*self defined function*/

/*search absolute path to get a dir, open it, and return*/
/* /a/b/c  -> return dir struct dirctory b */
/* /a/b/  -> return dir struct dirctory b  */
struct dir* path_to_dir(char* path_){
	/*parse path*/
	char *token, *save_ptr;
	int count=0;
	int i=0;
	int j=0;
	char path[MAX_DIR_PATH];
	strlcpy(path, path_,strlen(path_)+1);
	for(i=strlen(path)-1;i>=0;i--){
		if(path[i]=='/'){
			path[i]='\0';
			break;
		}
	}

	/*find out how many args are there*/
	for (token = strtok_r (path, "/", &save_ptr); token != NULL;
		token = strtok_r (NULL, "/", &save_ptr)){
		count++;
	}

	/*updates dirs according to count*/
	char* dirs[count];
	for(i=0;i<count;i++){
		while(path[j]=='\0'||path[j]=='/'){j++;}
		dirs[i]=&path[j];
		while(path[j]!='\0'){j++;}
	}

	/*using dirs to get the directory inode sector*/
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;
	for(i=0;i<count;i++){
		if (dir == NULL) return NULL;
		if(!dir_lookup (dir, dirs[i], &inode)) return NULL;
		/*if the intermiate path elem is not dir, return false*/
		if(!inode->is_dir)return NULL;
		dir_close (dir);
		dir = dir_open (inode);
	}
	return dir;
}

/*function translate relative dir path to absolute dir path*/
void relative_path_to_absolute(char* relative_path,char* result_path){
	static char path[MAX_DIR_PATH];
	struct thread* t=thread_current();

	/*if the relative_path is abs, do not cat*/
	if(relative_path[0]=='/'){
		strlcpy(path, relative_path,strlen(relative_path)+1);
	}else{
		strlcpy(path, t->cwd,strlen(t->cwd)+1);
		strlcat(path, relative_path, MAX_DIR_PATH);
	}



	char *token, *save_ptr;
	int count=0;
	int i=0;
	int j=0;

	/*find out how many args are there*/
	for (token = strtok_r (path, "/", &save_ptr); token != NULL;
		token = strtok_r (NULL, "/", &save_ptr)){
		count++;
	}
	/*updates dirs according to count*/
	char* dirs[count];
	char* dirs_abs[count];

	for(i=0;i<count;i++){
		while(path[j]=='\0'||path[j]=='/'){j++;}
		dirs[i]=&path[j];
		while(path[j]!='\0'){j++;}
	}

	int k=0;
	int pointer=-1;
	char c1,c2;
	for(i=0;i<count;i++){
		c1=dirs[i][0];
		c2=dirs[i][1];
		if(c1=='.'&&c2!='.'){
			/*case: . */
		}else if(c1=='.'&&c2=='.'){
			/*case .. */
			pointer--;
			if(pointer<-1)pointer=-1;
		}else{
			/*normal case*/
			pointer++;
			dirs_abs[pointer]=dirs[i];
		}
	}

	result_path[0]='/';
	result_path[1]='\0';
	for(i=0;i<=pointer;i++){
		strlcat(result_path, dirs_abs[i], MAX_DIR_PATH);
		strlcat(result_path, "/", MAX_DIR_PATH);
	}
}
