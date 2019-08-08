Final Report for Project 3: File System
=======================================

## Task 1: Buffer Cache

For the buffer cache, we basically followed Will's suggestion to create 
a separate `cache.c` and put everything related to the cache in there, 
and then change all other reference to `block_read` and `block_write` 
into `cache_read` and `cache_write`. 

We also made some changes to our synchronization method. In `cache_get`, 
the helper function to find the pointer to a given block, or evict a 
block if the cache is full, we first acquire the block entry lock and 
then release the global cache lock. This ensures atomicity for cache 
accesses.

## Task 2: Indexed and Extensible Files

Although Will suggested that we should implement a resize function 
instead of implementing support for sparse file, we kept our initial 
design. We do not have a resize function, and one can change the size of 
a file by writing to a location beyond EOF. This, in our opinions, is 
easier to implement than resizing, since we do not need to check for 
validity before writing; instead, we just allocate a block to write at 
if the location has not been allocated already. 

For initialization, we have to scrap our initial design since by the 
essence of having a buffer cache, we should not keep contents in memory 
except for the cache. We have to resolve to a more complicating method 
by creating an inode using a sequence of reads and writes. We tried to 
implement a function to find the address of a given cache block and to 
enable to caller to directly modify the cache, but the synchronization 
for this method is more troubling than we expected so have to resolve to 
the more reliable `cache_read` and `cache_write`.

## Task 3: Subdirectories

The essence of our implementation is `dir_resolve` and `split_path`. 
`dir_resolve`, given a input path string, finds and opens the 
corresponding directory; `split_path` removes the last level from the 
input path string (i.e. split `path/to/your/file.txt` into 
`path/to/your/` and `file.txt`). This allows us to incorporate the 
existing `dir_lookup` into our implementation.

We also followed Will's suggestion to handle `.` and `..` by inserting 
artificial subdirectories on directory initialization. By making some 
slight changes to `dir_empty`, this allows us to easily handle these 
special names.

## Reflection

This project is a debugging hell for us, especially for task 3. In 
retrospect, we did not have a clear structures before designing a more 
complex system like subdirectories. We should have drew out the class 
structures and a crude pipeline before actually start coding. We also 
should implemented the functionality first before adding synchronization. 
We spent a lot of time before the code actually compiled.

Also, a lot of bug were due to forgetting to release a lock before a 
short-circuited return. In retrospect, instead of using preemptive 
returns, we could using `goto` clause and simply solve this issue. We 
were afraid using `goto` and used complicated nested if clauses, which 
caused more problem than `goto`.

For reflection, as in the previous projects, we design the project in 
an offline meeting, and implemented by one person while other helped 
with debugging. 

## Student Testing Report

### 1. Hit rate test
- In`hit-rate`, we test the buffer cache?s effectiveness by measuring its cache hit rate. We create a file named `file0`, open it and write 2048 random bytes into it. We close `file0` and reset the cache through system call `cache_reset ()`. We open `file0` and read it sequentially block-by-block. We get the cold cache's hit rate through system call `hit_rate ()` and save it in variable `before`. We close `file0`, reopen it and read it block-by-block again. We call `hit_rate ()` and save the return value in variable `after`. In the end, we check if `after` is greater than `before`. In a cold cache, the hit rate is low because every block has to be pulled from the disk. The hit rate increases after the second attempt to read `file0` because the blocks have already been cached.
- We introduced two global variables `total_cnt` and `hit_cnt` in `cache.c` to calculate the hit rate.
- In our implementation, `cache_reset ()` sets every cache entry's `valid_bit` to `false`. It sets `total_cnt` and `hit_cnt` to 0 to make the hit rate of reading `file0` more accurate. It also calls `cache_close ()` such that the newly created `file0` can be written to the disk. 
- hit-rate.output
```c 
Copying tests/filesys/extended/hit-rate to scratch partition...
Copying tests/filesys/extended/tar to scratch partition...
qemu -hda /tmp/Yfcru5SNC4.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading...........
Kernel command line: -q -f extract run hit-rate
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  471,040,000 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 186 sectors (93 kB), Pintos OS kernel (20)
hda2: 237 sectors (118 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'hit-rate' into the file system...
Putting 'tar' into the file system...
Erasing ustar archive...
Executing 'hit-rate':
(hit-rate) begin
(hit-rate) Create, open and write to file0.
(hit-rate) Close file0 and reset cache.
(hit-rate) Open file0.
(hit-rate) Reading file0...
(hit-rate) The hit rate is now 74 percent.
(hit-rate) Close and reopen file0.
(hit-rate) Reading file0 again...
(hit-rate) The hit rate is now 87 percent.
(hit-rate) Hit rate increased.
(hit-rate) end
hit-rate: exit(0)
Execution of 'hit-rate' complete.
Timer: 63 ticks
Thread: 0 idle ticks, 61 kernel ticks, 2 user ticks
hdb1 (filesys): 281 reads, 281 writes
hda2 (scratch): 236 reads, 2 writes
Console: 1308 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```
- hit-rate.result
```c 
PASS
```
- If the kernel did not implement the clock algorithm correctly, the blocks pulled into the cache during the first read might keep evicting its previous block. As a result, during the second read, the hit rate would remain very low since the cache is still relatively "cold".
- If the kernel did not keep a file's blocks as valid in the cache when we close the file, the hit rate would not increase after the second read since we are still reading the blocks into a "cold" cache.

