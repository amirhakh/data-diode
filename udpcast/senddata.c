#include <sys/types.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include "threads.h"
#include "fec.h"
#include "log.h"
#include "util.h"
#include "socklib.h"
#include "fifo.h"
#include "produconsum.h"
#include "udpcast.h"
#include "udp-sender.h"
#include "udpc-protoc.h"
#include "statistics.h"

#ifdef USE_SYSLOG
#include <syslog.h>
#endif

#define DEBUG 0

typedef struct slice {
    uint64_t base; /* base address of slice in buffer */
    int64_t sliceNo;
    uint32_t bytes; /* bytes in slice */
    uint32_t nextBlock; /* index of next buffer to be transmitted */
    volatile enum {
        SLICE_FREE, /* free slice, and in the queue of free slices */
        SLICE_NEW, /* newly allocated. FEC calculation and first
            * transmission */
        SLICE_XMITTED, /* transmitted */
        SLICE_ACKED, /* acknowledged (if applicable) */
        SLICE_PRE_FREE /* no longer used, but not returned to queue */
    } state;
    char rxmitMap[MAX_SLICE_SIZE / BITS_PER_CHAR];
    /* blocks to be retransmitted */

    char isXmittedMap[MAX_SLICE_SIZE / BITS_PER_CHAR];
    /* blocks which have already been retransmitted during this round*/

    uint32_t rxmitId; /* used to distinguish among several retransmission
          * requests, so that we can easily discard answers to "old"
          * requests */

    /* This structure is used to keep track of clients who answered, and
     * to make the reqack message
     */
    struct reqackBm {
        struct reqack ra;
        char readySet[MAX_CLIENTS / BITS_PER_CHAR]; /* who is already ok? */
    } sl_reqack;

    char answeredSet[MAX_CLIENTS / BITS_PER_CHAR]; /* who answered at all? */

    int32_t nrReady; /* number of participants who are ready */
    int32_t nrAnswered; /* number of participants who answered; */
    int32_t needRxmit; /* does this need retransmission? */
    uint32_t lastGoodBlock; /* last good block of slice (i.e. last block having not
            * needed retransmission */

#ifdef BB_FEATURE_UDPCAST_FEC
    unsigned char *fec_data;
#endif
} *slice_t;

#define QUEUE_SIZE 1024

struct returnChannel {
    pthread_t thread; /* message receiving thread */
    int rcvSock; /* socket on which we receive the messages */
    produconsum_t incoming; /* where to enqueue incoming messages */
    produconsum_t freeSpace; /* free space */
    struct {
        int16_t clNo; /* client number */
        union clientMsg msg; /* its message */
    } q[QUEUE_SIZE];
    struct net_config *config;
    participantsDb_t participantsDb;
};

#define NR_SLICES 2

typedef struct senderState {
    struct returnChannel rc;
    struct fifo *fifo;

    struct net_config *config;
    sender_stats_t stats;
    int socket;
    
    struct slice slices[NR_SLICES];

    produconsum_t free_slices_pc;

    unsigned char *fec_data;
    pthread_t fec_thread;
    produconsum_t fec_data_pc;
} *sender_state_t;


static uint32_t getSliceBlocks(struct slice *slice, struct net_config *net_config)
{
    return (slice->bytes + net_config->blockSize - 1) / net_config->blockSize;
}

static int isSliceAcked(struct slice *slice)
{
#if DEBUG
    flprintf("Is slice %d acked?  %d\n", slice->sliceNo,
             slice->state);
#endif
    if(slice->state == SLICE_ACKED) {
        return 1;
    } else {
        return 0;
    }
}

static int isSliceXmitted(struct slice *slice) {
    if(slice->state == SLICE_XMITTED) {
        return 1;
    } else {
        return 0;
    }
}


static int freeSlice(sender_state_t sendst, struct slice *slice) {
#if DEBUG
    int i;
    i = slice - sendst->slices;
    flprintf("Freeing slice %p %d %d\n", slice, slice->sliceNo, i);
#endif
    slice->state = SLICE_PRE_FREE;
    while(1) {
        size_t pos = pc_getProducerPosition(sendst->free_slices_pc);
        if(sendst->slices[pos].state == SLICE_PRE_FREE)
            sendst->slices[pos].state = SLICE_FREE;
        else
            break;
        pc_produce(sendst->free_slices_pc, 1);
    }
    return 0;
}

static struct slice *makeSlice(sender_state_t sendst, int64_t sliceNo) {
    struct net_config *config = sendst->config;
    struct fifo *fifo = sendst->fifo;
    size_t i;
    struct slice *slice=NULL;

