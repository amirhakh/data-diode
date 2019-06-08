#include <stdio.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <net/if.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../udpcast/rateGovernor.h"


/**
 * Example of a rate governor.
 * Rate governors are dynamically loadable modules that allow to control the
 * speed at which udpcast transfers data.
 *
 * In order to do this, udpcast calls the governor's wait function before
 * transmitting a packet. The governor may then wait some time until it
 * returns, thusly controling the transmission speed.
 *
 * This example rate governor acts upon the traffic control packets that a
 * "Logic Innovations IP Encapsulator 3000" sends
 */

/**
 * One route. We assume here that all fields are in network byte order
 */
struct route {
  in_addr_t ip_address;
  in_addr_t ip_mask;
  uint32_t buffer_length; /* how many bytes IPE buffer can hold */
  uint32_t buffer_level;  /* current occupation level of IPE buffer */
};

/**
 * Entire traffic control packet. We assume here that all fields are in
 * network byte order
 */
struct ipe_packet {
  int total_routes;
  int total_length;
  struct route route[4096];
};


struct ipe {
  struct sockaddr_in recv; /* address to which the traffic control packets
			    * are sent */

  char *interface; /* name of network interface where traffic control
		      packets will be received */

  int fd; /* file descriptor on which the traffic control packets are
	     received */

  int maxFillLevel; /* percent of maximum buffer fill level */

  int havePacket; /* have we at least received one packet? */

  pthread_t thread; /* thread receiving the multicasted announcements */
  pthread_mutex_t mutex; /* mutex protecting the traffic control packets */
  pthread_cond_t cond; /* this condition will be signal when the traffic
			  control packet has been updated */

  struct ipe_packet packet[2]; /* array of two traffic control packets. One
				* is active, whereas the other is ready to
				* receive new data. After having received and
				* validated a new packet, the receiving thread
				* will switch the roles of both slots */
  int activePacket; /* which one of the 2 packets is currently active? */

  struct route *lastMatchedRoute; /* pointer to last matched route
				     (performance optimization) */
};

/**
 * answers whether the given route matches the IP
 */
static int routeMatches(struct route *route, in_addr_t ip)
{
  return (route->ip_address & route->ip_mask) == (ip & route->ip_mask);
}

/**
 * Find route to ip in packet. Returns NULL if none found
 */
static struct route *findRouteInPacket(struct ipe_packet *packet, in_addr_t ip)
{
  int i=0;
  for(i=0; i< packet->total_routes; i++)
    if(routeMatches(&packet->route[i], ip))
      return &packet->route[i];
  return NULL;
}

/**
 * Find route to given IP address in packet. First check whether cached route
 * matches, and if not, search through last received packet
 */
static struct route *findRoute(struct ipe *ipe, in_addr_t ip)
{
  if(ipe->lastMatchedRoute && routeMatches(ipe->lastMatchedRoute, ip))
    return ipe->lastMatchedRoute;
  ipe->lastMatchedRoute = findRouteInPacket(&ipe->packet[ipe->activePacket],ip);
  return ipe->lastMatchedRoute;
}

/**
 * Thread listening for traffic control packets
 */
static void *run(void *p)
{
  struct ipe *me = (struct ipe *) p;
  while(1) {
    int n;
    struct ipe_packet *packet = &me->packet[1-me->activePacket];
    n = recv(me->fd, packet, sizeof(*packet), 0);
    if(n < 0 ) {
      perror("receive traffic control");
      continue;
    }

    /* Quick sanity check of packet...*/
    if(n < 8) {
      fprintf(stderr, "Incomplete traffic control header\n");
      continue;
    }
    if(n < be32toh(packet->total_routes) * 16) {
      fprintf(stderr, "Incomplete traffic control data\n");
      continue;
    }

    /* packet is ok, activate it */
    pthread_mutex_lock(&me->mutex);
    me->activePacket = 1 - me->activePacket;
    me->havePacket = 1;
    me->lastMatchedRoute = NULL;
    pthread_cond_signal(&me->cond);
    pthread_mutex_unlock(&me->mutex); 
  }
}

/**
 * Allocate and initialize new instance private data structure for this
 * rate governor
 */
static void *ipe_initialize(void)
{
  struct ipe *me;
  me = calloc(sizeof(struct ipe),1);

  me->maxFillLevel=80;
  
  pthread_mutex_init(&me->mutex, NULL);
  pthread_cond_init(&me->cond, NULL);

  return me;
}

