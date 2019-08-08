#ifndef FILESYS_FREE_MAP_H
#define FILESYS_FREE_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "threads/synch.h"

struct lock free_map_lock;

void free_map_init (void);
void free_map_read (void);
void free_map_create (void);
void free_map_open (void);
void free_map_close (void);

bool free_map_alloc (block_sector_t *);
bool free_map_calloc (block_sector_t *);
void free_map_release (block_sector_t);

#endif /* filesys/free-map.h */
