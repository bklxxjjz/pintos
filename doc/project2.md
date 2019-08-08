Design Document for Project 2: User Programs
============================================

## Group Members

* Fanyu Meng <fy.meng@berkeley.edu> 
* Zhang, Ning <zhangning@berkeley.edu>
* Terrance Wang <conglin.wang@berkeley.edu>   
* Andy Zhang <andyzhangyn@berkeley.edu>



## Task 1: Argument Passing

#### Data structures and functions

Nothing to add.

#### Algorithm

In `process_execute` in `process.c`:
1. Before calling `thread_create`, use `strtok` to peel off the name of 
the process.

In `setup_stack` in `process.c`:
1. After allocating the page, push the process name onto the stack;
2. Iteratively calling `strtok` and pushing arguments onto the stack, 
until the input string is emptied. We also keep track of the number of 
arguments and their corresponding addresses in an array;
3. If the stack pointer is not word-aligned, push some empty data onto 
the stack to make it aligned;
4. Push a NULL terminator symbolizing the end of the array of arguments 
onto the stack;
5. Push the pointers to the arguments in a reverse order onto the stack.
Then free the array used to store the address of the arguments;
6. Push `*esp` onto the stack. This is the address of `argv`. Then push
`argc` onto the stack;
7. Push a dummy return address onto the stack.

#### Synchronization 

The parent thread and the child threads have separate stacks. They do 
not share any resources, so synchronization is not a concern here.

#### Rationale

This is a pretty straightforward task, and we cannot think of an 
alternative way of implementing it.

## Task 2: Process Control Syscalls

#### Data structures and functions

- In `syscall.h` and `syscall.c`, the following functions will be added:
```c
int sys_practice (int i);
int sys_halt (void);
int sys_exit (int status);
pid_t sys_exec (const char *cmd line);
int sys_wait (pid_t pid);
```

- In `thread.h`, the following struct will be created:
```c
struct exit_status 
  {
    int exit_code;              /* Child’s exit code. */
    struct semaphore sema;      /* Initialized to 0. Decrement by parent on wait and increment by child on exit. */ 
    int ref_count;              /* Initialized to 2. Decrement if either child or parent exits. */
    struct lock lock;           /* Lock to protect EXIT_CODE and REF_COUNT. */
    struct list_elem elem;      /* Stored in parent thread. */
    tid_t tid;                  /* Child thread's id. */
  }
```

- In `struct thread`, the following attributes will be added:
```c
struct list child_es_list;      /* List of the exit statuses of child threads. */
struct exit_status es;          /* The thread’s own exit status. */
```

#### Algorithm

- To handle more syscall, in `syscall_handler` in `syscall.c`:
    1. Sanity check on the stack pointer `f->esp`. If it's not 
    word-aligned, not in the user memory or is null, call `page_fault ()`; 
    2. Cast `f->esp` as `uint32_t *args`, Switch on `args[0]` and call 
    corresponding functions;
    3. If the syscall argument contains a pointer, check if the pointer 
    is below `PHYS_BASE` using `is_user_vaddr` and is not null. If not, 
    call `page_fault`;
    4. Store the return value into `f->eax`.
    
- To implement `int sys_practice (int i)`:
    1. Return `i + 1`.
    
- To implement `int sys_halt (void)`:
    1. Call `shutdown_power_off()`. 
    
- To implement `pid_t sys_exec (const char *cmd line)`:
    1. Call and return `process_execute (line)`;
    2. In `process.c`, add a global boolean variable `successful_load`;
    3. In `start_process`, after the child process loads the executable, update `successful_load` to `success` and call `sema_up (&temporary)`;
    4. Before `process_execute` returns, call `sema_down (&temporary)`. The parent process will block until the child process calls `sema_up (&temporary)`. This ensures that the parent process does not return until it knows if the child process successfully loaded its executable;
    5. In `process_execute`, if `successful_load`, return the corresponding child `tid`. Else, return -1.
    
