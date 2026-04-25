// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *desktop_argv[] = { "desktop", 0 };
char *sh_argv[]      = { "sh",      0 };
char *rshd_argv[]    = { "rshd",    0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr

  // rshd: persistent networking daemon
  if(fork() == 0){
    exec("rshd", rshd_argv);
    printf(1, "init: exec rshd failed\n");
    exit();
  }

  // desktop: GUI session. In nographic mode, screen_init fails and
  // desktop exits immediately; the sh loop below takes over.
  if(fork() == 0){
    exec("desktop", desktop_argv);
    printf(1, "init: exec desktop failed\n");
    exit();
  }

  // sh loop: serial-console shell, always available
  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){
      exec("sh", sh_argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    while((wpid=wait()) >= 0 && wpid != pid)
      ;  // silently collect desktop/rshd zombie exits
  }
}
