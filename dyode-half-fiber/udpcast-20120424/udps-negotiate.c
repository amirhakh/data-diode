#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "log.h"
#include "fifo.h"
#include "socklib.h"
#include "udpcast.h"
#include "udpc-protoc.h"
#include "udp-sender.h"
#include "participants.h"
#include "statistics.h"
#include "console.h"

#ifdef USE_SYSLOG
#include <syslog.h>
#endif

/**
 * This file contains the code to set up the connection
 */

static int doTransfer(int sock, 
		      participantsDb_t db,
		      struct disk_config *disk_config,
		      struct net_config *net_config,
		      struct stat_config *stat_config);


static int isPointToPoint(participantsDb_t db, int flags) {
    if(flags & FLAG_POINTOPOINT)
	return 1;
    if(flags & (FLAG_NOPOINTOPOINT | FLAG_ASYNC))
	return 0;
    return udpc_nrParticipants(db) == 1;
}


static int sendConnectionReply(participantsDb_t db,
			       int sock,
			       struct net_config *config,
			       struct sockaddr_in *client, 
			       int capabilities,
			       unsigned int rcvbuf) {
    struct connectReply reply;

    if(rcvbuf == 0)
	rcvbuf = 65536;

    if(capabilities & CAP_BIG_ENDIAN) {
	reply.opCode = htons(CMD_CONNECT_REPLY);
	reply.clNr = 
	    htonl(udpc_addParticipant(db,
				      client, 
				      capabilities,
				      rcvbuf,
				      config->flags & FLAG_POINTOPOINT));
	reply.blockSize = htonl(config->blockSize);
    } else {
	udpc_fatal(1, "Little endian protocol no longer supported");
    }
    reply.reserved = 0;

    if(config->flags & FLAG_POINTOPOINT) {
	copyIpFrom(&config->dataMcastAddr, client);
    }

    /* new parameters: always big endian */
    reply.capabilities = ntohl(config->capabilities);
    copyToMessage(reply.mcastAddr,&config->dataMcastAddr);
    /*reply.mcastAddress = mcastAddress;*/
    rgWaitAll(config, sock, client->sin_addr.s_addr, sizeof(reply));
    if(SEND(sock, reply, *client) < 0) {
	perror("reply add new client");
	return -1;
    }
    return 0;
}

void sendHello(struct net_config *net_config, int sock,
	       int streaming) {
    struct hello hello;
    /* send hello message */
    if(streaming)
	hello.opCode = htons(CMD_HELLO_STREAMING);
    else
	hello.opCode = htons(CMD_HELLO);
    hello.reserved = 0;
    hello.capabilities = htonl(net_config->capabilities);
    copyToMessage(hello.mcastAddr,&net_config->dataMcastAddr);
    hello.blockSize = htons(net_config->blockSize);
    rgWaitAll(net_config, sock, net_config->controlMcastAddr.sin_addr.s_addr,
	      sizeof(hello));
    BCAST_CONTROL(sock, hello);
}

/* Returns 1 if we should start because of clientWait, 0 otherwise */
static int checkClientWait(participantsDb_t db, 
			   struct net_config *net_config,
			   time_t *firstConnected)
{
    time_t now;
    if (!nrParticipants(db) || !firstConnected || !*firstConnected)
	return 0; /* do not start: no receivers */

    now = time(0);
    /*
     * If we have a max_client_wait, start the transfer after first client
     * connected + maxSendWait
     */
    if(net_config->max_receivers_wait &&
       (now >= *firstConnected + net_config->max_receivers_wait)) {
#ifdef USE_SYSLOG
	    syslog(LOG_INFO, "max wait[%d] passed: starting", 
			    net_config->max_receivers_wait );
#endif
	return 1; /* send-wait passed: start */
    }

    /*
     * Otherwise check to see if the minimum of clients
     *  have checked in.
     */
    else if (nrParticipants(db) >= net_config->min_receivers &&
	/*
	 *  If there are enough clients and there's a min wait time, we'll
	 *  wait around anyway until then.
	 *  Otherwise, we always transfer
	 */
	(!net_config->min_receivers_wait || 
	 now >= *firstConnected + net_config->min_receivers_wait)) {
#ifdef USE_SYSLOG
	    syslog(LOG_INFO, "min receivers[%d] reached: starting", 
			    net_config->min_receivers );
#endif
	    return 1;
    } else
	return 0;
}

