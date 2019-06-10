#include "config.h"

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <fcntl.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef __MINGW32__

#include <ws2tcpip.h>
#define ioctl ioctlsocket

#include <iprtrmib.h>
#include <iphlpapi.h>

#endif /* __MINGW32__ */

#include "log.h"
#include "socklib.h"
#include "util.h"

#ifndef SOL_IP
#define SOL_IP IPPROTO_IP
#endif

#ifdef __linux__

#include <linux/types.h>

#include <linux/ethtool.h>
#include <linux/sockios.h>

#endif

// for Darwin
#ifdef _SIZEOF_ADDR_IFREQ
#define IFREQ_SIZE(a) _SIZEOF_ADDR_IFREQ(a)
#endif

#ifndef DEBUG
#define DEBUG 0
#endif

#ifdef LOSSTEST
/**
 * Packet loss/swap testing...
 */
long int write_loss = 0;
long int read_loss = 0;
long int read_swap = 0;
unsigned int seed = 0;

int loseSendPacket(void)
{
    if (write_loss)
    {
        long r = random();
        if (r < write_loss)
            return 1;
    }
    return 0;
}

#define STASH_SIZE 64

static int stashed;
static struct packetStash
{
    unsigned char data[4092];
    int size;
} packetStash[STASH_SIZE];

/**
 * Lose a packet
 */
void loseRecvPacket(int s)
{
    if (read_loss)
    {
        while (random() < read_loss)
        {
            int x;
            flprintf("Losing packet\n");
            recv(s, (void *)&x, sizeof(x), 0);
        }
    }
    if (read_swap)
    {
        while (stashed < STASH_SIZE && random() < read_swap)
        {
            int size;
            flprintf("Stashing packet %d\n", stashed);
            size = recv(s, packetStash[stashed].data,
                        sizeof(packetStash[stashed].data), 0);
            packetStash[stashed].size = size;
            stashed++;
        }
    }
}

/**
 * bring stored packet back up...
 */
int RecvMsg(int s, struct msghdr *msg, int flags)
{
    if (read_swap && stashed)
    {
        if (random() / stashed < read_swap)
        {
            int slot = random() % stashed;
            int iovnr;
            char *data = packetStash[slot].data;
            int totalLen = packetStash[slot].size;
            int retBytes = 0;
            flprintf("Bringing out %d\n", slot);
            for (iovnr = 0; iovnr < msg->msg_iovlen; iovnr++)
            {
                int len = msg->msg_iov[iovnr].iov_len;
                if (len > totalLen)
                    len = totalLen;
                memcpy(msg->msg_iov[iovnr].iov_base, data, len);
                totalLen -= len;
                data += len;
                retBytes += len;
                if (totalLen == 0)
                    break;
            }
            packetStash[slot] = packetStash[stashed];
            stashed--;
            return retBytes;
        }
    }
    return recvmsg(s, msg, flags);
}

void setWriteLoss(char *l)
{
    write_loss = (long)(atof(l) * RAND_MAX);
}

void setReadLoss(char *l)
{
    read_loss = (long)(atof(l) * RAND_MAX);
}

void setReadSwap(char *l)
{
    read_swap = (long)(atof(l) * RAND_MAX);
}

void srandomTime(int printSeed)
{
    struct timeval tv;
    long seed;
    gettimeofday(&tv, 0);
    seed = (tv.tv_usec * 2000) ^ tv.tv_sec;
    if (printSeed)
        flprintf("seed=%ld\n", seed);
    srandom(seed);
}
#endif

/* makes a socket address */
int makeSockAddr(char *hostname, unsigned short port, struct sockaddr_in *addr)
{
    struct hostent *host;

    memset((char *)addr, 0, sizeof(struct sockaddr_in));
    if (hostname && *hostname)
    {
        char *inaddr;
        int len;

        host = gethostbyname(hostname);
        if (host == NULL)
        {
            udpc_fatal(1, "Unknown host %s\n", hostname);
        }

        inaddr = host->h_addr_list[0];
        len = host->h_length;
        memcpy((void *)&((struct sockaddr_in *)addr)->sin_addr, inaddr, (size_t)len);
    }

    ((struct sockaddr_in *)addr)->sin_family = AF_INET;
    ((struct sockaddr_in *)addr)->sin_port = htobe16(port);
    return 0;
}

#ifdef HAVE_PTON
#define INET_ATON(a, i) inet_pton(AF_INET, a, i)
#else
#ifdef HAVE_ATON
#define INET_ATON(a, i) inet_aton(a, i)
#else

