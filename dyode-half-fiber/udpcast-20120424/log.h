#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#undef HAVE_STDINT_H
#include "libbb_udpcast.h"

#define logprintf udpc_logprintf
#define flprintf udpc_flprintf
#define fatal udpc_fatal
#define printLongNum udpc_printLongNum 

/*void printNewlineIfNeeded(void);*/
#ifdef __GNUC__
int logprintf(FILE *logfile, const char *fmt, ...)
    __attribute__ ((format (printf, 2, 3)));
int flprintf(const char *fmt, ...) 
    __attribute__ ((format (printf, 1, 2)));
int fatal(int code, const char *fmt, ...) 
    __attribute__ ((noreturn)) __attribute__ ((format (printf, 2, 3)));
#else
int logprintf(FILE *logfile, const char *fmt, ...);
int flprintf(const char *fmt, ...) ;
int fatal(int code, const char *fmt, ...) ;
#endif

int printLongNum(unsigned long long x);

extern FILE *udpc_log;

#endif