/**
 * Set property. All properties are passed as strings, and should be
 * converted to the appropriate type by this rate governor
 *
 * In this example, the following properties are supported
 *  - ip	the multicast ip address which receives the traffic control
 *		packets
 *  - port	the UDP port which receives the traffic control packets
 *  - if	the network interface (default eth0) which receives the
 *		traffic control packets
 *  - maxFillLevel stop transmitting packets if buffers on IPE are fuller than
 *		this level (expressed in percent of available buffer size).
 *		Default is 80
 */
static void ipe_setProp(void *p, const char *key, const char *value)
{
  struct ipe *me = (struct ipe *) p;
  if(!strcmp("ip", key)) {
    inet_aton(value, &me->recv.sin_addr);
  } else if(!strcmp("port", key)) {
    char *eptr;
    me->recv.sin_port = htobe16(strtoul(value,&eptr, 0));
    if(*eptr)
      fprintf(stderr, "Bad port %s\n", value);
  } else if(!strcmp("if", key)) {
    me->interface = strdup(value);
  } else if(!strcmp("maxFillLevel", key)) {
    char *eptr;
    me->maxFillLevel = strtoul(value, &eptr, 0);
    if(*eptr)
      fprintf(stderr, "Bad port %s\n", value);
  } else {
    fprintf(stderr, "Unknown parameter %s=%s\n", key, value);
  }
}

/**
 * This method will be called after all properties have been set.
 * In this method, the rate governor knows its configuration, and may start
 * with
 *  - opening sockets and other communication channels
 *  - communicate with remote servers
 *  - start background threads
 */
static void ipe_endConfig(void *data)
{
  struct ipe *me = (struct ipe *) data;
  char *interface;
  int sock;
  struct ip_mreqn mreq; /* used for subscribing to multicast */
  int r; /* generic return value */

  sock = socket(PF_INET, SOCK_DGRAM, 0);
  if(socket < 0) {
    perror("socket");
    return;
  }

  if(bind(sock, (struct sockaddr*) &me->recv, sizeof(struct sockaddr_in)) < 0) {
    perror("bind");
    return;
  }

  if(me->interface)
    interface = me->interface;
  else
    interface = "eth0";

  mreq.imr_ifindex = if_nametoindex(interface);
  mreq.imr_address.s_addr = 0;
  mreq.imr_multiaddr = me->recv.sin_addr;
  r = setsockopt(sock, SOL_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
  if(r < 0) {
    perror("add membership error");
    /* consider this error non fatal, so that it still works with unicast
     * addresses */
  }

  me->fd = sock;

  pthread_create(&me->thread, NULL, run, me);
}

/**
 * This method is called by udpcast just before it transmits a packet.
 * The rate governor may uses this to wait until the ougoing channel is
 * ready to receive more data
 * Parameters:
 *  p	  the rate governor private data
 *  fd    file descriptor to which data is going to be sent
 *  ip    ip address to which data is going to be sent
 *  bytes bytes number of bytes which will be sent
 */
static void ipe_wait(void *p, int fd, in_addr_t ip, long bytes)
{
  struct ipe *me = (struct ipe *) p;
  struct route *route;
  int level=0;
  int length=0;

  pthread_mutex_lock(&me->mutex);

  while(1) {
    if(me->havePacket) {
      route = findRoute(me, ip);
      if(route == NULL)
	/* no route found => no ratelimit... */
	break;

      level = be32toh(route->buffer_level);
      length = be32toh(route->buffer_length);
      if(bytes < length * me->maxFillLevel / 100  - level)
	/* enough space left for new packet */
	break;
    }
    pthread_cond_wait(&me->cond, &me->mutex);
  }
  if(route) {
    /* Account packet that will be sent */
    level += bytes;
    route->buffer_level = htobe32(level);
  }
  pthread_mutex_unlock(&me->mutex);
}

/**
 * Finalize is called just before program exits. This can be used to
 *  - signal remote servers that this sender is going away
 *  - close filedescriptors/communications channels
 *  - stop background threads
 *  - deallocate data structures
 */
static void ipe_shutdown(void *p) {
  struct ipe *me = (struct ipe *) p;
  void *v;
  pthread_cancel(me->thread);
  pthread_join(me->thread, &v);
  close(me->fd);
  if(me->interface)
    free(me->interface);
}

/**
 * Table of methods, to be picket up by udpcast
 */
struct rateGovernor_t governor = {
  ipe_initialize,
  ipe_setProp,
  ipe_endConfig,
  ipe_wait,
  ipe_shutdown,
};
