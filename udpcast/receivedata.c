#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

#include "socklib.h"
#include "threads.h"
#include "log.h"
#include "fifo.h"
#include "udpc-protoc.h"
#include "udp-receiver.h"
#include "util.h"
#include "statistics.h"
#include "fec.h"

#define DEBUG 0

#define ADR(x, bs) (fifo->dataBuffer + \
    (slice->base+(x)*(bs)) % fifo->dataBufSize)

#define SLICEMAGIC 0x41424344
#define NR_SLICES 64        // 4 * 16
#define NR_BLOCKS 65536 // 4096*16

typedef enum slice_state {
    SLICE_FREE,			/* Free slice */
    SLICE_RECEIVING,		/* Data being received */
    SLICE_DONE			/* All data received */
#ifdef BB_FEATURE_UDPCAST_FEC
    ,
    SLICE_FEC,			/* Fec calculation in progress */
    SLICE_FEC_DONE		/* Fec calculation done */
#endif
} slice_state_t;

#ifdef BB_FEATURE_UDPCAST_FEC
struct fec_desc {
    unsigned char *adr; /* address of FEC block */
    uint32_t fecBlockNo; /* number of FEC block */
    uint32_t erasedBlockNo; /* erased data block */
};
#endif

typedef struct slice {
    int32_t magic;
    volatile slice_state_t state;
    uint64_t base; /* base offset of beginning of slice */
    int64_t sliceNo; /* current slice number */
    uint32_t blocksTransferred; /* blocks transferred during this slice */
    uint32_t dataBlocksTransferred; /* data blocks transferred during this slice */
    uint32_t bytes; /* number of bytes in this slice (or 0, if unknown) */
    int32_t bytesKnown; /* is number of bytes known yet? */
    uint64_t freePos; /* where the next data part will be stored to */
    struct retransmit retransmit;


    /* How many data blocks are there missing per stripe? */
    short missing_data_blocks[MAX_FEC_INTERLEAVE];

#ifdef BB_FEATURE_UDPCAST_FEC
    uint32_t fec_stripes; /* number of stripes for FEC */

    /* How many FEC blocs do we have per stripe? */
    uint16_t fec_blocks[MAX_FEC_INTERLEAVE];

    struct fec_desc fec_descs[MAX_SLICE_SIZE];
#endif
} *slice_t;

struct clientState {
    struct fifo *fifo;
    struct client_config *client_config;
    struct net_config *net_config;
    union serverDataMsg Msg;

    struct msghdr data_hdr;

    /* pre-prepared messages */
    struct iovec data_iov[2];

    struct slice *currentSlice;
    int64_t currentSliceNo;
    receiver_stats_t stats;
    
    produconsum_t free_slices_pc;
    struct slice slices[NR_SLICES];
//    uint16_t slice_pos, slice_free;

    /* Completely received slices */
    int64_t receivedPtr;
    int64_t receivedSliceNo;        /* Last slice stored */
    uint16_t receivingSliceWindowSize;       /* Slice window head */

    char tempBlock[MAX_BLOCK_SIZE];

    produconsum_t fec_data_pc;
    struct slice *fec_slices[NR_SLICES];
    pthread_t fec_thread;

    /* A reservoir of free blocks for FEC */
    produconsum_t freeBlocks_pc;
    unsigned char **blockAddresses; /* adresses of blocks in local queue */

    unsigned char **localBlockAddresses;
    /* local blocks: freed FEC blocks after we
                 * have received the corresponding data */
    int64_t localPos;

    unsigned char *blockData;
    unsigned char *nextBlock;

#ifdef BB_FEATURE_UDPCAST_FEC
    int8_t use_fec; /* do we use forward error correction ? */
#endif

    int8_t endReached; /* end of transmission reached:
               0: transmission in progress
               2: network transmission _and_ FEC
                  processing finished
            */

    int8_t netEndReached; /* In case of a FEC transmission; network
            * transmission finished. This is needed to avoid
            * a race condition, where the receiver thread would
            * already prepare to wait for more data, at the same
            * time that the FEC would set endReached. To avoid
            * this, we do a select without timeout before
            * receiving the last few packets, so that if the
            * race condition strikes, we have a way to protect
            * against
            */

    int8_t promptPrinted;  /* Has "Press any key..." prompt already been printed */

    int selectedFd;

#ifdef BB_FEATURE_UDPCAST_FEC
    fec_code_t fec_code;
#endif
};

static void printMissedBlockMap(struct clientState *clst, slice_t slice)
{
    uint32_t i, first=1;
    uint32_t blocksInSlice = (uint32_t) (slice->bytes +  clst->net_config->blockSize - 1) /
            clst->net_config->blockSize;

    for(i=0; i< blocksInSlice ; i++) {
        if(!BIT_ISSET(i,slice->retransmit.map)) {
            if(first)
                fprintf(stderr, "Missed blocks: ");
            else
                fprintf(stderr, ",");
            fprintf(stderr, "%d",i);
            first=0;
        }
    }
    if(!first)
        fprintf(stderr, "\n");
    first=1;
#ifdef BB_FEATURE_UDPCAST_FEC
    if(slice->fec_stripes != 0) {
        for(i=0; i<MAX_SLICE_SIZE; i++) {
            if(i / slice->fec_stripes <
                    slice->fec_blocks[i % slice->fec_stripes]) {
                if(first)
                    fprintf(stderr, "FEC blocks: ");
                else
                    fprintf(stderr, ",");
                fprintf(stderr, "%u",slice->fec_descs[i].fecBlockNo);
                first=0;
            }
        }
    }
#endif
    if(!first)
        fprintf(stderr, "\n");
    fprintf(stderr, "Blocks received: %u/%u/%d\n",
            slice->dataBlocksTransferred, slice->blocksTransferred,
            blocksInSlice);
#ifdef BB_FEATURE_UDPCAST_FEC
    for(i=0; i<slice->fec_stripes; i++) {
        fprintf(stderr, "Stripe %2d: %3d/%3d %s\n", i,
                slice->missing_data_blocks[i],
                slice->fec_blocks[i],
                slice->missing_data_blocks[i] > slice->fec_blocks[i] ?
                    "**************" :"");
    }
#endif
}

