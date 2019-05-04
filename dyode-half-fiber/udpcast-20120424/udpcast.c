#include "log.h"
#include "udpcast.h"
#include "udpc_process.h"

FILE *udpc_log;

int udpc_parseCommand(char *pipeName, char **arg) {
    char *ptr;
    int i;
    int haveSpace;
    
    haveSpace=1;
    i=0;
    for(ptr=pipeName; *ptr; ptr++) {
	if(*ptr == ' ') {
	    haveSpace=1;
	    *ptr = '\0';
	} else if(haveSpace) {
	    arg[i++] = ptr;
	    haveSpace=0;
	}
	if(i==256) {
	    udpc_fatal(1, "Too many arguments for pipe command\n");
	}
    }
    arg[i] = 0;
    return 0;
}

static int printProcessStatus(const char *message, int status)
{
#ifdef WIFEXITED
    if (WIFEXITED(status)) {
	if(WEXITSTATUS(status)) {
	    udpc_flprintf("%s process died with code %d\n",
			  message, WEXITSTATUS(status));
	    return(WEXITSTATUS(status));
	}
    } else {
	if(WIFSIGNALED(status)) {
	    udpc_flprintf("%s process caught signal %d\n",
			  message, WTERMSIG(status));
	    return 1;
	}
	udpc_flprintf("%s process did not cleanly exit\n", message);
	return 1;
    }
#else /* WIFEXITED */
    if(status != 0)
      udpc_flprintf("%s process died with code %d\n",		    
		    message, status);
#endif /* WIFEXITED */
    return 0;
}

/* wait for process.  If process returned abnormally, print message, and
 * exit too.
 */
int udpc_waitForProcess(int pid, const char *message)
{
    int status;

    /* wait for the writer to exit */
    if(waitpid(pid,&status,0) < 0) {
	return 0;
    }
    return printProcessStatus(message, status);
}
