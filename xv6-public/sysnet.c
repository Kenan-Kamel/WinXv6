#include "types.h"
#include "defs.h"
#include "param.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "net.h"

// Allocate a file descriptor backed by a socket.
// Returns fd on success, -1 on failure.
static int
sockfd_alloc(struct sock *s)
{
  struct file *f = filealloc();
  if (!f) return -1;

  f->type     = FD_SOCKET;
  f->readable = 1;
  f->writable = 1;
  f->ref      = 1;
  f->sock     = s;

  // Find a slot in the process file table
  for (int fd = 0; fd < NOFILE; fd++) {
    if (proc->ofile[fd] == 0) {
      proc->ofile[fd] = f;
      return fd;
    }
  }
  // No slot; release file
  f->ref = 0;
  f->type = FD_NONE;
  return -1;
}

// socket(domain, type, protocol) -> fd
// We only support AF_INET/SOCK_STREAM; args are ignored.
addr_t
sys_socket(void)
{
  struct sock *s = sockalloc();
  if (!s) return -1;

  int fd = sockfd_alloc(s);
  if (fd < 0) {
    sockfree(s);
    return -1;
  }
  return fd;
}

// bind(fd, port) -> 0 or -1
// Simplified: only takes an fd and a port number (no sockaddr struct).
addr_t
sys_bind(void)
{
  int fd, port;
  if (argint(0, &fd) < 0 || argint(1, &port) < 0)
    return -1;
  if (fd < 0 || fd >= NOFILE || proc->ofile[fd] == 0)
    return -1;
  struct file *f = proc->ofile[fd];
  if (f->type != FD_SOCKET)
    return -1;
  return sockbind(f->sock, (ushort)port);
}

// listen(fd, backlog) -> 0 or -1
// The socket must already be bound.  backlog is ignored (we use SOCK_BACKLOG).
addr_t
sys_listen(void)
{
  int fd, backlog;
  if (argint(0, &fd) < 0 || argint(1, &backlog) < 0)
    return -1;
  if (fd < 0 || fd >= NOFILE || proc->ofile[fd] == 0)
    return -1;
  struct file *f = proc->ofile[fd];
  if (f->type != FD_SOCKET)
    return -1;
  // sockbind already set state=TCP_LISTEN; nothing else to do
  (void)backlog;
  return 0;
}

// accept(fd, 0, 0) -> new_fd or -1
// Ignores addr/addrlen arguments.
addr_t
sys_accept(void)
{
  int fd;
  if (argint(0, &fd) < 0)
    return -1;
  if (fd < 0 || fd >= NOFILE || proc->ofile[fd] == 0)
    return -1;
  struct file *f = proc->ofile[fd];
  if (f->type != FD_SOCKET)
    return -1;

  struct sock *ns = 0;
  if (sockaccept(f->sock, &ns) < 0)
    return -1;

  int nfd = sockfd_alloc(ns);
  if (nfd < 0) {
    sockfree(ns);
    return -1;
  }
  return nfd;
}