    pc_consume(sendst->free_slices_pc, 1);
    i = pc_getConsumerPosition(sendst->free_slices_pc);
    slice = &sendst->slices[i];
    assert(slice->state == SLICE_FREE);
    BZERO(*slice);
    pc_consumed(sendst->free_slices_pc, 1);

    slice->base = pc_getConsumerPosition(sendst->fifo->data);
    slice->sliceNo = sliceNo;
    slice->bytes = (uint32_t) pc_consume(fifo->data, 10*config->blockSize);
    slice->rxmitId = 0;

    /* fixme: use current slice size here */
    if(slice->bytes > config->blockSize * config->sliceSize)
        slice->bytes = config->blockSize * config->sliceSize;

    if(slice->bytes > config->blockSize)
        slice->bytes -= slice->bytes % config->blockSize;

    pc_consumed(fifo->data, slice->bytes);
    slice->nextBlock = 0;
    slice->state = SLICE_NEW;
#if 0
    flprintf("Made slice %p %d\n", slice, slice->sliceNo);
#endif
    BZERO(slice->sl_reqack.readySet);
    slice->nrReady = 0;
#ifdef BB_FEATURE_UDPCAST_FEC
    slice->fec_data = sendst->fec_data + (i * config->fec_stripes *
                                          config->fec_redundancy *
                                          config->blockSize);
#endif
    return slice;
}


static int sendRawData(int sock,
                       struct net_config *config,
                       char *header,
                       size_t headerSize,
                       unsigned char *data,
                       size_t dataSize)
{
    struct iovec iov[2];
    struct msghdr hdr;
    size_t packetSize;
    ssize_t ret;
    
    iov[0].iov_base = header;
    iov[0].iov_len = headerSize;

    iov[1].iov_base = data;
    iov[1].iov_len = dataSize;

    hdr.msg_name = &config->dataMcastAddr;
    hdr.msg_namelen = sizeof(struct sockaddr_in);
    hdr.msg_iov = iov;
    hdr.msg_iovlen  = 2;
    initMsgHdr(&hdr);

    packetSize = dataSize + headerSize;
    rgWaitAll(config, sock, config->dataMcastAddr.sin_addr.s_addr, packetSize);
    ret = sendmsg(sock, &hdr, 0);
    if (ret < 0) {
        char ipBuffer[16];
        udpc_fatal(1, "Could not broadcast data packet to %s:%d (%s)\n",
                   getIpString(&config->dataMcastAddr, ipBuffer),
                   getPort(&config->dataMcastAddr),
                   strerror(errno));
    }

    return 0;
}


static int transmitDataBlock(sender_state_t sendst, struct slice *slice, uint16_t i)
{
    struct fifo *fifo = sendst->fifo;
    struct net_config *config = sendst->config;
    struct dataBlock msg;
    int64_t size;

    assert(i < MAX_SLICE_SIZE);
    
    msg.opCode  = htobe16(CMD_DATA);
    msg.sliceNo = htobe64(slice->sliceNo);
    msg.blockNo = htobe16(i);

    msg.reserved = 0;
//    msg.reserved2 = 0;
    msg.bytes = htobe32(slice->bytes);

    size = slice->bytes - i * config->blockSize;
    if(size < 0)
        size = 0;
    if(size > config->blockSize)
        size = config->blockSize;
    
    sendRawData(sendst->socket, config,
                (char *) &msg, sizeof(msg),
                fifo->dataBuffer +
                (slice->base + i * config->blockSize) % fifo->dataBufSize,
                (size_t) size);
    return 0;
}

#ifdef BB_FEATURE_UDPCAST_FEC
static int transmitFecBlock(sender_state_t sendst, struct slice *slice, uint16_t i)
{
    struct net_config *config = sendst->config;
    struct fecBlock msg;

    /* Do not transmit zero byte FEC blocks if we are not in async mode */
    if(slice->bytes == 0 && !(config->flags & FLAG_ASYNC))
        return 0;

    assert(i < config->fec_redundancy * config->fec_stripes);
    
    msg.opCode  = htobe16(CMD_FEC);
    msg.stripes = htobe16(config->fec_stripes);
    msg.sliceNo = htobe64(slice->sliceNo);
    msg.blockNo = htobe16(i);
    msg.reserved2 = 0;
    msg.bytes = htobe32(slice->bytes);
    sendRawData(sendst->socket, sendst->config,
                (char *) &msg, sizeof(msg),
                (slice->fec_data + i * config->blockSize), config->blockSize);
    return 0;
}
#endif

