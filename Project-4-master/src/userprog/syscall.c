#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include <console.h>
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

static void syscall_handler (struct intr_frame *);
void getArgs(int size, void *esp, int* args);
void halt(void);
void exit (int status);
bool valid_ptr(void *p);
tid_t exec (const char *cmd_line);
int open (const char *file);
int wait (tid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
bool valid_buffer(void *buffer, unsigned size);

bool mkdir(const char *);
bool readdir (int fd, char *name);
bool chdir (const char *dir);
bool isdir (int fd);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *pd;
  struct thread *cur  = thread_current();
  pd = cur->pagedir;
  uint32_t *esp = f->esp;

  /* null, outside of bounds, or unmapped, valid_ptr method at bottom */
  if (!valid_ptr(esp)) {
    exit(-1);
  }
  uint32_t getCall = *esp;

  /* switch method driven and changed by all, Gage made bones of switch case */
  switch(getCall) {

    /* 0 */
    case(SYS_HALT):
      halt();
      break;

    /* 1 */
    case(SYS_EXIT):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
      exit(*(esp+1));
      break;

    /* 2 */
    case(SYS_EXEC):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
      f->eax = exec(*(esp+1));
      break;

    /* 3 */
    case(SYS_WAIT):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
      f->eax = wait(*(int *)(esp+1));
      break;

    /* 4 */
    case(SYS_CREATE):
      if (!valid_ptr((esp+1)) || !valid_ptr((esp+2))) {
        exit(-1);
      }

      f->eax = create(*(esp+1),*(int *)(esp+2));
      break;

    /* 5 */
    case(SYS_REMOVE):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }

      f->eax = remove(*(esp+1));
      break;

    /* 6 */
    case(SYS_OPEN):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }

      f->eax = open(*(esp+1));
      break;

    /* 7 */
    case(SYS_FILESIZE):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }

      f->eax = filesize(*(int *)(esp+1));
      break;

    /* 8 */
    case(SYS_READ):
      if (!valid_ptr((esp+1)) || !valid_ptr((esp+2)) || !valid_ptr((esp+3))) {
        exit(-1);
      }

      f->eax = read(*(int *)(esp+1),*(int *)(esp+2),*(int *)(esp+3));
      break;

    /* 9 */
    case(SYS_WRITE):
      if (!valid_ptr((esp+1)) || !valid_ptr((esp+2)) || !valid_ptr((esp+3))) {
        exit(-1);
      }
      f->eax = write(*(int *)(esp+1),*(int *)(esp+2),*(int *)(esp+3));
      break;

    /* 10 */
    case(SYS_SEEK):
      if (!valid_ptr(esp+1) || !valid_ptr(esp+2)) {
        exit(-1);
      }
      seek(*(int *)(esp+1), *(int *)(esp+2));
      break;

    /* 11 */
    case(SYS_TELL):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
      f->eax = tell(*(int *)(esp+1));
      break;

    /* 12 */
    case(SYS_CLOSE):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
      close(*(int *)(esp+1));
      break;

    case (SYS_MKDIR):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
    	f->eax = mkdir(*(esp+1));
    	break;

    case (SYS_ISDIR):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
    	f->eax = isdir(*(esp+1));
    	break;

    case (SYS_READDIR):
      if (!valid_ptr(esp+1) || !valid_ptr(esp+2)) {
        exit(-1);
      }
      f->eax = readdir(*(esp+1), *(esp+2));
    break;

    case (SYS_CHDIR):
      if (!valid_ptr(esp+1)) {
        exit(-1);
      }
      f->eax = chdir(*(esp+1));
      break;

    default:
      break;
  }
  /* return not needed, but wanted */
  return;
}

/* Halts the running process */
void
halt() {
  shutdown_power_off();
}

/* exits the running process */
/* driven by Elad */
void
exit (int status) {
  struct thread *t = thread_current();
  t->exit_status = status;
  printf("%s: exit(%d)\n" ,t->name, status);
  thread_exit();
}

