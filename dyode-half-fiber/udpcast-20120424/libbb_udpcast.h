/* libbb_udhcp.h - busybox compatability wrapper */

#ifndef _LIBBB_UDPCAST_H
#define _LIBBB_UDPCAST_H

#ifndef UDPCAST_CONFIG_H
# define UDPCAST_CONFIG_H
# include "config.h"
#endif

#ifndef NO_BB
#undef HAVE_STDINT_H
#include "libbb.h"
#include "busybox.h"

#define COMBINED_BINARY

#else /* ! BB_BT */

#define TRUE			1
#define FALSE			0

#ifdef HAVE_MALLOC_H
    #include <malloc.h>
#endif

#define xmalloc malloc

#endif /* BB_VER */

#endif /* _LIBBB_UDPCAST_H */