static int sendSlice(sender_state_t sendst,
                     struct slice *slice,
                     int retransmitting)
{    
    struct net_config *config = sendst->config;

    uint32_t nrBlocks, i;
    int64_t rehello;
#ifdef BB_FEATURE_UDPCAST_FEC
    uint32_t fecBlocks;
#endif
    uint32_t retransmissions=0;

    if(retransmitting) {
        slice->nextBlock = 0;
        if(slice->state != SLICE_XMITTED)
            return 0;
    } else {
        if(slice->state != SLICE_NEW)
            return 0;
    }

    nrBlocks = getSliceBlocks(slice, config);
#ifdef BB_FEATURE_UDPCAST_FEC
    if((config->flags & FLAG_FEC) && !retransmitting) {
        fecBlocks = config->fec_redundancy * config->fec_stripes;
    } else {
        fecBlocks = 0;
    }
#endif

#if DEBUG
    if(retransmitting) {
        flprintf("%s slice %d from %d to %d (%d bytes) %d\n",
                 retransmitting ? "Retransmitting" : "Sending",
                 slice->sliceNo, slice->nextBlock, nrBlocks, slice->bytes,
                 config->blockSize);
    }
#endif

    if((sendst->config->flags & FLAG_STREAMING)) {
        rehello = nrBlocks - sendst->config->rehelloOffset;
        if(rehello < 0)
            rehello = 0;
    } else {
        rehello = -1;
    }

    /* transmit the data */
    for(i = slice->nextBlock; i < nrBlocks
    #ifdef BB_FEATURE_UDPCAST_FEC
        + fecBlocks
    #endif
        ; i++) {
        if(retransmitting) {
            if(!BIT_ISSET(i, slice->rxmitMap) ||
                    BIT_ISSET(i, slice->isXmittedMap)) {
                /* if slice is not in retransmit list, or has _already_
         * been retransmitted, skip it */
                if(i > slice->lastGoodBlock)
                    slice->lastGoodBlock = i;
                continue;
            }
            SET_BIT(i, slice->isXmittedMap);
            retransmissions++;
#if DEBUG
            flprintf("Retransmitting %d.%d\n", slice->sliceNo, i);
#endif
        }

        if(i == rehello) {
            sendHello(sendst->config, sendst->socket, 1);
        }

        if(i < nrBlocks)
            transmitDataBlock(sendst, slice, (uint16_t) i);
#ifdef BB_FEATURE_UDPCAST_FEC
        else
            transmitFecBlock(sendst, slice, (uint16_t) (i - nrBlocks));
#endif
        if(!retransmitting && pc_getWaiting(sendst->rc.incoming)) {
            i++;
            break;
        }
    }

    if(retransmissions)
        senderStatsAddRetransmissions(sendst->stats, retransmissions);
    slice->nextBlock = i;
    if(i == nrBlocks
        #ifdef BB_FEATURE_UDPCAST_FEC
            + fecBlocks
        #endif
            ) {
        slice->needRxmit = 0;
        if(!retransmitting)
            slice->state = SLICE_XMITTED;
#if DEBUG
        flprintf("Done: at block %d %d %d\n", i, retransmitting,
                 slice->state);
#endif
        return 2;
    }
#if DEBUG
    flprintf("Done: at block %d %d %d\n", i, retransmitting,
             slice->state);
#endif
    return 1;
}

static int ackSlice(struct slice *slice, struct net_config *net_config,
                    struct fifo *fifo, sender_stats_t stats)
{
    if(slice->state == SLICE_ACKED)
        /* already acked */
        return 0;
    if(!(net_config->flags & FLAG_SN)) {
        if(net_config->discovery == DSC_DOUBLING) {
            net_config->sliceSize += net_config->sliceSize / 4;
            if(net_config->sliceSize >= net_config->max_slice_size) {
                net_config->sliceSize = net_config->max_slice_size;
                net_config->discovery = DSC_REDUCING;
            }
            udpc_logprintf(udpc_log, "Doubling slice size to %d\n",
                           net_config->sliceSize);
        }
    }
    slice->state = SLICE_ACKED;
    pc_produce(fifo->freeMemQueue, slice->bytes);

    /* Statistics */
    senderStatsAddBytes(stats, slice->bytes);

    if(slice->bytes) {
        displaySenderStats(stats,
                           net_config->blockSize, net_config->sliceSize, 0);
    }
    /* End Statistics */

    return 0;
}