#ifndef INADDR_NONE
#define INADDR_NONE ((in_addr_t)-1)
#endif

static inline int INET_ATON(const char *a, struct in_addr *i)
{
    i->s_addr = inet_addr(a);
    if (i->s_addr == INADDR_NONE && strcmp(a, "255.255.255.255"))
        return 0; /* Address invalid */
    return 1;	 /* Address valid */
}
#endif
#endif

static int initSockAddress(addr_type_t addr_type,
                           net_if_t *net_if,
                           in_addr_t ip,
                           unsigned short port,
                           struct sockaddr_in *addr)
{
    memset((char *)addr, 0, sizeof(struct sockaddr_in));
    addr->sin_family = AF_INET;
    addr->sin_port = htobe16(port);

    if (!net_if && addr_type != ADDR_TYPE_MCAST)
        udpc_fatal(1, "initSockAddr without ifname\n");

    switch (addr_type)
    {
    case ADDR_TYPE_UCAST:
        addr->sin_addr = net_if->addr;
        break;
    case ADDR_TYPE_BCAST:
        addr->sin_addr = net_if->bcast;
        break;
    case ADDR_TYPE_MCAST:
        addr->sin_addr.s_addr = ip;
        break;
    }
    return 0;
}

int getMyAddress(net_if_t *net_if, struct sockaddr_in *addr)
{
    return initSockAddress(ADDR_TYPE_UCAST, net_if, INADDR_ANY, 0, addr);
}

int getBroadCastAddress(net_if_t *net_if, struct sockaddr_in *addr,
                        unsigned short port)
{
    int r = initSockAddress(ADDR_TYPE_BCAST, net_if, INADDR_ANY, port, addr);
    if (addr->sin_addr.s_addr == 0)
    {
        /* Quick hack to make it work for loopback */
        struct sockaddr_in ucast;
        initSockAddress(ADDR_TYPE_UCAST, net_if, INADDR_ANY, port, &ucast);

        if ((be32toh(ucast.sin_addr.s_addr) & 0xff000000) == 0x7f000000)
            addr->sin_addr.s_addr = ucast.sin_addr.s_addr;
    }
    return r;
}

static int mcastListen(int sock, net_if_t *net_if, struct sockaddr_in *addr);

static int safe_inet_aton(const char *address, struct in_addr *ip)
{
    if (!INET_ATON(address, ip))
        udpc_fatal(-1, "Bad address %s", address);
    return 0;
}

int getMcastAllAddress(struct sockaddr_in *addr, const char *address,
                       unsigned short port)
{
    struct in_addr ip;
    int ret;

    if (address == NULL || address[0] == '\0')
        safe_inet_aton("224.0.0.1", &ip);
    else
    {
        if ((ret = safe_inet_aton(address, &ip)) < 0)
            return ret;
    }
    return initSockAddress(ADDR_TYPE_MCAST, NULL, ip.s_addr, port, addr);
}

ssize_t doSend(int s, void *message, size_t len, struct sockaddr_in *to)
{
    /*    flprintf("sent: %08x %d\n", *(int*) message, len);*/
#ifdef LOSSTEST
    loseSendPacket();
#endif
    return sendto(s, message, len, 0, (struct sockaddr *)to, sizeof(*to));
}

ssize_t doReceive(int s, void *message, size_t len,
                  struct sockaddr_in *from, int portBase)
{
    socklen_t slen;
    ssize_t r;
    unsigned short port;
    char ipBuffer[16];

    slen = sizeof(*from);
#ifdef LOSSTEST
    loseRecvPacket(s);
#endif
    r = recvfrom(s, message, len, 0, (struct sockaddr *)from, &slen);
    if (r < 0)
        return r;
    port = be16toh(from->sin_port);
    if (port != RECEIVER_PORT(portBase) && port != SENDER_PORT(portBase))
    {
        udpc_flprintf("Bad message from port %s.%d\n",
                      getIpString(from, ipBuffer),
                      be16toh(((struct sockaddr_in *)from)->sin_port));
        return -1;
    }
    /*    flprintf("recv: %08x %d\n", *(int*) message, r);*/
    return r;
}

unsigned int getSendBuf(int sock)
{
    unsigned int bufsize;
    socklen_t len = sizeof(int);
    if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, &len) < 0)
        return 0;
    return bufsize;
}

