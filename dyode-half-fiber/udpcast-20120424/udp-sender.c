#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "log.h"
#include "socklib.h"
#include "udpcast.h"
#include "udp-sender.h"
#include "udpc_version.h"
#include "rateGovernor.h"
#include "rate-limit.h"
#include "auto-rate.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef USE_SYSLOG
#include <syslog.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef BB_FEATURE_UDPCAST_FEC
#include "fec.h"
#endif

#if defined HAVE_DLSYM && defined NO_BB
#define DL_RATE_GOVERNOR
#endif

#ifdef HAVE_GETOPT_LONG
static struct option options[] = {
    { "file", 1, NULL, 'f' },
    { "half-duplex", 0, NULL, 'c' },
    { "full-duplex", 0, NULL, 'd' },
    { "pipe", 1, NULL, 'p' },
    { "port", 1, NULL, 'P' },
    { "portbase", 1, NULL, 'P' },
    { "blocksize", 1, NULL, 'b' },
    { "interface", 1, NULL, 'i' },
    { "mcast_address", 1, NULL, 'm' }, /* Obsolete name */
    { "mcast-address", 1, NULL, 'm' }, /* Obsolete name */
    { "mcast_data_address", 1, NULL, 'm' },
    { "mcast-data-address", 1, NULL, 'm' },
    { "mcast_all_address", 1, NULL, 'M' }, /* Obsolete name */
    { "mcast-all-address", 1, NULL, 'M' }, /* Obsolete name */
    { "mcast_rdv_address", 1, NULL, 'M' },
    { "mcast-rdv-address", 1, NULL, 'M' },
    { "max_bitrate", 1, NULL, 'r' },
    { "max-bitrate", 1, NULL, 'r' },
    { "point-to-point", 0, NULL, '1' },
    { "point_to_point", 0, NULL, '1' },
    { "pointopoint", 0, NULL, '1' },

    { "nopoint-to-point", 0, NULL, '2' },
    { "nopoint_to_point", 0, NULL, '2' },
    { "nopointopoint", 0, NULL, '2' },

    { "async", 0, NULL, 'a' },
#ifdef FLAG_AUTORATE
    { "autorate", 0, NULL, 'A' },
#endif
    { "log", 1, NULL, 'l' },
    { "no-progress", 0, NULL, 0x701 },

    /* slice size configuration */
    { "min-slice-size", 1, NULL, 0x0101 },
    { "default-slice-size", 1, NULL, 0x0102 },
    { "slice-size", 1, NULL, 0x0102 },
    { "max-slice-size", 1, NULL, 0x0103 },

    { "ttl", 1, NULL, 't' },
#ifdef BB_FEATURE_UDPCAST_FEC
    { "fec", 1, NULL, 'F' },
    { "license", 0, NULL, 'L' },
#endif

#ifdef LOSSTEST
    /* simulating packet loss */
    { "write-loss", 1, NULL, 0x601 },
    { "read-loss", 1, NULL, 0x602 },
    { "seed", 1, NULL, 0x603 },
    { "print-seed", 0, NULL, 0x604 },
#endif

    { "rexmit-hello-interval", 1, NULL, 'H' },
    { "autostart", 1, NULL, 'S' },

    { "broadcast", 0, NULL, 'B' },

    { "sendbuf", 1, NULL, 's' },

    { "min-clients", 1, NULL, 'C' }, /* Obsolete name */
    { "min-receivers", 1, NULL, 'C' },
    { "max-wait", 1, NULL, 'W' },
    { "min-wait", 1, NULL, 'w' },
    { "nokbd", 0, NULL, 'k' },
    { "start-timeout", 1, NULL, 'T' },

    { "retriesUntilDrop", 1, NULL, 'R' }, /* Obsolete name */
    { "retries-until-drop", 1, NULL, 'R' },

    { "daemon-mode", 0, NULL, 'D'},
    { "pid-file", 1, NULL, 0x901 },

#ifdef HAVE_KILL
    { "kill", 0, NULL, 'K' },
#endif