### 2. Write coalesce test
- In test `coalesce`, we test the buffer cache?s ability to coalesce multiple writes to the same sector. We create a file named `file0` and open it. We get the `write_cnt` of the file system's block device through system call `write_cnt ()` and save the number in variable `before`. We write 64 KB of data into `file0` byte-by-byte and read all 64KB byte-by-byte. We call `write_cnt ()` again and save the count in variable `after`. Then we check if `after`-`before` is approximately 128. This is because the buffer cache's write back policy ensures that every 512 consecutive writes (block size is 512B) are first stored in the cache and only get flushed to the disk when the block is evicted. There should be approximately 128 `block_write`'s since 64 KB is 128 blocks.

- `coalesce.result`:
```c
Copying tests/filesys/extended/coalesce to scratch partition...
Copying tests/filesys/extended/tar to scratch partition...
qemu -hda /tmp/AchLvcKGve.dsk -hdb tmp.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading...........
Kernel command line: -q -f extract run coalesce
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  522,649,600 loops/s.
hda: 1,008 sectors (504 kB), model "QM00001", serial "QEMU HARDDISK"
hda1: 186 sectors (93 kB), Pintos OS kernel (20)
hda2: 236 sectors (118 kB), Pintos scratch (22)
hdb: 5,040 sectors (2 MB), model "QM00002", serial "QEMU HARDDISK"
hdb1: 4,096 sectors (2 MB), Pintos file system (21)
filesys: using hdb1
scratch: using hda2
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'coalesce' into the file system...
Putting 'tar' into the file system...
Erasing ustar archive...
Executing 'coalesce':
(coalesce) begin
(coalesce) Create and open file0.
(coalesce) Writing 64 KB of data into file0 byte-by-byte...
(coalesce) Reading file0 byte by byte...
(coalesce) Checking block device's write_cnt...
(coalesce) Block device's write_cnt increased by approximately 128.
(coalesce) end
coalesce: exit(0)
Execution of 'coalesce' complete.
Timer: 160 ticks
Thread: 0 idle ticks, 61 kernel ticks, 99 user ticks
hdb1 (filesys): 398 reads, 398 writes
hda2 (scratch): 235 reads, 2 writes
Console: 1240 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```
- `coalesce.result`:
```c 
PASS
```
- If the kernel did not implement the clock algorithm correctly, when reading `file0`, we might not be able to evict all the dirty blocks and write them to the disk. Then `write_cnt` would be smaller than 128.
- When we call the `write` syscall, if the kernel wrote the data directly to the disk by calling `block_write` instead of writing it into the cache, then `write_cnt` would be approximately the number of bytes being written.

### 3. Experience
Regarding improvement of the Pintos testing system, it might be better if we could select the specific lines in the output that we want to match exactly with the `.ck` file. For example, in test `hit-rate`, we print out the `before` and `after` variables in the output. We noticed that these numbers would change when we moved the test files from `extended` to `base` directory. If these difference could be ignored in the comparison process, we could still pass the test without having to change the `.output` file. From writing these tests, we learned that it is also necessary to "test" our test cases. For example, in `hit-rate.c`, we forgot to call `cache_close` in `cache_reset` to flush the newly created `file0` to the disk. As a result, the attempt to reopen `file0` failed and the subsequent changes in hit rate were not related to the reading of `file0`. Even though the hit rate did increase and passed the check, the cache's effectiveness did not really get tested.

