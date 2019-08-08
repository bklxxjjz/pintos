#include "filesys/directory.h"
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

struct dir_meta
  {
    struct inode *inode;                /* Unique backing store location. */
    int open_cnt;                       /* Number of instances using the dir. */
    struct list_elem elem;              /* List element. */
  };

struct list open_dirs;
struct lock open_dirs_lock;

void
dir_init (void)
{
  list_init (&open_dirs);
  lock_init (&open_dirs_lock);
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, block_sector_t parent_sector)
{
  if (inode_create (sector, 16 * sizeof (struct dir_entry)))
    {
      /* Add `.' and `..' subdirs. */
      struct dir *dir = dir_open (inode_open (sector));
      dir_add (dir, ".", sector, true);
      dir_add (dir, "..", parent_sector, true);
      dir_close (dir);
      return true;
    }
  else
    {
      return false;
    }
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode)
{
  if (inode == NULL)
    return NULL;

  lock_acquire (&open_dirs_lock);

  struct list_elem *e;
  struct dir_meta *dir_meta;
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_dirs); e != list_end (&open_dirs);
       e = list_next (e))
    {
      dir_meta = list_entry (e, struct dir_meta, elem);
      if (inode_get_inumber (dir_meta->inode) == inode_get_inumber (inode))
        return dir_reopen (dir_meta);
    }

  struct dir *dir = calloc (1, sizeof *dir);
  if (dir != NULL)
    {
      dir_meta = malloc (sizeof *dir_meta);
      dir_meta->inode = inode;
      dir_meta->open_cnt = 1;
      list_push_front (&open_dirs, &dir_meta->elem);

      dir->inode = inode;
      dir->pos = 0;
    }
  else
    {
      inode_close (inode);
      free (dir);
      dir = NULL;
    }

  lock_release (&open_dirs_lock);

  return dir;
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. Assumes already acquired
   OPEN_DIRS_LOCK and will release before return. */
struct dir *
dir_reopen (struct dir_meta *dir_meta)
{
  dir_meta->open_cnt++;

  struct dir *dir = calloc (1, sizeof *dir);
  if (dir != NULL)
    {
      dir->inode = inode_reopen (dir_meta->inode);
      dir->pos = 0;
    }
  else
    {
      inode_close (dir_meta->inode);
      free (dir);
      dir = NULL;
    }

  lock_release (&open_dirs_lock);
  return dir;
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir)
{
  if (dir == NULL)
    return;

  lock_acquire (&open_dirs_lock);

  struct list_elem *e;
  struct dir_meta *dir_meta;
  for (e = list_begin (&open_dirs); e != list_end (&open_dirs);
       e = list_next (e))
    {
      dir_meta = list_entry (e, struct dir_meta, elem);
      if (inode_get_inumber (dir_meta->inode) == inode_get_inumber (dir->inode))
        {
          --dir_meta->open_cnt;
          if (dir_meta->open_cnt == 0)
            {
              list_remove (&dir_meta->elem);
              free (dir_meta);
            }
          break;
        }
    }

  lock_release (&open_dirs_lock);

  inode_close (dir->inode);
  free (dir);
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
            struct dir_entry *entry)
{
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  return lookup (dir, name, entry, NULL);
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name,
         block_sector_t inode_sector, bool is_dir)
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
    return false;

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

  /* Write slot. This will write at the first empty block, or add a new block. */
  e.in_use = true;
  e.is_dir = is_dir;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  return success;
}

/* Checks if DIR is empty. A directory is empty iff it does no contain
 * any files and any directories other than `.' and `..'. We does not
 * consider recursive emptiness. */
bool
dir_empty (struct dir *dir)
{
  struct dir_entry e;
  size_t ofs;

  ASSERT (dir != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
    if (e.in_use)
      if (!e.is_dir || (strcmp (e.name, ".") && strcmp (e.name, "..")))
        return false;

  return true;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name)
{
  struct dir_entry entry;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &entry, &ofs))
    goto done;

  if (entry.is_dir)
    {
      /* Prevent removing of non-empty directory. */
      struct dir *target_dir = dir_open (inode_open (entry.inode_sector));
      if (!dir_empty (target_dir))
        {
          dir_close (target_dir);
          return false;
        }

      /* Prevent removing of directory used by other instances. */
      lock_acquire (&open_dirs_lock);
      struct list_elem *e;
      struct dir_meta *dir_meta;
      for (e = list_begin (&open_dirs); e != list_end (&open_dirs);
           e = list_next (e))
        {
          dir_meta = list_entry (e, struct dir_meta, elem);
          if (inode_get_inumber (dir_meta->inode) == inode_get_inumber (target_dir->inode)
              && dir_meta->open_cnt > 1)
            {
              lock_release (&open_dirs_lock);
              return false;
            }
        }
      lock_release (&open_dirs_lock);

      dir_close (target_dir);
    }

  /* Open inode. */
  inode = inode_open (entry.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  entry.in_use = false;
  inode_write_at (dir->inode, &entry, sizeof entry, ofs);

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
      if (e.in_use && strcmp (e.name, ".") && strcmp (e.name, ".."))
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        }
    }
  return false;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
 * next call will return the next file name part. Returns 1 if successful, 0 at
 * end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp)
{
  const char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes. If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0')
    {
      if (dst < part + NAME_MAX)
        *dst++ = *src;
      else
        return -1;
      src++;
    }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Opens a directory recursively given its PATH, or NULL if no such
 * directory exists. Note that if PATH represents a file, this will
 * returns NULL. */
struct dir *
dir_resolve (const char *_path)
{
  ASSERT (_path != NULL);

  char *path = malloc (strlen (_path) + 1);
  char *path_ori = path;
  strlcpy (path, _path, strlen (_path) + 1);
  char part[NAME_MAX + 1];

  struct dir *cur_dir = path[0] == '/' ? dir_open_root ()
                                       : dir_open (thread_current ()->cwd->inode);

  struct dir_entry entry;
  int result;
  while ((result = get_next_part (part, (const char **) &path)))
    {
      if (result == -1)
        {
          dir_close (cur_dir);
          free (path_ori);
          return NULL;
        }

      dir_lookup (cur_dir, part, &entry);
      if (!entry.is_dir)
        {
          dir_close (cur_dir);
          free (path_ori);
          return NULL;
        }

      dir_close (cur_dir);
      cur_dir = dir_open (inode_open (entry.inode_sector));
    }

  free (path_ori);

  return cur_dir;
}

/* Split PATH into the directory path and the target name, i.e.,
 * store everything after the last `/' into TARGET and the rest
 * into DIR. Trailing `/' at the end will be omitted. Note that
 * the caller must free the DIR and TARGET after usage. */
void
split_path (const char *path, char **dir, char **target)
{
  ASSERT (path != NULL);

  int start;
  for (start = 0; start < (int) strlen (path) && path[start] == '/'; ++start);

  int end;
  for (end = strlen (path) - 1; end >= start && path[end] == '/'; --end);

  int split;
  for (split = end; split >= 0 && path[split] != '/'; --split);

  *dir = malloc (split + 2);
  strlcpy (*dir, path, split + 2 );
  *target = malloc (end - split + 1);
  strlcpy (*target, path + split + 1, end - split + 1);
}

block_sector_t
dir_inumber (struct dir *dir)
{
  return inode_get_inumber (dir->inode);
}
