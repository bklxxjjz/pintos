#include "filesys/cache.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "threads/synch.h"

struct lock cache_lock;

#define CACHE_SIZE 64

int total_cnt;
int hit_cnt;

struct cache_t
  {
    block_sector_t sector;              /* The sector index of the cached block. */
    struct lock block_lock;             /* Lock on the current block of data. */
    char data[BLOCK_SECTOR_SIZE];       /* Cached data. */
    bool valid;                         /* Valid bit. */
    bool dirty;                         /* Dirty bit. */
    bool used;                          /* Used bit for clock algorithm. */
  };

struct cache_t cache[CACHE_SIZE];

int clock_hand;

struct cache_t *cache_get (struct block *, block_sector_t);
void cache_done (struct cache_t *);

int
get_hit_rate (void)
{
  return (hit_cnt * 100) / total_cnt;
}

void
cache_reset (void)
{
  cache_close (get_fs_device ());
  lock_acquire (&cache_lock);
  total_cnt = 0;
  hit_cnt = 0;
  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    {
      cache[i].valid = false;
    }
  lock_release (&cache_lock);
}

void
cache_init (void)
{
  lock_init (&cache_lock);
  clock_hand = 0;

  total_cnt = 0;
  hit_cnt = 0;

  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    lock_init (&cache[i].block_lock);
}

/* Close the cache and write all dirty blocks back to BLOCK. */
void
cache_close (struct block *block)
{
  lock_acquire (&cache_lock);

  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    if (cache[i].valid && cache[i].dirty)
      block_write (block, cache[i].sector, cache[i].data);

  lock_release (&cache_lock);
}

/* Returns the cache block that contains data corresponding to SECTOR.
 * This function also ensures to acquire the lock to the cache block.
 * If SECTOR cannot be found in the cache, a block will be evicted
 * using clock algorithm, and write back the data if dirty. Caller
 * should call CACHE_DONE after it finished its read or write to
 * release the block lock. */
struct cache_t *
cache_get (struct block *block, block_sector_t sector)
{
  lock_acquire(&cache_lock);
  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    if (cache[i].valid && cache[i].sector == sector)
      {
        lock_acquire (&cache[i].block_lock);
        hit_cnt++;
        lock_release(&cache_lock);
        return &cache[i];
      }

  /* Cache not found. Evict using clock algorithm. */
  while (cache[clock_hand].valid && cache[clock_hand].used)
    {
      cache[clock_hand].used = false;
      clock_hand = (clock_hand + 1) % CACHE_SIZE;
    }

  struct cache_t *cache_block = &cache[clock_hand];

  /* Save info about evicted block. */
  bool write_back = cache_block->valid && cache_block->dirty;
  block_sector_t old_sector = cache_block->sector;

  lock_acquire (&cache_block->block_lock);

  /* Update the metadate before releasing the global cache lock. */
  cache_block->sector = sector;
  cache_block->valid = true;
  cache_block->used = true;

  /* Write back if necessary. */
  if (write_back)
    block_write (block, old_sector, cache_block->data);

  /* Grab new block. */
  block_read (block, sector, cache_block->data);

  lock_release(&cache_lock);
  return cache_block;
}

void
cache_done (struct cache_t *cache_block)
{
  lock_release (&cache_block->block_lock);
}

void
cache_read (struct block *block, block_sector_t sector, void *buffer,
            int offset, int size)
{
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);
  total_cnt++;
  struct cache_t *cache_block = cache_get (block, sector);

  cache_block->used = true;
  memcpy (buffer, cache_block->data + offset, size);

  cache_done (cache_block);
}

void
cache_write (struct block *block, block_sector_t sector, const void *buffer,
             int offset, int size)
{
  ASSERT (offset + size <= BLOCK_SECTOR_SIZE);
  total_cnt++;
  struct cache_t *cache_block = cache_get (block, sector);

  cache_block->used = true;
  memcpy (cache_block->data + offset, buffer, size);
  cache_block->dirty = true;

  cache_done (cache_block);
}
