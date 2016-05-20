#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

typedef int pid_t;
struct lock file_lock;
struct list files;
struct file_elem 
  {
    pid_t holder;               /* PID of process holding the file. */
    int fd;                     /* File's file descriptor. */
    struct file* file;          /* Pointer to actual file. */
    struct list_elem elem;      /* List element for file*/
  };

static void syscall_handler (struct intr_frame *);
static void halt (void);
static pid_t exec (const char *cmd_line);
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned length);
static int write (int fd, const void *buffer, unsigned length);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
static int add_file (struct file *f);
static void validate_address(void* address);
static struct file* get_file_by_descriptor (int fd);
static void remove_file (int fd);
static int get_last_fd (void);
static int* map_pointer (const void * ptr);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
  list_init (&files);
}

static void
syscall_handler (struct intr_frame *f) 
{
  if (!is_user_vaddr ((void *) f->esp) || pagedir_get_page (thread_current ()->pagedir, f->esp) == NULL)
    exit (-1);
  
  int syscall = *(int *) f->esp;

  //At most 3 arguments
  int one = *((int *) f->esp + 1);
  int two = *((int *) f->esp + 2);
  int three = *((int *) f->esp + 3);

  switch (syscall)
  {
    case SYS_HALT:
    {
      halt ();
      break;
    }
    case SYS_EXIT:
    {
      validate_address((void *) one);
      exit (one);
      break;
    }
    case SYS_EXEC:
    {
      validate_address((void *) one);
      f->eax = exec ((char *) map_pointer ((void *) one));
      break;
    }
    case SYS_WAIT:
    {
      validate_address((void *) one);
      f->eax = wait (one);
      break;
    }
    case SYS_CREATE:
    {
      validate_address((void *) one);
      f->eax = create ((char *) map_pointer ((void *) one) , (unsigned) two);
      break;
    }
    case SYS_REMOVE:
    {
      validate_address((void *) one);
      f->eax = remove ((char *) one);
      break;
    }
    case SYS_OPEN:
    {
      f->eax = open ((char *) map_pointer ((void *) one));
      break;
    }
    case SYS_FILESIZE:
    {
      validate_address((void *) one);
      f->eax = filesize (one);
      break;
    }
    case SYS_READ:
    {
      validate_address((void *) two);
      f->eax = read (one, map_pointer ((void *) two), (unsigned) three);
      break;
    }
    case SYS_WRITE:
    {
      validate_address((void *) two);
      f->eax = write (one, map_pointer ((void *) two), (unsigned) three);
      break;
    }
    case SYS_SEEK:
    {
      validate_address((void *) one);
      seek (one, (unsigned) two);
      break;
    }
    case SYS_TELL:
    {
      validate_address((void *) one);
      f->eax = tell (one);
      break;
    }
    case SYS_CLOSE:
    {
      validate_address((void *) one);
      close (one);
      break;
    }
  }
} 

/* Terminates Pintos by calling shutdown_power_off. */
static void
halt ()
{
  shutdown_power_off ();
}

void
exit (int status)
{
  // Cleanup for OOM, disable to get other tests to work.
  while (!list_empty (&files))
    close (list_entry(list_back (&files), struct file_elem, elem)->fd);

  thread_current ()->return_status = status;
  thread_exit ();
}

/*
  Create and run executable and args. Returns process ID.
*/
static pid_t
exec (const char *cmd_line)
{
  int pid = process_execute (cmd_line);
  struct thread *t = get_thread_by_id (pid);
  if (t == NULL)
    return -1;

  sema_down (&t->load_semaphore);

  if (!t->loaded)
    return -1;

  return pid;
}

static int
wait (pid_t pid)
{
  return process_wait (pid);
}

static bool
create (const char *file, unsigned initial_size)
{
  lock_acquire (&file_lock);
  bool c = filesys_create (file, initial_size);
  lock_release (&file_lock);
  return c;
}

static bool
remove (const char *file)
{
  lock_acquire (&file_lock);
  bool r = filesys_remove (file);
  lock_release (&file_lock);
  return r;
}