static int sendReqack(struct slice *slice, struct net_config *net_config,
                      struct fifo *fifo, sender_stats_t stats,
                      int sock)
{
    /* in async mode, just confirm slice... */
    if((net_config->flags & FLAG_ASYNC) && slice->bytes != 0) {
        ackSlice(slice, net_config, fifo, stats);
        return 0;
    }

    if((net_config->flags & FLAG_ASYNC)
        #ifdef BB_FEATURE_UDPCAST_FEC
            &&
            (net_config->flags & FLAG_FEC)
        #endif
            ) {
        return 0;
    }

    if(!(net_config->flags & FLAG_SN) && slice->rxmitId != 0) {
        uint32_t nrBlocks;
        nrBlocks = getSliceBlocks(slice, net_config);
#if DEBUG
        flprintf("nrBlocks=%d lastGoodBlock=%d\n",
                 nrBlocks, slice->lastGoodBlock);
#endif
        if(slice->lastGoodBlock != 0 && slice->lastGoodBlock < nrBlocks) {
            net_config->discovery = DSC_REDUCING;
            if (slice->lastGoodBlock < net_config->sliceSize / 2) {
                net_config->sliceSize = net_config->sliceSize / 2;
            } else {
                net_config->sliceSize = slice->lastGoodBlock;
            }
            if(net_config->sliceSize < 32) {
                /* a minimum of 32 */
                net_config->sliceSize = 32;
            }
            udpc_logprintf(udpc_log, "Slice size=%d\n", net_config->sliceSize);
        }
    }

    slice->lastGoodBlock = 0;
#if DEBUG
    flprintf("Send reqack %d.%d\n", slice->sliceNo, slice->rxmitId);
#endif
    slice->sl_reqack.ra.opCode = htobe16(CMD_REQACK);
    slice->sl_reqack.ra.sliceNo = htobe64(slice->sliceNo);
    slice->sl_reqack.ra.bytes = htobe32(slice->bytes);

    slice->sl_reqack.ra.reserved = 0;
    memcpy((void*)&slice->answeredSet,(void*)&slice->sl_reqack.readySet,
           sizeof(slice->answeredSet));
    slice->nrAnswered = slice->nrReady;

    /* not everybody is ready yet */
    slice->needRxmit = 0;
    memset(slice->rxmitMap, 0, sizeof(slice->rxmitMap));
    memset(slice->isXmittedMap, 0, sizeof(slice->isXmittedMap));
    slice->sl_reqack.ra.rxmit = htobe32(slice->rxmitId);
    
    rgWaitAll(net_config, sock,
              net_config->dataMcastAddr.sin_addr.s_addr,
              sizeof(slice->sl_reqack));
#if DEBUG
    flprintf("sending reqack for slice %d\n", slice->sliceNo);
#endif
    BCAST_DATA(sock, slice->sl_reqack);
    return 0;
}

/**
 * mark slice as acknowledged, and work on slice size
 */

static int doRetransmissions(sender_state_t sendst,
                             struct slice *slice)
{
    if(slice->state == SLICE_ACKED)
        return 0; /* nothing to do */

#if DEBUG
    flprintf("Do retransmissions\n");
#endif
    /* FIXME: reduce slice size if needed */
    if(slice->needRxmit) {
        /* do some retransmissions */
        sendSlice(sendst, slice, 1);
    }
    return 0;
}


static void markParticipantAnswered(slice_t slice, int32_t clNo)
{
    if(BIT_ISSET(clNo, slice->answeredSet))
        /* client already has answered */
        return;
    slice->nrAnswered++;
    SET_BIT(clNo, slice->answeredSet);
}

/**
 * Handles ok message
 */
static int handleOk(sender_state_t sendst,
                    struct slice *slice,
                    int32_t clNo)
{
    if(slice == NULL)
        return 0;
    if(!udpc_isParticipantValid(sendst->rc.participantsDb, clNo)) {
        udpc_flprintf("Invalid participant %d\n", clNo);
        return 0;
    }
    if (BIT_ISSET(clNo, slice->sl_reqack.readySet)) {
        /* client is already marked ready */
#if DEBUG
        flprintf("client %d is already ready\n", clNo);
#endif
    } else {
        SET_BIT(clNo, slice->sl_reqack.readySet);
        slice->nrReady++;
#if DEBUG
        flprintf("client %d replied ok for %p %d ready=%d\n", clNo,
                 slice, slice->sliceNo, slice->nrReady);
#endif	
        senderSetAnswered(sendst->stats, clNo);
        markParticipantAnswered(slice, clNo);
    }
    return 0;
}

