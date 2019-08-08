/* Sticks a system call number (SYS_EXIT) 7 bytes below the
 * very top of the stack, then invokes a system call with the
 * stack pointer (%esp) set to its address. The process must
 * be terminated with -1 exit code because the argument to
 * the system call would have one byte be above the top of
 * the user address space. */

#include <syscall-nr.h>
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  asm volatile ("movl $0xbffffff9, %%esp; movl %0, (%%esp); int $0x30"
  : : "i" (SYS_EXIT));
  fail ("should have called exit(-1)");
}
