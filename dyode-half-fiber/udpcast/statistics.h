#ifndef STATISTICS_H
#define STATISTICS_H

#include "util.h"
#include <sys/time.h>
#include <stdio.h>
/**
 * Common part between receiver and sender stats
 */
struct stats {
    int fd;
    struct timeval lastPrinted;
    long statPeriod;
    char printUncompressedPos;
    char printRetransmissions;
    char noProgress;
};

struct receiver_stats {
    struct timeval tv_start;
    int bytesOrig;
    int64_t totalBytes;
    int timerStarted;
    struct stats s;
};

struct sender_stats {
    FILE *log;
    int64_t transferedBytes;
    int64_t totalBytes;
    int64_t retransmissions;
    int32_t clNo;
    int64_t periodBytes;
    struct timeval periodStart;
    long bwPeriod;
    struct stats s;
};

typedef struct receiver_stats *receiver_stats_t;
typedef struct sender_stats *sender_stats_t;

#define allocReadStats udpc_allocReadStats
#define receiverStatsStartTimer udpc_receiverStatsStartTimer
#define receiverStatsAddBytes udpc_receiverStatsAddBytes
#define displayReceiverStats udpc_displayReceiverStats

receiver_stats_t udpc_allocReadStats(int fd, long statPeriod,
                                     char printUncompressedPos,
                                     char printRetransmissions,
                                     char noProgress);
void udpc_receiverStatsStartTimer(receiver_stats_t);
void udpc_receiverStatsAddBytes(receiver_stats_t, int64_t bytes);
void udpc_displayReceiverStats(receiver_stats_t, int isFinal);

#define allocSenderStats udpc_allocSenderStats
#define senderStatsAddBytes udpc_senderStatsAddBytes
#define senderStatsAddRetransmissions udpc_senderStatsAddRetransmissions
#define displaySenderStats udpc_displaySenderStats
#define senderSetAnswered udpc_senderSetAnswered

sender_stats_t udpc_allocSenderStats(int fd, FILE *logfile, long bwPeriod,
                                     long statPeriod, char printUncompressedPos,
                                     char printRetransmissions,
                                     char noProgress, int64_t totalBytes);
void udpc_senderStatsAddBytes(sender_stats_t, int64_t bytes);
void udpc_senderStatsAddRetransmissions(sender_stats_t ss, 
                                        uint64_t retransmissions);
void udpc_displaySenderStats(sender_stats_t, uint32_t blockSize, uint32_t sliceSize,
                             int isFinal);
void udpc_senderSetAnswered(sender_stats_t ss, int32_t clNo);


#endif