static int handleRetransmit(sender_state_t sendst,
                            struct slice *slice,
                            int32_t clNo,
                            unsigned char *map,
                            uint32_t rxmit)
{
    unsigned int i;

#if DEBUG
    flprintf("Handle retransmit %d @%d\n", slice->sliceNo, clNo);
#endif

    if(!udpc_isParticipantValid(sendst->rc.participantsDb, clNo)) {
        udpc_flprintf("Invalid participant %d\n", clNo);
        return 0;
    }
    if(slice == NULL)
        return 0;
    if (rxmit < slice->rxmitId) {
#if 0
        flprintf("Late answer\n");
#endif
        /* late answer to previous Req Ack */
        return 0;
    }
#if DEBUG
    logprintf(udpc_log,
              "Received retransmit request for slice %d from client %d\n",
              slice->sliceNo,clNo);
#endif
    for(i=0; i <sizeof(slice->rxmitMap) / sizeof(char); i++) {
        slice->rxmitMap[i] |= ~map[i];
    }
    slice->needRxmit = 1;
    markParticipantAnswered(slice, clNo);
    return 0;
}

static int handleDisconnect1(struct slice *slice, int32_t clNo)
{    
    if(slice != NULL) {
        if (BIT_ISSET(clNo, slice->sl_reqack.readySet)) {
            /* avoid counting client both as left and ready */
            CLR_BIT(clNo, slice->sl_reqack.readySet);
            slice->nrReady--;
        }
        if (BIT_ISSET(clNo, slice->answeredSet)) {
            slice->nrAnswered--;
            CLR_BIT(clNo, slice->answeredSet);
        }
    }
    return 0;
}

static int handleDisconnect(participantsDb_t db,
                            struct slice *slice1,
                            struct slice *slice2,
                            int32_t clNo)
{
    handleDisconnect1(slice1, clNo);
    handleDisconnect1(slice2, clNo);
    udpc_removeParticipant(db, clNo);
    return 0;
}

static struct slice *findSlice(struct slice *slice1,
                               struct slice *slice2,
                               int64_t sliceNo)
{
    if(slice1 != NULL && slice1->sliceNo == sliceNo)
        return slice1;
    if(slice2 != NULL && slice2->sliceNo == sliceNo)
        return slice2;
    return NULL;
}

static int handleNextMessage(sender_state_t sendst,
                             struct slice *xmitSlice,
                             struct slice *rexmitSlice)
{
    size_t pos = pc_getConsumerPosition(sendst->rc.incoming);
    union clientMsg *msg = &sendst->rc.q[pos].msg;
    int16_t clNo = sendst->rc.q[pos].clNo;

#if DEBUG
    flprintf("handle next message\n");
#endif

    pc_consumeAny(sendst->rc.incoming);
    switch(be16toh(msg->opCode)) {
    case CMD_OK:
        handleOk(sendst,
                 findSlice(xmitSlice, rexmitSlice,
                           be64toh(msg->ok.sliceNo)),
                 clNo);
        break;
    case CMD_DISCONNECT:
        handleDisconnect(sendst->rc.participantsDb,
                         xmitSlice, rexmitSlice, clNo);
        break;
    case CMD_RETRANSMIT:
#if DEBUG
        flprintf("Received retransmittal request for %ld from %d:\n",
                 (long) xtohl(msg->retransmit.sliceNo), clNo);
#endif
        handleRetransmit(sendst,
                         findSlice(xmitSlice, rexmitSlice,
                                   be64toh(msg->retransmit.sliceNo)),
                         clNo,
                         msg->retransmit.map,
                         msg->retransmit.rxmit);
        break;
    default:
        udpc_flprintf("Bad command %04x\n",
                      (unsigned short) msg->opCode);
        break;
    }
    pc_consumed(sendst->rc.incoming, 1);
    pc_produce(sendst->rc.freeSpace, 1);
    return 0;
}

static THREAD_RETURN returnChannelMain(void *args) {
    struct returnChannel *returnChannel = (struct returnChannel *) args;

    while(1) {
        struct sockaddr_in from;
        int16_t clNo;
        size_t pos = pc_getConsumerPosition(returnChannel->freeSpace);
        pc_consumeAny(returnChannel->freeSpace);

        RECV(returnChannel->rcvSock,
             returnChannel->q[pos].msg, from,
             returnChannel->config->portBase);
        clNo = udpc_lookupParticipant(returnChannel->participantsDb, &from);
        if (clNo < 0) {
            /* packet from unknown provenance */
            continue;
        }
        returnChannel->q[pos].clNo = clNo;
        pc_consumed(returnChannel->freeSpace, 1);
        pc_produce(returnChannel->incoming, 1);
    }
    return 0;
}


