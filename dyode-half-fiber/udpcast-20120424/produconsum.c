#include <assert.h>
#include "threads.h"
#include <errno.h>
#include "log.h"
#include "produconsum.h"
#include "util.h"

#define DEBUG 0

/**
 * Simple implementation of producer-consumer pattern
 */
struct produconsum {
    unsigned int size;
    volatile unsigned int produced;
    unsigned int consumed;
    volatile int atEnd;
    pthread_mutex_t mutex;
    volatile int consumerIsWaiting;
    pthread_cond_t cond;
    const char *name;
};


produconsum_t pc_makeProduconsum(int size, const char *name)
{
    produconsum_t pc = MALLOC(struct produconsum);
    pc->size = size;
    pc->produced = 0;
    pc->consumed = 0;
    pc->atEnd = 0;
    pthread_mutex_init(&pc->mutex, NULL);
    pc->consumerIsWaiting = 0;
    pthread_cond_init(&pc->cond, NULL);
    pc->name = name;
    return pc;
}

static void wakeConsumer(produconsum_t pc)
{
    if(pc->consumerIsWaiting) {
	pthread_mutex_lock(&pc->mutex);
	pthread_cond_signal(&pc->cond);
	pthread_mutex_unlock(&pc->mutex);
    }
}

/**
 * We assume here that the producer never ever produces more than fits into
 * the buffer. To ensure this, use a second buffer, oriented in the other
 * direction
 */
void pc_produce(produconsum_t pc, unsigned int amount)
{
    unsigned int produced = pc->produced;
    unsigned int consumed = pc->consumed;

    /* sanity checks:
     * 1. should not produce more than size
     * 2. do not pass consumed+size
     */
    if(amount > pc->size) {
	udpc_fatal(1, "Buffer overflow in produce %s: %d > %d \n",
		   pc->name, amount, pc->size);
    }

    produced += amount;
    if(produced >= 2*pc->size)
	produced -= 2*pc->size;

    if(produced > consumed + pc->size ||
       (produced < consumed && produced > consumed - pc->size)) {
	udpc_fatal(1, "Buffer overflow in produce %s: %d > %d [%d] \n",
		   pc->name, produced, consumed, pc->size);
    }

    pc->produced = produced;
    wakeConsumer(pc);
}

void pc_produceEnd(produconsum_t pc)
{
    pc->atEnd = 1;
    wakeConsumer(pc);
}


static int getProducedAmount(produconsum_t pc) {
    unsigned int produced = pc->produced;
    unsigned int consumed = pc->consumed;
    if(produced < consumed)
	return produced + 2 * pc->size - consumed;
    else
	return produced - consumed;
}


unsigned int pc_getWaiting(produconsum_t pc)
{
    return getProducedAmount(pc);
}


static int _consumeAny(produconsum_t pc, unsigned int minAmount,
		       struct timespec *ts) {
    unsigned int amount;
#if DEBUG
    flprintf("%s: Waiting for %d bytes (%d:%d)\n", 
	    pc->name, minAmount, pc->consumed, pc->produced);
#endif
    pc->consumerIsWaiting=1;
    amount = getProducedAmount(pc);
    if(amount >= minAmount || pc->atEnd) {	
	pc->consumerIsWaiting=0;
#if DEBUG
	flprintf("%s: got %d bytes\n",pc->name, amount);
#endif
	return amount;
    }
    pthread_mutex_lock(&pc->mutex);
    while((amount=getProducedAmount(pc)) < minAmount && !pc->atEnd) {
#if DEBUG
	flprintf("%s: ..Waiting for %d bytes (%d:%d)\n", 
		pc->name, minAmount, pc->consumed, pc->produced);
#endif
	if(ts == 0)
	    pthread_cond_wait(&pc->cond, &pc->mutex);
	else {
	    int r;
#if DEBUG
	    flprintf("Before timed wait\n");
#endif
	    r=pthread_cond_timedwait(&pc->cond, &pc->mutex, ts);
#if DEBUG
	    flprintf("After timed wait %d\n", r);
#endif
	    if(r == ETIMEDOUT) {
		amount=getProducedAmount(pc);
		break;
	    }
	}
    }
    pthread_mutex_unlock(&pc->mutex);
#if DEBUG
    flprintf("%s: Got them %d (for %d) %d\n", pc->name, 
	    amount, minAmount, pc->atEnd);
#endif
    pc->consumerIsWaiting=0;
    return amount;
}


int pc_consumed(produconsum_t pc, int amount)
{
    unsigned int consumed = pc->consumed;
    if(consumed >= 2*pc->size - amount) {
	consumed += amount - 2 *pc->size;
    } else {
	consumed += amount;
    }
    pc->consumed = consumed;
    return amount;
}

int pc_consumeAny(produconsum_t pc)
{
    return _consumeAny(pc, 1, 0);
}

int pc_consumeAnyWithTimeout(produconsum_t pc, struct timespec *ts)
{
    return _consumeAny(pc, 1, ts);
}



int pc_consumeAnyContiguous(produconsum_t pc)
{
    return pc_consumeContiguousMinAmount(pc, 1);
}

int pc_consumeContiguousMinAmount(produconsum_t pc, int amount)
{
    int n = _consumeAny(pc, amount, 0);
    int l = pc->size - (pc->consumed % pc->size);
    if(n > l)
	n = l;
    return n;
    
}

int pc_consume(produconsum_t pc, int amount)
{
    return _consumeAny(pc, amount, 0);
}

unsigned int pc_getConsumerPosition(produconsum_t pc)
{
    return pc->consumed % pc->size;
}

unsigned int pc_getProducerPosition(produconsum_t pc)
{
    return pc->produced % pc->size;
}

unsigned int pc_getSize(produconsum_t pc) {
    return pc->size;
}
