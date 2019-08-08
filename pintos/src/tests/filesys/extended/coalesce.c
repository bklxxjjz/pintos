/* Write a large file byte-by-byte (make the total file size at least 64KB,
   which is twice the maximum allowed buffer cache size). Then, read it in
   byte-by-byte. The total number of device writes should be on the order of
   128 (because 64KB is 128 blocks). */

#include <string.h>
#include <stdio.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>

void
test_main (void)
{
  int i;
  int fd;
  char buf[10];

  msg ("Create and open file0.");
  create ("file0", 0);
  fd = open ("file0");

  random_init (0);
  random_bytes (buf, sizeof buf);

  unsigned long long before = write_cnt ();


  msg ("Writing 64 KB of data into file0 byte-by-byte...");
  for (i = 0; i < 65536; i++)
    {
      write (fd, buf, 1);
    }

  msg ("Reading file0 byte by byte...");
  for (i = 0; i < 65536; i++)
    {
      read (fd, buf, 1);
    }

  msg ("Checking block device's write_cnt...");
  unsigned long long after = write_cnt ();
  int increase = after - before;
  CHECK (increase >= 128 && increase <130, "Block device's write_cnt increased by approximately 128.");
}