/* *****************************************************
 * Receive and process a localization enquiry by a client
 * Params:
 * fd		- file descriptor for network socket on which to receiver 
 *		client requests
 * db		- participant database
 * disk_config	- disk configuration
 * net_config	- network configuration
 * keyboardFd	- keyboard filedescriptor (-1 if keyboard inaccessible,
 *		or configured away)
 * tries	- how many hello messages have been sent?
 * firstConnected - when did the first client connect?
 */
static int mainDispatcher(int *fd, int nr,
			  participantsDb_t db,
			  struct net_config *net_config,
			  console_t **console, int *tries,
			  time_t *firstConnected)
{
    struct sockaddr_in client;
    union message fromClient;
    fd_set read_set;
    int ret;
    int msgLength;
    int startNow=0;
    int selected;
    int keyPressed=0;
    long loopStart = time(0);

    if ((udpc_nrParticipants(db) || (net_config->flags &  FLAG_ASYNC)) &&
	!(net_config->flags &  FLAG_NOKBD) && *console != NULL)
#ifdef __MINGW32__
	fprintf(stderr, "Ready. Press return to start sending data.\n");
#else /* __MINGW32__ */
        fprintf(stderr, "Ready. Press any key to start sending data.\n");
#endif /* __MINGW32__ */
 
    if (firstConnected && !*firstConnected && udpc_nrParticipants(db)) {
	*firstConnected = time(0);
#ifdef USE_SYSLOG
        syslog(LOG_INFO,
	 "first connection: min wait[%d] secs - max wait[%d] - min clients[%d]",
	  net_config->min_receivers_wait, net_config->max_receivers_wait, 
	  net_config->min_receivers );
#endif
    }

    while(!startNow) {
	struct timeval tv;
	struct timeval *tvp;
	int nr_desc;

	int maxFd = prepareForSelect(fd, nr, &read_set);

	if(net_config->rexmit_hello_interval) {
	    tv.tv_usec = (net_config->rexmit_hello_interval % 1000)*1000;
	    tv.tv_sec = net_config->rexmit_hello_interval / 1000;
	    tvp = &tv;
	} else if((firstConnected && nrParticipants(db)) ||
		  net_config->startTimeout) {
	    tv.tv_usec = 0;
	    tv.tv_sec = 2;
	    tvp = &tv;
	} else
	    tvp = 0;
	nr_desc = selectWithConsole(*console, maxFd+1, &read_set, tvp,
				    &keyPressed);
	if(nr_desc < 0) {
	    perror("select");
	    return -1;
	}
	if(nr_desc > 0 || keyPressed)
	    /* key pressed, or receiver activity */
	    break;

	if(net_config->rexmit_hello_interval) {
	    /* retransmit hello message */
	    sendHello(net_config, fd[0], 0);
	    (*tries)++;
	    if(net_config->autostart != 0 && *tries > net_config->autostart)
		startNow=1;
	}

	if(firstConnected)
	    startNow = 
		startNow || checkClientWait(db, net_config, firstConnected);

	if(!startNow &&
	   net_config->startTimeout &&
	   time(0) - loopStart >= net_config->startTimeout) {
	  startNow = -1;
	  break;
	}
    }

    if(keyPressed) {
	restoreConsole(console,1);
	startNow = 1;
    }

    selected = getSelectedSock(fd, nr, &read_set);
    if(selected == -1)
	return startNow;

    BZERO(fromClient); /* Zero it out in order to cope with short messages
			* from older versions */

    msgLength = RECV(selected, fromClient, client, net_config->portBase);
    if(msgLength < 0) {
	perror("problem getting data from client");
	return 0; /* don't panic if we get weird messages */
    }

    if(net_config->flags & FLAG_ASYNC)
	return 0;

    switch(ntohs(fromClient.opCode)) {
	case CMD_CONNECT_REQ:
	    sendConnectionReply(db, fd[0],
				net_config,
				&client, 
				CAP_BIG_ENDIAN |
				ntohl(fromClient.connectReq.capabilities),
				ntohl(fromClient.connectReq.rcvbuf));
	    return startNow;
	case CMD_GO:
	    return 1;
	case CMD_DISCONNECT:
	    ret = udpc_lookupParticipant(db, &client);
	    if (ret >= 0)
		udpc_removeParticipant(db, ret);
	    return startNow;
	default:
	    break;
    }

    udpc_flprintf("Unexpected command %04x\n",
		  (unsigned short) fromClient.opCode);

    return startNow;
}


int udpc_openMainSenderSock(struct net_config *net_config,
			    const char *ifName)
{
    net_config->net_if = getNetIf(ifName);
    return makeSocket(ADDR_TYPE_UCAST,
		      net_config->net_if,
		      NULL,
		      SENDER_PORT(net_config->portBase));
}

