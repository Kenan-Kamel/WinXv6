#include "syscall.h"
#include "traps.h"

#define SYSCALL(name) \
  .global name; \
  name: \
    mov $SYS_ ## name, %rax; \
    mov %rcx, %r10 ;\
    syscall		  ;\
    ret

SYSCALL(fork)
SYSCALL(exit)
SYSCALL(wait)
SYSCALL(pipe)
SYSCALL(read)
SYSCALL(write)
SYSCALL(close)
SYSCALL(kill)
SYSCALL(exec)
SYSCALL(open)
SYSCALL(mknod)
SYSCALL(unlink)
SYSCALL(fstat)
SYSCALL(link)
SYSCALL(mkdir)
SYSCALL(chdir)
SYSCALL(dup)
SYSCALL(getpid)
SYSCALL(sbrk)
SYSCALL(sleep)
SYSCALL(uptime)
SYSCALL(screen_init)
SYSCALL(flush_screen)
SYSCALL(getmouse)
SYSCALL(getkey_async)
SYSCALL(halt)
SYSCALL(socket)
SYSCALL(bind)
SYSCALL(listen)
SYSCALL(accept)
