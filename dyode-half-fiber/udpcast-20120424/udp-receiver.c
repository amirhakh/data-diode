#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "log.h"
#include "udpcast.h"
#include "udp-receiver.h"
#include "socklib.h"
#include "udpc_version.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef USE_SYSLOG
#include <syslog.h>
#endif

#ifdef BB_FEATURE_UDPCAST_FEC
#include "fec.h"
#endif

#define MAXBLOCK 1024

#ifdef HAVE_GETOPT_LONG
static struct option options[] = {
    { "file", 1, NULL, 'f' },
    { "pipe", 1, NULL, 'p' },
    { "port", 1, NULL, 'P' },
    { "portbase", 1, NULL, 'P' },
    { "interface", 1, NULL, 'i' },
    { "ttl", 1, NULL, 't' },
    { "mcast_all_address", 1, NULL, 'M' }, /* Obsolete name */
    { "mcast-all-address", 1, NULL, 'M' }, /* Obsolete name */

    { "mcast_rdv_address", 1, NULL, 'M' },
    { "mcast-rdv-address", 1, NULL, 'M' },


#ifdef LOSSTEST
    /* simulating packet loss */
    { "write-loss", 1, NULL, 0x601 },
    { "read-loss", 1, NULL, 0x602 },
    { "seed", 1, NULL, 0x603 },
    { "print-seed", 0, NULL, 0x604 },
    { "read-swap", 1, NULL, 0x605 },
#endif

    { "passive", 0, NULL, 'd' },

    { "nosync", 0, NULL, 'n' },
    { "sync", 0, NULL, 'y' },

    { "rcvbuf", 1, NULL, 'b' },

    { "nokbd", 0, NULL, 'k' },

    { "exitWait", 1, NULL, 'w' }, /* Obsolete name */
    { "exit-wait", 1, NULL, 'w' },

    { "start-timeout", 1, NULL, 's' },
    { "receive-timeout", 1, NULL, 0x801 },

#ifdef BB_FEATURE_UDPCAST_FEC
    { "license", 0, NULL, 'L' },
#endif

    { "log", 1, NULL, 'l' },
    { "no-progress", 0, NULL, 0x701 },

    { "print-uncompressed-position", 1, NULL, 'x' },
    { "statistics-period", 1, NULL, 'z' },
    { "stat-period", 1, NULL, 'z' },

    { "ignore-lost-data", 0, NULL, 'Z' },

    { NULL, 0, NULL, 0 }
};

# define getopt_l(c,v,o) getopt_long(c, v, o, options, NULL)
#else /* HAVE_GETOPT_LONG */
# define getopt_l(c,v,o) getopt(c, v, o)
#endif /* HAVE_GETOPT_LONG */

static int signalNumber=0;

#ifdef SIG_UNBLOCK
static void signalForward(void) {
	if(signalNumber != 0) {
		sigset_t sig;
		signal(signalNumber, SIG_DFL);
		sigemptyset(&sig);
		sigaddset(&sig, signalNumber);
		sigprocmask(SIG_UNBLOCK, &sig, NULL);
		raise(signalNumber);
		perror("raise");
	}
}
#endif

static void intHandler(int nr) {
    signalNumber=nr;
    udpc_fatal(1, "Signal %d: Cancelled by user\n", nr);
}

#ifdef NO_BB
static void usage(char *progname) {
#ifdef HAVE_GETOPT_LONG
    fprintf(stderr, "%s [--file file] [--pipe pipe] [--portbase portbase] [--interface net-interface] [--log file] [--no-progress] [--ttl time-to-live] [--mcast-rdv-address mcast-rdv-address] [--rcvbuf buf] [--nokbd] [--exit-wait milliseconds] [--nosync] [--sync] [--start-timeout sto] [--receive-timeout rct] [--license] [-x uncomprStatPrint] [-z statPeriod] [--print-uncompressed-position flag] [--stat-period millis] [--ignore-lost-data]\n", 
	    progname);
#else /* HAVE_GETOPT_LONG */
    fprintf(stderr, "%s [--f file] [--p pipe] [-P portbase] [-i net-interface] [-l logfile] [-t time-to-live] [-M mcast-rdv-address] [-b rcvbuf] [-k] [-w exit-wait-milliseconds] [-n] [-y] [-s start-timeout] [-L] [-x uncomprStatPrint] [-z statPeriod] [-Z]\n", 
	    progname);
#endif /* HAVE_GETOPT_LONG */
    exit(1);
}
#endif

