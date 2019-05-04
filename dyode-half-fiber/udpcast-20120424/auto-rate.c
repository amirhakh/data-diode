#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>

#include "socklib.h"
#include "udp-sender.h"
#include "auto-rate.h"
#include "util.h"
#include "rateGovernor.h"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef FLAG_AUTORATE

struct auto_rate_t {
  int isInitialized; /* has this already been initialized? */
  int dir; /* 1 if TIOCOUTQ is remaining space, 
	    * 0 if TIOCOUTQ is consumed space */
  int sendbuf; /* sendbuf */
};

static int getCurrentQueueLength(int sock) {
#ifdef TIOCOUTQ
    int length;
    if(ioctl(sock, TIOCOUTQ, &length) < 0)
	return -1;
    return length;
#else
    return -1;
#endif
}

static void *allocAutoRate(void) {
    struct auto_rate_t *autoRate_l = MALLOC(struct auto_rate_t);
    if(autoRate_l == NULL)
	    return NULL;
    autoRate_l->isInitialized = 0;
    return autoRate_l;
}

static void initialize(struct auto_rate_t *autoRate_l, int sock) {
  int q = getCurrentQueueLength(sock);
  if(q == 0) {
    autoRate_l->dir = 0;
    autoRate_l->sendbuf = getSendBuf(sock);
  } else {
    autoRate_l->dir = 1;
    autoRate_l->sendbuf = q;
  }
  autoRate_l->isInitialized=1;
}

/**
 * If queue gets almost full, slow down things
 */
static void doAutoRate(void *data, int sock, in_addr_t ip, long size)
{
    struct auto_rate_t *autoRate_l = (struct auto_rate_t*) data;
    (void) ip;

    if(!autoRate_l->isInitialized)
      initialize(autoRate_l, sock);

    while(1) {
	int r = getCurrentQueueLength(sock);
	if(autoRate_l->dir)
	    r = autoRate_l->sendbuf - r;

	if(r < autoRate_l->sendbuf / 2 - size)
	    return;
#if DEBUG
	flprintf("Queue full %d/%d... Waiting\n", r, autoRate_l->sendbuf);
#endif
	usleep(2500);
    }
}

rateGovernor_t autoRate = {
    allocAutoRate,
    NULL,
    NULL,
    doAutoRate,
    NULL
};

#endif
