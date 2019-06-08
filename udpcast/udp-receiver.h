#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include "socklib.h"
#include "threads.h"
#include "statistics.h"
#include "console.h"
#include "udpcast.h"
#include "fifo.h"

#define S_UCAST 0
#define S_BCAST 1
#define S_MCAST_CTRL 2
#define S_MCAST_DATA 3

#define NR_CLIENT_SOCKS 4

struct client_config {
    int socks[NR_CLIENT_SOCKS];
    struct sockaddr_in serverAddr;
    int32_t clientNumber;
    int isStarted;
    pthread_t thread;
    int sender_is_newgen;
    console_t *console;
};

#define spawnNetReceiver udpc_spawnNetReceiver
#define writer udpc_writer
#define openPipe udpcr_openPipe
#define sendGo udpc_sendGo
#define sendDisconnect udpc_sendDisconnect
#define startReceiver udpc_startReceiver

int spawnNetReceiver(struct fifo *fifo,
                     struct client_config *client_config,
                     struct net_config *net_config,
                     receiver_stats_t stats);
int writer(struct fifo *fifo, int fd);
int openPipe(int disk, 
             struct disk_config *disk_config,
             int *pipePid);
ssize_t sendGo(struct client_config *);
void sendDisconnect(int, struct client_config *);
int startReceiver(int doWarn,
                  struct disk_config *disk_config,
                  struct net_config *net_config,
                  struct stat_config *stat_config,
                  const char *ifName);

#define SSEND(x) SEND(client_config->socks[S_UCAST], x, client_config->serverAddr)

enum ReceiverFlag {
/*
 * Receiver will passively listen to sender. Works best if sender runs
 * in async mode
 */
    FLAG_PASSIVE = 0x0010,
/*
 * Do not write file synchronously
 */
    FLAG_NOSYNC = 0x0040,
/*
 * Don't ask for keyboard input on receiver end.
 */
    FLAG_NOKBD = 0x0080,
/*
 * Do write file synchronously
 */
    FLAG_SYNC = 0x0100,
/*
 * Streaming mode
 */
    FLAG_STREAMING = 0x200,
/*
 * Ignore lost data
 */
    FLAG_IGNORE_LOST_DATA = 0x400,
};

#endif