    { "bw-period", 1, NULL, 'I'},

#ifdef DL_RATE_GOVERNOR
    { "rate-governor", 1, NULL, 'g'},
#endif

    { "print-uncompressed-position", 1, NULL, 'x' },
    { "statistics-period", 1, NULL, 'z' },
    { "stat-period", 1, NULL, 'z' },

    { "streaming", 0, NULL, 'Z' },
    { "rehello-offset", 0, NULL, 'Y' },

    { NULL, 0, NULL, 0}
};

# define getopt_l(c,v,o) getopt_long(c, v, o, options, NULL)
#else /* HAVE_GETOPT_LONG */
# define getopt_l(c,v,o) getopt(c, v, o)
#endif /* HAVE_GETOPT_LONG */

#ifdef NO_BB
static void usage(char *progname) {
#ifdef HAVE_GETOPT_LONG
    fprintf(stderr, "%s [--file file] [--full-duplex] [--pipe pipe] [--portbase portbase] [--blocksize size] [--interface net-interface] [--mcast-data-address data-mcast-address] [--mcast-rdv-address mcast-rdv-address] [--max-bitrate bitrate] [--pointopoint] [--async] [--log file] [--no-progress] [--min-slice-size min] [--max-slice-size max] [--slice-size] [--ttl time-to-live] [--fec <stripes>x<redundancy>/<stripesize>] [--print-seed] [--rexmit-hello-interval interval] [--autostart autostart] [--broadcast] [--min-receivers receivers] [--min-wait sec] [--max-wait sec] [--start-timeout n] [--retries-until-drop n] [--nokbd] [--bw-period n] [--streaming] [--rehello-offset offs]"
#ifdef DL_RATE_GOVERNOR
	    " [--rate-governor module:parameters]" 
#endif
#ifdef FLAG_AUTORATE
	    " [--autorate]"
#endif
	    "[--license]\n", progname); /* FIXME: copy new options to busybox */
#else /* HAVE_GETOPT_LONG */
    fprintf(stderr, "%s [-f file] [-d] [-p pipe] [-P portbase] [-b size] [-i net-interface] [-m data-mcast-address] [-M mcast-rdv-address] [-r bitrate] [-1] [-a] [-l logfile] [-t time-to-live] [-F <stripes>x<redundancy>/<stripesize>][-H hello-retransmit-interval] [-S autostart] [-B] [-C min-receivers] [-w min-wait-sec] [-w max-wait-sec] [-T start-timeout] [-R n] [-k] [-I n] [-x uncomprStatPrint] [-z statPeriod] [-Z] [-Y rehello-offset]"
#ifdef DL_RATE_GOVERNOR
	    " [-g rate-governor:parameters ]" 
#endif
#ifdef FLAG_AUTORATE
	    " [-A]"
#endif
	    " [-L]\n", progname); /* FIXME: copy new options to busybox */
#endif /* HAVE_GETOPT_LONG */
    exit(1);
}
#else
static inline void usage(char *progname) {
    (void) progname; /* shut up warning while compiling busybox... */
    bb_show_usage();
}
#endif

static const char *pidfile = NULL;

#ifdef HAVE_DAEMON
static void cleanPidfile(int nr UNUSED) {
    unlink(pidfile);
    signal(SIGTERM, SIG_DFL);
    raise(SIGTERM);
}
#endif

#ifndef NO_BB
int udp_sender_main(int argc, char **argv);
int udp_sender_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    int c;
    char *ptr;
#ifdef LOSSTEST
    int seedSet = 0;
    int printSeed = 0;
