#ifndef GROUP_CACHE_H
#define GROUP_CACHE_H

#include "devices/block.h"

void cache_init (void);
void cache_close (struct block *);
void cache_read (struct block *, block_sector_t, void *buffer,
                 int offset, int size);
void cache_write (struct block *, block_sector_t, const void *buffer,
                  int offset, int size);
int get_hit_rate (void);
void cache_reset (void);
struct block * get_fs_device (void);
#endif /* filesys/cache.h */