static ssize_t sendOk(struct client_config *client_config, int64_t sliceNo)
{
    struct ok ok;
    ok.opCode = htobe16(CMD_OK);
    ok.reserved = 0;
    ok.sliceNo = (int64_t) htobe64(sliceNo);
    return SSEND(ok);
}

static ssize_t sendRetransmit(struct clientState *clst,
                              struct slice *slice,
                              uint32_t rxmit) {
    struct client_config *client_config = clst->client_config;

    assert(slice->magic == SLICEMAGIC);
    slice->retransmit.opCode = htobe16(CMD_RETRANSMIT);
    slice->retransmit.reserved = 0;
    slice->retransmit.sliceNo = (int64_t) htobe64(slice->sliceNo);
    slice->retransmit.rxmit = htobe32(rxmit);
    return SSEND(slice->retransmit);
}


static unsigned char *getBlockSpace(struct clientState *clst)
{
    uint64_t pos;

    if(clst->localPos) {
        clst->localPos--;
        return clst->localBlockAddresses[clst->localPos];
    }

    pc_consume(clst->freeBlocks_pc, 1);
    pos = pc_getConsumerPosition(clst->freeBlocks_pc);
    pc_consumed(clst->freeBlocks_pc, 1);
    return clst->blockAddresses[pos];
}

#ifdef BB_FEATURE_UDPCAST_FEC
static void freeBlockSpace(struct clientState *clst, unsigned char *block)
{
    size_t pos = pc_getProducerPosition(clst->freeBlocks_pc);
    assert(block != 0);
    clst->blockAddresses[pos] = block;
    pc_produce(clst->freeBlocks_pc, 1);
}
#endif

static void setNextBlock(struct clientState *clst)
{
    clst->nextBlock = getBlockSpace(clst);
}

/**
 * Initialize slice for new slice number
 * memory is not touched
 */
static struct slice *initSlice(struct clientState *clst, 
                               struct slice *slice,
                               int64_t sliceNo)
{
    assert(slice->state == SLICE_FREE || slice->state == SLICE_RECEIVING);

    slice->magic = SLICEMAGIC;
    slice->state = SLICE_RECEIVING;
    slice->blocksTransferred = 0;
    slice->dataBlocksTransferred = 0;
    BZERO(slice->retransmit);
    slice->freePos = 0;
    slice->bytes = 0;
//    if(clst->currentSlice != NULL) {
//        if(!clst->currentSlice->bytesKnown)
//            udpc_fatal(1, "Previous slice size not known\n");
//        if(clst->net_config->flags & FLAG_IGNORE_LOST_DATA)
//            slice->bytes = clst->currentSlice->bytes;
//    }

    if(!(clst->net_config->flags & FLAG_STREAMING))
        if(clst->currentSliceNo != sliceNo-1) {
            udpc_fatal(1, "Slice no mismatch %lu <-> %lu\n",
                       sliceNo, clst->currentSliceNo);
        }
    slice->bytesKnown = 0;
    slice->sliceNo = sliceNo;

    BZERO(slice->missing_data_blocks);
#ifdef BB_FEATURE_UDPCAST_FEC
    BZERO(slice->fec_stripes);
    BZERO(slice->fec_blocks);
    BZERO(slice->fec_descs);
#endif
    clst->currentSlice = slice;
    clst->currentSliceNo = sliceNo;
    return slice;
}

static struct slice *newSlice(struct clientState *clst, int64_t sliceNo)
{
    struct slice *slice=NULL;
    size_t i;

#if DEBUG
    flprintf("Getting new slice %d\n",
             pc_getConsumerPosition(clst->free_slices_pc));
#endif
    pc_consume(clst->free_slices_pc, 1);
#if DEBUG
    flprintf("Got new slice\n");
#endif
    i = pc_getConsumerPosition(clst->free_slices_pc);
    pc_consumed(clst->free_slices_pc, 1);
    slice = &clst->slices[i];
    assert(slice->state == SLICE_FREE);

    /* wait for free data memory */
    slice->base = pc_getConsumerPosition(clst->fifo->freeMemQueue);
#if DEBUG
    if(pc_consume(clst->fifo->freeMemQueue, 0) <
            clst->net_config->blockSize * MAX_SLICE_SIZE)
        flprintf("Pipeline full\n");
#endif
    pc_consume(clst->fifo->freeMemQueue,
               clst->net_config->blockSize * MAX_SLICE_SIZE);
    initSlice(clst, slice, sliceNo);
    return slice;
}

static void checkSliceComplete(struct clientState *clst, struct slice *slice);

static struct slice *findSlice(struct clientState *clst, int64_t sliceNo);

static void setSliceBytes(struct slice *slice,
                          struct clientState *clst,
                          uint32_t bytes);

