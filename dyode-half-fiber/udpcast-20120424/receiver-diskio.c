#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include <errno.h>
#include <string.h>

#include "log.h"
#include "udpcast.h"
#include "fifo.h"
#include "udp-receiver.h"
#include "udpc_process.h"

#define BLOCKSIZE 4096

#ifdef O_BINARY
# include <io.h>
#endif

int writer(struct fifo *fifo, int outFile) {
    int fifoSize = pc_getSize(fifo->data);
    if(fifoSize % BLOCKSIZE)
	udpc_fatal(1, "Fifo size not a multiple of block size\n");
    while(1) {
	int pos=pc_getConsumerPosition(fifo->data);
	int bytes = pc_consumeContiguousMinAmount(fifo->data, BLOCKSIZE);
	if (bytes == 0) {
	    return 0;
	}

	/*
	 * If we have more than blocksize, round down to nearest blocksize
	 * multiple
	 */
	if(pos + bytes != fifoSize && 
	   bytes > (pos + bytes) % BLOCKSIZE)
	    bytes -= (pos + bytes) % BLOCKSIZE;
#if DEBUG
	flprintf("writing at pos=%p\n", fifo->dataBuffer + pos);
#endif

	/* make sure we don't write to big a chunk... Better to
	 * liberate small chunks one by one rather than attempt to
	 * write out a bigger chunk and block reception for too
	 * long */
	if (bytes > 128 * 1024)
	    bytes = 64 * 1024;

	bytes = write(outFile, fifo->dataBuffer + pos, bytes);
	if(bytes < 0) {
	    perror("write");
	    exit(1);
	}
	pc_consumed(fifo->data, bytes);
	pc_produce(fifo->freeMemQueue, bytes);
    }
}


int openPipe(int outFile, 
	     struct disk_config *disk_config,
	     int *pipePid) {
    if(disk_config->pipeName != NULL) {
	char *arg[256];
	int filedes[2];
	
	udpc_parseCommand(disk_config->pipeName, arg);
	if(pipe(filedes) < 0) {
	    perror("pipe");
	    exit(1);
	}
#ifdef O_BINARY
	setmode(filedes[0], O_BINARY);
	setmode(filedes[1], O_BINARY);
#endif
	*pipePid=open2(filedes[0], outFile, arg, filedes[1]);
	close(filedes[0]);
	outFile = filedes[1];
    }
    return outFile;
}
