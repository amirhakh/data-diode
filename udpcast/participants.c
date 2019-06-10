#include "log.h"
#include "util.h"
#include "participants.h"
#include "udpcast.h"
#ifdef USE_SYSLOG
#include <syslog.h>
#endif

int16_t addParticipant(participantsDb_t,
                       struct sockaddr_in *addr,
                       uint32_t capabilities,
                       uint32_t rcvbuf,
                       int pointopoint);

int isParticipantValid(struct participantsDb *db, int32_t i) {
    return db->clientTable[i].used;
}

int removeParticipant(struct participantsDb *db, int32_t i) {
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

int16_t lookupParticipant(struct participantsDb *db, struct sockaddr_in *addr) {
    int16_t i;
    for (i=0; i < MAX_CLIENTS; i++) {
        if (db->clientTable[i].used &&
                ipIsEqual(&db->clientTable[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

int16_t nrParticipants(participantsDb_t db) {
    return db->nrParticipants;
}

int16_t udpc_addParticipant(participantsDb_t db,
                        struct sockaddr_in *addr,
                        uint32_t capabilities,
                        uint32_t rcvbuf,
                        int pointopoint) {
    int16_t i;

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

uint32_t getParticipantCapabilities(participantsDb_t db, int16_t i)
{
    return db->clientTable[i].capabilities;
}

uint32_t getParticipantRcvBuf(participantsDb_t db, int16_t i)
{
    return db->clientTable[i].rcvbuf;
}

struct sockaddr_in *getParticipantIp(participantsDb_t db, int16_t i)
{
    return &db->clientTable[i].addr;
}

void printNotSet(participantsDb_t db, char *d)
{
    int first=1;
    int32_t i;
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
    int16_t i;
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
