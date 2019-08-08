#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "user/syscall.h"

void syscall_init (void);

int sys_practice (int);
void sys_halt (void);
void sys_exit (int);
pid_t sys_exec (const char *);
int sys_wait (pid_t);

bool sys_create (const char *, unsigned);
bool sys_remove (const char *);
int sys_open (const char *);
int sys_filesize (int);
int sys_read (int, void *, unsigned);
int sys_write (int, const void *, unsigned);
int sys_seek (int, unsigned);
int sys_tell (int);
int sys_close (int);

bool sys_chdir (const char *);
bool sys_mkdir (const char *);
bool sys_readdir (int, char *);
bool sys_isdir (int);
int sys_inumber (int);

#endif /* userprog/syscall.h */