/*
  Changes current working directory to dir
  Returns false on failure
  Driven by Miguel
*/
bool
chdir (const char *dir){
  return filesys_chdir(dir);
}

/*
  Creates a relative or absolute directory
  Returns false if dir already exists or fails to arrive at the indicated
  directory.
  Driven by Miguel
*/
bool
mkdir (const char *name){
  block_sector_t inode_sector = 0;
  bool success = false;
  struct dir *dir = get_path(name);
  char* file_name = get_name(name);
  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 2)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) {
    free_map_release (inode_sector, 1);
  }

  struct inode* inode = inode_open(inode_sector);
  struct dir* child_dir = dir_open(inode);
  dir_add(child_dir, ".", inode_sector);
  dir_add(child_dir, "..", dir_get_inode(dir)->sector);
  dir_close(child_dir);
  dir_close(dir);
  free(file_name);
  return success;
}

/*
  Reads a directory entryfrom file descriptor and saves name of file into
  name if succesful, otherwise return false.
*/
bool
readdir (int fd, char *name){
  struct file* file = thread_current()->used_files[fd];
  if (file == NULL)
    return false;

  struct inode* inode = file_get_inode(file);

  if(inode == NULL)
    return false;

  if(!inode->is_dir)
   return false;

   return dir_readdir((struct dir*) file, name);
}

/*
  Returns true if
    file directory represents a directory
  Returns false if
    file directory only represents a file
*/
/* Driven by Gage */
bool
isdir (int fd){
  struct thread *t = thread_current();
  bool isdir = false;
  if (t->used_files[fd] == NULL) {
    exit(-1);
  }
  struct file *file = t->used_files[fd];
  struct inode *inode = file_get_inode(file);
  if (inode != NULL) {

    isdir = inode->is_dir;
  }

  return isdir;

}

/*
  Returns inode number of the file directory's respective inode.
  fd can represent a file or directory.
*/
/*Driven by Miguel*/

int
inumber (int fd){
  struct thread *t = thread_current();
  int inumber=0;

  if (t->used_files[fd] == NULL) {
    exit(-1);
  }
  struct file *file = t->used_files[fd];
  struct inode *inode = file_get_inode(file);
  inumber = byte_to_sector(inode, inode->length);
  return inumber;

}

/* creates a fork and execs the process if pointer is valid */
/* Driven by Gage */
tid_t
exec (const char *cmd_line) {

  if (!valid_ptr(cmd_line))
    exit(-1);

  tid_t tid = process_execute (cmd_line);

  if(tid == TID_ERROR)
    return -1;

  return tid;
}

/* Waits on the TID(PID) process to finish */
/* Driven by Gage, what a champ with this large function */
int
wait (tid_t pid) {
  return process_wait(pid);
}