static void fakeSliceComplete(struct clientState *clst)
{
    slice_t slice = NULL;
    slice = findSlice(clst, clst->receivedSliceNo+1);
    assert(slice != NULL);
    assert(slice->state != SLICE_DONE);
    if(! slice->bytesKnown )
        setSliceBytes(slice, clst, slice->bytes);
    slice->blocksTransferred = slice->dataBlocksTransferred =
            (slice->bytes + clst->net_config->blockSize - 1) /
            clst->net_config->blockSize;
    checkSliceComplete(clst, slice);
}

static struct slice *findSlice(struct clientState *clst, int64_t sliceNo)
{
    if(! clst->currentSlice) {
        /* Streaming mode? */
        clst->currentSliceNo = sliceNo-1;
        return newSlice(clst, sliceNo);
    }
    if(sliceNo <= clst->currentSliceNo) {
        struct slice *slice = clst->currentSlice;
        int64_t pos = slice - clst->slices;
        assert(slice == NULL || slice->magic == SLICEMAGIC);
        while(slice->sliceNo != sliceNo) {
            if(slice->state == SLICE_FREE)
                return NULL;
            assert(slice->magic == SLICEMAGIC);
            pos--;
            if(pos < 0)
                pos += NR_SLICES;
            slice = &clst->slices[pos];
        }
        return slice;
    }

    if((clst->net_config->flags & FLAG_STREAMING) &&
            sliceNo != clst->currentSliceNo) {
        assert((clst->currentSlice = &clst->slices[0]));
        return initSlice(clst, clst->currentSlice, sliceNo);
    }

    if(sliceNo < clst->receivedSliceNo + NR_SLICES) // && sliceNo > clst->currentSliceNo
    {
        int64_t number = clst->currentSliceNo + 1;
        struct slice * slice = NULL;
        while(number <= sliceNo)
        {
            slice = newSlice(clst, number++);
        }
        return slice;
    }

    while(sliceNo > clst->receivedSliceNo + 2 ||
          sliceNo != clst->currentSliceNo + 1) {
        slice_t slice = findSlice(clst, clst->receivedSliceNo+1);
        if(clst->net_config->flags & FLAG_IGNORE_LOST_DATA)
            fakeSliceComplete(clst);
        else {
            udpc_flprintf("Dropped by server now=%lu last=%lu\n", sliceNo,
                          clst->receivedSliceNo);
            if(slice != NULL)
                printMissedBlockMap(clst, slice);
            exit(1);
        }
    }
    return newSlice(clst, sliceNo);
}

static void setSliceBytes(struct slice *slice,
                          struct clientState *clst,
                          uint32_t bytes) {
    assert(slice->magic == SLICEMAGIC);
    if(slice->bytesKnown) {
        if(slice->bytes != bytes) {
            udpc_fatal(1, "Byte number mismatch %u <-> %u\n",
                       bytes, slice->bytes);
        }
    } else {
        slice->bytesKnown = 1;
        slice->bytes = bytes;
        if(bytes == 0)
            clst->netEndReached=1;
        if(! (clst->net_config->flags & FLAG_STREAMING) ) {
            /* In streaming mode, do not reserve space as soon as first
         * block of slice received, but only when slice complete.
         * For detailed discussion why, see comment in checkSliceComplete
         */
            pc_consumed(clst->fifo->freeMemQueue, bytes);
        }
    }
}

/**
 * Advance pointer of received slices
 */
static void advanceReceivedPointer(struct clientState *clst) {
    int64_t pos = clst->receivedPtr;
    while(1) {
        slice_t slice = &clst->slices[pos];
        if(
        #ifdef BB_FEATURE_UDPCAST_FEC
                slice->state != SLICE_FEC &&
                slice->state != SLICE_FEC_DONE &&
        #endif
                slice->state != SLICE_DONE)
            break;
        pos++;
        clst->receivedSliceNo = slice->sliceNo;
        if(pos >= NR_SLICES)
            pos -= NR_SLICES;
    }
    clst->receivedPtr = pos;
}

/**
 * Cleans up all finished slices. At first, invoked by the net receiver
 * thread. However, once FEC has become active, net receiver will no longer
 * call it, and instead it will be called by the fec thread.
 */
static void cleanupSlices(struct clientState *clst, unsigned int doneState)
{
    while(1) {
        size_t pos = pc_getProducerPosition(clst->free_slices_pc);
        int64_t bytes;
        slice_t slice = &clst->slices[pos];
#if DEBUG
        flprintf("Attempting to clean slice %d %d %d %d at %d\n",
                 slice->sliceNo,
                 slice->state, doneState, clst->use_fec, pos);
#endif
        if(slice->state != doneState)
            break;
        receiverStatsAddBytes(clst->stats, slice->bytes);
        displayReceiverStats(clst->stats, 0);
        bytes = slice->bytes;

        /* signal data received */
        if(bytes == 0) {
            pc_produceEnd(clst->fifo->data);
        } else
            pc_produce(clst->fifo->data, slice->bytes);

        /* free up slice structure */
        clst->slices[pos].state = SLICE_FREE;
#if DEBUG
        flprintf("Giving back slice %d => %d %p\n",
                 clst->slices[pos].sliceNo, pos, &clst->slices[pos]);
#endif
        pc_produce(clst->free_slices_pc, 1);

        /* if at end, exit this thread */
        if(!bytes) {
            clst->endReached = 2;
        }
    }
}