#endif

    int daemon_mode = 0;
    int doKill = 0;

    int r;
    struct net_config net_config;
    struct disk_config disk_config;
    struct stat_config stat_config;
    char *ifName = NULL;

    int dataMcastSupplied = 0;
    int mainSock;

    /* argument parsing */
    disk_config.fileName = NULL;
    disk_config.pipeName = NULL;
    disk_config.flags = 0;

    clearIp(&net_config.dataMcastAddr);
    net_config.mcastRdv = NULL;
    net_config.blockSize = 1456;
    net_config.sliceSize = 16;
    net_config.portBase = 9000;
    net_config.nrGovernors = 0;
    net_config.flags = 0;
    net_config.capabilities = 0;
    net_config.min_slice_size = 16;
    net_config.max_slice_size = 1024;
    net_config.default_slice_size = 0;
    net_config.ttl = 1;
    net_config.rexmit_hello_interval = 0;
    net_config.autostart = 0;
    net_config.requestedBufSize = 0;

    net_config.min_receivers=0;
    net_config.max_receivers_wait=0;
    net_config.min_receivers_wait=0;
    net_config.startTimeout=0;

    net_config.retriesUntilDrop = 200;

    net_config.rehelloOffset = 50;

    stat_config.log = NULL;
    stat_config.bwPeriod = 0;
    stat_config.printUncompressedPos = -1;
    stat_config.statPeriod = DEFLT_STAT_PERIOD;
    stat_config.noProgress = 0;

    ptr = strrchr(argv[0], '/');
    if(!ptr)
	ptr = argv[0];
    else
	ptr++;

    net_config.net_if = NULL;
    if (strcmp(ptr, "init") == 0) {
	disk_config.pipeName = strdup("/bin/gzip -c");
	disk_config.fileName = "/dev/hda";
    } else {
	const char *argLetters = 
	    "b:C:f:F:"
#ifdef DL_RATE_GOVERNOR
	    "g:"
#endif
	    "H:i:I:"
#ifdef HAVE_KILL
	    "K"
#endif
	    "l:m:M:p:P:r:R:s:S:t:T:w:W:x:z:12a"
#ifdef FLAG_AUTORATE
	    "A"
#endif
	    "BcdDkLY:Z";
        while( (c=getopt_l(argc, argv, argLetters)) 
	       != EOF ) {
	    switch(c) {
		case 'a':
		    net_config.flags |= FLAG_ASYNC|FLAG_SN;
		    break;
		case 'c':
		    net_config.flags &= ~FLAG_SN;
		    net_config.flags |= FLAG_NOTSN;
		    break;
		case 'd':
		    net_config.flags |= FLAG_SN;
		    break;		    
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
		case '1':
		    net_config.flags |= FLAG_POINTOPOINT;
		    break;
		case '2':
		    net_config.flags |= FLAG_NOPOINTOPOINT;
		    break;
		case 'b':
		    net_config.blockSize = strtoul(optarg, 0, 0);
		    net_config.blockSize -= net_config.blockSize % 4;
		    if (net_config.blockSize <= 0) {
			perror("block size too small");
			exit(1);
		    }
#if 0
		    if (net_config.blockSize > 1456) {
			perror("block size too large");
			exit(1);
		    }
#endif
		    break;
		case 'l':
		    stat_config.log = udpc_log = fopen(optarg, "a");
		    break;
		case 0x701:
		    stat_config.noProgress = 1;
		    break;
		case 'm':
		    setIpFromString(&net_config.dataMcastAddr, optarg);
		    ipIsZero(&net_config.dataMcastAddr);
		    dataMcastSupplied = 1;
		    break;
		case 'M':
		    net_config.mcastRdv = strdup(optarg);
		    break;
		case 'r':
		    {
			void *gov = rgInitGovernor(&net_config, &maxBitrate);
			maxBitrate.rgSetProp(gov, MAX_BITRATE, optarg);
		    }
		    break;
		case 'A':
#ifdef FLAG_AUTORATE
		    rgInitGovernor(&net_config, &autoRate);
#else
		    fatal(1, 
			  "Auto rate limit not supported on this platform\n");
#endif
		    break;
		case 0x0101:
		    net_config.min_slice_size = atoi(optarg);
		    if(net_config.min_slice_size > MAX_SLICE_SIZE)
			fatal(1, "min slice size too big\n");
		    break;
		case 0x0102:
		    net_config.default_slice_size = atoi(optarg);
		    break;
		case 0x0103:
		    net_config.max_slice_size = atoi(optarg);
		    if(net_config.max_slice_size > MAX_SLICE_SIZE)
			fatal(1, "max slice size too big\n");
		    break;
	        case 't': /* ttl */
		    net_config.ttl = atoi(optarg);
		    break;
#ifdef BB_FEATURE_UDPCAST_FEC
	        case 'F': /* fec */
		    net_config.flags |= FLAG_FEC;
		    {
			char *eptr;
			ptr = strchr(optarg, 'x');
			if(ptr) {
			    net_config.fec_stripes = 
				strtoul(optarg, &eptr, 10);
			    if(ptr != eptr) {
				flprintf("%s != %s\n", ptr, eptr);
				usage(argv[0]);
			    }
			    ptr++;
			} else {
			    net_config.fec_stripes = 8;
			    ptr = optarg;
			}
			net_config.fec_redundancy = strtoul(ptr, &eptr, 10);
			if(*eptr == '/') {
			    ptr = eptr+1;
			    net_config.fec_stripesize = 
				strtoul(ptr, &eptr, 10);
			} else {
			    net_config.fec_stripesize = 128;
			}
			if(*eptr) {
			    flprintf("string not at end %s\n", eptr);
			    usage(argv[0]);
			}
			fprintf(stderr, "stripes=%d redund=%d stripesize=%d\n",
				net_config.fec_stripes,
				net_config.fec_redundancy,
				net_config.fec_stripesize);
		    }
		    break;
	    case 'z':
		    stat_config.statPeriod = atoi(optarg) * 1000;
		    break;
	    case 'x':
		    stat_config.printUncompressedPos = atoi(optarg);
		    break;
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
#endif
	        case 'H': /* rexmit-hello-interval */
		    net_config.rexmit_hello_interval = atoi(optarg);
		    break;
	        case 'S': /* autostart */
		    net_config.autostart = atoi(optarg);
		    break;
	        case 'B': /* broadcast */
		    net_config.flags |= FLAG_BCAST;
		    break;		    
	        case 's': /* sendbuf */
		    net_config.requestedBufSize=parseSize(optarg);
		    break;		    
	        case 'C': /* min-clients */
		    net_config.min_receivers = atoi(optarg);
		    break;
	        case 'W': /* max-wait */
		    net_config.max_receivers_wait = atoi(optarg);
		    break;
	        case 'w': /* min-wait */
		    net_config.min_receivers_wait = atoi(optarg);
		    break;
	        case 'T': /* start timeout */
		    net_config.startTimeout = atoi(optarg);
		    break;
	        case 'k': /* nokbd */
		    net_config.flags |= FLAG_NOKBD;
		    break;

	        case 'R': /* retries-until-drop */
		    net_config.retriesUntilDrop = atoi(optarg);
		    break;

	        case 'D': /* daemon-mode */
		    daemon_mode++;
		    break;
		case 'K':
		    doKill = 1;
		    break;
		case 0x901:
		    pidfile = optarg;
		    break;

	        case 'I': /* bw-period */
		    stat_config.bwPeriod = atol(optarg);
		    break;
#ifdef DL_RATE_GOVERNOR
	        case 'g': /* rate governor */
		    rgParseRateGovernor(&net_config, optarg);
		    break;
#endif
		case 'Z':
		    net_config.flags |= FLAG_STREAMING;
		    break;
		case 'Y':
		    net_config.rehelloOffset = atol(optarg);
		    break;
	        default:
		case '?':
		    usage(argv[0]);
	    }
	}
    }

