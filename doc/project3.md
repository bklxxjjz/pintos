Design Document for Project 3: File Systems
============================================

## Group Members

* Fanyu Meng <fy.meng@berkeley.edu> 
* Zhang, Ning <zhangning@berkeley.edu>
* Terrance Wang <conglin.wang@berkeley.edu> 
* Andy Zhang <andyzhangyn@berkeley.edu>

## Task 1: Buffer Cache

### Data structures and functions

In `filesys.c`, the following variables will be added:
```c
struct cache_t *cache[64];    /* An array of pointers to 64 cache_t objects. */
int clock_hand;               /* The index of the cache_t object that clock_hand points to. */
bool use_bit[64];             /* Use bit map. */
bool evict_bit[64];           /* Evict bit map. */
block_sector_t new_idx[64];   /* Map of indices of the new block to be brought in next. */
```

In `filesys.h`, the following struct will be added:
```c
struct cache_t
  {
    block_sector_t sector_idx;    /* The sector index corresponding to the cached block. */
    bool dirty_bit;               /* Dirty bit of the cached block. */
    char data[512];               /* Cached data. */
    struct lock *lock;            /* Lock on the current block of data. */
  }
```

### Algorithm

#### For `inode_read_at()`:
1. In the while loop in`inode_read_at()`, after obtaining `sector_idx`, iterate through `cache` array and compare the current `sector_idx` with the `sector_idx` of each element in the array. Check `evict_bit[i]` and `new_idx[i]` where `i` is the index of the current element;
2. If the same `sector_idx` is found in `cache` and`evict_bit[i]` is false (cache hit), or `sector_idx` is different, `evict_bit[i]` is true and `new_idx[i]` == requested `sector_idx` (future cache hit), call `lock_acquire()`. Use `memcpy` to copy the data from the `cache_t` object's `data` field into `buffer`. Set `use_bit[i]` to 1. Release the lock at the end;
3. Else (cache miss), update `clock_hand` to (`clock_hand` + 1) % 64. Index into array `cache` using `clock_hand`, check the `cache_t` object's `use_bit[i]`; 
4. If `use_bit[i]` is 0, set `evict_bit[i]` to 1 and update `new_idx[i]`. Call `lock_acquire()`. If `dirty_bit` is 1, write `data` back to disk using `block_write()`. Set the content of `data` to NULL. Call `block_read()` to copy the requested block into `data`. Use `memcpy()` to copy the data from the `cache_t` object’s `data` field into `buffer`. Set `use_bit[i]` to 1,`dirty_bit` to 0 and `evict_bit[i]` to 0. Update `sector_idx`. Release the lock at the end;
5. If `use_bit` is 1, set `use_bit` to 0 and repeat from step 3.

#### For `inode_write_at()`:
1. In the while loop in`inode_read_at()`, after obtaining `sector_idx`, iterate through `cache` array and compare the current `sector_idx` with the `sector_idx` of each element in the array. Check `evict_bit[i]` and `new_idx[i]` where `i` is the index of the current element;
2. If the same `sector_idx` is found in `cache` and`evict_bit[i]` is false (cache hit), or `sector_idx` is different, `evict_bit[i]` is true and `new_idx[i]` == requested `sector_idx` (future cache hit), call `lock_acquire()`. Use `memcpy` to copy the data from `buffer` into the `cache_t` object's `data` field. Set `use_bit[i]` and `dirty_bit` to 1. Release the lock at the end;
3. Else (cache miss), update `clock_hand` to (`clock_hand` + 1) % 64. Index into array `cache` using `clock_hand`, check the `cache_t` object's `use_bit[i]`; 
4. If `use_bit[i]` is 0, set `evict_bit[i]` to 1 and update `new_idx[i]`. Call `lock_acquire()`. If `dirty_bit` is 1, write `data` back to disk using `block_write()`. Set the content of `data` to NULL. Call `block_read()` to copy the requested block into `data`. Call `memcpy()` to write the content of `buffer` to `data`. Set `use_bit[i]` and `dirty_bit` to 1. Set `evict_bit[i]` to 0. Update `sector_idx`. Release the lock at the end;
5. If `use_bit` is 1, set `use_bit` to 0 and repeat from step 3.