/**
 * Check whether this slice is sufficiently complete
 */
static void checkSliceComplete(struct clientState *clst,
                               struct slice *slice)
{
    uint32_t blocksInSlice;

    assert(slice->magic == SLICEMAGIC);
    if(slice->state != SLICE_RECEIVING)
        /* bad starting state */
        return;

    /* is this slice ready ? */
    assert(clst->net_config->blockSize != 0);
    blocksInSlice = (slice->bytes + clst->net_config->blockSize - 1) /
            clst->net_config->blockSize;
    if(blocksInSlice == slice->blocksTransferred) {
        if(clst->net_config->flags & FLAG_STREAMING) {
        /* If we are in streaming mode, the storage space for the first
         * entire slice is only consumed once it is complete. This
         * is because it is only at comletion time that we know
         * for sure that it can be completed (before, we could
         * have missed the beginning).
         * After having completed one slice, revert back to normal
         * mode where we consumed the free space as soon as the
         * first block is received (not doing this would produce
         * errors if a new slice is started before a previous one
         * is complete, such as during retransmissions)
         */
            pc_consumed(clst->fifo->freeMemQueue, slice->bytes);
            clst->net_config->flags &= ~FLAG_STREAMING;
        }
        if(blocksInSlice == slice->dataBlocksTransferred)
            slice->state = SLICE_DONE;
        else {
#ifdef BB_FEATURE_UDPCAST_FEC
            assert(clst->use_fec == 1);
            slice->state = SLICE_FEC;
#else
            assert(0);
#endif
        }
        advanceReceivedPointer(clst);
#ifdef BB_FEATURE_UDPCAST_FEC
        if(clst->use_fec) {
            uint64_t n = pc_getProducerPosition(clst->fec_data_pc);
            assert(slice->state == SLICE_DONE || slice->state == SLICE_FEC);
            clst->fec_slices[n] = slice;
            pc_produce(clst->fec_data_pc, 1);
        } else
#endif
            cleanupSlices(clst, SLICE_DONE);
    }
}

#ifdef BB_FEATURE_UDPCAST_FEC
static uint32_t getSliceBlocks(struct slice *slice, struct net_config *net_config)
{
    assert(net_config->blockSize != 0);
    return (slice->bytes + net_config->blockSize - 1) / net_config->blockSize;
}

static void fec_decode_one_stripe(struct clientState *clst,
                                  struct slice *slice,
                                  uint32_t stripe,
                                  int64_t bytes,
                                  uint32_t stripes,
                                  uint16_t nr_fec_blocks,
                                  struct fec_desc *fec_descs) {
    struct fifo *fifo = clst->fifo;
    struct net_config *config = clst->net_config;
    unsigned char *map = slice->retransmit.map;

    /*    int nrBlocks = (bytes + data->blockSize - 1) / data->blockSize; */
    uint32_t nrBlocks = getSliceBlocks(slice, config);
    uint64_t leftOver = bytes % config->blockSize;
    uint32_t j;

    // FIXME: change to malloc
    unsigned char *fec_blocks[nr_fec_blocks];
    unsigned int fec_block_nos[nr_fec_blocks];
    unsigned int erased_blocks[nr_fec_blocks];
    unsigned char *data_blocks[128];

    uint32_t erasedIdx = stripe;
    uint32_t i;
    for(i=stripe, j=0; i<nrBlocks; i+= stripes) {
        if(!BIT_ISSET(i, map)) {
#if DEBUG
            flprintf("Repairing block %d with %d@%p\n",
                     i,
                     fec_descs[erasedIdx].fecBlockNo,
                     fec_descs[erasedIdx].adr);
#endif
            fec_descs[erasedIdx].erasedBlockNo=i;
            erased_blocks[j++] = i/stripes;
            erasedIdx += stripes;
        }
    }
    assert(erasedIdx == stripe+nr_fec_blocks*stripes);

    for(i=stripe, j=0; j<nr_fec_blocks; i+=stripes, j++) {
        fec_block_nos[j] = fec_descs[i].fecBlockNo/stripes;
        fec_blocks[j] = fec_descs[i].adr;
    }

    if(leftOver>0) {
        unsigned char *lastBlock = ADR(nrBlocks - 1, config->blockSize);
        memset(lastBlock+leftOver, 0, config->blockSize-leftOver);
    }

    for(i=stripe, j=0; i< nrBlocks; i+=stripes, j++)
        data_blocks[j] = ADR(i, config->blockSize);
    fec_decode(config->blockSize,  data_blocks, j,
               fec_blocks,  fec_block_nos, erased_blocks, nr_fec_blocks);
}


