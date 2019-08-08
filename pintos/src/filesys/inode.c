#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stddef.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCKS 12
#define INDIRECT_BLOCKS 128
#define DBL_INDIRECT_BLOCKS (128 * 128)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes */
    block_sector_t direct[12];          /* 6KiB from direct pointers */
    block_sector_t indirect;            /* 64KiB from level 1 indirect pointer */
    block_sector_t dbl_indirect;        /* 8MiB from level 2 indirect pointer */
    unsigned magic;                     /* Magic number */
    uint32_t unused[112];               /* Not used */
  };

unsigned inode_magic = INODE_MAGIC;

block_sector_t inode_create_sector (block_sector_t, off_t);
void inode_free_sector (block_sector_t);

bool inode_alloc_direct (block_sector_t, int);
bool inode_alloc_indirect (block_sector_t, int);
bool inode_alloc_dbl_indirect (block_sector_t, int);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock lock;                   /* Lock for the metadata of the inode. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns 0 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
inode_get_sector (const block_sector_t inode_sector, const off_t pos)
{
  size_t idx = pos / BLOCK_SECTOR_SIZE;
  block_sector_t sector = 0;

  if (idx < DIRECT_BLOCKS)
    {
      cache_read (fs_device, inode_sector, &sector,
                  offsetof (struct inode_disk, direct)
                    + idx * sizeof (block_sector_t),
                  sizeof (block_sector_t));
    }
  else if (idx < DIRECT_BLOCKS + INDIRECT_BLOCKS)
    {
      cache_read (fs_device, inode_sector, &sector,
                  offsetof (struct inode_disk, indirect),
                  sizeof (block_sector_t));
      cache_read (fs_device, sector, &sector,
                  (idx - DIRECT_BLOCKS) * sizeof (block_sector_t),
                  sizeof (block_sector_t));
    }
  else if (idx < DIRECT_BLOCKS + INDIRECT_BLOCKS + DBL_INDIRECT_BLOCKS)
    {
      cache_read (fs_device, inode_sector, &sector,
                  offsetof (
      struct inode_disk, indirect),
      sizeof (block_sector_t));

      int dbl_num = (idx - DIRECT_BLOCKS - INDIRECT_BLOCKS)
                    / INDIRECT_BLOCKS;
      cache_read (fs_device, sector, &sector,
                  dbl_num * sizeof (block_sector_t),
                  sizeof (block_sector_t));

      int dbl_offset = (idx - DIRECT_BLOCKS - INDIRECT_BLOCKS)
                       % INDIRECT_BLOCKS;
      cache_read (fs_device, sector, &sector,
                  dbl_offset * sizeof (block_sector_t),
                  sizeof (block_sector_t));
    }

  return sector;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Allocates a new block if INODE does not contain data for a byte
   at offset POS, and the newly allocated block will be zero-filled.
   Returns 0 if allocation fails. */
