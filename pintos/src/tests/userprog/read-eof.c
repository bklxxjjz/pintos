/* Try seeking beyond the size of the file and try to read
 * it. This should no error and should return 0 bytes.
 * indicating EOF. */

#include <string.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  char buf[16] = "012345676543210";
  char cmp[16] = "012345676543210";

  int file = open ("sample.txt");
  msg ("open \"sample.txt\"");

  int size = filesize (file);
  msg ("file size is %d", size);
  seek (file, size + 1);

  int bytes_read = read (file, &buf, 16);
  msg ("read return %d bytes", bytes_read);

  if (bytes_read)
    fail ("read should return 0 bytes");
  else if (strcmp (buf, cmp))
    fail ("buffer should not be modified");
}