static THREAD_RETURN fecMain(void *args0)
{
    struct clientState *clst = (struct clientState *) args0;
    size_t pos;
    struct fifo *fifo = clst->fifo;
    struct net_config *config = clst->net_config;
    
    assert(fifo->dataBufSize % config->blockSize == 0);
    assert(config->blockSize != 0);

    while(clst->endReached < 2) {
        struct slice *slice;
        pc_consume(clst->fec_data_pc, 1);
        pos = pc_getConsumerPosition(clst->fec_data_pc);
        slice = clst->fec_slices[pos];
        pc_consumed(clst->fec_data_pc, 1);
        if(slice->state != SLICE_FEC &&
                slice->state != SLICE_DONE)
            /* This can happen if a SLICE_DONE was enqueued after a SLICE_FEC:
         * the cleanup after SLICE_FEC also cleaned away the SLICE_DONE (in
         * main queue), and thus we will find it as SLICE_FREE in the
         * fec queue. Or worse receiving, or whatever if it made full
         * circle ... */
            continue;
        if(slice->state == SLICE_FEC) {
            uint32_t stripes = slice->fec_stripes;
            struct fec_desc *fec_descs = slice->fec_descs;
            uint32_t stripe;

            /* Record the addresses of FEC blocks */
            for(stripe=0; stripe<stripes; stripe++) {
                assert(config->blockSize != 0);
                fec_decode_one_stripe(clst, slice,
                                      stripe,
                                      slice->bytes,
                                      slice->fec_stripes,
                                      slice->fec_blocks[stripe],
                                      fec_descs);
            }

            slice->state = SLICE_FEC_DONE;
            for(stripe=0; stripe<stripes; stripe++) {
                uint32_t i;
                assert(slice->missing_data_blocks[stripe] >=
                       slice->fec_blocks[stripe]);
                for(i=0; i<slice->fec_blocks[stripe]; i++) {
                    freeBlockSpace(clst,fec_descs[stripe+i*stripes].adr);
                    fec_descs[stripe+i*stripes].adr=0;
                }
            }
        }  else if(slice->state == SLICE_DONE) {
            slice->state = SLICE_FEC_DONE;
        }
        assert(slice->state == SLICE_FEC_DONE);
        cleanupSlices(clst, SLICE_FEC_DONE);
    }
    return 0;
}

#ifdef BB_FEATURE_UDPCAST_FEC
static void initClstForFec(struct clientState *clst)
{
    clst->use_fec = 1;
    pthread_create(&clst->fec_thread, NULL, fecMain, clst);
}
#endif

static void initSliceForFec(struct clientState *clst, struct slice *slice)
{
    uint32_t i, j;
    uint32_t blocksInSlice;

    assert(slice->magic == SLICEMAGIC);

#ifdef BB_FEATURE_UDPCAST_FEC
    /* make this client ready for fec */
    if(!clst->use_fec)
        initClstForFec(clst);
#endif

    /* is this slice ready ? */
    assert(clst->net_config->blockSize != 0);
    blocksInSlice = (slice->bytes + clst->net_config->blockSize - 1) / clst->net_config->blockSize;

    for(i=0; i<slice->fec_stripes; i++) {
        slice->missing_data_blocks[i]=0;
        slice->fec_blocks[i]=0;
    }

    for(i=0; i< (blocksInSlice+7)/8 ; i++) {
        if(slice->retransmit.map[i] != 0xff) {
            uint32_t max = i*8+8;
            if(max > blocksInSlice)
                max = blocksInSlice;
            for(j=i*8; j < max; j++)
                if(!BIT_ISSET(j, slice->retransmit.map))
                    slice->missing_data_blocks[j % slice->fec_stripes]++;
        }
    }
}



static int processFecBlock(struct clientState *clst,
                           uint16_t stripes,
                           int64_t sliceNo,
                           uint16_t blockNo,
                           uint32_t bytes)
{
    struct slice *slice = findSlice(clst, sliceNo);
    unsigned char *shouldAddress, *isAddress;
    uint32_t stripe;
    struct fec_desc *desc;
    uint32_t adr;

#if DEBUG
    flprintf("Handling FEC packet %d %d %d %d\n",
             stripes, sliceNo, blockNo, bytes);
#endif
    assert(slice == NULL || slice->magic == SLICEMAGIC);
    if(slice == NULL ||
            slice->state == SLICE_FREE ||
            slice->state == SLICE_DONE ||
            slice->state == SLICE_FEC) {
        /* an old slice. Ignore */
        return 0;
    }

    shouldAddress = clst->nextBlock;
    isAddress = clst->data_hdr.msg_iov[1].iov_base;

    setSliceBytes(slice, clst, bytes);

    if(slice->fec_stripes == 0) {
        slice->fec_stripes = stripes;
        initSliceForFec(clst, slice);
    } else if(slice->fec_stripes != stripes) {
        udpc_flprintf("Interleave mismatch %d <-> %d",
                      slice->fec_stripes, stripes);
        return 0;
    }
    stripe = blockNo % slice->fec_stripes;
    if(slice->missing_data_blocks[stripe] <=
            slice->fec_blocks[stripe]) {
        /* not useful */
        /* FIXME: we should forget block here */

        checkSliceComplete(clst, slice);
        advanceReceivedPointer(clst);
#ifdef BB_FEATURE_UDPCAST_FEC
        if(!clst->use_fec)
            cleanupSlices(clst, SLICE_DONE);
#endif
        return 0;
    }

    adr = slice->fec_blocks[stripe]*stripes+stripe;

    {
        uint32_t i;
        /* check for duplicates, in case of retransmission... */
        for(i=stripe; i<adr; i+= stripes) {
            desc = &slice->fec_descs[i];
            if(desc->fecBlockNo == blockNo) {
                udpc_flprintf("**** duplicate block...\n");
                return 0;
            }
        }
    }

    if(shouldAddress != isAddress)
        /* copy message to the correct place */
        memcpy(shouldAddress, isAddress,  clst->net_config->blockSize);

    desc = &slice->fec_descs[adr];
    desc->adr = shouldAddress;
    desc->fecBlockNo = blockNo;
    slice->fec_blocks[stripe]++;
    slice->blocksTransferred++;
    setNextBlock(clst);
    slice->freePos = MAX_SLICE_SIZE;
    checkSliceComplete(clst, slice);
    advanceReceivedPointer(clst);
    return 0;
}
#endif

