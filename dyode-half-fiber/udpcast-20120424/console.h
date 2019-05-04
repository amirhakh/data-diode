#ifndef CONSOLE_H
#define CONSOLE_H

#ifdef __MINGW32__
#include <winsock2.h>
#include <winbase.h>
#else
#include <sys/select.h>
#endif /* __MINGW32__ */

#define prepareConsole udpc_prepareConsole
#define getConsoleFd udpc_getConsoleFd
#define restoreConsole udpc_restoreConsole

typedef struct console_t console_t;

/**
 * Prepares a console on given fd. If fd = -1, opens /dev/tty instead
 */
console_t *prepareConsole(int fd);

/**
 * Select on the console in addition to the read_set
 * If character available on console, stuff it into c
 */
int selectWithConsole(console_t *con, int maxFd, 
		      fd_set *read_set, struct timeval *tv,
		      int *keyPressed);

/**
 * Restores console into its original state, and restores everything as it was
 * before
 */
void restoreConsole(console_t **, int);

#endif