#### For `filesys_done()`:
1. Iterate through array `cache`. For each `cache_t` object, check its `dirty_bit`.
2. If `dirty_bit` is 1, write `data` back to disk using `block_write()`.

### Synchronization 

- In our implementation, we do not allow concurrent read of the same block.
- A thread must acquire the lock before evicting a cached block. If another thread is actively reading or writing the block, the thread attempting to evict will be put into sleep until the reading/writing thread finishes and releases the lock.
- Before the eviction, we acquire the lock such that all other access to the block will be blocked.
- We iterate through all cached blocks and only evict and load a new one if the requested block is not in the cache, or will not be brought in by an early thread (this is done by checking `new_idx[i]`). This prevents the same block from being loaded more than once. 


### Rationale
We choose the "clock" algorithm over the more expensive LRU. This is because the "clock" algorithm only advances the clock hand upon a cache miss whereas the LRU requires updating the most recent use time and the priorioty queue after every access.

## Task 2: Indexed and Extensible Files

### Data structures and functions

`struct inode_disk` will be modified as follows:
```c
struct inode_disk
  {
    off_t length;                     /* File size in bytes */
    block_sector_t direct[12];        /* 6KiB from direct pointers */ 
    block_sector_t indirect;          /* 64KiB from level 1 indirect pointer */                   
    block_sector_t d_indirect;        /* 8MiB from level 2 indirect pointer */
    unsigned magic;                   /* Magic number. */
    uint32_t unused[112];              /* Not used. */
  };
```

### Algorithm

- `inode_create`

    After allocating the `inode_disk`, we will allocate the sectors according to `length / BLOCK_SECTOR_SIZE`. If this is less than or equals to 12, then we will only allocate enough direct pointers; 

    if this is larger than 12 but is less than 140, then we will allocate a block for the level one indirect pointer and allocate more blocks and store the pointers inside the block pointed by `indirect`; 
    
    if this is larger than 140, than we will allocate a block for level 2 indirect pointer, and apply the level 1 create algorithm for all the 128 potential entries in the level 2 block, until the expected number of blocks is met. 
    
- `inode_close`

    Iterate through all `block_sector_t` pointers. If a direct pointer is not `NULL`, free it; if the level one indirect pointer is not `NULL`, apply the direct pointer close algorithm on all pointers inside the block; if the level two indirect pointer is not `NULL`, apply the level one close algorithm to all pointers inside the block.

- `byte_to_sector`

    Let `int sec_num = pos / BLOCK_SECTOR_SIZE`. If `sec_num` is less than or equals to 12, returns the corresponding direct pointer, or returns -1 if the entry in the array is `NULL`; 

    if `sec_num` is larger than 12 but is less than 140, access into `indirect` and `block_read` `indirect` on `sec_num - 12`, or returns -1 if either `indirect` or the block read result is `NULL`; 
    
    if `sec_num` is greater than 140, try access into `d_indirect`, `block_read` `d_indirect` on `(sec_num - 140) / 128`, and access the level one block like in the previous line to get the actual number. Returns -1 if any one of the pointer in the two levels is `NULL`.
    
- `byte_to_sector_create`

    This function will be added to `inode.c`. Similarly to `byte_to_sector`, this function will take in an inode pointer and an offset and returns the sector number. However, this function will allocate a new block if either level is `NULL`. All newly created block will be manually write as 0.

- `inode_read_at`

    Find the sector number using `byte_to_sector`. Read `min(size, BLOCK_SECTOR_SIZE - offset % BLOCK_SECTOR_SIZE)` from the block each time, decrease `size` and increment `offset` by bytes read, and then loop untils `size` becomes 0 or any step errors.

