#include "libbb_udpcast.h"
#include "fifo.h"

void udpc_initFifo(struct fifo *fifo, int blockSize)
{
    fifo->dataBufSize = blockSize * 4096;
    fifo->dataBuffer = xmalloc(fifo->dataBufSize+4096);
    fifo->dataBuffer += 4096 - (((unsigned long)fifo->dataBuffer) % 4096);

    /* Free memory queue is initially full */
    fifo->freeMemQueue = pc_makeProduconsum(fifo->dataBufSize, "free mem");
    pc_produce(fifo->freeMemQueue, fifo->dataBufSize);

    fifo->data = pc_makeProduconsum(fifo->dataBufSize, "receive");
}