static void initReturnChannel(struct returnChannel *returnChannel,
                              struct net_config *config,
                              int sock) {
    returnChannel->config = config;
    returnChannel->rcvSock = sock;
    returnChannel->freeSpace = pc_makeProduconsum(QUEUE_SIZE,"msg:free-queue");
    pc_produce(returnChannel->freeSpace, QUEUE_SIZE);
    returnChannel->incoming = pc_makeProduconsum(QUEUE_SIZE,"msg:incoming");

    pthread_create(&returnChannel->thread, NULL,
                   returnChannelMain, returnChannel);

}

static void cancelReturnChannel(struct returnChannel *returnChannel) {
    /* No need to worry about the pthread_cond_wait in produconsum, because
     * at the point where we enter here (to cancel the thread), we are sure
     * that nobody else uses that produconsum any more
     */
    pthread_cancel(returnChannel->thread);
    pthread_join(returnChannel->thread, NULL);
}

static THREAD_RETURN netSenderMain(void	*args0)
{
    sender_state_t sendst = (sender_state_t) args0;
    struct net_config *config = sendst->config;
    struct timeval tv;
    struct timespec ts;
    int atEnd = 0;
    int nrWaited=0;
    long waitAverage=10000; /* Exponential average of last wait times */

    struct slice *xmitSlice=NULL; /* slice being transmitted a first time */
    struct slice *rexmitSlice=NULL; /* slice being re-transmitted */
    int64_t sliceNo = 0;

    /* transmit the data */
    if(config->default_slice_size == 0) {
#ifdef BB_FEATURE_UDPCAST_FEC
        if(config->flags & FLAG_FEC) {
            config->sliceSize =
                    config->fec_stripesize * config->fec_stripes;
        } else
#endif
            if(config->flags & FLAG_ASYNC)
                config->sliceSize = 1024;
            else if (sendst->config->flags & FLAG_SN) {
                sendst->config->sliceSize = 112;
            } else
                sendst->config->sliceSize = 130;
        sendst->config->discovery = DSC_DOUBLING;
    } else {
        config->sliceSize = config->default_slice_size;
#ifdef BB_FEATURE_UDPCAST_FEC
        if((config->flags & FLAG_FEC) &&
                (config->sliceSize > 128 * config->fec_stripes))
            config->sliceSize = 128 * config->fec_stripes;
#endif
    }

#ifdef BB_FEATURE_UDPCAST_FEC
    if( (sendst->config->flags & FLAG_FEC) &&
            config->max_slice_size > config->fec_stripes * 128)
        config->max_slice_size = config->fec_stripes * 128;
#endif

    if(config->sliceSize > config->max_slice_size)
        config->sliceSize = config->max_slice_size;

    assert(config->sliceSize <= MAX_SLICE_SIZE);

    do {
        /* first, cleanup rexmit Slice if needed */

        if(rexmitSlice != NULL) {
            if(rexmitSlice->nrReady ==
                    udpc_nrParticipants(sendst->rc.participantsDb)){
#if DEBUG
                flprintf("slice is ready\n");
#endif
                ackSlice(rexmitSlice, sendst->config, sendst->fifo,
                         sendst->stats);
            }
            if(isSliceAcked(rexmitSlice)) {
                freeSlice(sendst, rexmitSlice);
                rexmitSlice = NULL;
            }
        }

        /* then shift xmit slice to rexmit slot, if possible */
        if(rexmitSlice == NULL &&  xmitSlice != NULL &&
                isSliceXmitted(xmitSlice)) {
            rexmitSlice = xmitSlice;
            xmitSlice = NULL;
            sendReqack(rexmitSlice, sendst->config, sendst->fifo, sendst->stats,
                       sendst->socket);
        }

        /* handle any messages */
        if(pc_getWaiting(sendst->rc.incoming)) {
#if DEBUG
            flprintf("Before message %d\n",
                     pc_getWaiting(sendst->rc.incoming));
#endif
            handleNextMessage(sendst, xmitSlice, rexmitSlice);

            /* restart at beginning of loop: we may have acked the rxmit
         * slice, makeing it possible to shift the pipe */
            continue;
        }

        /* do any needed retransmissions */
        if(rexmitSlice != NULL && rexmitSlice->needRxmit) {
            doRetransmissions(sendst, rexmitSlice);
            /* restart at beginning: new messages may have arrived during
         * retransmission  */
            continue;
        }

        /* if all participants answered, send req ack */
        if(rexmitSlice != NULL &&
                rexmitSlice->nrAnswered ==
                udpc_nrParticipants(sendst->rc.participantsDb)) {
            rexmitSlice->rxmitId++;
            sendReqack(rexmitSlice, sendst->config, sendst->fifo, sendst->stats,
                       sendst->socket);
        }

        if(xmitSlice == NULL && !atEnd) {
#if DEBUG
            flprintf("SN=%d\n", sendst->config->flags & FLAG_SN);
#endif
            if((sendst->config->flags & FLAG_SN) ||
                    rexmitSlice == NULL) {
#ifdef BB_FEATURE_UDPCAST_FEC
                if(sendst->config->flags & FLAG_FEC) {
                    size_t i;
                    pc_consume(sendst->fec_data_pc, 1);
                    i = pc_getConsumerPosition(sendst->fec_data_pc);
                    xmitSlice = &sendst->slices[i];
                    pc_consumed(sendst->fec_data_pc, 1);
                }
                else
#endif
                {
                    xmitSlice = makeSlice(sendst, sliceNo++);
                }
                if(xmitSlice->bytes == 0)
                    atEnd = 1;
            }
        }

        if(xmitSlice != NULL && xmitSlice->state == SLICE_NEW) {
            sendSlice(sendst, xmitSlice, 0);
#if DEBUG
            flprintf("%d Interrupted at %d/%d\n", xmitSlice->sliceNo,
                     xmitSlice->nextBlock,
                     getSliceBlocks(xmitSlice, sendst->config));
#endif
            continue;
        }
        if(atEnd && rexmitSlice == NULL && xmitSlice == NULL)
            break;

        if(sendst->config->flags & FLAG_ASYNC)
            break;

#if DEBUG
        flprintf("Waiting for timeout...\n");
#endif
        gettimeofday(&tv, 0);
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = (long)(tv.tv_usec + 1.1*waitAverage) * 1000;

#ifdef WINDOWS
        /* Windows has a granularity of 1 millisecond in its timer. Take this
     * into account here */
#define GRANULARITY 1000000
        ts.tv_nsec += 3*GRANULARITY/2;
        ts.tv_nsec -= ts.tv_nsec % GRANULARITY;
#endif

#define BILLION 1000000000

        while(ts.tv_nsec >= BILLION) {
            ts.tv_nsec -= BILLION;
            ts.tv_sec++;
        }

        if(rexmitSlice->rxmitId > 10)
            /* after tenth retransmission, wait minimum one second */
            ts.tv_sec++;

        if(pc_consumeAnyWithTimeout(sendst->rc.incoming, &ts) != 0) {
#if DEBUG
            flprintf("Have data\n");
#endif
            {
                struct timeval tv2;
                long timeout;
                gettimeofday(&tv2, 0);
                timeout =
                        (tv2.tv_sec - tv.tv_sec) * 1000000+
                        tv2.tv_usec - tv.tv_usec;
                if(nrWaited)
                    timeout += waitAverage;
                waitAverage += 9; /* compensate against rounding errors */
                waitAverage = (long)(0.9 * waitAverage + 0.1 * timeout);
            }
            nrWaited = 0;
            continue;
        }
        if(rexmitSlice == NULL) {
            udpc_flprintf("Weird. Timeout and no rxmit slice");
            break;
        }
        if(nrWaited > 5){
#ifndef WINDOWS
            /* on Cygwin, we would get too many of those messages... */
            udpc_flprintf("Timeout notAnswered=");
            udpc_printNotSet(sendst->rc.participantsDb,
                             rexmitSlice->answeredSet);
            udpc_flprintf(" notReady=");
            udpc_printNotSet(sendst->rc.participantsDb, rexmitSlice->sl_reqack.readySet);
            udpc_flprintf(" nrAns=%d nrRead=%d nrPart=%d avg=%ld\n",
                          rexmitSlice->nrAnswered,
                          rexmitSlice->nrReady,
                          udpc_nrParticipants(sendst->rc.participantsDb),
                          waitAverage);
            nrWaited=0;
#endif
        }
        nrWaited++;
        if(rexmitSlice->rxmitId > config->retriesUntilDrop) {
            int32_t i;
            for(i=0; i < MAX_CLIENTS; i++) {
                if(udpc_isParticipantValid(sendst->rc.participantsDb, i) &&
                        !BIT_ISSET(i, rexmitSlice->sl_reqack.readySet)) {
                    udpc_flprintf("Dropping client #%d because of timeout\n",
                                  i);
#ifdef USE_SYSLOG
                    syslog(LOG_INFO, "dropped client #%d because of timeout",
                           i);
#endif
                    udpc_removeParticipant(sendst->rc.participantsDb, i);
                    if(nrParticipants(sendst->rc.participantsDb) == 0)
                        goto exit_main_loop;
                }
            }
            continue;
        }
        rexmitSlice->rxmitId++;
        sendReqack(rexmitSlice, sendst->config, sendst->fifo, sendst->stats,
                   sendst->socket);
    } while(udpc_nrParticipants(sendst->rc.participantsDb)||
            (config->flags & FLAG_ASYNC));
exit_main_loop:
    cancelReturnChannel(&sendst->rc);
    pc_produceEnd(sendst->fifo->freeMemQueue);
    return 0;
}