int startSender(struct disk_config *disk_config,
		struct net_config *net_config,
		struct stat_config *stat_config,
		int mainSock)
{
    char ipBuffer[16];
    int tries;
    int r; /* return value for maindispatch. If 1, start transfer */
    int j;
    time_t firstConnected = 0;
    time_t *firstConnectedP;
    console_t *console=NULL;

    participantsDb_t db;

    /* make the socket and print banner */
    int sock[3];
    int nr=0;
    int fd;

#ifdef SIG_BLOCK
    /* signal sets */
    sigset_t sig, oldsig;
    int shouldRestoreSig;
#endif

    sock[nr++] = mainSock;

    if(! (net_config->flags & (FLAG_SN | FLAG_NOTSN)) ) {
      if(isFullDuplex(sock[0], net_config->net_if->name) == 1) {
	fprintf(stderr, "Using full duplex mode\n");
	net_config->flags |= FLAG_SN;
      }
    }
    
    fd = makeSocket(ADDR_TYPE_BCAST,
		    net_config->net_if,
		    NULL,
		    SENDER_PORT(net_config->portBase));
    if(fd >= 0)
	sock[nr++] = fd;

    if(net_config->requestedBufSize)
	setSendBuf(sock[0], net_config->requestedBufSize);

    net_config->controlMcastAddr.sin_addr.s_addr =0;
    if(net_config->ttl == 1 && net_config->mcastRdv == NULL) {
	getBroadCastAddress(net_config->net_if,
			    &net_config->controlMcastAddr,
			    RECEIVER_PORT(net_config->portBase));
	setSocketToBroadcast(sock[0]);
    } 

    if(net_config->controlMcastAddr.sin_addr.s_addr == 0) {
	getMcastAllAddress(&net_config->controlMcastAddr,
			   net_config->mcastRdv,
			   RECEIVER_PORT(net_config->portBase));
	/* Only do the following if controlMcastAddr is indeed an
	   mcast address ... */
	if(isMcastAddress(&net_config->controlMcastAddr)) {
	    setMcastDestination(sock[0], net_config->net_if,
				&net_config->controlMcastAddr);
	    setTtl(sock[0], net_config->ttl);
	    sock[nr++] = makeSocket(ADDR_TYPE_MCAST,
				    net_config->net_if,
				    &net_config->controlMcastAddr,
				    SENDER_PORT(net_config->portBase));
	}
    }

    if(!(net_config->flags & FLAG_POINTOPOINT) &&
       ipIsZero(&net_config->dataMcastAddr)) {
	getDefaultMcastAddress(net_config->net_if, 
			       &net_config->dataMcastAddr);
	udpc_flprintf("Using mcast address %s\n",
		      getIpString(&net_config->dataMcastAddr, ipBuffer));
    }

    if(net_config->flags & FLAG_POINTOPOINT) {
	clearIp(&net_config->dataMcastAddr);
    }

    setPort(&net_config->dataMcastAddr, RECEIVER_PORT(net_config->portBase));

    udpc_flprintf("%sUDP sender for %s at ", 
		  disk_config->pipeName == NULL ? "" : "Compressed ",
		  disk_config->fileName == NULL ? "(stdin)" :
		  disk_config->fileName);
    printMyIp(net_config->net_if);
    udpc_flprintf(" on %s \n", net_config->net_if->name);
    udpc_flprintf("Broadcasting control to %s\n",
		  getIpString(&net_config->controlMcastAddr, ipBuffer));

    net_config->capabilities = SENDER_CAPABILITIES;
    if(net_config->flags & FLAG_ASYNC)
	net_config->capabilities |= CAP_ASYNC;

    sendHello(net_config, sock[0], 0);
    db = udpc_makeParticipantsDb();
    tries = 0;

    if(!(net_config->flags & FLAG_NOKBD))
	console = prepareConsole((disk_config->fileName != NULL) ? 0 : -1);

    if(net_config->min_receivers || net_config->min_receivers_wait ||
       net_config->max_receivers_wait)
	firstConnectedP = &firstConnected;
    else
	firstConnectedP = NULL;	

    while(!(r=mainDispatcher(sock, nr, db, net_config,
			     &console,&tries,firstConnectedP))){}
    for(j=1; j<nr; j++)
      if(sock[j] != sock[0])
	closesocket(sock[j]);

#ifdef SIG_BLOCK
    sigemptyset(&sig);
    sigaddset(&sig, SIGTERM);
    if(sigprocmask(SIG_BLOCK, &sig, &oldsig) == 0)
      shouldRestoreSig=1;
#endif

    restoreConsole(&console,0);
    if(r == 1) {
	int i;
	for(i=1; i<nr; i++)
	    udpc_closeSock(sock, nr, i);
	if((net_config->flags & FLAG_ASYNC) ||
	   udpc_nrParticipants(db) > 0)
	  doTransfer(sock[0], db, disk_config, net_config, stat_config);
	else
	  fprintf(stderr, "No participants... exiting\n");
    }
    free(db);
#ifdef SIG_BLOCK
    if(shouldRestoreSig)
      sigprocmask(SIG_SETMASK, &oldsig, NULL);
#endif
    return 0;
}

