Design Document for Project 1: Threads
======================================

## Group Members

* Fanyu Meng <fy.meng@berkeley.edu>
* Terrance Wang <conglin.wang@berkeley.edu>  
* Andy Zhang <andyzhangyn@berkeley.edu>
* Zhang, Ning <zhangning@berkeley.edu>

## Task 1: Efficient Alarm Clock

The problem with default Pintos `timer_sleep`  is that it is possible for the scheduler to switch back to the thread we just put into sleep. Thus, our idea is to keep a list of sleeping threads and ensure that no thread in the sleeping list will be scheduled unless enough time has elapsed.

1. Data Structures and Functions

    - In `thread.h`, the following attribute will be added to each `struct thread` instance:
        ```
        int64_t wakeup_time;
        ```
        which will record the time a sleeping thread should wake up.

    - In `timer.c`, the following global variables will be added:
        ```
        struct list sleeping_list;
        ```
        which will keep track of all sleeping threads ordered by their `wakeup_time` value.
    
    - In `thread.c`, the following function will be added:
        ```
        void wakeup_threads();
        ```
        which will attempt waking up all sleeping threads which are ready to be woken up. This function will be called by `schedule()` before it calls `next_thread_to_run()`. 

2. Algorithms

    - During a call to `timer_sleep(tick)`:
        1. Tests if `tick` is greater than zero. This matches with the default behavior (non-positive value will be ignored and no intentional switch will happen);
        2. Disable interrupts;
        2. Records the correct `wakeup_time` as the return value of `timer_ticks()`+ `tick`; 
        3. Adds the thread to `sleeping_list` in the correct location by iterating through the list and comparing the `wakeup_time`.
        4. Calls `thread_block()`;
        5. Re-enables interrupts.
    
    - During a call to `timer_interrupt(UNUSED)`:
    
        1. Disable interrupts;
        2. Record current `timer_ticks()`;
        3. For all threads in `sleeping_list`, calls `thread_unblock(thread)` if its `wakeup_time` is less than or equal to the current tick. Stop if a thread's `wakeup_time` is greater than current tick. This will wakeup all ready threads since `sleeping_list` is ordered by `wakeup_time`;
        4. Re-enables interrupts;
        5. Execute original `timer_interrupt`.

3. Synchronization

    `sleeping_list` might be accessed from multiple threads, so we will disable interrupts for `timer_sleep()` and `wakeup_threads()`. Note that interrupts must be disabled for the entirety of `timer_sleep()`, or a thread might be interrupted when it is already added to  `sleeping_list` but yet to be `THREAD_BLOCKED`.

    Note that the access to `ready_list` can also be concurrent through `thread_unblock(thread)`, but the called function has handled this case through interrupt disables.

4. Rationale

    Both `timer_sleep` and `wakeup_threads` will take O(n) time where n is the max number of threads. `sleeping_list` also takes an addition O(n) space.

    One alternative is to record the remaining time to wakeup instead of the timer's exact end time. However, this requires all waiting threads' attributes to be updated for every call to `schedule()` and is inefficient.

    Another alternative is to use a priority queue instead of a linked list for `waiting_list`. This will change the time complexity of `timer_sleep` to O(log n) and `wakeup_threads` to O(n log n) since multiple threads might need to be waken up. `wakeup_threads` or `schedule` is considered to happen more frequently, so the simpler linked list approach is chosen.

## Task 2: Priority Scheduler

Note that the most of the scheduling feature is already handled by the default behavior, and all we need to do is to order `ready_list` so that it puts the thread with highest priority in the front.

1. Data Structures and Functions

    - In `thread.h`, the following attributes will be added to each `struct thread` instance:
        ```
        static int init_priority;
        ```
        which is the priority that the thread is initialized with. This value will never change for a given thread.
        ```
        struct lock *waited_lock;
        ```
        which is the lock that the thread is currently waiting for, assuming it exists.
        ```
        struct list donating_threads;
        ```
        which is a list of threads that are currently donating priority to the current thread. Note that these threads must have a strictly higher priority than the current thread.
    
    - In `thread.h`, the following function will be added:
        ```
        static bool thread_cmp(const struct list_elem *x, 
                               const struct list_elem *y, 
                               void *aux UNUSED);
        ```
        which compares two `struct thread` elements `x` and `y` by their `priority`, to be used in `list_insert_ordered`.
    
