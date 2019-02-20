#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/synch.h"
#include "threads/thread.h"

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

  if (format)
    do_format ();

  /* init global lock for filsys */
  /* Driven by Miranda */
  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
/* Driven by Elad */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir)
{
  block_sector_t inode_sector = 0;
  struct dir *dir = get_path(name);
  char* file_name = get_name(name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) {
      free_map_release (inode_sector, 1);
  }
  dir_close (dir);
  free(file_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */

   /* Driven by Miguel  */
struct file *
filesys_open (const char *name)
{
  /* cannot open empty file names*/
  if (*name == NULL){
    return -1;
  }
  char* file_name = get_name(name);
  struct dir *dir = get_path(name);

  struct inode *inode = NULL;
  if ((strlen(file_name) == 0))
  {
    free(file_name);
    return (struct file *) dir;
  }

  if(dir_lookup(dir, file_name, &inode)) {
    dir_close (dir);
    return file_open (inode);
  }
  else {
    dir_lookup(dir, "..", &inode);
    dir_close (dir);
    return (struct file *) dir_open(inode);
  }


  dir_close (dir);
  free(file_name);

  if (!inode) {
    return NULL;
  }

  if (inode->is_dir)
    return (struct file *) dir_open(inode);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
   /* Driven by Miranda */
bool
filesys_remove (const char *name)
{
  struct dir* dir = get_path(name);
  char* file_name = get_name(name);
  struct inode* inode = NULL;
  bool success;
  if(dir_lookup(dir, file_name, &inode)) {
    success = dir != NULL && dir_remove (dir, file_name);
    dir_close (dir);
  }
  else {
    dir_lookup(dir, "..", &inode);
    struct dir* parent = dir_open(inode);
    success = parent != NULL && dir_remove (parent, file_name);
    dir_close (dir);
    dir_close(parent);
  }
  free(file_name);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
/* chage current directory to a given name */
/* Driven by Gage */
bool
filesys_chdir(const char* name)
{
  struct dir* dir = get_path(name);
  char* file_name = get_name(name);
  struct inode *inode = NULL;

  if(dir == NULL)
  {
    free(name);
    return false;
  }
  struct dir* temp;
  if(!dir_lookup(dir, "..", &inode)) {
    return false;
  }
  temp = dir_open(inode);
  dir_lookup(temp, file_name, &inode);
  if(inode == NULL) {
    dir_close(temp);
    return false;
  }

  if(dir == NULL)
  {
    free(file_name);
    return false;
  }
  dir_close(thread_current()->cur_dir);
  thread_current()->cur_dir = dir;
  free(file_name);
  return true;
}
