#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "devices/block.h"

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

  cache_init ();
  inode_init ();
  free_map_init ();
  dir_init ();

  if (format)
    do_format ();

  free_map_open ();
}

struct block *
get_fs_device (void)
{
  return fs_device;
}


unsigned long long
get_fs_device_write_cnt (void)
{
  return get_write_cnt (fs_device);
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  cache_close (fs_device);
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size)
{
  block_sector_t inode_sector = 0;

  char *dir_path, *target;
  split_path (path, &dir_path, &target);

  struct dir *dir = dir_resolve (dir_path);

  bool success = (dir != NULL
                  && free_map_alloc (&inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, target, inode_sector, false));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector);
  dir_close (dir);

  free (dir_path);
  free (target);

  return success;
}

/* Opens the file or directory with the given PATH and returns true
 * if success, or false if no such file or directory exists. If
 * success, the pointer and category of the given file or directory
 * will be written to PTR and IS_DIR. */
bool
filesys_open (const char *path, void **ptr, bool *is_dir)
{
  if (strlen (path) == 0)
    return false;

  char *dir_path, *target;
  split_path (path, &dir_path, &target);

  struct dir *dir = dir_resolve (dir_path);
  struct dir_entry entry;

  bool success = false;

  if (dir != NULL)
    {
      if (target[0] == '\0') /* Opening root. */
        {
          *is_dir = true;
          *ptr = (void *) dir_open (inode_open (dir_inumber (dir)));
          success = true;
        }
      else if (dir_lookup (dir, target, &entry))
        {
          *is_dir = entry.is_dir;
          struct inode *inode = inode_open (entry.inode_sector);
          *ptr = entry.is_dir ? (void *) dir_open (inode)
                              : (void *) file_open (inode);
          success = true;
        }
      dir_close (dir);
    }

  free (dir_path);
  free (target);

  return success;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path)
{
  char *dir_path, *target;
  split_path (path, &dir_path, &target);

  struct dir *dir = dir_resolve (dir_path);
  bool success = dir != NULL && dir_remove (dir, target);
  dir_close (dir);

  free (dir_path);
  free (target);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