#ifdef HAVE_KILL
    if(doKill) {
	FILE *p;
	char line[80];
	int pid;

	if(pidfile == NULL) {
	    fprintf(stderr, "-K only works together with --pidfile\n");
	    return 1;
	}
	p = fopen(pidfile, "r");
	if(p == NULL) {
	    perror("Could not read pidfile");
	    return 1;
	}
	if(fgets(line, sizeof(line), p) == NULL) {
	    fprintf(stderr, "Empty pid file\n");
	    return 1;
	}
	fclose(p);
	pid = atoi(line);
	if(pid <= 0) {
	    fprintf(stderr, "Negative or null pid\n");
	    return -1;
	}

	if(kill(pid, SIGTERM) < 0) {
	    if(errno == ESRCH) {
		/* Process does not exist => already killed */
		unlink(pidfile);
	    }
	    perror("Kill");
	    return 1;	   
	}
	return 0;
    }
#endif

    if(net_config.flags & FLAG_ASYNC) {
      if(dataMcastSupplied)
	net_config.flags &= ~FLAG_POINTOPOINT;
      if(net_config.flags & FLAG_POINTOPOINT) {
	fprintf(stderr, "Pointopoint supplied together with async, but no dataMcastAddress (-m)\n");
	return -1;
      }
    }

    if(optind < argc && !disk_config.fileName) {
	disk_config.fileName = argv[optind++];
    }

    if(optind < argc) {
	fprintf(stderr, "Extra argument \"%s\" ignored\n", argv[optind]);
    }

    if((net_config.flags & FLAG_POINTOPOINT) &&
       (net_config.flags & FLAG_NOPOINTOPOINT)) {
	fatal(1,"pointopoint and nopointopoint cannot be set both\n");
    }

    if( (net_config.autostart || (net_config.flags & FLAG_ASYNC)) &&
	net_config.rexmit_hello_interval == 0)
	net_config.rexmit_hello_interval = 1000;

