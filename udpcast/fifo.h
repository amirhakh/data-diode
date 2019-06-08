#ifndef FIFO_H
#define FIFO_H

#include "threads.h"
#include "produconsum.h"

typedef struct fifo { 
    unsigned char *dataBuffer;
    size_t dataBufSize;

    produconsum_t freeMemQueue; /* queue for free memory */
    produconsum_t data; /* queue for received data or data received
             * from disk */

    pthread_t thread;
} *fifo_t;

#define initFifo udpc_initFifo

void initFifo(struct fifo *fifo, size_t blockSize);

#endif
