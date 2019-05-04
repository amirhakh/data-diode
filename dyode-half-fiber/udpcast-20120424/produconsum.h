#ifndef PRODUCONSUM_H
#define PRODUCONSUM_H

typedef struct produconsum *produconsum_t;

produconsum_t pc_makeProduconsum(int size, const char *name);
void pc_produce(produconsum_t pc, unsigned int amount);
void pc_produceEnd(produconsum_t pc);
int pc_consumeAny(produconsum_t pc);
int pc_consumeAnyWithTimeout(produconsum_t pc, struct timespec *tv);


/**
 * Get contiguous chunk of data
 */
int pc_consumeAnyContiguous(produconsum_t pc);

/**
 * Get contiguous chunk of data, of at least amount x
 */
int pc_consumeContiguousMinAmount(produconsum_t pc, int amount);

/**
 * Consume minimum amount bytes. Wait until at least amount is
 * available, or end of file reached. This only makes sure that amount
 * bytes are available. It does not actually remove them from the queue
 * To remove bytes from the queue (i.e. move the consumer position), call
 * pc_consumed
 */
int pc_consume(produconsum_t pc, int amount);

/**
 * Get current position of consumer (moved by pc_consued)
 */
unsigned int pc_getConsumerPosition(produconsum_t pc);

/**
 * Get current position of producer (moved by pc_produced)
 */
unsigned int pc_getProducerPosition(produconsum_t pc);

/**
 * Get total size of circular buffer
 */
unsigned int pc_getSize(produconsum_t pc);


/**
 * Get total amount of data currently waiting to be consumed, without 
 * blocking
 */
unsigned int pc_getWaiting(produconsum_t pc);


/**
 * Signal that data has been consumed
 */
int pc_consumed(produconsum_t pc, int amount);


#endif
