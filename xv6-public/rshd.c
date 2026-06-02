// rshd - remote shell daemon with password authentication
//
// Listens on TCP port 23. Requires password before spawning shell.
// Connection: nc <host-ip> 2323   (password: xv6)
//
// QEMU forwards host:2323 -> xv6:23

#include "types.h"
#include "user.h"
#include "fcntl.h"

#define PORT     23
#define PASSWORD "xv6"

static void
dbg(char *msg)
{
  int fd = open("console", O_RDWR);
  if(fd >= 0){ write(fd, msg, strlen(msg)); close(fd); }
}

static void
net_write(int fd, char *s)
{
  write(fd, s, strlen(s));
}

// Read one line from fd, stripping CR and LF. Returns char count or -1 on error.
static int
readline(int fd, char *buf, int len)
{
  int i = 0;
  char c;
  while(i < len - 1){
    if(read(fd, &c, 1) <= 0) return -1;
    if(c == '\r') continue;
    if(c == '\n') break;
    buf[i++] = c;
  }
  buf[i] = 0;
  return i;
}

// Simple strcmp for the password check
static int
streq(char *a, char *b)
{
  while(*a && *b && *a == *b){ a++; b++; }
  return (*a == 0 && *b == 0);
}

int
main(void)
{
  dbg("rshd: starting on port 23 (host port 2323)\n");

  int lfd = socket(0, 0, 0);
  if(lfd < 0){ dbg("rshd: socket failed\n"); exit(); }

  if(bind(lfd, PORT) < 0){ dbg("rshd: bind failed\n"); exit(); }
  if(listen(lfd, 4) < 0){ dbg("rshd: listen failed\n"); exit(); }

  dbg("rshd: ready — connect with: nc <host-ip> 2323  (password: xv6)\n");

  for(;;){
    int cfd = accept(lfd, 0, 0);
    if(cfd < 0) continue;

    int pid = fork();
    if(pid < 0){ close(cfd); continue; }

    if(pid == 0){
      // Child: handle connection
      close(lfd);

      // Send banner + password prompt
      net_write(cfd, "\r\n");
      net_write(cfd, "xv6 Remote Shell\r\n");
      net_write(cfd, "Password: ");

      // Read password (echoing is not suppressed — xv6 has no termios)
      char pass[64];
      if(readline(cfd, pass, sizeof(pass)) < 0){
        net_write(cfd, "\r\nConnection error.\r\n");
        close(cfd);
        exit();
      }

      if(!streq(pass, PASSWORD)){
        net_write(cfd, "\r\nAccess denied.\r\n");
        close(cfd);
        exit();
      }

      net_write(cfd, "\r\nWelcome to xv6!\r\n");
      net_write(cfd, "Type commands below. 'exit' to disconnect.\r\n\r\n");

      // Wire socket fd to stdin/stdout/stderr
      close(0); dup(cfd);
      close(1); dup(cfd);
      close(2); dup(cfd);
      close(cfd);

      char *argv[] = { "sh", 0 };
      exec("sh", argv);

      // exec failed
      write(1, "rshd: exec sh failed\r\n", 22);
      exit();
    }

    // Parent: close per-connection fd, loop back to accept
    close(cfd);
  }
}