void setSendBuf(int sock, unsigned int bufsize)
{
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, sizeof(bufsize)) < 0)
        perror("Set send buffer");
}

unsigned int getRcvBuf(int sock)
{
    unsigned int bufsize;
    socklen_t len = sizeof(int);
    if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *)&bufsize, &len) < 0)
        return 0;
    return bufsize;
}

void setRcvBuf(int sock, unsigned int bufsize)
{
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                   (char *)&bufsize, sizeof(bufsize)) < 0)
        perror("Set receiver buffer");
}

int setSocketToBroadcast(int sock)
{
    /* set the socket to broadcast */
    int p = 1;
    return setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&p, sizeof(int));
}

int setTtl(int sock, int ttl)
{
    /* set the socket to broadcast */
    return setsockopt(sock, SOL_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof(int));
}

#ifdef HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
#define IP_MREQN ip_mreqn
#else
#define IP_MREQN ip_mreq
#endif

#define getSinAddr(addr) (((struct sockaddr_in *)addr)->sin_addr)

/**
 * Fill in the mreq structure with the given interface and address
 */
static int fillMreq(net_if_t *net_if, struct in_addr addr,
                    struct IP_MREQN *mreq)
{
#ifdef HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
    mreq->imr_ifindex = net_if->index;
    mreq->imr_address.s_addr = 0;
#else
    mreq->imr_interface = net_if->addr;
#endif
    mreq->imr_multiaddr = addr;

    return 0;
}

/**
 * Perform a multicast operation
 */
static int mcastOp(int sock, net_if_t *net_if, struct in_addr addr,
                   int code, const char *message)
{
    struct IP_MREQN mreq;
    int r;

    fillMreq(net_if, addr, &mreq);
    r = setsockopt(sock, SOL_IP, code, (char *)&mreq, sizeof(mreq));
    if (r < 0)
    {
        perror(message);
        exit(1);
    }
    return 0;
}

/*
struct in_addr getSinAddr(struct sockaddr_in *addr) {
    return ((struct sockaddr_in *) addr)->sin_addr;
}
*/

/**
 * Set socket to listen on given multicast address Not 100% clean, it
 * would be preferable to make a new socket, and not only subscribe it
 * to the multicast address but also _bind_ to it. Indeed, subscribing
 * alone is not enough, as we may get traffic destined to multicast
 * address subscribed to by other apps on the machine. However, for
 * the moment, we skip this concern, as udpcast's main usage is
 * software installation, and in that case it runs on an otherwise
 * quiet system.
 */
static int mcastListen(int sock, net_if_t *net_if, struct sockaddr_in *addr)
{
    return mcastOp(sock, net_if, getSinAddr(addr), IP_ADD_MEMBERSHIP,
                   "Subscribe to multicast group");
}

int setMcastDestination(int sock, net_if_t *net_if, struct sockaddr_in *addr)
{
#ifdef WINDOWS
    int r;
    struct sockaddr_in interface_addr;
    struct in_addr if_addr;
    getMyAddress(net_if, &interface_addr);
    if_addr = getSinAddr(&interface_addr);
    r = setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF,
                   (char *)&if_addr, sizeof(if_addr));
    if (r < 0)
        fatal(1, "Set multicast send interface");
    return 0;
#else
    /* IP_MULTICAST_IF not correctly supported on Cygwin */
    return mcastOp(sock, net_if, getSinAddr(addr), IP_MULTICAST_IF,
                   "Set multicast send interface");
#endif
}

#ifdef __MINGW32__
static MIB_IFROW *getIfRow(MIB_IFTABLE *iftab, DWORD dwIndex)
{
    int j;

    /* Find the corresponding interface row (for name and
     * MAC address) */
    for (j = 0; j < iftab->dwNumEntries; j++)
    {
        MIB_IFROW *ifrow = &iftab->table[j];
        /* eth0, eth1, ... */
        if (ifrow->dwIndex == dwIndex)
            return ifrow;
    }
    return NULL;
}

static char *fmtName(MIB_IFROW *ifrow)
{
    char *out;
    int l = ifrow->dwDescrLen + 1;
    if (ifrow->dwPhysAddrLen)
        l += 2 + 3 * ifrow->dwPhysAddrLen;
    out = malloc(ifrow->dwDescrLen + l + 1);
    if (!out)
        return NULL;
    memcpy(out, ifrow->bDescr, ifrow->dwDescrLen);
    out[ifrow->dwDescrLen] = '\0';

    if (ifrow->dwPhysAddrLen)
    {
        int k;
        char *ptr = out + strlen(out);
        strcpy(ptr, " (");
        ptr += 2;
        for (k = 0; k < ifrow->dwPhysAddrLen; k++)
        {
            if (k)
                *ptr++ = '-';
            sprintf(ptr, "%02x", 255 & ifrow->bPhysAddr[k]);
            ptr += 2;
        }
        strcpy(ptr, ")");
    }
    return out;
}
#endif /* __MINGW32__ */

