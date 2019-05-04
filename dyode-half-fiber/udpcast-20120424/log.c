#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include "log.h"

static int needNewline=0;

static void printNewlineIfNeeded(void) {
    if (needNewline) {
	fprintf(stderr, "\n");
    }
    needNewline=0;
}

static int vlogprintf(FILE *logfile, const char *fmt, va_list ap);

/**
 * Print message to the log, if not null
 */
int logprintf(FILE *logfile, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    return vlogprintf(logfile, fmt, ap);
}

static int newlineSeen=1;

static int vlogprintf(FILE *logfile, const char *fmt, va_list ap) {
    if(logfile != NULL) {	
	char buf[9];
	struct timeval tv;
	int r;
	if(newlineSeen) {
	    gettimeofday(&tv, NULL);
	    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&tv.tv_sec));
	    fprintf(logfile, "%s.%06ld ", buf, tv.tv_usec);
	}
	newlineSeen = (strchr(fmt, '\n') != NULL);
	r= vfprintf(logfile, fmt, ap);
	if(newlineSeen)
	    fflush(logfile);
	return r;
    } else
	return -1;
}

/**
 * Print message to stdout, adding a newline "if needed"
 * A newline is needed if this function has not yet been invoked
 * since last statistics printout
 */
int flprintf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);

    if(udpc_log)
	return vlogprintf(udpc_log, fmt, ap);
    else {
	printNewlineIfNeeded();
	return vfprintf(stderr, fmt, ap);
    }
}

volatile int quitting = 0;

/**
 * Print message to stdout, adding a newline "if needed"
 * A newline is needed if this function has not yet been invoked
 * since last statistics printout
 */
int fatal(int code, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);

    if(quitting)
	_exit(code);
    quitting=1;
    
    printNewlineIfNeeded();
    vfprintf(stderr, fmt, ap);
#if 0
    assert(0); /* make it easyer to use a debugger to see where this came 
		* from */
#endif
    exit(code);
}

int printLongNum(unsigned long long x) {
/*    fprintf(stderr, "%03d ", (int) ( x / 1000000000000LL   ));*/
    long long divisor;
    long long minDivisor;
    int nonzero;
    char suffix=' ';

    if(x > 1000000000000LL) {
	minDivisor = 1048576L;
	suffix='M';	
    } else if(x >= 1000000000) {
	minDivisor = 1024L;
	suffix='K';
    } else {
	minDivisor = 1;
	suffix=' ';
    }
    divisor = minDivisor * 1000000LL;

    nonzero = 0;

    while(divisor >= minDivisor) {
	int digits;
	const char *format;

	digits = (int) ((x / divisor) % 1000);
	if (nonzero) {
	    format = "%03d";
	} else {
	    format = "%3d";
	}
	if (digits || nonzero)
	    fprintf(stderr, format, digits);
	else
	    fprintf(stderr, "    ");
	    
	if(digits) {
	    nonzero = 1;
	}
	divisor = divisor / 1000;
	if(divisor >= minDivisor)
	    fprintf(stderr, " ");
	else
	    fprintf(stderr, "%c", suffix);
    }
    needNewline = 1;
    return 0;
}