#define ADR(x, bs) (fifo->dataBuffer + \
    (slice->base+(x)*bs) % fifo->dataBufSize)

#ifdef BB_FEATURE_UDPCAST_FEC
static void fec_encode_all_stripes(sender_state_t sendst,
                                   struct slice *slice)
{
    uint32_t stripe;
    struct net_config *config = sendst->config;
    struct fifo *fifo = sendst->fifo;
    uint32_t bytes = slice->bytes;
    uint16_t stripes = config->fec_stripes;
    uint32_t redundancy = config->fec_redundancy;
    uint32_t nrBlocks = (bytes + config->blockSize - 1) / config->blockSize;
    uint32_t leftOver = bytes % config->blockSize;
    unsigned char *fec_data = slice->fec_data;

    // FIXME: change to malloc
    unsigned char *fec_blocks[redundancy];
    unsigned char *data_blocks[128];

    if(leftOver) {
        unsigned char *lastBlock = ADR(nrBlocks - 1, config->blockSize);
        memset(lastBlock+leftOver, 0, config->blockSize-leftOver);
    }

    for(stripe=0; stripe<stripes; stripe++) {
        uint32_t i;
        uint32_t j;
        for(i=0; i<redundancy; i++)
            fec_blocks[i] = fec_data+config->blockSize*(stripe+i*stripes);
        for(i=stripe, j=0; i< nrBlocks; i+=stripes, j++)
            data_blocks[j] = ADR(i, config->blockSize);
        fec_encode(config->blockSize, data_blocks, j, fec_blocks, redundancy);

    }
}


