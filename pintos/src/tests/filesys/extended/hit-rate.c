/* First, reset the buffer cache. Open a file and read it sequentially,
   to determine the cache hit rate for a cold cache. Then, close it, re-open it,
   and read it sequentially again, to make sure that the cache hit rate improves. */

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
  char buf[2048];
  random_init (0);
  random_bytes (buf, sizeof buf);

  msg ("Create, open and write to file0.");
  create ("file0", 0);
  fd = open ("file0");
  write (fd, buf, 2048);

  msg ("Close file0 and reset cache.");
  close (fd);
  cache_reset ();

  msg ("Open file0.");
  fd = open ("file0");

  msg ("Reading file0...");
  for (i = 0; i < 2048; i += 512)
  {
    read (fd, buf, 512);
  }

  int before = hit_rate ();
  msg ("The hit rate is now %d percent.", before);

  msg ("Close and reopen file0.");
  close (fd);
  fd = open ("file0");

  msg ("Reading file0 again...");
  for (i = 0; i < 2048; i += 512)
  {
    read (fd, buf, 512);
  }

  int after = hit_rate ();
  msg ("The hit rate is now %d percent.", after);
  CHECK (after > before, "Hit rate increased.");
}