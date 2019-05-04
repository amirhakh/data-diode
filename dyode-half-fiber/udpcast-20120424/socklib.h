#ifndef SOCKLIB_H
#define SOCKLIB_H

#ifndef UDPCAST_CONFIG_H
# define UDPCAST_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <string.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef __MINGW32__
#define WINDOWS
#undef USE_SYSLOG
#endif /* __MINGW32__ */

#ifdef __CYGWIN__
/* Untested so far ... */
#define WINDOWS
#endif

#define RECEIVER_PORT(x) (x)
#define SENDER_PORT(x) ((x)+1)

#define loseSendPacket udpc_loseSendPacket
#define loseRecvPacket udpc_loseRecvPacket
#define setWriteLoss udpc_setWriteLoss
#define setReadLoss udpc_setReadLoss
#define setReadSwap udpc_setReadSwap
#define srandomTime udpc_srandomTime
#define RecvMsg udpc_RecvMsg
#define doAutoRateLimit udpc_doAutoRateLimit
#define makeSockAddr udpc_makeSockAddr
#define getMyAddress udpc_getMyAddress
#define getBroadCastAddress udpc_getBroadCastAddress
#define getMcastAllAddress udpc_getMcastAllAddress
#define doSend udpc_doSend
#define doReceive udpc_doReceive
#define printMyIp udpc_printMyIp
#define makeSocket udpc_makeSocket
#define setSocketToBroadcast udpc_setSocketToBroadcast
#define setTtl udpc_setTtl
#define setMcastDestination udpc_setMcastDestination
#define isFullDuplex udpc_isFullDuplex
#define getNetIf udpc_getNetIf
#define getSendBuf udpc_getSendBuf
#define setSendBuf udpc_setSendBuf
#define getRcvBuf udpc_getRcvBuf
#define setRcvBuf udpc_setRcvBuf
#define getPort udpc_getPort
#define setPort udpc_setPort
#define getIpString udpc_getIpString
#define ipIsEqual udpc_ipIsEqual
#define ipIsZero udpc_ipIsZero
#define clearIp udpc_clearIp
#define setIpFromString udpc_setIpFromString
#define copyIpFrom udpc_copyIpFrom
#define getDefaultMcastAddress udpc_getDefaultMcastAddress
#define copyToMessage udpc_copyToMessage
#define copyFromMessage udpc_copyFromMessage
#define isAddressEqual udpc_isAddressEqual
#define parseSize udpc_parseSize


#define zeroSockArray udpc_zeroSockArray
#define selectSock udpc_selectSock
#define prepareForSelect udpc_prepareForSelect
#define getSelectedSock udpc_getSelectedSock
#define closeSock udpc_closeSock


#ifdef LOSSTEST
int loseSendPacket(void);
void loseRecvPacket(int s);
void setWriteLoss(char *l);
void setReadLoss(char *l);
void setReadSwap(char *l);
void srandomTime(int printSeed);
int RecvMsg(int s, struct msghdr *msg, int flags);
#endif

struct net_if {
    struct in_addr addr;
    struct in_addr bcast;
    const char *name;
#ifdef SIOCGIFINDEX
    int index;
#endif
};
typedef struct net_if net_if_t;

typedef enum addr_type_t {
  ADDR_TYPE_UCAST,
  ADDR_TYPE_MCAST,
  ADDR_TYPE_BCAST
} addr_type_t;

void doAutoRateLimit(int sock, int dir, int qsize, int size);

int makeSockAddr(char *hostname, short port, struct sockaddr_in *addr);

int getMyAddress(net_if_t *net_if, struct sockaddr_in *addr);
int getBroadCastAddress(net_if_t *net_if, struct sockaddr_in *addr, short port);
int getMcastAllAddress(struct sockaddr_in *addr, const char *address, short port);


int doSend(int s, void *message, size_t len, struct sockaddr_in *to);
int doReceive(int s, void *message, size_t len,
	      struct sockaddr_in *from, int portBase);

void printMyIp(net_if_t *net_if);


int makeSocket(addr_type_t addr_type, net_if_t *net_if, 
	       struct sockaddr_in *tmpl, int port);

int setSocketToBroadcast(int sock);
int setTtl(int sock, int ttl);

int setMcastDestination(int,net_if_t *,struct sockaddr_in *);
int isFullDuplex(int sock, const char *ifName);
net_if_t *getNetIf(const char *ifName);

int getSendBuf(int sock);
void setSendBuf(int sock, unsigned int bufsize);
unsigned int getRcvBuf(int sock);
void setRcvBuf(int sock, unsigned int bufsize);


#define SEND(s, msg, to) \
	doSend(s, &msg, sizeof(msg), &to)

#define RECV(s, msg, from, portBase ) \
	doReceive((s), &msg, sizeof(msg), &from, (portBase) )

#define BCAST_CONTROL(s, msg) \
	doSend(s, &msg, sizeof(msg), &net_config->controlMcastAddr)

unsigned short getPort(struct sockaddr_in *addr);
void setPort(struct sockaddr_in *addr, unsigned short port);
char *getIpString(struct sockaddr_in *addr, char *buffer);
int ipIsEqual(struct sockaddr_in *left, struct sockaddr_in *right);
int ipIsZero(struct sockaddr_in *ip);

void clearIp(struct sockaddr_in *addr);
void setIpFromString(struct sockaddr_in *addr, char *ip);

void copyIpFrom(struct sockaddr_in *dst, struct sockaddr_in *src);

void getDefaultMcastAddress(net_if_t *net_if, struct sockaddr_in *mcast);

void copyToMessage(unsigned char *dst, struct sockaddr_in *src);
void copyFromMessage(struct sockaddr_in *dst, unsigned char *src);

int isAddressEqual(struct sockaddr_in *a, struct sockaddr_in *b);

unsigned long parseSize(char *sizeString);

void zeroSockArray(int *socks, int nr);
int selectSock(int *socks, int nr, int startTimeout);
int prepareForSelect(int *socks, int nr, fd_set *read_set);
int getSelectedSock(int *socks, int nr, fd_set *read_set);
void closeSock(int *socks, int nr, int target);

int isMcastAddress(struct sockaddr_in *addr);

int udpc_socklibFatal(int code);

#ifdef __MINGW32__ /* __MINGW32__ */

struct iovec {
    void *iov_base;
    int iov_len;
};
struct msghdr {
    void *msg_name;
    int msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;

};

ssize_t sendmsg(int s, const struct msghdr *msg, int flags);
ssize_t recvmsg (int fd, struct msghdr *msg, int flags);

#define usleep(x) Sleep((x)/1000)
#define sleep(x) Sleep(1000L*(x))
#endif /* __MINGW32__ */

static inline void initMsgHdr(struct msghdr *hdr) {
#ifndef WINDOWS
    hdr->msg_control = 0;
    hdr->msg_controllen = 0;
    hdr->msg_flags = 0;
#endif
}

#ifndef __MINGW32__
#undef closesocket
#define closesocket(x) close(x)
#endif

#ifndef HAVE_IN_ADDR_T
typedef unsigned long in_addr_t;
#endif

#endif
