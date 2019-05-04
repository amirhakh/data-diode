#include "udpc_process.h"
#include "log.h"

#ifndef __MINGW32__

#include <unistd.h>
#include <string.h>
#include <errno.h>

static void dupFd(int src, int target) {
  if(src != target) {
    close(target);
    if(dup2(src, target) < 0)
      udpc_fatal(1, "dup2 %d->%d: %s\n", src, target, strerror(errno));
    close(src);
  }
}

int open2(int in, int out, char **arg, int closeFd) {
  int pid;
  switch( (pid=fork()) ) {
  case 0: /* child */

    dupFd(in,0);
    dupFd(out,1);
 
    if(closeFd != -1)
      close(closeFd);
    
    execvp(arg[0], arg);
    udpc_fatal(1, "exec %s: %s\n", arg[0], strerror(errno));
  case -1: /* fork error */
    perror("fork");
    return -1;
  default: /* Father: just return */
    return pid;
  }
}

#else /* __MINGW32__ */

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Thanks http://lists.gnu.org/archive/html/groff/2003-07/msg00107.html */
static int dupFd(int src, int target, int *savedFd) {
  *savedFd = -1;

  if(src != target) {
    if ((*savedFd = dup (target)) < 0)
      udpc_fatal(1, "dup parent_fd %d %s", target, strerror(errno));
    
    if (dup2 (src, target) < 0)
      udpc_fatal(1, "dup2 child_fd %d %s", target, strerror(errno));    
  }

  return 0;
}

static int restoreFd(int src, int target) {
  if(src == -1 || src == target)
    /* Do nothing... */
    return 0;
  if (dup2 (src, target) < 0)
    udpc_fatal(1, "restore child_fd %d %s", target, strerror(errno));  
  close(src);
  return 0;
}

int open2(int in, int out, char **arg, int closeFd) {
  int parent_in=-1;
  int parent_out=-1;
  int child;

  dupFd(in, 0, &parent_in);
  dupFd(out, 1, &parent_out);

  child = _spawnvp(P_NOWAIT, arg[0], (const char* const*)&arg[1]);
 
  restoreFd(parent_in, 0);
  restoreFd(parent_out, 1);
  return child;
}


#endif /* __MINGW32__ */
