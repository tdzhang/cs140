/* tar.c

   Creates a tar archive. */

#include <ustar.h>
#include <syscall.h>
#include <stdio.h>
#include <string.h>

static void usage (void);
static bool make_tar_archive (const char *archive_name,
                              char *files[], size_t file_cnt);

int
main (int argc, char *argv[]) 
{
  if (argc < 3)
    usage ();

  return (make_tar_archive (argv[1], argv + 2, argc - 2)
          ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void
usage (void) 
{
  printf ("tar, tar archive creator\n"
          "Usage: tar ARCHIVE FILE...\n"
          "where ARCHIVE is the tar archive to create\n"
          "  and FILE... is a list of files or directories to put into it.\n"
          "(ARCHIVE itself will not be included in the archive, even if it\n"
          "is in a directory to be archived.)\n");
  exit (EXIT_FAILURE);
}

static bool archive_file (char file_name[], size_t file_name_size,
                          int archive_fd, bool *write_error);

static bool archive_ordinary_file (const char *file_name, int file_fd,
                                   int archive_fd, bool *write_error);
static bool archive_directory (char file_name[], size_t file_name_size,
                               int file_fd, int archive_fd, bool *write_error);
static bool write_header (const char *file_name, enum ustar_type, int size,
                          int archive_fd, bool *write_error);

static bool do_write (int fd, const char *buffer, int size, bool *write_error);

static bool
make_tar_archive (const char *archive_name, char *files[], size_t file_cnt) 
{
	printf (">>>>>make_tar_archive>> archive_name=%s, file cnt=%d \n", archive_name,file_cnt);
  static const char zeros[512];
  int archive_fd;
  bool success = true;
  bool write_error = false;
  size_t i;
  printf (">>>>>make_tar_archive>> before create (archive_name, 0) \n");
  if (!create (archive_name, 0)) 
    {
      printf ("%s: create failed\n", archive_name);
      return false;
    }
  printf (">>>>>make_tar_archive>> after create (archive_name, 0) \n");

  archive_fd = open (archive_name);
  printf (">>>>>make_tar_archive>> after open (archive_name) \n");
  if (archive_fd < 0)
    {
      printf ("%s: open failed\n", archive_name);
      return false;
    }
  printf (">>>>>make_tar_archive>> before for (i = 0; i < file_cnt; i++) \n");
  for (i = 0; i < file_cnt; i++) 
    {
      char file_name[128];
      
      strlcpy (file_name, files[i], sizeof file_name);
      printf (">>>>>make_tar_archive>> archive_file %d name=%s \n",i,file_name);
      if (!archive_file (file_name, sizeof file_name,
                         archive_fd, &write_error))
        success = false;
    }
  printf (">>>>>make_tar_archive>> before do_write (archive_fd, zeros, 512, &write_error \n");
  if (!do_write (archive_fd, zeros, 512, &write_error)
      || !do_write (archive_fd, zeros, 512, &write_error)) 
    success = false;
  printf (">>>>>make_tar_archive>> after do_write (archive_fd, zeros, 512, &write_error \n");
  close (archive_fd);
  printf (">>>>>make_tar_archive>> after close (archive_fd) \n");
  return success;
}

static bool
archive_file (char file_name[], size_t file_name_size,
              int archive_fd, bool *write_error) 
{
	printf ("   ------>archive_file>> getting in \n");
	printf ("   ------>archive_file>> open file_name=%s\n",file_name);
  int file_fd = open (file_name);
  	  printf ("   ------>archive_file>> open file_fd=%d\n",file_fd);
  if (file_fd >= 0) 
    {
      bool success;
      printf ("   ------>archive_file>> archive_fd=%d\n",archive_fd);
      if (inumber (file_fd) != inumber (archive_fd)) 
        {
    	  	  printf ("   ------>archive_file>> in inumber (file_fd)%zu != inumber (archive_fd)%zu\n",inumber (file_fd),inumber (archive_fd));
          if (!isdir (file_fd)){
        	  	  printf ("   ------>archive_file>> !isdir (file_fd)\n");
        	  	  success = archive_ordinary_file (file_name, file_fd,
        	                                               archive_fd, write_error);

          }

          else{
        	  	  printf ("   ------>archive_file>> isdir (file_fd)\n");
        	  	  success = archive_directory (file_name, file_name_size, file_fd,
        	                                           archive_fd, write_error);

          }

        }
      else
        {
    	  printf ("   ------>archive_file>> in inumber (file_fd)%zu == inumber (archive_fd)%zu\n",inumber (file_fd),inumber (archive_fd));
          /* Nothing to do: don't try to archive the archive file. */
          success = true;
        }
  
      close (file_fd);

      return success;
    }
  else
    {
      printf ("%s: open failed\n", file_name);
      return false;
    }
}

static bool
archive_ordinary_file (const char *file_name, int file_fd,
                       int archive_fd, bool *write_error)
{
  bool read_error = false;
  bool success = true;
  int file_size = filesize (file_fd);

  if (!write_header (file_name, USTAR_REGULAR, file_size,
                     archive_fd, write_error))
    return false;

  while (file_size > 0) 
    {
      static char buf[512];
      int chunk_size = file_size > 512 ? 512 : file_size;
      int read_retval = read (file_fd, buf, chunk_size);
      int bytes_read = read_retval > 0 ? read_retval : 0;

      if (bytes_read != chunk_size && !read_error) 
        {
          printf ("%s: read error\n", file_name);
          read_error = true;
          success = false;
        }

      memset (buf + bytes_read, 0, 512 - bytes_read);
      if (!do_write (archive_fd, buf, 512, write_error))
        success = false;

      file_size -= chunk_size;
    }

  return success;
}

static bool
archive_directory (char file_name[], size_t file_name_size, int file_fd,
                   int archive_fd, bool *write_error)
{
  size_t dir_len;
  bool success = true;
  printf ("       ===>archive_directory>> geting in\n");
  printf ("       ===>archive_directory>> file_name=%s file_name_size=%zu file_fd=%d archive_fd=%d\n",file_name,file_name_size,file_fd,archive_fd);
  dir_len = strlen (file_name);
  printf ("       ===>archive_directory>> strlen (file_name)=%d\n",dir_len);
  if (dir_len + 1 + READDIR_MAX_LEN + 1 > file_name_size) 
    {
      printf ("%s: file name too long\n", file_name);
      return false;
    }

  printf ("       ===>archive_directory>> before write_header\n");
  if (!write_header (file_name, USTAR_DIRECTORY, 0, archive_fd, write_error))
    return false;
  printf ("       ===>archive_directory>> after write_header\n");
  file_name[dir_len] = '/';
  printf ("       ===>archive_directory>> file_name=%s\n", file_name);

  printf ("       ===>archive_directory>> while (readdir)\n");
  while (readdir (file_fd, &file_name[dir_len + 1])) {
	  printf ("       ===>archive_directory>>  in while (readdir)\n");
	  printf ("       -------------------file_name=%s file_name_size=%s, archive_fd=%d\n",file_name,file_name_size,archive_fd);
	  if (!archive_file (file_name, file_name_size, archive_fd, write_error))
	        success = false;
  }
  printf ("       ===>archive_directory>> out while (readdir)\n");
  file_name[dir_len] = '\0';
  printf ("       ===>archive_directory>> geting out success=%d\n",success);
  return success;
}

static bool
write_header (const char *file_name, enum ustar_type type, int size,
              int archive_fd, bool *write_error) 
{
  static char header[512];
  return (ustar_make_header (file_name, type, size, header)
          && do_write (archive_fd, header, 512, write_error));
}

static bool
do_write (int fd, const char *buffer, int size, bool *write_error) 
{
  if (write (fd, buffer, size) == size) 
    return true;
  else
    {
      if (!*write_error) 
        {
          printf ("error writing archive\n");
          *write_error = true; 
        }
      return false; 
    }
}
