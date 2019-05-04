#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include "log.h"
#include "socklib.h"
#include "udpcast.h"
#include "udpc-protoc.h"
#include "fifo.h"
#include "udp-receiver.h"
#include "util.h"
#include "produconsum.h"
#include "statistics.h"

#ifndef O_BINARY
# define O_BINARY 0
#endif

#ifndef O_SYNC
# define O_SYNC 0
#endif

#ifndef O_TRUNC
# define O_TRUNC 0
#endif

static int sendConnectReq(struct client_config *client_config,
			  struct net_config *net_config,
			  int haveServerAddress) {
    struct connectReq connectReq;

    if(net_config->flags & FLAG_PASSIVE)
	return 0;

    connectReq.opCode = htons(CMD_CONNECT_REQ);
    connectReq.reserved = 0;
    connectReq.capabilities = htonl(RECEIVER_CAPABILITIES);
    connectReq.rcvbuf = htonl(getRcvBuf(client_config->S_UCAST));
    if(haveServerAddress)
      return SSEND(connectReq);
    else
      return BCAST_CONTROL(client_config->S_UCAST, connectReq);
}

int sendGo(struct client_config *client_config) {
    struct go go;
    go.opCode = htons(CMD_GO);
    go.reserved = 0;
    return SSEND(go);
}

struct client_config *global_client_config=NULL;

static void fixConsole(void) {
    if(global_client_config)
	restoreConsole(&global_client_config->console,0);
}

static void sendDisconnectWrapper(void) {
    if(global_client_config)
	sendDisconnect(0, global_client_config);
}

void sendDisconnect(int exitStatus,
		    struct client_config *client_config) {    
    struct disconnect disconnect;
    disconnect.opCode = htons(CMD_DISCONNECT);
    disconnect.reserved = 0;
    SSEND(disconnect);
    if (exitStatus == 0)
	udpc_flprintf("Transfer complete.\007\n");
}


struct startTransferArgs {
    int fd;
    int pipeFd;
    struct client_config *client_config;
    int doWarn;
};

static int openOutFile(struct disk_config *disk_config)
{
    int outFile=1;
    if(disk_config->fileName != NULL) {
	int oflags = O_CREAT | O_WRONLY | O_TRUNC;
	if((disk_config->flags & FLAG_SYNC)) {
	    oflags |= O_SYNC;
	}
	outFile = open(disk_config->fileName, oflags | O_BINARY, 0644);
	if(outFile < 0) {
#ifdef NO_BB
#ifndef errno
	    extern int errno;
#endif
#endif
	    udpc_fatal(1, "open outfile %s: %s\n",
		       disk_config->fileName, strerror(errno));
	}
    } else {
#ifdef __MINGW32__
	_setmode(1, O_BINARY);
#endif
    }
    return outFile;
}