#ifndef __MINGW32__
/**
 * Tests whether the given card has link
 * 0 no
 * 1 yes
 * -1 unknown
 */
static unsigned int hasLink(int s, const char *ifname)
{

#ifdef ETHTOOL_GLINK
    struct ifreq ifr;
    struct ethtool_value edata;

    edata.cmd = ETHTOOL_GLINK;

    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_data = (char *)&edata;

    if (ioctl(s, SIOCETHTOOL, &ifr) == -1)
    {
        /* Operation not supported */
        return 0;
    }
    else
    {
        return edata.data;
    }
#else
    return 0;
#endif
}
#endif

/**
 * Tests whether the given card operates in full duplex mode
 * 0 no
 * 1 yes
 * -1 unknown
 */
int isFullDuplex(int s, const char *ifname)
{

#ifdef ETHTOOL_GLINK
    struct ifreq ifr;
    struct ethtool_cmd ecmd;

    ecmd.cmd = ETHTOOL_GSET;

    strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
    ifr.ifr_data = (char *)&ecmd;

    if (ioctl(s, SIOCETHTOOL, &ifr) == -1)
    {
        /* Operation not supported */
        return -1;
    }
    else
    {
        return ecmd.duplex;
    }
#else
    return -1;
#endif
}

/**
 * Canonize interface name. If attempt is not NULL, pick the interface
 * which has that address.
 * If attempt is NULL, pick interfaces in the following order of preference
 * 1. eth0
 * 2. Anything starting with eth0:
 * 3. Anything starting with eth
 * 4. Anything else
 * 5. localhost
 * 6. zero address
 */
