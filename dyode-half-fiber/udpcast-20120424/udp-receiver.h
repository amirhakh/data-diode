#ifndef UDP_RECEIVER_H
#define UDP_RECEIVER_H

#include "threads.h"
#include "statistics.h"
#include "console.h"

#define S_UCAST socks[0]
#define S_BCAST socks[1]
#define S_MCAST_CTRL socks[2]
#define S_MCAST_DATA socks[3]

#define NR_CLIENT_SOCKS 4

struct client_config {
    int socks[NR_CLIENT_SOCKS];
    struct sockaddr_in serverAddr;
    int clientNumber;
    int isStarted;
    pthread_t thread;
    int sender_is_newgen;
    console_t *console;
};

struct fifo;

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
int sendGo(struct client_config *);
void sendDisconnect(int, struct client_config *);
int startReceiver(int doWarn,
		  struct disk_config *disk_config,
		  struct net_config *net_config,
		  struct stat_config *stat_config,
		  const char *ifName);

#define SSEND(x) SEND(client_config->S_UCAST, x, client_config->serverAddr)

/**
 * Receiver will passively listen to sender. Works best if sender runs
 * in async mode
 */
#define FLAG_PASSIVE 0x0010


/**
 * Do not write file synchronously
 */
#define FLAG_NOSYNC 0x0040

/*
 * Don't ask for keyboard input on receiver end.
 */
#define FLAG_NOKBD 0x0080

/**
 * Do write file synchronously
 */
#define FLAG_SYNC 0x0100

/**
 * Streaming mode
 */
#define FLAG_STREAMING 0x200

/**
 * Ignore lost data
 */
#define FLAG_IGNORE_LOST_DATA 0x400

#endif
