#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "fifo.h"
#include "udp-sender.h"
#include "udpcast.h"
#include "udpc_process.h"

#define BLOCKSIZE 4096

#ifdef O_BINARY
#include <io.h>
#define HAVE_O_BINARY
#endif

#ifndef O_BINARY
# define O_BINARY 0
#endif

int openFile(struct disk_config *config)
{
    if(config->fileName != NULL) {
	int in = open(config->fileName, O_RDONLY | O_BINARY, 0);
	if (in < 0) {
#ifdef NO_BB
#ifndef errno
	    extern int errno;
#endif
#endif
	    udpc_fatal(1, "Could not open file %s: %s\n", config->fileName,
		       strerror(errno));
	}
	return in;
    } else {
#ifdef __MINGW32__
	setmode(0, O_BINARY);
#endif
	return 0;
    }
}


int openPipe(struct disk_config *config, int in, int *pidp)
{
    /**
     * Open the pipe
     */
    *pidp=0;
    if(config->pipeName != NULL) {
	/* pipe */
	char *arg[256];
	int filedes[2];

	udpc_parseCommand(config->pipeName, arg);

	if(pipe(filedes) < 0) {
	    perror("pipe");
	    exit(1);
	}
#ifdef HAVE_O_BINARY
	setmode(filedes[0], O_BINARY);
	setmode(filedes[1], O_BINARY);
#endif
	*pidp=open2(in, filedes[1], arg, filedes[0]);
	close(filedes[1]);
	in = filedes[0];
    }
    return in;
}

/**
 * This file is reponsible for reading the data to be sent from disk
 */
int localReader(struct fifo *fifo, int in)
{
    while(1) {
	int pos = pc_getConsumerPosition(fifo->freeMemQueue);
	int bytes = 
	    pc_consumeContiguousMinAmount(fifo->freeMemQueue, BLOCKSIZE);
	if(bytes > (pos + bytes) % BLOCKSIZE)
	    bytes -= (pos + bytes) % BLOCKSIZE;

	if(bytes == 0)
		/* net writer exited? */
		break;

	bytes = read(in, fifo->dataBuffer + pos, bytes);
	if(bytes < 0) {
	    perror("read");
	    exit(1);
	}

	if (bytes == 0) {
	    /* the end */
	    pc_produceEnd(fifo->data);
	    break;
	} else {
	    pc_consumed(fifo->freeMemQueue, bytes);
	    pc_produce(fifo->data, bytes);
	}
    }
    return 0;
}