- To implement `int sys_exit (int status)`:
    1. Release all `locks` in the current thread's `held_locks`;
    2. Save `status` into the thread's own `exit_status`;
    3. Decrement `ref_count` in `exit_status` by 1. if it becomes 0, 
    free the `exit_status`; otherwise, call `sema_up` on `es->sema` to 
    wakeup the parent;
    5. Iterate through the thread's `child_es_list` and decrement 
    `ref_count`. If any `ref_count` becomes 0, free the `exit_status`;
    6. Free `child_es_list`;
    7. Call `thread_exit ()`.
    
- To implement `int sys_wait (pid_t pid)`:
    1. Find the `exist_status` in `child_es_list` with matching `pid`. If 
    no such thread exists, return -1;
    2. Call `sema_down` on `es->sema`. If the child is alive, this will
    block;
    3. Obtain the `exit_code` from `es`;
    4. Decrement `ref_count`. If it becomes 0, free the `exit_status`.
    5. Remove `es` from `child_es_list`.

#### Synchronization 

The synchronization between the parent and the child on `wait` or `exit`
will be handled using the `lock` in `exit_status`. Otherwise, there 
aren't any synchronization issues.

#### Rationale

Rather straightforward task. The implementation of `wait` and `exit` 
follows the hint form discussion.

## Task 3: File Operation Syscalls

#### Data structures and functions

- In `thread.h`, the following struct will be created:
```c
struct fd_node
  {
    int num;
    list_elem elem;
    struct file file;
  }
```

- In `thread.h`, the following struct will be added to `struct thread`:
```c
struct list fd_list;    // list of file descriptor nodes
unsigned next_fd_num;   // next available fd number, initialized to 2
```

- In `filesys.c`, the follow attributes will be added:
```c
struct lock;            // filesys global lock
```

- In `syscall.h` and `syscall.c`, the following functions will be added:
```c
bool sys_create (const char *file, unsigned initial size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
int sys_seek (int fd, unsigned position);
int sys_tell (int fd);
int close (int fd);
```

#### Algorithm
    
- To preventing resource leaks, in `sys_exit (int status)`:
    1. Additionally, call `sys_close` on all remaining `fd_node` in 
    current thread's `fd_list`.

Note that the validity of the pointers is checked in `syscall_handler`.

- To implement `bool sys_create (const char *file, unsigned initial size)`:
    1. Call `filesys_create (file, size)`.

- To implement  `bool sys_remove (const char *file)`:
    1. Call `filesys_remove (file)`.

- To implement  `int sys_open (const char *file)`:
    1. Call `filesys_open (file)`;
    2. Malloc a `fd_node`. Store `file` into the descriptor, set its 
    number to current thread's `next_fd_num` and increment `next_fd_num`;
    3. Return `num` of said `fd_node`. 

- To implement  `int sys_filesize (int fd)`:
    1. Sanity check on `fd_node`. Check if it's in the current thread’s 
    `fd_list`. If sanity check fails, return -1;
    2. Call and return `file_length (fd->file)`.

- To implement `int sys_read (int fd, void *buffer, unsigned size)`:
    1. Sanity check on `fd`. If it's 0, return `input_getc()`; otherwise, 
    check if it's in the current thread’s `fd_list`. If sanity check 
    fails, return -1;
    2. Sanity check on argument `buffer`; If sanity check fails, call
    `page_fault ()`;
    3. Call and return `file_read (fd_node->file, buffer, size)`.
    
- To implement `int sys_write (int fd, const void *buffer, unsigned size)`: 
    1. Sanity check on `fd`. If it's 1, write to stdout using `putbuf()`; 
    otherwise, check if if it's in the current thread’s `fd_list`. If 
    sanity check fails, return -1;
    2. Sanity check on argument `buffer`; If sanity check fails, call
    `page_fault ()`;
    3. Call and return `file_write (fd_node->file, buffer, size)`.
    
