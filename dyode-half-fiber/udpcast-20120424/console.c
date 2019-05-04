#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "console.h"
#include "util.h"
#include "udpcast.h"

#ifndef __MINGW32__

#include <termios.h>
#include <sys/select.h>

struct console_t {
    int fd; /* Filedescriptor for console, or -1 if disabled */
    struct termios oldtio; /* old settings, for restore */
    int needClose; /* Does file descriptor need to be closed on reset? */
    int needRestore; /* Is the file descriptor indeed a terminal, and needs
		      * to be restored? */
};

console_t *prepareConsole(int fd) {
    struct termios newtio;
    int needClose=0;
    console_t *c;

    if(fd < 0) {
	fd = open("/dev/tty", O_RDWR);
	if(fd < 0) {
	    fprintf(stderr, "Could not open keyboard: %s\n", strerror(errno));
	    return NULL;
	}
	needClose=1;
    }

    c = MALLOC(console_t);
    if(c == NULL)
	return c;

    c->fd = fd;
    c->needClose = needClose;
    c->needRestore = 0;

    if(tcgetattr(c->fd, &c->oldtio) >= 0) {
	newtio = c->oldtio;
	newtio.c_lflag &= ~ECHO;
	newtio.c_lflag &= ~ICANON;
	newtio.c_cc[VMIN] = 1;
	newtio.c_cc[VTIME] = 0;
	if(tcsetattr(c->fd, TCSAFLUSH, &newtio) < 0)
	    perror("Set terminal to raw");
	else
	    c->needRestore = 1;
    }

    return c;
}

int selectWithConsole(console_t *con, int maxFd, 
		      fd_set *read_set, struct timeval *tv,
		      int *keyPressed) {
    int ret;

    if(con) {
	int fd = con->fd;
	FD_SET(fd, read_set);
	if(fd >= maxFd)
	    maxFd = fd+1;
    }
    ret = select(maxFd, read_set, NULL, NULL, tv);
    if(ret < 0)
	return -1;
    if(con && FD_ISSET(con->fd, read_set)) {
	*keyPressed = 1;
    }
    return ret;
}

void restoreConsole(console_t **cp, int doConsume) {
    console_t *c=*cp;
    int ch='\0';
    int r UNUSED;

    if(c == NULL)
      return;

    /* If key pressed, consume it. If letter is q, quit */
    if(doConsume) {
      r = read(c->fd, &ch, 1);
    }

    if(c->needRestore && tcsetattr(c->fd, TCSAFLUSH, &c->oldtio) < 0) {
	perror("Restore terminal settings");
    }
    *cp = NULL;
    if(c->needClose)
	close(c->fd);
    free(c);
    if(ch == 'q')
	exit(1);
}

#else /* __MINGW32__ */

#include "log.h"

struct console_t {
    HANDLE thread[2]; /* 0: console, 1: select */
    int fd;
    int maxFd;
    fd_set read_set;
    struct timeval tv;
    struct timeval *tvp;
    int select_return;
    int keyPressed;
    char ch;
};

static DWORD WINAPI waitForKeyPress(LPVOID lpParameter) {
    console_t *con = lpParameter;
    int n=read(con->fd, &con->ch, 1);
    if(n == 1)
	con->keyPressed=1;
    return 0;
}

static DWORD WINAPI waitForSelect(LPVOID lpParameter) {
    console_t *con = lpParameter;
    con->select_return = select(con->maxFd, &con->read_set, 
				NULL, NULL, con->tvp);
    return 0;
}


console_t *prepareConsole(int fd) { 
    console_t *con;
    if(fd < 0) {
	fd = open("CON", O_RDONLY);
	if(fd < 0)
	    return NULL;
    } else {
	fd = dup(fd);  /* dup console filedescriptor in order to avoid 
			*  race with pipe spawner... */      
    }    
    con = MALLOC(console_t);
    if(con == NULL)
	return con;
    con->thread[0] = con->thread[1] = NULL;
    con->keyPressed=0;
    con->fd = fd;
    return con;
}

static HANDLE startThread(console_t *con,
			  LPTHREAD_START_ROUTINE lpStartAddress) {
    /* Start thread ... 
     * see http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dllproc/base/createthread.asp
     */	       
    return CreateThread(NULL,	/* lpThreadAttributes */
			0,	/* dwStackSize */
			lpStartAddress,
			con,	/* lpParameter */
			0,	/* dwCreationFlags */
			NULL    /* lpThreadId */);
}

int selectWithConsole(console_t *con, int maxFd, 
		      fd_set *read_set, struct timeval *tv,
		      int *keyPressed) {
    int r;
    if(con == NULL)
	return select(maxFd, read_set, NULL, NULL, tv);
    if(!con->thread[0]) {
	con->thread[0]=startThread(con, waitForKeyPress);
	if(!con->thread[0])
	    udpc_fatal(1, "Could not start console listen thread");
    }

    if(con->thread[1])
	udpc_fatal(1, "Two select threads started at once!");

    con->maxFd = maxFd;
    memcpy(&con->read_set, read_set, sizeof(*read_set));
    if(tv) {
	memcpy(&con->tv, tv, sizeof(*tv));
	con->tvp = &con->tv;
    } else {
	con->tvp=NULL;
    }

    /* Start select thread */
    con->thread[1]=startThread(con, waitForSelect);
    if(!con->thread[1])
	udpc_fatal(1, "Could not start select thread");

    /* http://msdn.microsoft.com/library/default.asp?url=/library/en-us/dllproc/base/waitformultipleobjects.asp
     */    
    switch( (r=WaitForMultipleObjects(2, con->thread, FALSE, INFINITE)) ) {
    case WAIT_OBJECT_0:
	*keyPressed=1;
	CloseHandle(con->thread[0]);
	CloseHandle(con->thread[1]);
	FD_ZERO(read_set);
	return 1;
    case WAIT_OBJECT_0+1:
	*keyPressed=0;
	CloseHandle(con->thread[1]);
	con->thread[1]=NULL;
	memcpy(read_set, &con->read_set, sizeof(*read_set));
	return con->select_return;
    default:
	udpc_fatal(1, "Unexpected result %d for waitForMultipleObjects", r);
	return -1;
    }
}

void restoreConsole(console_t **cp, int doConsume) { 
    console_t *c=*cp;

    /* If key pressed, consume it. If letter is q, quit */
    if(doConsume) {
	if (c->keyPressed && c->ch == 'q') {
	    exit(1);
	}
    }

    /* We do not free the console, because the select thread might
     * still be active... */
    *cp = NULL;
}

#endif /* __MINGW32__ */
