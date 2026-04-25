// rshd - remote shell daemon
// Listens on TCP port 23.  For each incoming connection, forks a child
// that redirects stdin/stdout/stderr to the socket and exec's the shell.
//
// Usage (inside xv6): rshd
// From host:          telnet localhost 2323
//                     nc   localhost 2323

#include "types.h"
#include "user.h"
#include "fcntl.h"

#define PORT 23

// Write a string to the console regardless of where fd 1 currently points.
// Opens /console directly so debug output always goes to the xv6 console.
static void
dbg(char *msg)
{
  int fd = open("console", O_RDWR);
  if (fd >= 0) {
    write(fd, msg, strlen(msg));
    close(fd);
  }
}

int
main(void)
{
  dbg("rshd: start\n");

  int lfd = socket(0, 0, 0);
  printf(1, "rshd: socket() = %d\n", lfd);
  if (lfd < 0) {
    printf(2, "rshd: socket failed\n");
    exit();
  }

  int r = bind(lfd, PORT);
  printf(1, "rshd: bind(%d) = %d\n", PORT, r);
  if (r < 0) {
    printf(2, "rshd: bind failed\n");
    exit();
  }

  r = listen(lfd, 4);
  printf(1, "rshd: listen() = %d\n", r);
  if (r < 0) {
    printf(2, "rshd: listen failed\n");
    exit();
  }
  printf(1, "rshd: listening on port %d (lfd=%d)\n", PORT, lfd);

  for (;;) {
    dbg("rshd: waiting in accept...\n");
    int cfd = accept(lfd, 0, 0);
    printf(1, "rshd: accept() = %d\n", cfd);
    if (cfd < 0) {
      printf(2, "rshd: accept failed\n");
      continue;
    }

    // Send a banner directly over the socket before forking, to test
    // whether the socket write path works at all.
    char *banner = "rshd: connection accepted, spawning shell\r\n";
    int bw = write(cfd, banner, strlen(banner));
    printf(1, "rshd: banner write = %d bytes\n", bw);

    int pid = fork();
    printf(1, "rshd: fork() = %d\n", pid);
    if (pid < 0) {
      printf(2, "rshd: fork failed\n");
      close(cfd);
      continue;
    }

    if (pid == 0) {
      // Child: wire socket to stdin/stdout/stderr then exec shell.
      // Use dbg() for any child-side logging since fd 1/2 are about
      // to be replaced.
      dbg("rshd[child]: redirecting fds\n");

      close(0); int r0 = dup(cfd);
      close(1); int r1 = dup(cfd);
      close(2); int r2 = dup(cfd);

      // Log dup results to console before we lose easy console access.
      // (After this point fd 0/1/2 are the socket.)
      {
        char buf[64];
        buf[0] = 'r'; buf[1] = 's'; buf[2] = 'h'; buf[3] = 'd';
        buf[4] = '['; buf[5] = 'c'; buf[6] = ']'; buf[7] = ':';
        buf[8] = ' '; buf[9] = 'd'; buf[10] = 'u'; buf[11] = 'p';
        buf[12] = 's'; buf[13] = '=';
        buf[14] = '0' + r0;
        buf[15] = ',';
        buf[16] = '0' + r1;
        buf[17] = ',';
        buf[18] = '0' + r2;
        buf[19] = '\n';
        buf[20] = 0;
        dbg(buf);
      }

      close(cfd);
      close(lfd);

      // Write a test string to the socket (now fd 1) to check that
      // output actually reaches the remote client.
      write(1, "rshd: shell starting\r\n", 22);

      dbg("rshd[child]: exec sh\n");
      char *argv[] = { "sh", 0 };
      exec("sh", argv);

      dbg("rshd[child]: exec sh FAILED\n");
      write(1, "rshd: exec sh failed\r\n", 22);
      exit();
    }

    // Parent: close the per-connection fd; do not wait (accept loop continues).
    close(cfd);
  }
}
