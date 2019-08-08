Final Report for Project 2: User Programs
=========================================

## Task 1: Argument Parsing

In this part, we implemented argument parsing and setup the stack before
a process could ran. 

There is no major modifications. We used `strtok_r` instead of `strtok`
to prevent race conditions, as discussed in the meeting.

## Task 2: Process Control Syscalls

In this part, we implemented some syscalls with basic process control
functionality.

We abandoned the second approach to validate pointers suggested by the 
specs, and instead validate in each syscall. We used a shared
`execute_status` struct to block the parent process until the child 
process loads its executable or errors as suggested in the meeting.

## Task 3: File Operation Syscalls

In this part, we added more syscalls regarding file operations using
file descriptors for each process.

We applied `file_deny_write` on loaded executables, closed all opened 
files and validate the address as we forgot to mention it in our design 
doc. We kept our initial design of using a list to store the file 
descriptors so that it could support any number of opened files, though 
we noticed a slightly slower runtime, but overall we felt like using a
list is easier to implement.

## Reflection

Same as in the previous project, we basically just did the whole project 
during offline meeting, or one of us will implement the basic structure 
and the rest will help testing and debugging.

One thing that we should have done first is that we should have create a
code style format before we started the format. This time we set a code
formatter and it's much easier to format the code style. Another thing
that went well is that our commit messages is rather organized; we have
to roll back due to some errors, and the commit messages helped us 
achieve that without any problems.

One problem is that some times GDB refuse to hook up on the machine of 
one of us, so we have to push the modification up to github, pull it 
down onto another machine and then ran GDB to debug. It was quite 
painful.

## Student Testing Report

1. In `sc-bad-arg-2`, we further test the address validation in syscall.
    We test on the non-word-aligned stack pointer situation by setting 
    the stack pointer 7 bytes below the user space boundary. In this way, 
    the last byte of the second argument of this syscall would be in the
    kernel space, and should errors and be killed.

    Expected output:
    ```
    Copying tests/userprog/sc-bad-arg-2 to scratch partition...
    qemu -hda /tmp/7MSNaVLhk1.dsk -m 4 -net none -nographic -monitor null
    PiLo hda1
    Loading..........
    Kernel command line: -q -f extract run sc-bad-arg-2
    Pintos booting with 4,088 kB RAM...
    382 pages available in kernel pool.
    382 pages available in user pool.
    Calibrating timer...  423,526,400 loops/s.
    hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
    hda1: 175 sectors (87 kB), Pintos OS kernel (20)
    hda2: 4,096 sectors (2 MB), Pintos file system (21)
    hda3: 101 sectors (50 kB), Pintos scratch (22)
    filesys: using hda2
    scratch: using hda3
    Formatting file system...done.
    Boot complete.
    Extracting ustar archive from scratch device into file system...
    Putting 'sc-bad-arg-2' into the file system...
    Erasing ustar archive...
    Executing 'sc-bad-arg-2':
    (sc-bad-arg-2) begin
    sc-bad-arg-2: exit(-1)
    Execution of 'sc-bad-arg-2' complete.
    Timer: 64 ticks
    Thread: 0 idle ticks, 63 kernel ticks, 1 user ticks
    hda2 (filesys): 61 reads, 206 writes
    hda3 (scratch): 100 reads, 2 writes
    Console: 888 characters output
    Keyboard: 0 keys pressed
    Exception: 0 page faults
    Powering off...
    PASS
    ```
    
    If the syscall handler does not check all of the address of all
    arguments, this program would run normally and potentially read from 
    the kernel memory and violate the system.
    
    Similarly, if the syscall handler only checks the first byte for 
    each arugment of a syscall, than this program would run normally and 
    potentially read from the kernel memory and violate the system. 
    
2. In `read-eof`, we test the functionality of `filesize`, `seek` and 
    `read`. We open a file, find its size, set the pointer to one byte
    after EOF using `seek` and then try to read. `read` should return
    zero bytes, and the buffer should not be modified.
    
    Expected output:
    ```
    Copying tests/userprog/read-eof to scratch partition...
    Copying ../../tests/userprog/sample.txt to scratch partition...
    qemu -hda /tmp/swjKadOx2Q.dsk -m 4 -net none -nographic -monitor null
    PiLo hda1
    Loading..........
    Kernel command line: -q -f extract run read-eof
    Pintos booting with 4,088 kB RAM...
    382 pages available in kernel pool.
    382 pages available in user pool.
    Calibrating timer...  419,430,400 loops/s.
    hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
    hda1: 175 sectors (87 kB), Pintos OS kernel (20)
    hda2: 4,096 sectors (2 MB), Pintos file system (21)
    hda3: 105 sectors (52 kB), Pintos scratch (22)
    filesys: using hda2
    scratch: using hda3
    Formatting file system...done.
    Boot complete.
    Extracting ustar archive from scratch device into file system...
    Putting 'read-eof' into the file system...
    Putting 'sample.txt' into the file system...
    Erasing ustar archive...
    Executing 'read-eof':
    (read-eof) begin
    (read-eof) open "sample.txt"
    (read-eof) file size is 239
    (read-eof) read return 0 bytes
    (read-eof) end
    read-eof: exit(0)
    Execution of 'read-eof' complete.
    Timer: 66 ticks
    Thread: 0 idle ticks, 65 kernel ticks, 1 user ticks
    hda2 (filesys): 92 reads, 216 writes
    hda3 (scratch): 104 reads, 2 writes
    Console: 1011 characters output
    Keyboard: 0 keys pressed
    Exception: 0 page faults
    Powering off...
    PASS
    ```
    
    If the kernel does not implement `seek` and `read` beyond the file
    size, the process could fail or even cause a kernel panic from 
    read from an invalid address.
    
    If the kernel does not validate the size first and then copy the 
    content to the buffer, instead read byte-by-byte, the buffer could
    potentially be modified.
