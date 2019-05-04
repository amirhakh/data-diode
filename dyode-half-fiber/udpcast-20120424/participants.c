#include "socklib.h"
#include "log.h"
#include "util.h"
#include "participants.h"
#include "udpcast.h"
#ifdef USE_SYSLOG
#include <syslog.h>
#endif

struct  participantsDb {
    int nrParticipants;
    
    struct clientDesc {
	struct sockaddr_in addr;
	int used;
	int capabilities;
	unsigned int rcvbuf;
    } clientTable[MAX_CLIENTS];
};

int addParticipant(participantsDb_t,
		   struct sockaddr_in *addr, 
		   int capabilities, 
		   unsigned int rcvbuf,
		   int pointopoint);

int isParticipantValid(struct participantsDb *db, int i) {
    return db->clientTable[i].used;
}

int removeParticipant(struct participantsDb *db, int i) {
    if(db->clientTable[i].used) {
	char ipBuffer[16];	
	flprintf("Disconnecting #%d (%s)\n", i, 
		 getIpString(&db->clientTable[i].addr, ipBuffer));
#ifdef USE_SYSLOG
	syslog(LOG_INFO, "Disconnecting #%d (%s)\n", i,
			getIpString(&db->clientTable[i].addr, ipBuffer));
#endif
	db->clientTable[i].used = 0;
	db->nrParticipants--;
    }
    return 0;
}

int lookupParticipant(struct participantsDb *db, struct sockaddr_in *addr) {
    int i;
    for (i=0; i < MAX_CLIENTS; i++) {
	if (db->clientTable[i].used && 
	    ipIsEqual(&db->clientTable[i].addr, addr)) {
	    return i;
	}
    }
    return -1;
}

int nrParticipants(participantsDb_t db) {
    return db->nrParticipants;
}

int addParticipant(participantsDb_t db,
		   struct sockaddr_in *addr, 
		   int capabilities,
		   unsigned int rcvbuf,
		   int pointopoint) {
    int i;

    if((i = lookupParticipant(db, addr)) >= 0)
	return i;

    for (i=0; i < MAX_CLIENTS; i++) {
	if (!db->clientTable[i].used) {
	    char ipBuffer[16];
	    db->clientTable[i].addr = *addr;
	    db->clientTable[i].used = 1;
	    db->clientTable[i].capabilities = capabilities;
	    db->clientTable[i].rcvbuf = rcvbuf;
	    db->nrParticipants++;

	    fprintf(stderr, "New connection from %s  (#%d) %08x\n", 
		    getIpString(addr, ipBuffer), i, capabilities);
#ifdef USE_SYSLOG
	    syslog(LOG_INFO, "New connection from %s  (#%d)\n",
			    getIpString(addr, ipBuffer), i);
#endif
	    return i;
	} else if(pointopoint)
	    return -1;
    }

    return -1; /* no space left in participant's table */
}

participantsDb_t makeParticipantsDb(void)
{
    return MALLOC(struct participantsDb);
}

int getParticipantCapabilities(participantsDb_t db, int i)
{
    return db->clientTable[i].capabilities;
}

unsigned int getParticipantRcvBuf(participantsDb_t db, int i)
{
    return db->clientTable[i].rcvbuf;
}

struct sockaddr_in *getParticipantIp(participantsDb_t db, int i)
{
    return &db->clientTable[i].addr;
}
    
void printNotSet(participantsDb_t db, char *d)
{
    int first=1;
    int i;
    flprintf("[");
    for (i=0; i < MAX_CLIENTS; i++) {
	if (db->clientTable[i].used) {
	    if(!BIT_ISSET(i, d)) {
		if(!first)
		    flprintf(",");
		first=0;
		flprintf("%d", i);
	    }
	}
    }
    flprintf("]");
}


void printSet(participantsDb_t db, char *d)
{
    int first=1;
    int i;
    flprintf("[");
    for (i=0; i < MAX_CLIENTS; i++) {
	if (db->clientTable[i].used) {
	    if(BIT_ISSET(i, d)) {
		if(!first)
		    flprintf(",");
		first=0;
		flprintf("%d", i);
	    }
	}
    }
    flprintf("]");
}
