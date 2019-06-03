#ifndef UDPCAST_H
#define UDPCAST_H

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#endif

#include "socklib.h"
#include <sys/time.h>
#include <stdio.h>

#define BITS_PER_INT (sizeof(int) * 8)
#define BITS_PER_CHAR 8


#define MAP_ZERO(l, map) (memset(map, 0, ((l) + BITS_PER_INT - 1)/ BIT_PER_INT))
#define BZERO(data) (memset((void *)&data, 0, sizeof(data)))


#define RDATABUFSIZE (2*(MAX_SLICE_SIZE + 1)* MAX_BLOCK_SIZE)

#define DATABUFSIZE (RDATABUFSIZE + 4096 - RDATABUFSIZE % 4096)

int udpc_writeSize(void);
int udpc_largeReadSize(void);
int udpc_smallReadSize(void);
int udpc_makeDataBuffer(int blocksize);
int udpc_parseCommand(char *pipeName, char **arg);

int udpc_printLongNum(long long x);
int udpc_printULongNum(unsigned long long x);
int udpc_waitForProcess(int pid, const char *message);

struct disk_config {
    int origOutFile;
    const char *fileName;
    int64_t fileSize;
    char *pipeName;
    int flags;

    struct timeval stats_last_printed;
};

#define MAX_GOVERNORS 10

struct net_config {
    net_if_t *net_if; /* Network interface (eth0, isdn0, etc.) on which to
               * multicast */
    uint16_t portBase; /* Port base */
    uint32_t blockSize;
    uint32_t sliceSize;
    struct sockaddr_in controlMcastAddr;
    struct sockaddr_in dataMcastAddr;
    const char *mcastRdv;
    int ttl;
    int nrGovernors;
    struct rateGovernor_t *rateGovernor[MAX_GOVERNORS];
    void *rateGovernorData[MAX_GOVERNORS];
    /*int async;*/
    /*int pointopoint;*/
    struct timeval ref_tv;

    enum discovery {
        DSC_DOUBLING,
        DSC_REDUCING
    } discovery;

    /* int autoRate; do queue watching using TIOCOUTQ, to avoid overruns */

    int flags; /* non-capability command line flags */
    uint32_t capabilities;

    uint32_t min_slice_size;
    uint32_t default_slice_size;
    uint32_t max_slice_size;
    uint32_t rcvbuf;

    int rexmit_hello_interval; /* retransmission interval between hello's.
                * If 0, hello message won't be retransmitted
                */
    int autostart; /* autostart after that many retransmits */

    uint32_t requestedBufSize; /* requested receiver buffer */

    /* sender-specific parameters */
    int16_t min_receivers;
    int32_t max_receivers_wait;
    int32_t min_receivers_wait;

    uint32_t retriesUntilDrop;

    /* receiver-specif parameters */
    int exitWait; /* How many milliseconds to wait on program exit */

    int startTimeout; /* Timeout at start */
    int receiveTimeout; /* Receive timeout */

    /* FEC config */
#ifdef BB_FEATURE_UDPCAST_FEC
    uint32_t fec_redundancy; /* how much fec blocks are added per group */
    uint32_t fec_stripesize; /* size of FEC group */
    uint16_t fec_stripes; /* number of FEC stripes per slice */
#endif

    uint32_t rehelloOffset; /* how far before end will rehello packet will
              be retransmitted */
};

struct stat_config {
    FILE *log; /* Log file for statistics */
    long bwPeriod; /* How often are bandwidth estimations logged? */

    int statPeriod;
    char printUncompressedPos;
    char printRetransmissions;
    char noProgress;
};


void *rgInitGovernor(struct net_config *cfg, struct rateGovernor_t *gov);
void rgParseRateGovernor(struct net_config *net_config, char *rg);
void rgWaitAll(struct net_config *cfg, int sock, in_addr_t ip, size_t size);
void rgShutdownAll(struct net_config *cfg);

/**
 * Answers whether given fd is seekable
 */
char udpc_shouldPrintUncompressedPos(char deflt, int fd, int pipe);

#define MAX_SLICE_SIZE 1024

#define DEFLT_STAT_PERIOD 500000

#ifndef DEBUG
# define DEBUG 0
#endif

#endif
