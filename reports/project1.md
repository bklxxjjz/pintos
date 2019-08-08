Final Report for Project 1: Threads
===================================

## Task 1: Efficient Alarm Clock

The only change we made is that we moved `sleeping_list` into `thread.h`
for consistency.

## Task 2: Priority Scheduler

We made some fundamental changes on our algorithm. Instead of having a
list of donating threads in `struct thread`, we kept a list of held
locks. This is due to the fact that the `struct list_elem` can only be
in one list at any given time. However, our implementation requires
multiple threads having the same thread in their donating list, and thus
is harder to implement. 

Since we propagate priorities when failed to acquire a lock, we only
need to look at the 1-depth waiters for all the held locks to properly
refresh priority of the releasing thread, so this algorithm even has a
better time complexity than our original design.

Some other points we didn't think of is we need to `thread_yield` after
any action that may have cause the priority of the current thread not be
the maximum value anymore, and priority need to refreshed after a call
to `thread_change_priority`. These problems led to some frustrating
debugging.

## Task 3: Multi-level Feedback Queue Scheduler

Nothing is changed. Pretty straightforward to implement.

## Reflection

We basically just did the whole project together. Most are done during 
offline meeting, or one of us will implement the basic structure and the
rest will help testing and debugging.

One thing that we should have done first is that we should have create a
code style format before we started the format. The quite different 
pintos code style created some trouble for us when we finished up the 
project and have to locate everything wrote and change the code style.
Due to some problems with the VM, IDE cannot recognize and link the 
source files, and thus the auto-formatting functionality became useless.
Hope the rest of the projects can be more editor-friendly, since without
the ability of auto-completion and jumping is quite frustrating.