int startReceiver(int doWarn,
		  struct disk_config *disk_config,
		  struct net_config *net_config,
		  struct stat_config *stat_config,
		  const char *ifName)
{
    char ipBuffer[16];
    union serverControlMsg Msg;
    int connectReqSent=0;
    struct client_config client_config;
    int outFile=1;
    int pipedOutFile;
    struct sockaddr_in myIp;
    int pipePid = 0;
    int origOutFile;
    int haveServerAddress;
    int ret=0;

    client_config.sender_is_newgen = 0;

    net_config->net_if = getNetIf(ifName);
    zeroSockArray(client_config.socks, NR_CLIENT_SOCKS);

    client_config.S_UCAST = makeSocket(ADDR_TYPE_UCAST,
				       net_config->net_if,
				       0, RECEIVER_PORT(net_config->portBase));
    client_config.S_BCAST = makeSocket(ADDR_TYPE_BCAST,
				       net_config->net_if,
				       0, RECEIVER_PORT(net_config->portBase));

    if(net_config->ttl == 1 && net_config->mcastRdv == NULL) {
	getBroadCastAddress(net_config->net_if,
			    &net_config->controlMcastAddr,
			    SENDER_PORT(net_config->portBase));
	setSocketToBroadcast(client_config.S_UCAST);
    } else {
	getMcastAllAddress(&net_config->controlMcastAddr,
			   net_config->mcastRdv,
			   SENDER_PORT(net_config->portBase));
	if(isMcastAddress(&net_config->controlMcastAddr)) {
	    setMcastDestination(client_config.S_UCAST, net_config->net_if,
				&net_config->controlMcastAddr);
	    setTtl(client_config.S_UCAST, net_config->ttl);
	    
	    client_config.S_MCAST_CTRL =
		makeSocket(ADDR_TYPE_MCAST,
			   net_config->net_if,
			   &net_config->controlMcastAddr,
			   RECEIVER_PORT(net_config->portBase));
	    // TODO: subscribe address as receiver to!
	}
    }
    clearIp(&net_config->dataMcastAddr);
    udpc_flprintf("%sUDP receiver for %s at ", 
		  disk_config->pipeName == NULL ? "" :  "Compressed ",
		  disk_config->fileName == NULL ? "(stdout)":disk_config->fileName);
    printMyIp(net_config->net_if);
    udpc_flprintf(" on %s\n", net_config->net_if->name);

    connectReqSent = 0;
    haveServerAddress = 0;

    client_config.clientNumber= 0; /*default number for asynchronous transfer*/
    while(1) {
	// int len;
	int msglen;
	int sock;

	if (!connectReqSent) {
	    if (sendConnectReq(&client_config, net_config,
			       haveServerAddress) < 0) {
		perror("sendto to locate server");
	    }
	    connectReqSent = 1;
	}

	haveServerAddress=0;

	sock = udpc_selectSock(client_config.socks, NR_CLIENT_SOCKS,
			       net_config->startTimeout);
	if(sock < 0) {
		return -1;
	}

	// len = sizeof(server);
	msglen=RECV(sock, 
		    Msg, client_config.serverAddr, net_config->portBase);
	if (msglen < 0) {
	    perror("recvfrom to locate server");
	    exit(1);
	}
	
	if(getPort(&client_config.serverAddr) != 
	   SENDER_PORT(net_config->portBase))
	    /* not from the right port */
	    continue;

	switch(ntohs(Msg.opCode)) {
	    case CMD_CONNECT_REPLY:
		client_config.clientNumber = ntohl(Msg.connectReply.clNr);
		net_config->blockSize = ntohl(Msg.connectReply.blockSize);

		udpc_flprintf("received message, cap=%08lx\n",
			      (long) ntohl(Msg.connectReply.capabilities));
		if(ntohl(Msg.connectReply.capabilities) & CAP_NEW_GEN) {
		    client_config.sender_is_newgen = 1;
		    copyFromMessage(&net_config->dataMcastAddr,
				    Msg.connectReply.mcastAddr);
		}
		if (client_config.clientNumber == -1) {
		    udpc_fatal(1, "Too many clients already connected\n");
		}
		goto break_loop;

	    case CMD_HELLO_STREAMING:
	    case CMD_HELLO_NEW:
	    case CMD_HELLO:
		connectReqSent = 0;
		if(ntohs(Msg.opCode) == CMD_HELLO_STREAMING)
			net_config->flags |= FLAG_STREAMING;
		if(ntohl(Msg.hello.capabilities) & CAP_NEW_GEN) {
		    client_config.sender_is_newgen = 1;
		    copyFromMessage(&net_config->dataMcastAddr,
				    Msg.hello.mcastAddr);
		    net_config->blockSize = ntohs(Msg.hello.blockSize);
		    if(ntohl(Msg.hello.capabilities) & CAP_ASYNC)
			net_config->flags |= FLAG_PASSIVE;
		    if(net_config->flags & FLAG_PASSIVE)
			goto break_loop;
		}
		haveServerAddress=1;
		continue;
	    case CMD_CONNECT_REQ:
	    case CMD_DATA:
	    case CMD_FEC:
		continue;
	    default:
		break;
	}


	udpc_fatal(1, 
		   "Bad server reply %04x. Other transfer in progress?\n",
		   (unsigned short) ntohs(Msg.opCode));
    }

 break_loop:
    udpc_flprintf("Connected as #%d to %s\n", 
		  client_config.clientNumber, 
		  getIpString(&client_config.serverAddr, ipBuffer));

    getMyAddress(net_config->net_if, &myIp);

    if(!ipIsZero(&net_config->dataMcastAddr)  &&
       !ipIsEqual(&net_config->dataMcastAddr, &myIp) &&
       (ipIsZero(&net_config->controlMcastAddr) ||
       !ipIsEqual(&net_config->dataMcastAddr, &net_config->controlMcastAddr)
	)) {
	udpc_flprintf("Listening to multicast on %s\n",
		      getIpString(&net_config->dataMcastAddr, ipBuffer));
	client_config.S_MCAST_DATA = 
	  makeSocket(ADDR_TYPE_MCAST, net_config->net_if, 
		     &net_config->dataMcastAddr, 
		     RECEIVER_PORT(net_config->portBase));
    }


    if(net_config->requestedBufSize) {
      int i;
      for(i=0; i<NR_CLIENT_SOCKS; i++)
	if(client_config.socks[i] != -1)
	  setRcvBuf(client_config.socks[i],net_config->requestedBufSize);
    }

    outFile=openOutFile(disk_config);
    origOutFile = outFile;
    pipedOutFile = openPipe(outFile, disk_config, &pipePid);

    global_client_config= &client_config;
    atexit(sendDisconnectWrapper);
    {
	struct fifo fifo;
	int printUncompressedPos =
	    udpc_shouldPrintUncompressedPos(stat_config->printUncompressedPos,
					    origOutFile, pipedOutFile);

	receiver_stats_t stats = allocReadStats(origOutFile,
						stat_config->statPeriod,
						printUncompressedPos,
						stat_config->noProgress);
	
	udpc_initFifo(&fifo, net_config->blockSize);

	fifo.data = pc_makeProduconsum(fifo.dataBufSize, "receive");

	client_config.isStarted = 0;

	if((net_config->flags & (FLAG_PASSIVE|FLAG_NOKBD))) {
	  /* No console used */
	  client_config.console = NULL;
	} else {
	  if(doWarn)
	    udpc_flprintf("WARNING: This will overwrite the hard disk of this machine\n");
	  client_config.console = prepareConsole(0);
	  atexit(fixConsole);
	}

	spawnNetReceiver(&fifo,&client_config, net_config, stats);
	writer(&fifo, pipedOutFile);
	if(pipePid) {
	    close(pipedOutFile);
	}
	pthread_join(client_config.thread, NULL);

	/* if we have a pipe, now wait for that too */
	if(pipePid) {
	    ret=udpc_waitForProcess(pipePid, "Pipe");
	}
#ifndef __MINGW32__
	fsync(origOutFile);
#endif /* __MINGW32__ */
	displayReceiverStats(stats, 1);

    }
    fixConsole();
    sendDisconnectWrapper();
    global_client_config= NULL;
    return ret;
}
