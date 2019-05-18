#ifndef PRODUCONSUM_H
#define PRODUCONSUM_H

#include "threads.h"

/**
 * Simple implementation of producer-consumer pattern
 */
struct produconsum {
    size_t size;
    volatile size_t produced;
    size_t consumed;
    volatile int atEnd;
    pthread_mutex_t mutex;
    volatile int consumerIsWaiting;
    pthread_cond_t cond;
    const char *name;
};

typedef struct produconsum *produconsum_t;

produconsum_t pc_makeProduconsum(size_t size, const char *name);
void pc_produce(produconsum_t pc, size_t amount);
void pc_produceEnd(produconsum_t pc);
size_t pc_consumeAny(produconsum_t pc);
size_t pc_consumeAnyWithTimeout(produconsum_t pc, struct timespec *tv);


/**
 * Get contiguous chunk of data
 */
size_t pc_consumeAnyContiguous(produconsum_t pc);

/**
 * Get contiguous chunk of data, of at least amount x
 */
size_t pc_consumeContiguousMinAmount(produconsum_t pc, size_t amount);

/**
 * Consume minimum amount bytes. Wait until at least amount is
 * available, or end of file reached. This only makes sure that amount
 * bytes are available. It does not actually remove them from the queue
 * To remove bytes from the queue (i.e. move the consumer position), call
 * pc_consumed
 */
size_t pc_consume(produconsum_t pc, size_t amount);

/**
 * Get current position of consumer (moved by pc_consued)
 */
size_t pc_getConsumerPosition(produconsum_t pc);

/**
 * Get current position of producer (moved by pc_produced)
 */
size_t pc_getProducerPosition(produconsum_t pc);

/**
 * Get total size of circular buffer
 */
size_t pc_getSize(produconsum_t pc);


/**
 * Get total amount of data currently waiting to be consumed, without
 * blocking
 */
size_t pc_getWaiting(produconsum_t pc);


/**
 * Signal that data has been consumed
 */
size_t pc_consumed(produconsum_t pc, size_t amount);


#endif