net_if_t *getNetIf(const char *wanted)
{
#ifndef __MINGW32__
    struct ifreq *ifrp, *ifend, *chosen;
    struct ifconf ifc;
    int s;
#else  /* __MINGW32__ */
    int i;

    int etherNo = -1;
    int wantedEtherNo = -2; /* Wanted ethernet interface */

    MIB_IPADDRTABLE *iptab = NULL;
    MIB_IFTABLE *iftab = NULL;

    MIB_IPADDRROW *iprow, *chosen = NULL;
    MIB_IFROW *chosenIf = NULL;
    WORD wVersionRequested; /* Version of Winsock to load */
    WSADATA wsaData;		/* Winsock implementation details */
    ULONG a;

    int r;
#endif /* __MINGW32__ */

    int lastGoodness = 0;
    struct in_addr wantedAddress;
    int isAddress = 0;
    size_t wantedLen = 0;
    net_if_t *net_if;

    if (wanted == NULL)
    {
        wanted = getenv("IFNAME");
    }

    if (wanted && INET_ATON(wanted, &wantedAddress))
        isAddress = 1;
    else
        wantedAddress.s_addr = 0;

    if (wanted)
        wantedLen = strlen(wanted);

    net_if = MALLOC(net_if_t);
    if (net_if == NULL)
        udpc_fatal(1, "Out of memory error");

#ifndef __MINGW32__

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s < 0)
    {
        perror("make socket");
        exit(1);
    }

    ifc.ifc_len = sizeof(struct ifreq) * 10;
    while (1)
    {
        int len = ifc.ifc_len;
        ifc.ifc_buf = (caddr_t)malloc((size_t)ifc.ifc_len);
        if (ifc.ifc_buf == NULL)
        {
            udpc_fatal(1, "Out of memory error");
        }

        if (ioctl(s, SIOCGIFCONF, (char *)&ifc) < 0 ||
                ifc.ifc_len < (signed int)sizeof(struct ifreq))
        {
            perror("udpcast: SIOCGIFCONF: ");
            exit(1);
        }

        if (len == ifc.ifc_len)
        {
            ifc.ifc_len += sizeof(struct ifreq) * 10;
            free(ifc.ifc_buf);
        }
        else
            break;
    }

    ifend = (struct ifreq *)((char *)ifc.ifc_buf + ifc.ifc_len);
    chosen = NULL;

    for (ifrp = (struct ifreq *)ifc.ifc_buf; ifrp < ifend;
     #ifdef IFREQ_SIZE
         ifrp = IFREQ_SIZE(*ifrp) + (char *)ifrp
     #else
         ifrp++
     #endif
         )
    {
        in_addr_t iaddr = getSinAddr(&ifrp->ifr_addr).s_addr;
        int goodness;

        if (ifrp->ifr_addr.sa_family != PF_INET)
            continue;

        if (wanted)
        {
            if (isAddress && iaddr == wantedAddress.s_addr)
            {
                goodness = 8;
            }
            else if (strcmp(wanted, ifrp->ifr_name) == 0)
            {
                /* perfect match on interface name */
                goodness = 12;
            }
            else if (wanted != NULL &&
                     strncmp(wanted, ifrp->ifr_name, wantedLen) == 0)
            {
                /* prefix match on interface name */
                goodness = 7;
            }
            else
            {
                /* no match, try next */
                continue;
            }
        }
        else
        {
            if (iaddr == 0)
            {
                /* disregard interfaces whose address is zero */
                goodness = 1;
            }
            else if (iaddr == htobe32(0x7f000001))
            {
                /* disregard localhost type devices */
                goodness = 2;
            }
            else if (strcmp("eth0", ifrp->ifr_name) == 0 ||
                     strcmp("en0", ifrp->ifr_name) == 0)
            {
                /* prefer first ethernet interface */
                goodness = 6;
            }
            else if (strncmp("eth0:", ifrp->ifr_name, 5) == 0)
            {
                /* second choice: any secondary addresses of first ethernet */
                goodness = 5;
            }
            else if (strncmp("eth", ifrp->ifr_name, 3) == 0 ||
                     strncmp("en", ifrp->ifr_name, 2) == 0)
            {
                /* and, if not available, any other ethernet device */
                goodness = 4;
            }
            else
            {
                goodness = 3;
            }
        }

        if (hasLink(s, ifrp->ifr_name))
            /* Good or unknown link status privileged over known
           * disconnected */
            goodness += 3;

        /* If all else is the same, prefer interfaces that
         * have broadcast */
        goodness = goodness * 2;
        if (goodness >= lastGoodness)
        {
            /* Privilege broadcast-enabled interfaces */
            if (ioctl(s, SIOCGIFBRDADDR, ifrp) < 0)
                udpc_fatal(-1, "Error getting broadcast address for %s: %s",
                           ifrp->ifr_name, strerror(errno));
            if (getSinAddr(&ifrp->ifr_ifru.ifru_broadaddr).s_addr)
                goodness++;
        }

        if (goodness > lastGoodness)
        {
            chosen = ifrp;
            lastGoodness = goodness;
            net_if->addr.s_addr = iaddr;
        }
    }

    if (!chosen)
    {
        fprintf(stderr, "No suitable network interface found\n");
        fprintf(stderr, "The following interfaces are available:\n");

        for (ifrp = (struct ifreq *)ifc.ifc_buf; ifrp < ifend;
     #ifdef IFREQ_SIZE
             ifrp = IFREQ_SIZE(*ifrp) + (char *)ifrp
     #else
             ifrp++
     #endif
             )
        {
            char buffer[16];

            if (ifrp->ifr_addr.sa_family != PF_INET)
                continue;

            fprintf(stderr, "\t%s\t%s\n",
                    ifrp->ifr_name,
                    udpc_getIpString((struct sockaddr_in *)&ifrp->ifr_addr, buffer));
        }
        exit(1);
    }

    net_if->name = strdup(chosen->ifr_name);

#ifdef HAVE_STRUCT_IP_MREQN_IMR_IFINDEX
    /* Index for multicast subscriptions */
    if (ioctl(s, SIOCGIFINDEX, chosen) < 0)
        udpc_fatal(-1, "Error getting index for %s: %s", net_if->name,
                   strerror(errno));
    net_if->index = chosen->ifr_ifindex;
#endif

    /* Broadcast */
    if (ioctl(s, SIOCGIFBRDADDR, chosen) < 0)
        udpc_fatal(-1, "Error getting broadcast address for %s: %s",
                   net_if->name, strerror(errno));
    net_if->bcast = getSinAddr(&chosen->ifr_ifru.ifru_broadaddr);

    close(s);
    free(ifc.ifc_buf);