static THREAD_RETURN fecMain(void *args0)
{
    sender_state_t sendst = (sender_state_t) args0;

    slice_t slice;
    int64_t sliceNo = 0;

    while(1) {
        /* consume free slice */
        slice = makeSlice(sendst, sliceNo++);
        /* do the fec calculation here */
        fec_encode_all_stripes(sendst, slice);
        pc_produce(sendst->fec_data_pc, 1);
    }
    return 0;
}
#endif

int spawnNetSender(struct fifo *fifo,
                   int sock,
                   struct net_config *config,
                   participantsDb_t db,
                   sender_stats_t stats)
{
    int i;

    sender_state_t sendst = MALLOC(struct senderState);
    sendst->fifo = fifo;
    sendst->socket = sock;
    sendst->config = config;
    sendst->stats = stats;
#ifdef BB_FEATURE_UDPCAST_FEC
    if(sendst->config->flags & FLAG_FEC)
        sendst->fec_data =  xmalloc(NR_SLICES *
                                    config->fec_stripes *
                                    config->fec_redundancy *
                                    config->blockSize);
#endif
    sendst->rc.participantsDb = db;
    initReturnChannel(&sendst->rc, sendst->config, sendst->socket);

    sendst->free_slices_pc = pc_makeProduconsum(NR_SLICES, "free slices");
    pc_produce(sendst->free_slices_pc, NR_SLICES);
    for(i = 0; i <NR_SLICES; i++)
        sendst->slices[i].state = SLICE_FREE;

#ifdef BB_FEATURE_UDPCAST_FEC
    if(sendst->config->flags & FLAG_FEC) {
        /* Free memory queue is initially full */
        fec_init();
        sendst->fec_data_pc = pc_makeProduconsum(NR_SLICES, "fec data");

        pthread_create(&sendst->fec_thread, NULL, fecMain, sendst);
    }
#endif

    pthread_create(&fifo->thread, NULL, netSenderMain, sendst);
    return 0;
}