/* create a file of the initial size if pointer is valid */
/* Driven by Miranda */
bool
create (const char *file, unsigned initial_size) {
  if(!valid_buffer(file, initial_size))
    exit(-1);
  if (!valid_ptr(file))
    exit(-1);
  if(filesys_create(file, initial_size, false))
    return true;

  return false;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails.
   exits if pointer is invalid */
/* Driven by Miranda */
bool
remove (const char *file) {
  if (!valid_ptr(file)) {
    exit(-1);
  }

  bool removeReturn = filesys_remove(file);
  return removeReturn;
}

/* Opens the file with the given pointer.
   Returns the fd if successful or -1
   otherwise.
   Fails if no file named NAME exists,if an internal memory
   allocation fails, or if the pointer is invalid. */
/* Driven by Elad */
int
open (const char *file) {
  if (!valid_ptr(file)) {
    exit(-1);
  }

  int i;

  struct file *f = filesys_open(file);

  struct thread *t = thread_current();

  if(f == NULL || f == -1)
    return -1;

  for(i=2; i<MAX_FILES; i++) {
    if(t->used_files[i]==NULL) {
      t->used_files[i] = f;
      return i;
    }
  }
  return -1;
}

/* if fd is value, returns the size */
/* Driven by Miguel */
int
filesize (int fd) {
  struct thread *t = thread_current();
  struct file *file;
  if (t->used_files[fd] == NULL)
    return -1;
  file = t->used_files[fd];

  int filesize_return = file_length(file);
  return filesize_return;
}

/* reads file at fd location, unless takes standard input,
   if pointer is valid */
/* Driven by Elad */
int
read (int fd, void *buffer, unsigned size) {
  if(!valid_buffer(buffer,size))
    exit(-1);
  if (!valid_ptr(buffer)) {
    exit(-1);
  }

  /* invalid fd, check array size */
  if (fd < STDIN_FILENO || fd > MAX_FILES - 1) {
    exit(-1);
  }

  if (fd == STDIN_FILENO) {
    unsigned i;
    int *input_buffer = (int *)buffer;
    for(i = 0; i < size; i++)
      input_buffer[i] = input_getc();
    return size;
  }

  if(fd == STDIN_FILENO || fd == STDOUT_FILENO) {
    exit(-1);
  }

  struct thread *t = thread_current();
  struct file *file;
  if (t->used_files[fd] == NULL)
    return -1;

  file = t->used_files[fd];

  int read_return = file_read(file,buffer,size);
  return read_return;
}

/* Writes a file at fd location, if pointer is valid */
/* Driven by Miguel */
int
write (int fd, const void *buffer, unsigned size)
{
  if (!valid_ptr(buffer)) {
    exit(-1);
  }
  /* invalid fd, check array size */
  if (fd < STDIN_FILENO || fd > MAX_FILES - 1) {
    exit(-1);
  }

  struct thread *t = thread_current();

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    return size;
  }

  struct file *file;

  if (t->used_files[fd] == NULL) {
    exit(-1);
  }

  file = t->used_files[fd];

  if(file_get_inode(file)->is_dir) {
    exit(-1);
  }

  int write_return = file_write(file, buffer, size);
  return write_return;
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */
/* Driven by Miranda */
void
seek (int fd, unsigned position) {
  struct thread *t = thread_current();

  struct file *file;
  if (t->used_files[fd] == NULL) {
    exit(-1);
  }

  file = t->used_files[fd];

  file_seek (file, position);

}

/* Returns the current position in FILE as a byte offset from the
   start of the file. */
/* Driven by Elad */
unsigned
tell (int fd) {
  struct thread *t = thread_current();
  struct file *file;

  /* invalid fd, check array size */
  if (fd < STDIN_FILENO || fd > MAX_FILES - 1) {
    exit(-1);
  }

  if (t->used_files[fd] == NULL) {
    exit(-1);
  }

  file = t->used_files[fd];

  unsigned tell_return = file_tell(file);
  return tell_return;
}

/* closes the file at the fd index if valid, and removes from
   fd list on the thread */
/* Driven by Miranda */
void
close (int fd) {
  struct thread *t = thread_current();
  struct file *file;

  /* If an invalid fd and invalid array size, exit -1 */
  if (fd < STDIN_FILENO || fd > MAX_FILES - 1) {
    exit(-1);
  }

  file = t->used_files[fd];
  t->used_files[fd] = NULL;
  file_close(file);
}

bool valid_buffer(void *buffer, unsigned size){
	char *buff_pointer = (char *)buffer;
	unsigned i;
	for(i = 0; i < (int)(size/PGSIZE); i++){
		if(!valid_ptr((void *)buff_pointer)){
			return false;
		}
		buff_pointer+=PGSIZE;
	}
	return true;
}

/* Validates null, user, or unmapped pointers */
/* Driven by Gage */
bool
valid_ptr (void *p) {
  if (p == NULL)
    return false;

  if(!is_user_vaddr(p))
    return false;

  if(pagedir_get_page(thread_current()->pagedir, p) == NULL)
    return false;

  return true;
}