static int processDataBlock(struct clientState *clst,
                            int64_t sliceNo,
                            uint16_t blockNo,
                            uint32_t bytes)
{
    struct fifo *fifo = clst->fifo;
    struct slice *slice = findSlice(clst, sliceNo);
    unsigned char *shouldAddress, *isAddress;

    assert(slice == NULL || slice->magic == SLICEMAGIC);

    if(slice == NULL ||
            slice->state == SLICE_FREE ||
            slice->state == SLICE_DONE
        #ifdef BB_FEATURE_UDPCAST_FEC
            ||
            slice->state == SLICE_FEC ||
            slice->state == SLICE_FEC_DONE
        #endif
            ) {
        /* an old slice. Ignore */
        return 0;
    }

    if(sliceNo >= clst->receivedSliceNo + NR_SLICES) //clst->currentSliceNo+2)
        udpc_fatal(1, "We have been dropped by sender\n");

    if(BIT_ISSET(blockNo, slice->retransmit.map)) {
        /* we already have this packet, ignore */
#if 0
        flprintf("Packet %d:%d not for us\n", sliceNo, blockNo);
#endif
        return 0;
    }

    if(slice->base % clst->net_config->blockSize) {
        udpc_fatal(1, "Bad base %lu, not multiple of block size %d\n",
                   slice->base, clst->net_config->blockSize);
    }

    shouldAddress = ADR(blockNo, clst->net_config->blockSize);
    isAddress = clst->data_hdr.msg_iov[1].iov_base;
//    if(shouldAddress != isAddress)
    {
        /* copy message to the correct place */
        memcpy(shouldAddress, isAddress,  clst->net_config->blockSize);
    }

    if(clst->client_config->sender_is_newgen && bytes != 0)
        setSliceBytes(slice, clst, bytes);
    if(clst->client_config->sender_is_newgen && bytes == 0)
        clst->netEndReached = 0;

    SET_BIT(blockNo, slice->retransmit.map);
#ifdef BB_FEATURE_UDPCAST_FEC
    if(slice->fec_stripes) {
        uint32_t stripe = blockNo % slice->fec_stripes;
        slice->missing_data_blocks[stripe]--;
        assert(slice->missing_data_blocks[stripe] >= 0);
        if(slice->missing_data_blocks[stripe] <
                slice->fec_blocks[stripe]) {
            uint32_t blockIdx;
            /* FIXME: FEC block should be enqueued in local queue here...*/
            slice->fec_blocks[stripe]--;
            blockIdx = stripe+slice->fec_blocks[stripe]*slice->fec_stripes;
            assert(slice->fec_descs[blockIdx].adr != 0);
            clst->localBlockAddresses[clst->localPos++] =
                    slice->fec_descs[blockIdx].adr;
            slice->fec_descs[blockIdx].adr=0;
            slice->blocksTransferred--;
        }
    }
#endif
    slice->dataBlocksTransferred++;
    slice->blocksTransferred++;
    while(slice->freePos < MAX_SLICE_SIZE &&
          BIT_ISSET(slice->freePos, slice->retransmit.map))
        slice->freePos++;
    checkSliceComplete(clst, slice);
    return 0;
}

static int processReqAck(struct clientState *clst,
                         int64_t sliceNo,
                         uint32_t bytes,
                         uint32_t rxmit)
{   
    struct slice *slice = findSlice(clst, sliceNo);
    uint32_t blocksInSlice;
    char *readySet = (char *) clst->data_hdr.msg_iov[1].iov_base;

#if DEBUG
    flprintf("Received REQACK (sn=%d, rxmit=%d sz=%d) %d\n",
             sliceNo, rxmit,  bytes, (slice - &clst->slices[0]));
#endif

    assert(slice == NULL || slice->magic == SLICEMAGIC);

    {
        struct timeval tv;
        gettimeofday(&tv, 0);
        /* usleep(1); DEBUG: FIXME */
    }
    if(BIT_ISSET(clst->client_config->clientNumber, readySet)) {
        /* not for us */
#if DEBUG
        flprintf("Not for us\n");
#endif
        return 0;
    }

    if(slice == NULL) {
        /* an old slice => send ok */
#if DEBUG
        flprintf("old slice => sending ok\n");
#endif
        return (int) sendOk(clst->client_config, sliceNo);
    }

    setSliceBytes(slice, clst, bytes);
    assert(clst->net_config->blockSize != 0);
    blocksInSlice = (slice->bytes + clst->net_config->blockSize - 1) /
            clst->net_config->blockSize;
    if (blocksInSlice == slice->blocksTransferred) {
        /* send ok */
        sendOk(clst->client_config, slice->sliceNo);
    } else {
#if DEBUG
        flprintf("Ask for retransmission (%d/%d %d)\n",
                 slice->blocksTransferred, blocksInSlice, bytes);
#endif
        sendRetransmit(clst, slice, rxmit);
    }
#if DEBUG
    flprintf("Received reqack %d %d\n", slice->sliceNo, bytes);
#endif
    checkSliceComplete(clst, slice); /* needed for the final 0 sized slice */
    advanceReceivedPointer(clst);
#ifdef BB_FEATURE_UDPCAST_FEC
    if(!clst->use_fec)
        cleanupSlices(clst, SLICE_DONE);
#endif
    return 0;
}

/**
 * Close all sockets except the named file descriptor and the slot 0 socket
 * The 0 should not be closed, because we use that for sending
 */