2. Algorithms

    - During a call to `lock_acquire(lock)`:
    
        1. The thread sets its `waited_lock` to `lock` if `lock` currently has another `holder`;
        2. Iteratively try to propagate the priority down through `waited_lock`. For every successful propagation, the current thread is added to the receiver's `donating_threads`. Stop if `waited_lock->holder` has a higher or equal priority as the current thread. This ensures that the propagation terminates if a deadlock cycle exists. For example, if thread A is trying to acquire a `lock` held by thread B, it first accesses thread B through `waited_lock->holder`, and then compare its priority with that of thread B. If thread A's priority is higher than thread B's, update thread B's priority and add thread A to thread B's `donating_threads`. Then we move on to thread B's `waited_lock` and its `holder` to continue the propagation; 
        3. Default `lock_acquire` actions.

    - During a call to `lock_release(lock)`:
        
        1. Remove any threads that are waiting for the lock to be released in the thread's `donating_threads`. This is done by iterating through the list `donating_threads` and check if the thread's `waited_lock` is the same as `lock`;
        2. Set `lock->holder` to `NULL`;
        3. Set the current thread's priority to the maximum priority in `donating_threads` because the current thread may still be receiving donations due to other locks. This step also removes any threads with a lower priority or `DYING` threads from the list;
        4. Default `lock_release` actions;
        5. Compare the thread's priority with that of the top thread in `ready_list`. If it no longer has the highest priority, call `thread_yield()` .
        
        Note that in step (d), the `sema_up` action will unblock the waiter with the highest priority and allow it to own the released `lock`, as discussed in the next section.
        
    - During a call to `thread_yield()`:
        
        `ready_list` will enforce priority ordering: any insertion to the list will be done using `list_insert_ordered` with `thread_cmp`.
        
        Since `ready_list` is always ordered, a call to `next_thread_to_run()` will always yield the thread with the highest priority.
            
        If there are multiple thread with the same priority, the comparison will ensure that the newly yielded thread is inserted *behind* other threads with the same priority. Then the next thread to run is a different thread, and threads with the same priority will share the resources in a round robin fashion.
    
    - Handling semaphores:
    
        `sema->waiter` will enforce priority ordering: any insertion to the list will be done using `list_insert_ordered` with `thread_cmp`. 
    
    - Handling monitors:
    
        `cond->waiter` will enforce priority ordering: any insertion to the list will be done using `list_insert_ordered` with `thread_cmp`. 
    
3. Synchronization

    `thread_yield()` already has interrupts turned off, and it will be kept that way. The access to `thread->waited_locks`, `thread->donating_threads`, `sema->waiters`, `cond->waiters`, `ready_list` might be concurrent, and thus the interrupts must first be turned off when calling `lock_acquire` and `lock_release` to read and write these variables so that the access could be atomic.

4. Rationale

    All of these added actions will take O(n) time where n is the number of threads. 
    
    We chose keeping track of the lock that a thread is waiting for instead of a list of locks that a thread is holding for time efficiency. Keeping a list of lock would require a recursive tree search when refreshing priority, which would take O(n+m) time where m is the number of locks. This might be a minor downgrade, but using recursion would exert a larger overhead. 

## Task 3: Multi-level Feedback Queue Scheduler

1. Data Structures and Functions 

    - In `thread.h`, the following attribute will be added to each `struct thread` instance:
        ```
        int nice;
        ```
        which is the init nice value of each thread;
        ```
        fixed_point_t recent_cpu; 
        ```
        which is the amount of CPU time a thread has received “recently”.

    - In `timer.h`, the following global variables will be added:
        ```
        fixed_point_t load_avg;
        ```
        which is  the average number of threads ready to run over the past minute.
    
2. Algorithms

    - Once per tick, in `timer_interrupt`, the running thread's `recent_cpu` is increment by 1;
    
    - Once every 4 ticks, the current running thread updates its `recent_cpu` and `priority`;
    
    - Once per second, the system updates its `load_avg`, all of the threads update their `recent_cpu` and `priority`. It then run the thread with highest priority. If a tie occurs, the scheduler would choose the thread with the least `recent_cpu` value.
    
3. Synchronization

Most update actions is done in the interrupt handler and has no concurrent accesses. The access from `next_thread_to_run` is also atomic.

4. Rationale

This is a very straightforward task, so we implement it in the simplest way we could.

## Additional Questions

1. The test case runs as follows:

    - Run thread O with init priority 63, which initializes a semaphore with value 1, executes `sema_down`, waits for 5 seconds, and then executes `sema_up`;
    
    - Run thread A with init priority 31 in the background, which executes `sema_down` on the semaphore, prints `"thread A runs with priority 31"` and then execute `sema_up`;
    
    - Run thread B with init priority 0 in the background, which executes `sema_down` on the semaphore, prints `"thread B runs with priority 0"` and then execute `sema_up`.
    
    - Expected output:
    ```
    thread A runs with priority 31
    thread B runs with priority 0
    ```
    
    - Actual output:
    ```
    thread B runs with priority 0
    thread A runs with priority 31
    ```

2. 

| timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | next thread to run |
|:-----------:|:----:|:----:|:----:|:----:|:----:|:----:|:------------------:|
| 0           |   0  |   0  |   0  |  63  |  61  |  59  |          A         |
| 4           |   4  |   0  |   0  |  62  |  61  |  59  |          A         |
| 8           |   8  |   0  |   0  |  61  |  61  |  59  |          B         |
| 12          |   8  |   4  |   0  |  61  |  60  |  59  |          A         |
| 16          |  12  |   4  |   0  |  60  |  60  |  59  |          B         |
| 20          |  12  |   8  |   0  |  60  |  59  |  59  |          A         |
| 24          |  16  |   8  |   0  |  59  |  59  |  59  |          C         |
| 28          |  16  |   8  |   4  |  59  |  59  |  58  |          B         |
| 32          |  16  |  12  |   4  |  59  |  58  |  58  |          A         |
| 36          |  20  |  12  |   4  |  58  |  58  |  58  |          C         |

3. Yes. At tick 8, 16, 20, 24, 28, 32, 36, a tie occurs. Our policy for breaking a tie is that the scheduler chooses the thread with the least `recent_cpu` value, so that threads that rarely run recently has a higher chance to be run.