#else  /* __MINGW32__ */

    /* WINSOCK initialization */
    wVersionRequested = MAKEWORD(2, 0);				  /* Request Winsock v2.0 */
    if (WSAStartup(wVersionRequested, &wsaData) != 0) /* Load Winsock DLL */
    {
        fprintf(stderr, "WSAStartup() failed");
        exit(1);
    }
    /* End WINSOCK initialization */

    a = 0;
    r = GetIpAddrTable(iptab, &a, TRUE);
    iptab = malloc(a);
    r = GetIpAddrTable(iptab, &a, TRUE);

    a = 0;
    r = GetIfTable(iftab, &a, TRUE);
    iftab = malloc(a);
    r = GetIfTable(iftab, &a, TRUE);

    if (wanted && !strncmp(wanted, "eth", 3) && wanted[3])
    {
        char *ptr;
        int n = strtoul(wanted + 3, &ptr, 10);
        if (!*ptr)
            wantedEtherNo = n;
    }

    for (i = 0; i < iptab->dwNumEntries; i++)
    {
        int goodness = -1;
        unsigned long iaddr;
        int isEther = 0;
        MIB_IFROW *ifrow;

        iprow = &iptab->table[i];
        iaddr = iprow->dwAddr;

        ifrow = getIfRow(iftab, iprow->dwIndex);

        if (ifrow && ifrow->dwPhysAddrLen == 6 && iprow->dwBCastAddr)
        {
            isEther = 1;
            etherNo++;
        }

        if (wanted)
        {
            if (isAddress && iaddr == wantedAddress.s_addr)
            {
                goodness = 8;
            }
            else if (isEther && wantedEtherNo == etherNo)
            {
                goodness = 9;
            }
            else if (ifrow->dwPhysAddrLen)
            {
                int j;
                const char *ptr = wanted;
                for (j = 0; *ptr && j < ifrow->dwPhysAddrLen; j++)
                {
                    int digit = strtoul(ptr, (char **)&ptr, 16);
                    if (digit != ifrow->bPhysAddr[j])
                        break; /* Digit mismatch */
                    if (*ptr == '-' || *ptr == ':')
                    {
                        ptr++;
                    }
                }
                if (!*ptr && j == ifrow->dwPhysAddrLen)
                {
                    goodness = 9;
                }
            }
        }
        else
        {
            if (iaddr == 0)
            {
                /* disregard interfaces whose address is zero */
                goodness = 1;
            }
            else if (iaddr == htobe32(0x7f000001))
            {
                /* disregard localhost type devices */
                goodness = 2;
            }
            else if (isEther)
            {
                /* prefer ethernet */
                goodness = 6;
            }
            else if (ifrow->dwPhysAddrLen)
            {
                /* then prefer interfaces which have a physical address */
                goodness = 4;
            }
            else
            {
                goodness = 3;
            }
        }

        goodness = goodness * 2;
        /* If all else is the same, prefer interfaces that
         * have broadcast */
        if (goodness >= lastGoodness)
        {
            /* Privilege broadcast-enabled interfaces */
            if (iprow->dwBCastAddr)
                goodness++;
        }

        if (goodness > lastGoodness)
        {
            chosen = iprow;
            chosenIf = ifrow;
            lastGoodness = goodness;
        }
    }

    if (!chosen)
    {
        fprintf(stderr, "No suitable network interface found%s%s\n",
                wanted ? " for " : "", wanted ? wanted : "");
        fprintf(stderr, "The following interfaces are available:\n");

        for (i = 0; i < iptab->dwNumEntries; i++)
        {
            char buffer[16];
            struct sockaddr_in addr;
            MIB_IFROW *ifrow;
            char *name = NULL;
            iprow = &iptab->table[i];
            addr.sin_addr.s_addr = iprow->dwAddr;
            ifrow = getIfRow(iftab, iprow->dwIndex);
            name = fmtName(ifrow);
            fprintf(stderr, " %15s  %s\n",
                    udpc_getIpString(&addr, buffer),
                    name ? name : "");
            if (name)
                free(name);
        }
        exit(1);
    }

    net_if->bcast.s_addr = net_if->addr.s_addr = chosen->dwAddr;
    if (chosen->dwBCastAddr)
        net_if->bcast.s_addr |= ~chosen->dwMask;
    if (chosenIf)
    {
        net_if->name = fmtName(chosenIf);
    }
    else
    {
        net_if->name = "*";
    }
    free(iftab);
    free(iptab);
#endif /* __MINGW32__ */

    return net_if;
}

