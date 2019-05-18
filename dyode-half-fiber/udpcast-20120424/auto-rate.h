#ifndef AUTO_RATE_H
#define AUTO_RATE_H

#include <stdint.h>

struct auto_rate_t {
    int isInitialized; /* has this already been initialized? */
    int dir; /* 1 if TIOCOUTQ is remaining space,
        * 0 if TIOCOUTQ is consumed space */
    uint64_t sendbuf; /* sendbuf */
};

extern struct rateGovernor_t autoRate;

#endif /* AUTO_RATE_H */