static void closeAllExcept(struct clientState *clst, int fd) {
    int i;
    int *socks = clst->client_config->socks;
    
    if(clst->selectedFd >= 0)
        return;

    restoreConsole(&clst->client_config->console, 0);
    clst->selectedFd = fd;
    for(i=1; i<NR_CLIENT_SOCKS; i++)
        if(socks[i] != -1 && socks[i] != fd)
            closeSock(socks, NR_CLIENT_SOCKS, i);
}

/* Receives a message from network, and dispatches it
 * Only called in network reception thread
 */
static int dispatchMessage(struct clientState *clst)
{
    ssize_t ret;
    struct sockaddr_in lserver;
//    struct fifo *fifo = clst->fifo;
    int fd = -1;
    struct client_config *client_config = clst->client_config;

    /* set up message header */
//    if (clst->currentSlice != NULL &&
//            clst->currentSlice->freePos < MAX_SLICE_SIZE) {
//        struct slice *slice = clst->currentSlice;
//        assert(slice->magic == SLICEMAGIC);
//        clst->data_iov[1].iov_base =
//                ADR(slice->freePos, clst->net_config->blockSize);
//    } else {
//        clst->data_iov[1].iov_base = clst->nextBlock;
//    }

    clst->data_iov[1].iov_len = clst->net_config->blockSize;
    clst->data_hdr.msg_iovlen = 2;

    clst->data_hdr.msg_name = &lserver;
    clst->data_hdr.msg_namelen = sizeof(struct sockaddr_in);

    while(clst->endReached || clst->netEndReached) {
        int oldEndReached = clst->endReached;
        int nr_desc;
        struct timeval tv;
        fd_set read_set;

        int maxFd = prepareForSelect(client_config->socks,
                                     NR_CLIENT_SOCKS, &read_set);

        tv.tv_sec = clst->net_config->exitWait / 1000;
        tv.tv_usec = (clst->net_config->exitWait % 1000)*1000;
        nr_desc = select(maxFd,  &read_set, 0, 0,  &tv);
        if(nr_desc < 0) {
            flprintf("Select error: %s\n", strerror(errno));
            break;
        }
        fd = getSelectedSock(client_config->socks, NR_CLIENT_SOCKS, &read_set);
        if(fd >= 0)
            break;

        /* Timeout expired */
        if(oldEndReached >= 2) {
            clst->endReached = 3;
            return 0;
        }
    }

    if(fd < 0)
        fd = clst->selectedFd;

    if(fd < 0) {
        struct timeval tv, *tvp;
        fd_set read_set;
        int keyPressed = 0;
        int maxFd = prepareForSelect(client_config->socks,
                                     NR_CLIENT_SOCKS, &read_set);
        if(client_config->console && !clst->promptPrinted)
#ifdef __MINGW32__
            fprintf(stderr, "Press return to start receiving data!\n");
#else /* __MINGW32__ */
            fprintf(stderr, "Press any key to start receiving data!\n");
#endif /* __MINGW32__ */
        clst->promptPrinted=1;

        if(clst->net_config->startTimeout == 0) {
            tvp=NULL;
        } else {
            tv.tv_sec = clst->net_config->startTimeout;
            tv.tv_usec = 0;
            tvp = &tv;
        }

        ret = selectWithConsole(client_config->console, maxFd+1, &read_set,
                                tvp, &keyPressed);
        if(ret < 0) {
            perror("Select");
            return 0;
        }
        if(ret == 0) {
            clst->endReached=3;
            clst->netEndReached=3;
            pc_produceEnd(clst->fifo->data);
            return 1;
        }
        if(keyPressed) {
            /* Close our console and flush pending keystroke.
       * a restore console will be done later in closeAllExcept, but this
       * one is necessary to flush out buffered character */
            restoreConsole(&client_config->console, 1);

            /* ... and send go signal */
            udpc_flprintf("Sending go signal\n");
            if(sendGo(client_config) < 0)
                perror("Send go");
            return 0; /* Trigger next loop */
        }
        fd = getSelectedSock(clst->client_config->socks,
                             NR_CLIENT_SOCKS, &read_set);
    }

#ifdef LOSSTEST
    loseRecvPacket(fd);
    ret=RecvMsg(fd, &clst->data_hdr, 0);
#else
    ret=recvmsg(fd, &clst->data_hdr,
#ifdef MSG_DONTWAIT
                clst->net_config->receiveTimeout ? MSG_DONTWAIT :
#endif
                0);
#ifdef MSG_DONTWAIT
    if(ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        struct timeval tv;
        fd_set read_set;
        int nr_desc;

        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        tv.tv_sec = clst->net_config->receiveTimeout;
        tv.tv_usec = 0;
        nr_desc = select(fd+1,  &read_set, 0, 0,  &tv);
        if(nr_desc == 0) {
            flprintf("Receiver timeout\n");
            exit(1);
        }
        ret = recvmsg(fd, &clst->data_hdr, MSG_DONTWAIT);
    }
#endif

#endif
    if (ret < 0) {
#if DEBUG
        flprintf("data recvfrom %d: %s\n", fd, strerror(errno));
#endif
        return -1;
    }


#if 0
    flprintf("received packet for slice %d, block %d\n",
             be32toh(Msg.sliceNo), be16toh(db.blockNo));
#endif

    if(!udpc_isAddressEqual(&lserver,
                            &clst->client_config->serverAddr)) {
        char buffer1[16], buffer2[16];
        udpc_flprintf("Rogue packet received %s:%d, expecting %s:%d\n",
                      udpc_getIpString(&lserver, buffer1), getPort(&lserver),
                      udpc_getIpString(&clst->client_config->serverAddr, buffer2),
                      udpc_getPort(&clst->client_config->serverAddr));
        return -1;
    }

    switch(be16toh(clst->Msg.opCode)) {
    case CMD_DATA:
        closeAllExcept(clst, fd);
        udpc_receiverStatsStartTimer(clst->stats);
        clst->client_config->isStarted = 1;
        return processDataBlock(clst,
                                be64toh(clst->Msg.dataBlock.sliceNo),
                                be16toh(clst->Msg.dataBlock.blockNo),
                                be32toh(clst->Msg.dataBlock.bytes));
#ifdef BB_FEATURE_UDPCAST_FEC
    case CMD_FEC:
        closeAllExcept(clst, fd);
        receiverStatsStartTimer(clst->stats);
        clst->client_config->isStarted = 1;
        return processFecBlock(clst,
                               be16toh(clst->Msg.fecBlock.stripes),
                               be64toh(clst->Msg.fecBlock.sliceNo),
                               be16toh(clst->Msg.fecBlock.blockNo),
                               be32toh(clst->Msg.fecBlock.bytes));
#endif
    case CMD_REQACK:
        closeAllExcept(clst, fd);
        receiverStatsStartTimer(clst->stats);
        clst->client_config->isStarted = 1;
        return processReqAck(clst,
                             be64toh(clst->Msg.reqack.sliceNo),
                             be32toh(clst->Msg.reqack.bytes),
                             be32toh(clst->Msg.reqack.rxmit));
    case CMD_HELLO_STREAMING:
    case CMD_HELLO_NEW:
    case CMD_HELLO:
        /* retransmission of hello to find other participants ==> ignore */
        return 0;
    default:
        break;
    }

    udpc_flprintf("Unexpected opcode %04x\n",
                  (unsigned short) clst->Msg.opCode);
    return -1;
}