block_sector_t
inode_create_sector (const block_sector_t inode_sector, const off_t pos)
{
  size_t idx = pos / BLOCK_SECTOR_SIZE;

  block_sector_t sector, indirect, dbl_indirect;

  if (idx < DIRECT_BLOCKS)
    {
      /* Read DISK_INODE->DIRECT[IDX] into SECTOR. */
      cache_read (fs_device, inode_sector, &sector,
                  offsetof (struct inode_disk, direct)
                    + idx * sizeof (block_sector_t),
                  sizeof (block_sector_t));

      /* If SECTOR == 0, then try to allocate a new block.
       * If success, write back to DISK_INODE; otherwise, SECTOR will still be 0.  */
      if (!sector)
        {
          if (free_map_calloc (&sector))
            cache_write (fs_device, inode_sector, &sector,
                         offsetof (struct inode_disk, direct)
                           + idx * sizeof (block_sector_t),
                         sizeof (block_sector_t));
          else
            return 0;
        }

      return sector;
    }
  else if (idx < DIRECT_BLOCKS + INDIRECT_BLOCKS)
    {
      /* Read DISK_INODE->INDIRECT into INDIRECT */
      cache_read (fs_device, inode_sector, &indirect,
                  offsetof (struct inode_disk, indirect),
                  sizeof (block_sector_t));

      /* If INDIRECT == 0, then try to allocate a new block.
       * If success, write back to DISK_INODE; otherwise fail.  */
      if (!indirect)
        {
          if (free_map_calloc (&indirect))
            cache_write (fs_device, inode_sector, &indirect,
                         offsetof (struct inode_disk, indirect),
                         sizeof (block_sector_t));
          else
            return 0;
        }

      /* Read the correct pointer into SECTOR. */
      idx -= DIRECT_BLOCKS;
      cache_read (fs_device, indirect, &sector,
                  idx * sizeof (block_sector_t), sizeof (block_sector_t));

      /* If SECTOR == 0, then try to allocate a new block into SECTOR.
       * If success, write back to BLOCK_SECTOR; otherwise, SECTOR will still be 0.  */
      if (!sector)
        {
          if (free_map_calloc (&sector))
            cache_write (fs_device, indirect, &sector,
                         idx * sizeof (block_sector_t),
                         sizeof (block_sector_t));
          else
            return 0;
        }

      return sector;
    }
  else if (idx < DIRECT_BLOCKS + INDIRECT_BLOCKS + DBL_INDIRECT_BLOCKS)
    {
      /* Read DISK_INODE->DBL_INDIRECT into DBL_INDIRECT */
      cache_read (fs_device, inode_sector, &dbl_indirect,
                  offsetof (struct inode_disk, dbl_indirect),
                  sizeof (block_sector_t));

      /* If BLOCK_SECTOR == 0, then try to allocate a new block into
       * BLOCK_SECTOR. If success, write back to DISK_INODE.  */
      if (!dbl_indirect)
        {
          if (free_map_calloc (&dbl_indirect))
            cache_write (fs_device, inode_sector, &dbl_indirect,
                         offsetof (struct inode_disk, dbl_indirect),
                         sizeof (block_sector_t));
          else
            return 0;
        }

      /* Read the correct indirect pointer into INDIRECT. */
      idx -= DIRECT_BLOCKS + INDIRECT_BLOCKS;
      cache_read (fs_device, dbl_indirect, &indirect,
                  (idx / INDIRECT_BLOCKS) * sizeof (block_sector_t),
                  sizeof (block_sector_t));

      /* If SECTOR == 0, then try to allocate a new block into SECTOR.
       * If success, write back to BLOCK_SECTOR; otherwise, SECTOR will still
       * be 0.  */
      if (!indirect)
        {
          if (free_map_calloc (&indirect))
            cache_write (fs_device, dbl_indirect, &indirect,
                         (idx / INDIRECT_BLOCKS) * sizeof (block_sector_t),
                         sizeof (block_sector_t));
          else
            return 0;
        }

      /* Read the correct pointer into SECTOR. */
      cache_read (fs_device, indirect, &sector,
                  (idx % INDIRECT_BLOCKS) * sizeof (block_sector_t),
                  sizeof (block_sector_t));

      /* If SECTOR == 0, then try to allocate a new block.
       * If success, write back to BLOCK_SECTOR; otherwise, SECTOR will still
       * be 0.  */
      if (!sector)
        {
          if (free_map_calloc (&sector))
            cache_write (fs_device, indirect, &sector,
                         (idx % INDIRECT_BLOCKS) * sizeof (block_sector_t),
                         sizeof (block_sector_t));
          else
            return 0;
        }

      return sector;
    }
  else
    { return 0; }
}

/* Free the allocated pointers in the STRUCT INODE_DISK correspoinding to
 * SECTOR. The STRUCT INODE_DISK itself will NOT be freed.
 */
