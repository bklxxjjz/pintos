#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/cache.h"

static void syscall_handler (struct intr_frame *);
static int validate_addr (void *addr);
static void validate_args (void *esp, int argc);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


int
sys_practice (int i)
{
  return i + 1;
}

void
sys_halt (void)
{
  shutdown_power_off ();
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();

  cur->exit_status->exit_code = status;

  /* Decrement self REF_COUNT and try to free self EXIT_STATUS. */
  lock_acquire (&cur->exit_status->lock);
  cur->exit_status->ref_count--;
  if (cur->exit_status->ref_count == 0)
    free (cur->exit_status);
  else
    sema_up (&cur->exit_status->sema);
  lock_release (&cur->exit_status->lock);

  /* Decrement REF_COUNT of all children and try to free child's EXIT_STATUS. */
  while (!list_empty (&cur->child_status_list))
    {
      struct list_elem *e = list_pop_front (&cur->child_status_list);
      struct exit_status_t *exit_status = list_entry (e, struct exit_status_t, elem);
      lock_acquire (&exit_status->lock);
      exit_status->ref_count--;
      if (exit_status->ref_count == 0)
        free (exit_status);
      else
        lock_release (&exit_status->lock);
    }

  printf ("%s: exit(%d)\n", (char *) &cur->name, status);
  thread_exit ();
}

pid_t
sys_exec (const char *cmd)
{
  return process_execute (cmd);
}

int
sys_wait (pid_t pid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->child_status_list); e != list_end (&cur->child_status_list);
       e = list_next (e))
    {
      struct exit_status_t *exit_status = list_entry (e, struct exit_status_t, elem);
      if ((pid_t) exit_status->tid == pid)
        {
          list_remove (e);
          sema_down (&exit_status->sema);

          int exit_code = exit_status->exit_code;

          lock_acquire (&exit_status->lock);
          exit_status->ref_count--;
          if (exit_status->ref_count == 0)
            free (exit_status);
          else
            lock_release (&exit_status->lock);

          return exit_code;
        }
    }
  return -1;
}


bool
sys_create (const char *path, unsigned initial_size)
{
  return filesys_create (path, initial_size);
}

bool
sys_remove (const char *path)
{
  return filesys_remove (path);
}

int
sys_open (const char *path)
{
  struct thread *cur = thread_current ();
  struct fd_t *fd = malloc (sizeof (struct fd_t));

  if (filesys_open (path, &fd->ptr, &fd->is_dir))
    {
      fd->num = cur->next_fd_num++;
      list_push_back (&cur->fd_list, &fd->elem);
      return fd->num;
    }
  else
    {
      free (fd);
      return -1;
    }
}

int
sys_filesize (int fd_num)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        {
          if (!fd->is_dir)
            return file_length ((struct file *) fd->ptr);
          else
            return -1;
        }
    }
  return -1;
}

int
sys_read (int fd_num, void *buffer, unsigned size)
{
  if (fd_num == 0)
    {
      unsigned i;
      for (i = 0; i < size; ++i)
        *((uint8_t *) buffer++) = input_getc ();
      return size;
    }
  else
    {
      struct thread *cur = thread_current ();
      struct list_elem *e;
      for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
        {
          struct fd_t *fd = list_entry (e, struct fd_t, elem);
          if (fd->num == fd_num)
            {
              if (!fd->is_dir)
                return file_read ((struct file *) fd->ptr, buffer, size);
              else
                return -1;
            }
        }
      return -1;
    }
}

int
sys_write (int fd_num, const void *buffer, unsigned size)
{
  if (fd_num == 1)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    {
      struct thread *cur = thread_current ();
      struct list_elem *e;
      for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
        {
          struct fd_t *fd = list_entry (e, struct fd_t, elem);
          if (fd->num == fd_num)
            {
              if (!fd->is_dir)
                return file_write ((struct file *) fd->ptr, buffer, size);
              else
                return -1;
            }
        }
      return -1;
    }
}

int
sys_seek (int fd_num, unsigned position)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        {
          if (!fd->is_dir)
            {
              file_seek ((struct file *) fd->ptr, position);
              return 0;
            }
          else
            {
              return -1;
            }
        }
    }
  return -1;
}

int
sys_tell (int fd_num)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        {
          if (!fd->is_dir)
            return file_tell ((struct file *) fd->ptr);
          else
            return - 1;
        }
    }
  return -1;
}

int
sys_close (int fd_num)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        {
          if (fd->is_dir)
            dir_close ((struct dir *) fd->ptr);
          else
            file_close ((struct file *) fd->ptr);
          list_remove (e);
          free (fd);
          return 0;
        }
    }
  return -1;
}


bool sys_chdir (const char *path)
{
  void *ptr;
  bool is_dir;
  if (filesys_open (path, &ptr, &is_dir))
    {
      if (is_dir)
        {
          dir_close (thread_current ()->cwd);
          thread_current ()->cwd = (struct dir *) ptr;
          return true;
        }
      else
        {
          file_close ((struct file *) ptr);
          return false;
        }
    }
  else
    {
      return false;
    }
}

