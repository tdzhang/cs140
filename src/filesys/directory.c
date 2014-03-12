#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/thread.h"



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
  if(!success) goto done;

  /*if is_dir, need to create .. and . for the new dir*/
  if(is_dir){
	  struct inode *new_inode = inode_open(inode_sector);
	  /*create ..*/
	  for (ofs = 0; inode_read_at (new_inode, &e, sizeof e, ofs) == sizeof e;
	         ofs += sizeof e)
	      if (!e.in_use)
	        break;
	  e.in_use = true;
	  e.is_dir = true;
	  strlcpy (e.name, "..", 3);
	  e.inode_sector = dir->inode->sector;
	  success = inode_write_at (new_inode, &e, sizeof e, ofs) == sizeof e;
	  if(!success) {
		  inode_close(new_inode);
		  goto done;
	  }

	  /*create .*/
	  for (ofs = 0; inode_read_at (new_inode, &e, sizeof e, ofs) == sizeof e;
			 ofs += sizeof e)
		  if (!e.in_use)
			break;
	  e.in_use = true;
	  e.is_dir = true;
	  strlcpy (e.name, ".", 3);
	  e.inode_sector = new_inode->sector;
	  success = inode_write_at (new_inode, &e, sizeof e, ofs) == sizeof e;

	  if(!success) {
	  		  inode_close(new_inode);
	  		  goto done;
	  }
	  inode_close(new_inode);
  }

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
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

  /* cannot remove root dir */
  if (inode->sector == ROOT_DIR_SECTOR) {
	  goto done;
  }

  if (inode->is_dir) {
	  /* check if the dir is empty, if not, goto done */
	  off_t offset;
	  struct dir_entry entry;
	  for (offset = 0; inode_read_at (inode, &entry, sizeof entry, offset) == sizeof entry;
			  offset += sizeof entry) {
	      if (entry.in_use && (strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0)){
	          goto done;
	      }
	  }
  }

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
      if (e.in_use && (strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0)){
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

/*self defined function*/

/*search path to get a dir, open it, and return*/
/* /a/b/c  -> return dir struct dirctory b */
/* /a/b/c/  -> return dir struct dirctory b */
struct dir* path_to_dir(char* path_, char* file_name_out){
	ASSERT(path_ != NULL && path_[0]!=0);

	/*bool var indicate relative/abs path*/
	bool relative_path=false;
	if(path_[0]!='/')relative_path=true;

	/*parse path*/
	char *token, *save_ptr;
	int count=0;
	int i=0;
	int j=0;
	char path[MAX_DIR_PATH];
	strlcpy(path, path_,strlen(path_)+1);

	/*erase the last continuous slash*/
	i=strlen(path);
	while(i>0){
		i--;
		if(path[i]!='/') break;
	}
	path[i+1]=0;

	/*if root, return root dir*/
	if(strlen(path_)==1 &&path_[0]=='/'){
		file_name_out[0]=0;
		return dir_open_root ();
	}

	/*return file_name_out*/
	char *last_slash = strrchr(path, '/');
	if (last_slash == NULL) {
		if(strlen(path)>NAME_MAX){
			file_name_out[0]=0;
			return NULL;
		}

		strlcpy(file_name_out, path, NAME_MAX + 1);
	} else {
		if(strlen(last_slash+1)>NAME_MAX){
			file_name_out[0]=0;
			return NULL;
		}
		last_slash[0]=0;
		strlcpy(file_name_out, (last_slash+1), NAME_MAX + 1);
	}
	ASSERT(strlen(file_name_out)>0);


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
	struct dir *dir;
	if(relative_path){
		dir=dir_open(inode_open(thread_current()->cwd_sector));
	}else{
		dir= dir_open_root ();
	}

	struct inode *inode = NULL;
	for(i=0;i<count;i++){
		if (dir == NULL) return NULL;
		if(!dir_lookup (dir, dirs[i], &inode)) {
			dir_close (dir);
			return NULL;
		}
		/*if the intermiate path elem is not dir or it's marked as removed,
		 * fail it*/
		if(!inode->is_dir || inode->removed) {
			dir_close (dir);
			return NULL;
		}
		dir_close (dir);
		dir = dir_open (inode);
	}
	return dir;
}

/*function translate relative dir path to absolute dir path*/
void relative_path_to_absolute(char* relative_path, char* result_path){
	ASSERT (strlen(relative_path) > 0);
	static char path[MAX_DIR_PATH];
	struct thread* t=thread_current();

	/*if the relative_path is abs, do not cat*/
	if(relative_path[0]=='/'){
		strlcpy(path, relative_path,strlen(relative_path)+1);
	}else{
		ASSERT (t->cwd[strlen(t->cwd)-1] == '/');
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

	if (!is_root_dir(result_path) && has_end_slash(result_path)) {
		result_path[strlen(result_path)-1]=0;
	}
}


bool is_root_dir(char *dir) {
	return (strlen(dir) == 1 && dir[0] == '/');
}

bool has_end_slash(char *dir) {
	ASSERT (strlen(dir) > 0);
	return (dir[strlen(dir)-1]=='/');
}