- To implement `int sys_seek (int fd, unsigned position)`:
    1. Sanity check on `fd`. Check if it's in the current thread’s
    `fd_list`. If sanity check fails, return -1;
    2. Call and return `file_seek (fd_node->file)`.
    
- To implement `int sys_tell (int fd)`:
    // what to do when fd is not valid?
    1. Sanity check on `fd`. Check if it's in the current thread’s 
    `fd_list`. If sanity check fails, return -1;
    2. Call and return `file_tell (fd_node->file)`.

- To implement `int sys_close (int fd)`:
    // what to do when fd is not valid?
    1. Sanity check on `fd`. Check if it's in the current thread’s 
    `fd_list`. If sanity check fails, return -1;
    2. Call `file_close (fd_node->file)`;
    3. Remove and free the corresponding `fd_node` from current thread's 
    `fd_list`. 

#### Synchronization 

To ensure atomic file system accesses, all of the syscall will acquire 
the file system global lock after its sanity checks, and release it 
before the call returns. 

#### Rationale
We use a list to store the file descriptors since it allows arbitrary 
number of opened files. The limit on the number is `UINT_MAX` and should
be way sufficient. 

To handle invalid pointers, we chose to only checking that the pointer 
points to somewhere in the user space, and leave the rest to `page_fault`.
This way we don't need to sanity check addresses in all syscalls and is
less prone to bugs.



## Additional Questions

1. `sc-bad-sp.c` uses an invalid stack pointer. On line 18, it set the 
stack pointer to `-(64*1024*1024)`, which is about 64MB below the code
segment. Then it jumps to `syscall_handler` and should page fault.

2. `sc-bad-arg.c` invokes memory beyond boundary. It sets the stack 
pointer to the top of the stack and jumps to `syscall_handler`. While
trying to grab the syscall number, the handler will exceeds user virtual
address and should page fault.

3. Memory access that partially exceeds boundary is not fully tested. 
e.g., set the stack pointer to `0xbffffffa`, then the later 2 bytes of
the syscall number exceeds user virtual address.

4. GDB Questions
    1. The name of the thread running `process_execute` is "main". Its 
    address is 0xc000e000. There is only one other thread present in 
    pintos at the time.
    ```c
    struct thread {
        tid = 2;
        status = THREAD_BLOCKED;
        name = "idle";
        stack = 0xc0104f34 "";
        priority = 0;
        allelem = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>};
        elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>};
        pagedir = 0x0;
        magic = 3446325067;
    }
    ```
    2. Backtrace with line numbers at the end of each line:
    ```c
    #0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32
    #1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288
    #2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340
    #3  main () at ../../threads/init.c:133
    ```
    3. The name of the thread running `start_process` is 
    `"args-none\000\000\000\000\000\000"`. Its address is `0xc010a000`.
    There are two other threads present in pintos at the time.
    ```c
    struct thread {
        tid = 1;
        status = THREAD_BLOCKED;
        name = "main";
        stack = 0xc000eebc "\001";
        priority = 31;
        allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020};
        elem = {prev = 0xc0036554 <temporary+4>, next = 0xc003655c <temporary+12>};
        pagedir = 0x0;
        magic = 3446325067;
    }
    ```
    ```c
    struct thread {
        tid = 2;
        status = THREAD_BLOCKED;
        name = "idle";
        stack = 0xc0104f34 "";
        priority = 0;
        allelem = {prev = 0xc000e020, next = 0xc010a020};
        elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>};
        pagedir = 0x0;
        magic = 3446325067;
    }
    ```
    4. The thread running `start_process` is created in `process_execute` 
    in `process.c`. The code is in line 45:
   ```c
    tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
   ```
    5. The hex address returned by `btpagefault` is 0x0804870c.
    6. After loading the symbols, we get the following result from 
    `btpagefault`:
    ```c
    _start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9
    ```
    7. Since we have not implemented argument passing, the kernel will 
    not push`argc` and `argv` onto the stack before it allows the user 
    program to execute. When the user program tries to access `argc` and
    `argv`, it goes above its own virtual address space and causes a 
    page fault.