/**
 * @param addr_type
 *   UCAST - my unicast address attached to this device
 *   BCAST - my broadcast addres attached to this device
 *   MCAST - multicast address
 * @param ifname
 *   Interface name
 * @param mcast
 *   Multicast address (only used if type MCAST)
 * @param port
 *   Port to bind address to
 */
int makeSocket(addr_type_t addr_type,
               net_if_t *net_if,
               struct sockaddr_in *tmpl,
               unsigned short port)
{
    int ret, s;
    struct sockaddr_in myaddr;
    in_addr_t ip = 0;

#ifdef WINDOWS
    static int lastSocket = -1;
    /* Very ugly hack, but hey!, this is for Windows */

    if (addr_type == ADDR_TYPE_MCAST)
    {
        mcastListen(lastSocket, net_if, tmpl);
        return -1;
    }
    else if (addr_type != ADDR_TYPE_UCAST)
        return -1;
#endif

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s < 0)
    {
        perror("make socket");
        exit(1);
    }

    if (addr_type == ADDR_TYPE_MCAST && tmpl != NULL)
    {
        ip = tmpl->sin_addr.s_addr;
    }

    ret = initSockAddress(addr_type, net_if, ip, port, &myaddr);
    if (ret < 0)
        udpc_fatal(1, "Could not get socket address fot %d/%s",
                   addr_type, net_if->name);
    if (addr_type == ADDR_TYPE_BCAST && myaddr.sin_addr.s_addr == 0)
    {
        /* Attempting to bind to broadcast address on not-broadcast media ... */
        closesocket(s);
        return -1;
    }
    ret = bind(s, (struct sockaddr *)&myaddr, sizeof(myaddr));
    if (ret < 0)
    {
        char buffer[16];
        udpc_fatal(1, "bind socket to %s:%d (%s)\n",
                   udpc_getIpString(&myaddr, buffer),
                   udpc_getPort(&myaddr),
                   strerror(errno));
    }

    if (addr_type == ADDR_TYPE_MCAST)
        mcastListen(s, net_if, &myaddr);
#ifdef WINDOWS
    lastSocket = s;
#endif
    return s;
}

void printMyIp(net_if_t *net_if)
{
    char buffer[16];
    struct sockaddr_in myaddr;

    getMyAddress(net_if, &myaddr);
    udpc_flprintf("%s", udpc_getIpString(&myaddr, buffer));
}

char *udpc_getIpString(struct sockaddr_in *addr, char *buffer)
{
    long iaddr = htobe32(getSinAddr(addr).s_addr);
    sprintf(buffer, "%ld.%ld.%ld.%ld",
            (iaddr >> 24) & 0xff,
            (iaddr >> 16) & 0xff,
            (iaddr >> 8) & 0xff,
            iaddr & 0xff);
    return buffer;
}

int ipIsEqual(struct sockaddr_in *left, struct sockaddr_in *right)
{
    return getSinAddr(left).s_addr == getSinAddr(right).s_addr;
}

int ipIsZero(struct sockaddr_in *ip)
{
    return getSinAddr(ip).s_addr == 0;
}

unsigned short udpc_getPort(struct sockaddr_in *addr)
{
    return be16toh(((struct sockaddr_in *)addr)->sin_port);
}

void setPort(struct sockaddr_in *addr, unsigned short port)
{
    ((struct sockaddr_in *)addr)->sin_port = htobe16(port);
}

void clearIp(struct sockaddr_in *addr)
{
    addr->sin_addr.s_addr = 0;
    addr->sin_family = AF_INET;
}

void setIpFromString(struct sockaddr_in *addr, char *ip)
{
    safe_inet_aton(ip, &addr->sin_addr);
    addr->sin_family = AF_INET;
}

void copyIpFrom(struct sockaddr_in *dst, struct sockaddr_in *src)
{
    dst->sin_addr = src->sin_addr;
    dst->sin_family = src->sin_family;
}

void getDefaultMcastAddress(net_if_t *net_if, struct sockaddr_in *mcast)
{
    getMyAddress(net_if, mcast);
    mcast->sin_addr.s_addr &= htobe32(0x07ffffff);
    mcast->sin_addr.s_addr |= htobe32(0xe8000000);
}

void copyToMessage(unsigned char *dst, struct sockaddr_in *src)
{
    memcpy(dst, (char *)&((struct sockaddr_in *)src)->sin_addr,
           sizeof(struct in_addr));
}

