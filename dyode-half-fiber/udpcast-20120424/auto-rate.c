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


static unsigned int getCurrentQueueLength(int sock) {
#ifdef TIOCOUTQ
    unsigned int length;
    if(ioctl(sock, TIOCOUTQ, &length) < 0)
        return 0;
    return length;
#else
    return 0;
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
    unsigned int q = getCurrentQueueLength(sock);
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
static void doAutoRate(void *data, int sock, in_addr_t ip, size_t size)
{
    struct auto_rate_t *autoRate_l = (struct auto_rate_t*) data;
    (void) ip;

    if(!autoRate_l->isInitialized)
        initialize(autoRate_l, sock);

    while(1) {
        size_t r = getCurrentQueueLength(sock);
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