static int
open (const char *file)
{
  lock_acquire (&file_lock);
  struct file *f = filesys_open (file);
  
  if (f == NULL) 
  {
    lock_release (&file_lock);
    return -1;
  }
  
  int fd = add_file (f);
  lock_release (&file_lock);
  return fd;
}

static int
filesize (int fd)
{
  lock_acquire (&file_lock);
  struct file *file = get_file_by_descriptor (fd);
  
  if (file == NULL) // FD doesn't exist
  {
    lock_release (&file_lock);
    return -1;
  }

  int s = file_length (file);
  lock_release (&file_lock);
  return s;
}

static int
read (int fd, void *buffer, unsigned size)
{
  lock_acquire (&file_lock);
  struct file *file = get_file_by_descriptor (fd);

  if (file == NULL) // FD doesn't exist
  {
    lock_release (&file_lock);
    return -1;
  }
  
  int r = file_read (file, buffer, size);
  lock_release (&file_lock);
  return r;
}

static int
write (int fd, const void *buffer, unsigned length)
{
  if (fd == STDOUT_FILENO) //Write to Console
  {
    putbuf ((char *) buffer, length);
    return length;
  }

  lock_acquire (&file_lock);

  struct file *file = get_file_by_descriptor (fd);

  if (file == NULL) // FD doesn't exist
  {
    lock_release (&file_lock);
    return -1;
  }

  int w = file_write (file, buffer, length);
  lock_release (&file_lock);
  return w;
}

static void
seek (int fd, unsigned position)
{
  lock_acquire (&file_lock);
  struct file *file = get_file_by_descriptor (fd);
  
  if (file == NULL) // FD doesn't exist
  {
    lock_release (&file_lock);
    return;
  }

  file_seek (file, position);
  lock_release (&file_lock);
}

static unsigned
tell (int fd)
{
  lock_acquire (&file_lock);
  struct file *file = get_file_by_descriptor (fd);
  
  if (file == NULL) // FD doesn't exist
  {
    lock_release (&file_lock);
    return -1;
  }

  unsigned i = file_tell (get_file_by_descriptor (fd));
  lock_release (&file_lock);
  return i;
}

static void
close (int fd)
{
  lock_acquire (&file_lock);

  struct file *f = get_file_by_descriptor (fd);
  if (f == NULL)
  {
    lock_release (&file_lock);
    return;
  }

  remove_file (fd);
  lock_release (&file_lock);
}

static void
validate_address(void* address)
{
  if (!is_user_vaddr (address))
    exit (-1);
}

static int
get_last_fd ()
{
  if (list_empty (&files))
    return 1;
  return list_entry(list_back (&files), struct file_elem, elem)->fd;
}

static int
add_file (struct file *f)
{
  struct file_elem *file_e = malloc (sizeof(struct file_elem));
  int next = get_last_fd () + 1;
  file_e->holder = thread_current ()->tid;
  file_e->fd = next;
  file_e->file = f;
  list_push_back (&files, &file_e->elem);
  return next;
}

static void
remove_file (int fd)
{
  struct list_elem *e = list_begin (&files);
  while (e != list_end (&files))
    {
      struct file_elem *file = list_entry (e, struct file_elem, elem);
      struct list_elem *next = list_next (e);
      
      if (fd == file->fd)
      {
        // Not owned by current file, do not close.
        if (file->holder != thread_current ()->tid)
          return;

        file_close (file->file);
        list_remove (e);
        free (file);
      }
      
      e = next;
    }
}

static struct file*
get_file_by_descriptor (int fd)
{
  for (struct list_elem *e = list_begin (&files); e != list_end (&files); e = list_next (e))
    {
      struct file_elem *f = list_entry (e, struct file_elem, elem);

      if (f->fd == fd)
        return f->file;
    }

  return NULL;
}

/* Get pointer in mapped virtual memory from program. */
static int*
map_pointer (const void * ptr)
{
  validate_address((void*) ptr);
  int *converted = pagedir_get_page (thread_current ()->pagedir, ptr);
  if (converted == NULL)
    exit (-1);

  return converted;
}