- `inode_write_at`
 
    Find the sector number using `byte_to_sector_create`. Write `min(size, BLOCK_SECTOR_SIZE - offset % BLOCK_SECTOR_SIZE)` from the block each time, decrease `size` and increment `offset` by bytes read, and then loop untils `size` becomes 0 or any step errors. 

- `sys_inumber`
    Find the correspoinding file through fd, and returns `file->inode->sector`.

### Synchronization 

Most synchronization problems will be handled at file level. For this task, we will use a lock to ensure that the access to `open_inodes` is atomic and it should be sufficient. 

### Rationale

We allow sparse file through `byte_to_sector_write`, since we are lazily allocating the block until an actual write occurs. In this way, we don't need to implement a resize function.

## Task 3: Subdirectories

### Data structures and functions

In `struct dir` in `directory.c`, the following fields will be added:
```c 
struct *dir parent_dir;    /* Current directory's parent. */
int use_cnt;               /* Number of processes that are using this directory. */
struct lock lock;          /* Lock of the directory. */
```

In `struct dir_entry` in `directory.c`, the following field will be added:
```c
bool is_dir;    /* Whether the dir_entry points to a direcotry or a file. */
int read_idx;   /* The next index to be read by readdir. */
```

In `struct thread` in `thread.h`, the following field will be added:
```c 
struct dir *cur_dir;    /* The thread's current working directory. */  
```

### Algorithm

- bool `chdir (const char *dir)`

    If `dir` does not start with `/`, use `get_next_part()` to get the first part of the path, and pass it to `dir_lookup()`. If it can be found under the current directory, open it and recursively look up the next part of the path in the found directory. Else (starts with `/`), treat the path as an absolute path. Call `dir_open_root()` to get the root directory and recursively look for each part of the path. Once `dir` is found, update the thread's `cur_dir`.
    
    If `dir` starts with `..`, set `cur_dir` to `cur_dir->parent_dir` and recurse. If `dir` starts with `.`, do nothing and recurse.

- bool `mkdir (const char *dir)`

    If `dir` starts with `/`, treat it as an absolute path. Call `dir_open_root()` to get the root directory. Use `get_next_path()` to get each part of the parth and recursively look it up by calling `dir_lookup()`. If a name is not found, and `srcp` is not empty, return `false`. If a name is not found and we have reached the end of the path, call `free_map_allocate()` to allocate a sector for the new directory. Call `dir_add()` to add the new directory under the current working directory. Call `dir_create()` to make a new directory. If `dir` does not start with `/`, treat it as a relative path. Recursively look up each part of the path in a similar way starting from the current working directory.

- handling opening directories

    This is automatically handled by `dir_lookup`, since we treat both a file and a directory as a `dir_entry`. However, the content of `fd_list` will be changed from `file` to `dir_entry`, and most file operations will be changed to go through an additional `!is_dir` check and an additional level of abstraction through `dir_entry.inode_sector`.

- `bool readdir (int fd, char *name)`

    Find the corresponding `dir_entry` through inode read with an offset of `read_idx * sizeof struct dir_entry`, store its `name` field, advance `read_idx` and return true. If `read_idx` reaches the `length` of the underlying inode, set `read_idx` back to 0 and return false. 

- `bool isdir (int fd)`

    Find the corresponding `dir_entry` through inode read with an offset of `read_idx * sizeof struct dir_entry`, and returns its `is_dir` field.
    
- In our implementation, a user process is not allowed to delete a directory if it is the cwd of a running process. This can be ensured by checking `use_cnt` before deleting a directory.  

### Synchronization 

We add an lock for each directory to ensure atomic access for any access to the same directory.

### Rationale

Straightforward task.

## Additional Questions

### Write-behind

Implement a helper function that writes all the dirty pages back to the disk. Let a thread call this helper function in an infinite while loop. Each call is followed by a call to `timer_sleep(t)` where `t` is the time period between every two write-backs.

### Read-ahead

Any read access on a block will perform an access on both the desired block and the next consecute block. The access on the consecute block will put onto a stack, and be actually executed by a kernel thread. If we have a large consecutive access, then this method would improve the performance by a large margin.
