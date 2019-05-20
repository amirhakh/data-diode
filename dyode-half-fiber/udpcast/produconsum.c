#include <assert.h>
#include <errno.h>
#include "log.h"
#include "produconsum.h"
#include "util.h"

#define DEBUG 0


produconsum_t pc_makeProduconsum(size_t size, const char *name)
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
void pc_produce(produconsum_t pc, size_t amount)
{
    size_t produced = pc->produced;
    size_t consumed = pc->consumed;

    /* sanity checks:
     * 1. should not produce more than size
     * 2. do not pass consumed+size
     */
    if(amount > pc->size) {
        udpc_fatal(1, "Buffer overflow in produce %s: %zu > %zu \n",
                   pc->name, amount, pc->size);
    }

    produced += amount;
    if(produced >= 2*pc->size)
        produced -= 2*pc->size;

    if(produced > consumed + pc->size ||
            (produced < consumed && produced > consumed - pc->size)) {
        udpc_fatal(1, "Buffer overflow in produce %s: %zu > %zu [%zu] \n",
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


static size_t getProducedAmount(produconsum_t pc) {
    size_t produced = pc->produced;
    size_t consumed = pc->consumed;
    if(produced < consumed)
        return produced + 2 * pc->size - consumed;
    else
        return produced - consumed;
}


size_t pc_getWaiting(produconsum_t pc)
{
    return getProducedAmount(pc);
}


static size_t _consumeAny(produconsum_t pc, size_t minAmount,
                       struct timespec *ts) {
    size_t amount;
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
        if(ts == NULL)
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


size_t pc_consumed(produconsum_t pc, size_t amount)
{
    size_t consumed = pc->consumed;
    if(consumed >= 2*pc->size - amount) {
        consumed += amount - 2 *pc->size;
    } else {
        consumed += amount;
    }
    pc->consumed = consumed;
    return amount;
}

size_t pc_consumeAny(produconsum_t pc)
{
    return _consumeAny(pc, 1, 0);
}

size_t pc_consumeAnyWithTimeout(produconsum_t pc, struct timespec *ts)
{
    return _consumeAny(pc, 1, ts);
}



size_t pc_consumeAnyContiguous(produconsum_t pc)
{
    return pc_consumeContiguousMinAmount(pc, 1);
}

size_t pc_consumeContiguousMinAmount(produconsum_t pc, size_t amount)
{
    size_t n = _consumeAny(pc, amount, 0);
    size_t l = pc->size - (pc->consumed % pc->size);
    if(n > l)
        n = l;
    return n;
    
}

size_t pc_consume(produconsum_t pc, size_t amount)
{
    return _consumeAny(pc, amount, 0);
}

size_t pc_getConsumerPosition(produconsum_t pc)
{
    return pc->consumed % pc->size;
}

size_t pc_getProducerPosition(produconsum_t pc)
{
    return pc->produced % pc->size;
}

size_t pc_getSize(produconsum_t pc) {
    return pc->size;
}