void inode_free_sector (block_sector_t inode_sector)
{
  lock_acquire (&free_map_lock);

  int i, j;
  block_sector_t sector, indirect, dbl_indirect;

  /* Free direct pointers. */
  for (i = 0; i < DIRECT_BLOCKS; ++i)
    {
      cache_read (fs_device, inode_sector, &sector,
                  offsetof (struct inode_disk, direct)
      + i * sizeof (block_sector_t),
        sizeof (block_sector_t));
      if (sector)
        free_map_release (sector);

      sector = NULL;
      cache_write (fs_device, inode_sector, &sector,
                   offsetof (struct inode_disk, direct)
      + i * sizeof (block_sector_t),
        sizeof (block_sector_t));
    }

  /* Free indirect pointers. */
  cache_read (fs_device, inode_sector, &indirect,
              offsetof (struct inode_disk, indirect), sizeof (block_sector_t));
  if (indirect)
    {
      for (i = 0; i < INDIRECT_BLOCKS; ++i)
        {
          cache_read (fs_device, indirect, &sector,
                      i * sizeof (block_sector_t), sizeof (block_sector_t));

          if (sector)
            free_map_release (sector);

          sector = NULL;
          cache_write (fs_device, indirect, &sector,
                      i * sizeof (block_sector_t), sizeof (block_sector_t));
        }
      free_map_release (indirect);
    }

  /* Free double indirect pointers. */
  cache_read (fs_device, inode_sector, &dbl_indirect,
              offsetof (struct inode_disk, dbl_indirect),
  sizeof (block_sector_t));
  if (dbl_indirect)
    {
      for (j = 0; j < INDIRECT_BLOCKS; ++j)
        {
          cache_read (fs_device, dbl_indirect, &indirect,
                      i * sizeof (block_sector_t), sizeof (block_sector_t));
          if (indirect)
            {
              for (i = 0; i < INDIRECT_BLOCKS; ++i)
                {
                  cache_read (fs_device, indirect, &sector,
                              i * sizeof (block_sector_t),
                              sizeof (block_sector_t));
                  if (sector)
                    free_map_release (sector);

                  sector = NULL;
                  cache_write (fs_device, indirect, &sector,
                              i * sizeof (block_sector_t),
                              sizeof (block_sector_t));
                }
              free_map_release (indirect);
            }
        }
      free_map_release (dbl_indirect);
    }

  lock_release (&free_map_lock);
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof (struct inode_disk) == BLOCK_SECTOR_SIZE);

  size_t sectors = bytes_to_sectors (length);

  lock_acquire (&free_map_lock);

  size_t i;
  for (i = 0; i < sectors; ++i)
    if (!inode_create_sector (sector, i))
      {
        lock_release (&free_map_lock);
        inode_free_sector (sector);
        return false;
      }

  lock_release (&free_map_lock);

  /* Save metadata to DISK_INODE. */

  cache_write (fs_device, sector, &length,
               offsetof (struct inode_disk, length), sizeof (off_t));
  cache_write (fs_device, sector, &inode_magic,
               offsetof (struct inode_disk, magic), sizeof (unsigned));

  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire (&open_inodes_lock);

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release (&open_inodes_lock);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    {
      lock_release (&open_inodes_lock);
      return NULL;
    }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);

  lock_release (&open_inodes_lock);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  lock_acquire (&inode->lock);
  if (inode != NULL)
    inode->open_cnt++;
  lock_release (&inode->lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire (&inode->lock);
  int open_cnt = --inode->open_cnt;
  lock_release (&inode->lock);

  /* Release resources if this was the last opener. */
  if (open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire (&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release (&open_inodes_lock);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          inode_free_sector (inode->sector);
          free_map_release (inode->sector);
        }

      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  lock_acquire (&inode->lock);
  inode->removed = true;
  lock_release (&inode->lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. If a block
   is within the length but not initialized, it will be initialized here
   before reading since sparse file is supported. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  lock_acquire (&inode->lock);

  while (size > 0)
    {
      /* Bytes left in inode. */
      off_t inode_left = inode_length (inode) - offset;
      /* Disk sector to read, create if within length and not initialized. */
      block_sector_t sector_idx = inode_left > 0
                                  ? inode_create_sector (inode->sector, offset)
                                  : inode_get_sector (inode->sector, offset);
      /* Starting byte offset within sector. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in sector, lesser of the two. */
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read (fs_device, sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  lock_release (&inode->lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. If EOF is exceed, the file
   will be extended to OFFSET + SIZE. Note that sparse file is
   supported, in that the sectors between previous length and
   OFFSET is not initialized until someone read or write on it. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  lock_acquire (&inode->lock);

  if (inode->deny_write_cnt) {
      lock_release (&inode->lock);
      return 0;
  }

  if (inode_length (inode) < offset + size)
    {
      off_t new_length = size + offset;
      cache_write (fs_device, inode->sector, &new_length,
                   offsetof (struct inode_disk, length), sizeof (off_t));
    }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = inode_create_sector (inode->sector, offset);
      if (sector_idx == 0)
        {
          lock_release (&inode->lock);
          return bytes_written;
        }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in sector. */
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < sector_left ? size : sector_left;

      cache_write (fs_device, sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  lock_release (&inode->lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire (&inode->lock);

  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);

  lock_release (&inode->lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  lock_acquire (&inode->lock);

  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;

  lock_release (&inode->lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  off_t length;
  cache_read (fs_device, inode->sector, &length,
              offsetof (struct inode_disk, length), sizeof (off_t));
  return length;
}