void copyFromMessage(struct sockaddr_in *dst, unsigned char *src)
{
    memcpy((char *)&dst->sin_addr, src, sizeof(struct in_addr));
}

int isAddressEqual(struct sockaddr_in *a, struct sockaddr_in *b)
{
    return !memcmp((char *)a, (char *)b, 8);
}

unsigned long parseSize(char *sizeString)
{
    char *eptr;
    unsigned long size = strtoul(sizeString, &eptr, 10);
    if (eptr && *eptr)
    {
        switch (*eptr)
        {
        case 'm':
        case 'M':
            size *= 1024 * 1024;
            break;
        case 'k':
        case 'K':
            size *= 1024;
            break;
        case '\0':
            break;
        default:
            udpc_fatal(1, "Unit %c unsupported\n", *eptr);
        }
    }
    return size;
}

void zeroSockArray(int *socks, int nr)
{
    int i;

    for (i = 0; i < nr; i++)
        socks[i] = -1;
}

int selectSock(int *socks, int nr, int startTimeout)
{
    fd_set read_set;
    int r;
    int maxFd;
    struct timeval tv, *tvp;
    if (startTimeout)
    {
        tv.tv_sec = startTimeout;
        tv.tv_usec = 0;
        tvp = &tv;
    }
    else
    {
        tvp = NULL;
    }
    maxFd = prepareForSelect(socks, nr, &read_set);
    r = select(maxFd + 1, &read_set, NULL, NULL, tvp);
    if (r < 0)
        return r;
    return getSelectedSock(socks, nr, &read_set);
}

int prepareForSelect(int *socks, int nr, fd_set *read_set)
{
    int i;
    int maxFd;
    FD_ZERO(read_set);
    maxFd = -1;
    for (i = 0; i < nr; i++)
    {
        if (socks[i] == -1)
            continue;
        FD_SET(socks[i], read_set);
        if (socks[i] > maxFd)
            maxFd = socks[i];
    }
    return maxFd;
}

int getSelectedSock(int *socks, int nr, fd_set *read_set)
{
    int i;
    for (i = 0; i < nr; i++)
    {
        if (socks[i] == -1)
            continue;
        if (FD_ISSET(socks[i], read_set))
            return socks[i];
    }
    return -1;
}

void closeSock(int *socks, int nr, int target)
{
    int i;
    int sock = socks[target];

    socks[target] = -1;
    for (i = 0; i < nr; i++)
        if (socks[i] == sock)
            return;
    closesocket(sock);
}

int isMcastAddress(struct sockaddr_in *addr)
{
    int ip = be32toh(addr->sin_addr.s_addr) >> 24;
    return ip >= 0xe0 && ip < 0xf0;
}

#ifdef __MINGW32__

static ssize_t getLength(const struct msghdr *msg)
{
    ssize_t size = 0;
    int i;

    for (i = 0; i < msg->msg_iovlen; i++)
    {
        size += msg->msg_iov[i].iov_len;
    }
    return size;
}

static void doCopy(const struct msghdr *msg, char *ptr, int n, int dir)
{
    int i;
    for (i = 0; n >= 0 && i < msg->msg_iovlen; i++)
    {
        int l = msg->msg_iov[i].iov_len;
        if (l > n)
            l = n;
        if (dir)
        {
            memcpy(msg->msg_iov[i].iov_base, ptr, l);
        }
        else
        {
            memcpy(ptr, msg->msg_iov[i].iov_base, l);
        }
        n -= l;
        ptr += l;
    }
}

ssize_t recvmsg(int s, struct msghdr *msg, int flags)
{
    ssize_t size = getLength(msg);
    char *buffer = malloc(size);
    int n; /* bytes left to copy */

    if (buffer == NULL)
    {
        /* Out of memory */
        errno = ENOMEM;
        return -1;
    }
    n = recvfrom(s, buffer, size, flags,
                 msg->msg_name, &msg->msg_namelen);
    doCopy(msg, buffer, n, 1);
    free(buffer);
    return n;
}

ssize_t sendmsg(int fd, const struct msghdr *msg, int flags)
{
    ssize_t size = getLength(msg);
    char *buffer = malloc(size);
    int n;

    if (buffer == NULL)
    {
        /* Out of memory */
        errno = ENOMEM;
        return -1;
    }
    doCopy(msg, buffer, size, 0);
    n = sendto(fd, buffer, size, flags,
               msg->msg_name, msg->msg_namelen);
    free(buffer);
    return n;
}

#endif /* __MINGW32__ */