#ifndef NO_BB
int udp_receiver_main(int argc, char **argv);
int udp_receiver_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    int ret;
    char *ptr;
    struct net_config net_config;
    struct disk_config disk_config;
    struct stat_config stat_config;
    int c;
    int doWarn=0;
    char *ifName=NULL;

#ifdef LOSSTEST
    int seedSet = 0;
    int printSeed = 0;
#endif

#ifdef SIG_UNBLOCK
    atexit(signalForward);
#endif

    disk_config.fileName=NULL;
    disk_config.pipeName=NULL;
    disk_config.flags = 0;

    net_config.portBase = 9000;
    net_config.ttl = 1;
    net_config.flags = 0;
    net_config.mcastRdv = NULL;
    net_config.exitWait = 500;
    net_config.startTimeout = 0;
    net_config.receiveTimeout = 0;

    stat_config.statPeriod = DEFLT_STAT_PERIOD;
    stat_config.printUncompressedPos = -1;
    stat_config.noProgress = 0;

#ifdef WINDOWS
    /* windows is basically unusable with its default buffer size of 8k...*/
    net_config.requestedBufSize = 1024*1024;
#else
    net_config.requestedBufSize = 0;
#endif

    ptr = strrchr(argv[0], '/');
    if(!ptr)
	ptr = argv[0];
    else
	ptr++;
    
    net_config.net_if = NULL;
    if (strcmp(ptr, "init") == 0) {
	doWarn = 1;
	disk_config.pipeName = strdup("/bin/gzip -dc");
	disk_config.fileName = "/dev/hda";
    }
    while( (c=getopt_l(argc, argv, "b:f:p:P:i:l:M:s:t:w:x:z:dkLnyZ")) != EOF ) {
	switch(c) {
	    case 'f':
		disk_config.fileName=optarg;
		break;
	    case 'i':
		ifName=optarg;
		break;
	    case 'p':
		disk_config.pipeName=optarg;
		break;
	    case 'P':
		net_config.portBase = atoi(optarg);
		break;
	    case 'l':
		udpc_log = fopen(optarg, "a");
		break;
	    case 0x701:
		stat_config.noProgress = 1;
		break;
	    case 't': /* ttl */
		net_config.ttl = atoi(optarg);
		break;
	    case 'M':
		net_config.mcastRdv = strdup(optarg);
		break;

#ifdef BB_FEATURE_UDPCAST_FEC
	    case 'L':
		    fec_license();
		    break;
#endif

#ifdef LOSSTEST
	    case 0x601:
		setWriteLoss(optarg);
		break;
	    case 0x602:
		setReadLoss(optarg);
		break;
	    case 0x603:
		seedSet=1;
		srandom(strtoul(optarg,0,0));
		break;
	    case 0x604:
		printSeed=1;
		break;
	    case 0x605:
		setReadSwap(optarg);
		break;
#endif
	    case 'd': /* passive */
		net_config.flags|=FLAG_PASSIVE;
		break;
	    case 'n': /* nosync */
		disk_config.flags|=FLAG_NOSYNC;
		break;
	    case 'y': /* sync */
		disk_config.flags|=FLAG_SYNC;
		break;
	    case 'b': /* rcvbuf */
		net_config.requestedBufSize=parseSize(optarg);
		break;
	    case 'k': /* nokbd */
		net_config.flags |= FLAG_NOKBD;
		break;

	    case 'w': /* exit-wait */
		net_config.exitWait = atoi(optarg);
		break;

	    case 's': /* start-timeout */
		net_config.startTimeout = atoi(optarg);		
		break;

	    case 0x801: /* receive-timeout */
		net_config.receiveTimeout = atoi(optarg);
		break;

	    case 'z':
		stat_config.statPeriod = atoi(optarg) * 1000;
		break;
	    case 'x':
		stat_config.printUncompressedPos = atoi(optarg);
		break;

	    case 'Z':
		net_config.flags |= FLAG_IGNORE_LOST_DATA;
		break;

	    case '?':
#ifndef NO_BB
	        bb_show_usage();
#else
		usage(argv[0]);
#endif
	}
    }

    fprintf(stderr, "Udp-receiver %s\n", version);

#ifdef LOSSTEST
    if(!seedSet)
	srandomTime(printSeed);
#endif

    signal(SIGINT, intHandler);
#ifdef USE_SYSLOG
    openlog((const char *)"udpcast", LOG_NDELAY|LOG_PID, LOG_SYSLOG);
#endif
    
    ret= startReceiver(doWarn, &disk_config, &net_config, &stat_config, ifName);
    if(ret < 0) {
      fprintf(stderr, "Receiver error\n");
    }
    return ret;
}