static int setupMessages(struct clientState *clst) {
    /* the messages received from the server */
    clst->data_iov[0].iov_base = (void *)&clst->Msg;
    clst->data_iov[0].iov_len = sizeof(clst->Msg);

    clst->data_iov[1].iov_base = clst->tempBlock;
    
    /* namelen set just before reception */
    clst->data_hdr.msg_iov = clst->data_iov;
    /* iovlen set just before reception */

    initMsgHdr(&clst->data_hdr);
    return 0;
}

static int setMaximumPriorityThread()
{
    pthread_t this_thread = pthread_self();
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    return pthread_setschedparam(this_thread, SCHED_FIFO, &params);
}

static THREAD_RETURN netReceiverMain(void *args0)
{
    struct clientState *clst = (struct clientState *) args0;

    setMaximumPriorityThread();

    setupMessages(clst);
    
    clst->currentSliceNo = -1l;
    clst->currentSlice = NULL;
    clst->promptPrinted = 0;
    if(! (clst->net_config->flags & FLAG_STREAMING))
        newSlice(clst, 0);
    else {
        clst->currentSliceNo = 0;
    }

    while(clst->endReached < 3) {
        dispatchMessage(clst);
    }
#ifdef BB_FEATURE_UDPCAST_FEC
    if(clst->use_fec)
        pthread_join(clst->fec_thread, NULL);
#endif
    return 0;
}

int spawnNetReceiver(struct fifo *fifo,
                     struct client_config *client_config,
                     struct net_config *net_config,
                     receiver_stats_t stats)
{
    uint32_t i;
    struct clientState *clst = MALLOC(struct clientState);
    clst->fifo = fifo;
    clst->client_config = client_config;
    clst->net_config = net_config;
    clst->stats = stats;
    clst->endReached = 0;
    clst->netEndReached = 0;
    clst->selectedFd = -1;
    clst->localPos = 0;
    clst->receivedPtr = 0;

    clst->free_slices_pc = pc_makeProduconsum(NR_SLICES, "free slices");
    pc_produce(clst->free_slices_pc, NR_SLICES);
    for(i = 0; i <NR_SLICES; i++) {
        clst->slices[i].state = SLICE_FREE;
        clst->slices[i].sliceNo = -1l;
    }
    clst->receivedPtr = 0;
    clst->receivedSliceNo = -1l;
    clst->receivingSliceWindowSize = 0;

#ifdef BB_FEATURE_UDPCAST_FEC
    fec_init(); /* fec new involves memory
         * allocation. Better do it here */
    clst->use_fec = 0;
    clst->fec_data_pc = pc_makeProduconsum(NR_SLICES, "fec data");
#endif

    clst->freeBlocks_pc = pc_makeProduconsum(NR_BLOCKS, "free blocks");
    pc_produce(clst->freeBlocks_pc, NR_BLOCKS);
    clst->blockAddresses = calloc(NR_BLOCKS, sizeof(char *));
    clst->localBlockAddresses = calloc(NR_BLOCKS, sizeof(char *));
    clst->blockData = xmalloc(NR_BLOCKS * net_config->blockSize);
    for(i = 0; i < NR_BLOCKS; i++)
        clst->blockAddresses[i] = clst->blockData + i * net_config->blockSize;
    clst->localPos=0;

    setNextBlock(clst);

    setMaximumPriorityThread();

    return pthread_create(&client_config->thread, NULL, netReceiverMain, clst);
}
