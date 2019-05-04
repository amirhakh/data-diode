#ifndef RATE_GOVERNOR_H
#define RATE_GOVERNOR_H

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

typedef struct rateGovernor_t {
  /**
   * Allocate and initialize new instance private data structure for this
   * rate governor
   */
  void *(*rgInitialize)(void);

  /**
   * Set property. All properties are passed as strings, and should be
   * converted to the appropriate type by this rate governor
   *
   * The properties are set by specifying them on the udp-sender command line,
   * after the name of the module:
   * udp-sender -f myfile -g ipe.so:ip=224.1.2.3,port=5555
   * This would load the rate governor ipe.so, and pass set its property ip
   * to 224.1.23 and its port to 5555. The port would actually passed as a
   * string, it's the responsibility of the module to convert that into an
   * integer as neded.
   */
  void (*rgSetProp)(void *, const char *key, const char *value);

  /**
   * Called after all properties have been set
   * This method will be called after all properties have been set.
   * In this method, the rate governor knows its configuration, and may start
   * with
   *  - opening sockets and other communication channels
   *  - communicate with remote servers
   *  - start background threads
   */
  void (*rgEndConfig)(void *);

  /**
   * Wait enough time so we can transmit the requested number of bytes
   * This method is called by udpcast just before it transmits a packet.
   * The rate governor may uses this to wait until the ougoing channel is
   * ready to receive more data
   * Parameters:
   *  p	  the rate governor private data
   *  fd    file descriptor to which data is going to be sent
   *  ip    ip address to which data is going to be sent
   *  bytes bytes number of bytes which will be sent
   */
  void (*rgWait)(void *, int fd, in_addr_t ip, long bytes);

  /**
   * Shut down the rate governor
   * Shutdown is called just before program exits. This can be used to
   *  - signal remote servers that this sender is going away
   *  - close filedescriptors/communications channels
   *  - stop background threads
   *  - deallocate data structures
   */
  void (*rgShutdown)(void *);

} rateGovernor_t;

#endif