/*
 * Do the actual data transfer
 */
static int doTransfer(int sock, 
		      participantsDb_t db,
		      struct disk_config *disk_config,
		      struct net_config *net_config,
		      struct stat_config *stat_config)
{
    int i;
    struct fifo fifo;
    sender_stats_t stats;
    int in;
    int origIn;
    int pid;
    int isPtP = isPointToPoint(db, net_config->flags);
    int printUncompressedPos;

    if((net_config->flags & FLAG_POINTOPOINT) &&
       udpc_nrParticipants(db) != 1) {
	udpc_fatal(1,
		   "pointopoint mode set, and %d participants instead of 1\n",
		   udpc_nrParticipants(db));
    }

    net_config->rcvbuf=0;

    for(i=0; i<MAX_CLIENTS; i++)
	if(udpc_isParticipantValid(db, i)) {
	    unsigned int pRcvBuf = udpc_getParticipantRcvBuf(db, i);
	    if(isPtP)
		copyIpFrom(&net_config->dataMcastAddr, 
			   udpc_getParticipantIp(db,i));
	    net_config->capabilities &= 
		udpc_getParticipantCapabilities(db, i);
	    if(pRcvBuf != 0 && 
	       (net_config->rcvbuf == 0 || net_config->rcvbuf > pRcvBuf))
		net_config->rcvbuf = pRcvBuf;
	}

    if(isMcastAddress(&net_config->dataMcastAddr))
	setMcastDestination(sock, net_config->net_if, 
			    &net_config->dataMcastAddr);

    udpc_flprintf("Starting transfer: %08x\n", net_config->capabilities);
#ifdef USE_SYSLOG
    syslog(LOG_INFO, 
      "Starting transfer: file[%s] pipe[%s] port[%d] if[%s] participants[%d]",
	    disk_config->fileName == NULL ? "" : disk_config->fileName,
	    disk_config->pipeName == NULL ? "" : disk_config->pipeName,
	    net_config->portBase,
	    net_config->net_if->name == NULL ? "" : net_config->net_if->name,
	    udpc_nrParticipants(db) );
#endif

    if(! (net_config->capabilities & CAP_BIG_ENDIAN))
	udpc_fatal(1, "Peer with incompatible endianness");

    if(! (net_config->capabilities & CAP_NEW_GEN)) {
       net_config->dataMcastAddr = net_config->controlMcastAddr;
       net_config->flags &= ~(FLAG_SN | FLAG_ASYNC);
    }
    if(net_config->flags & FLAG_BCAST)
       net_config->dataMcastAddr = net_config->controlMcastAddr;

    origIn = openFile(disk_config);
    in = openPipe(disk_config, origIn, &pid);

    printUncompressedPos =
      udpc_shouldPrintUncompressedPos(stat_config->printUncompressedPos,
				      origIn, in);

    stats = allocSenderStats(origIn, stat_config->log, stat_config->bwPeriod,
			     stat_config->statPeriod,
			     printUncompressedPos,
			     stat_config->noProgress);
    udpc_initFifo(&fifo, net_config->blockSize);
    spawnNetSender(&fifo, sock, net_config, db, stats);
    localReader(&fifo, in);

    close(in);

    /* if we have a pipe, now wait for that too */
    if(pid) {
	udpc_waitForProcess(pid, "Pipe");
    }

    pthread_join(fifo.thread, NULL);    
    displaySenderStats(stats, 
		       net_config->blockSize, net_config->sliceSize,
		       1);
    /* This has to be done last, or else the final sender stats will not be
       able to print uncompressed position */
    if(in != origIn)
      close(origIn);

    udpc_flprintf("Transfer complete.\007\n");
#ifdef USE_SYSLOG
    syslog(LOG_INFO, "Transfer complete.");
#endif

    /* remove all participants */
    for(i=0; i < MAX_CLIENTS; i++) {
	udpc_removeParticipant(db, i);
    }
    udpc_flprintf("\n");
    return 0;
}