bool sys_mkdir (const char *path)
{
  char *dir_path, *target;
  split_path (path, &dir_path, &target);

  struct dir *dir;
  bool success = false;
  block_sector_t sector;
  if ((dir = dir_resolve (dir_path)) != NULL)
    {
      sector = 0;
      if (free_map_calloc (&sector)
          && dir_create (sector, dir_inumber (dir))
          && dir_add (dir, target, sector, true))
        success = true;
      dir_close (dir);
    }

  if (!success && sector != 0)
    free_map_release (sector);

  free (dir_path);
  free (target);

  return success;
}

bool sys_readdir (int fd_num, char *name)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        {
          if (fd->is_dir)
            return dir_readdir ((struct dir *) fd->ptr, name);
          else
            return false;
        }
    }
  return false;
}

bool sys_isdir (int fd_num)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        return fd->is_dir;
    }
  return false;
}

int sys_inumber (int fd_num)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->fd_list); e != list_end (&cur->fd_list); e = list_next (e))
    {
      struct fd_t *fd = list_entry (e, struct fd_t, elem);
      if (fd->num == fd_num)
        {
          if (fd->is_dir)
            return (int) dir_inumber ((struct dir *) fd->ptr);
          else
            return (int) file_inumber ((struct file *) fd->ptr);
        }
    }
  return -1;
}


static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *args = ((uint32_t *) f->esp);
  /* Validating ARGS[0] */
  validate_addr ((void *) args);
  validate_addr ((void *) args + sizeof (void *) - 1);

  char *ptr;
  unsigned i;

  switch (args[0])
    {
      case SYS_PRACTICE:
        validate_args (f->esp, 1);
      f->eax = sys_practice ((int) args[1]);
      break;

      case SYS_HALT:
        sys_halt ();
      break;

      case SYS_EXIT:
        validate_args (f->esp, 1);
      sys_exit ((int) args[1]);
      break;

      case SYS_EXEC:
        validate_args (f->esp, 1);
      for (ptr = (char *) args[1]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_exec ((char *) args[1]);
      break;

      case SYS_WAIT:
        validate_args (f->esp, 1);
      f->eax = sys_wait ((pid_t) args[1]);
      break;

      case SYS_CREATE:
        validate_args (f->esp, 2);
      for (ptr = (char *) args[1]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_create ((char *) args[1], (unsigned) args[2]);
      break;

      case SYS_REMOVE:
        validate_args (f->esp, 1);
      for (ptr = (char *) args[1]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_remove ((char *) args[1]);
      break;

      case SYS_OPEN:
        validate_args (f->esp, 1);
      for (ptr = (char *) args[1]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_open ((char *) args[1]);
      break;

      case SYS_FILESIZE:
        validate_args (f->esp, 1);
      f->eax = sys_filesize ((int) args[1]);
      break;

      case SYS_READ:
        validate_args (f->esp, 3);
      for (i = 0; validate_addr ((void *) args[2] + i) && i < (unsigned) args[3]; ++i);
      f->eax = sys_read ((int) args[1], (void *) args[2], (unsigned) args[3]);
      break;

      case SYS_WRITE:
        validate_args (f->esp, 3);
      for (i = 0; validate_addr ((void *) args[2] + i) && i < (unsigned) args[3]; ++i);
      f->eax = sys_write ((int) args[1], (void *) args[2], (unsigned) args[3]);
      break;

      case SYS_SEEK:
        validate_args (f->esp, 2);
      f->eax = sys_seek ((int) args[1], (unsigned) args[2]);
      break;

      case SYS_TELL:
        validate_args (f->esp, 1);
      f->eax = sys_tell ((int) args[1]);
      break;

      case SYS_CLOSE:
        validate_args (f->esp, 1);
      f->eax = sys_close ((int) args[1]);
      break;

      case SYS_CHDIR:
        validate_args (f->esp, 1);
      for (ptr = (char *) args[1]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_chdir ((char *) args[1]);
      break;

      case SYS_MKDIR:
        validate_args (f->esp, 1);
      for (ptr = (char *) args[1]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_mkdir ((char *) args[1]);
      break;

      case SYS_READDIR:
        validate_args (f->esp, 2);
      for (ptr = (char *) args[2]; validate_addr (ptr) && *ptr != '\0'; ++ptr);
      f->eax = sys_readdir ((int) args[1], (char *) args[2]);
      break;

      case SYS_ISDIR:
        validate_args (f->esp, 1);
      f->eax = sys_isdir ((int) args[1]);
      break;

      case SYS_INUMBER:
        validate_args (f->esp, 1);
      f->eax = sys_inumber ((int) args[1]);
      break;

      case SYS_WRITE_CNT:
        f->eax = (unsigned) get_fs_device_write_cnt ();
      break;

      case SYS_HIT_RATE:
        f->eax = get_hit_rate ();
      break;

      case SYS_CACHE_RESET:
        cache_reset ();
      break;

      default:
        sys_exit (-1);
    }
}

/* Validating ADDR. Exit if ADDR is not a valid user vaddr. */
static int
validate_addr (void *addr)
{
  if (!addr || !is_user_vaddr (addr)
      || !pagedir_get_page (thread_current ()->pagedir, addr))
    {
      sys_exit (-1);
    }
  return 1;
}

/* Validating the address of the arguments of the syscall. */
static void
validate_args (void *esp, int argc)
{
  validate_addr (esp + (argc + 1) * sizeof (void *) - 1);
}