#ifdef LOSSTEST
    if(!seedSet)
	srandomTime(printSeed);
#endif
    if(net_config.flags &  FLAG_ASYNC) {
	if(net_config.rateGovernor == 0) {
	    fprintf(stderr, 
		    "Async mode chosen but no rate governor ==> unsafe\n");
	    fprintf(stderr, 
		    "Transmission would fail due to buffer overrung\n");
	    fprintf(stderr, 
		    "Add \"--max-bitrate 9500k\" to commandline (for example)\n");
	    exit(1);
	}
#ifdef BB_FEATURE_UDPCAST_FEC
	if(! (net_config.flags & FLAG_FEC)) 
#endif
	  {
	    fprintf(stderr, 
		    "Warning: Async mode but no forward error correction\n");
	    fprintf(stderr, 
		    "Transmission may fail due to packet loss\n");
	    fprintf(stderr, 
		    "Add \"--fec 8x8\" to commandline\n");
	}
    }

    if(net_config.min_slice_size < 1)
	net_config.min_slice_size = 1;
    if(net_config.max_slice_size < net_config.min_slice_size)
	net_config.max_slice_size = net_config.min_slice_size;
    if(net_config.default_slice_size != 0) {
	if(net_config.default_slice_size < net_config.min_slice_size)
	    net_config.default_slice_size = net_config.min_slice_size;
	if(net_config.default_slice_size > net_config.max_slice_size)
	    net_config.default_slice_size = net_config.max_slice_size;
    }

    if(daemon_mode < 2)
	fprintf(stderr, "Udp-sender %s\n", version);

    /*
    if(disk_config.fileName == NULL && disk_config.pipeName == NULL) {
	fatal(1, "You must supply file or pipe\n");
    }
     end of argument parsing */

#ifdef USE_SYSLOG
    openlog((const char *)"udpcast", LOG_NDELAY|LOG_PID, LOG_SYSLOG);
#endif

    mainSock = openMainSenderSock(&net_config, ifName);
    if(mainSock < 0) {
	perror("Make main sock");
	exit(1);
    }

#ifdef HAVE_DAEMON
    if(daemon_mode == 2) {
	net_config.flags |= FLAG_NOKBD;
	stat_config.noProgress = 1;
	if(daemon(1, 0) < 0) {
	    perror("Could not daemonize");
	    exit(1);
	}
	if(pidfile) {
	    FILE *p = fopen(pidfile, "w");
	    fprintf(p, "%d\n", getpid());
	    fclose(p);
	    signal(SIGTERM, cleanPidfile);
	}
    }
#endif

    do {
	r= startSender(&disk_config, &net_config, &stat_config, mainSock);
    } while(daemon_mode);

    closesocket(mainSock);
    rgShutdownAll(&net_config);
    return r;
}
