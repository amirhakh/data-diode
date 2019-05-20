#ifndef PARTICIPANTS_H
#define PARTICIPANTS_H

#include "socklib.h"

#define MAX_CLIENTS 1024

#define isParticipantValid udpc_isParticipantValid
#define removeParticipant udpc_removeParticipant
#define lookupParticipant udpc_lookupParticipant
#define nrParticipants udpc_nrParticipants
#define addParticipant udpc_addParticipant
#define makeParticipantsDb udpc_makeParticipantsDb
#define getParticipantCapabilities udpc_getParticipantCapabilities
#define getParticipantRcvBuf udpc_getParticipantRcvBuf
#define getParticipantIp udpc_getParticipantIp
#define printNotSet udpc_printNotSet
#define printSet udpc_printSet

struct  participantsDb {
    int32_t nrParticipants;

    struct clientDesc {
        struct sockaddr_in addr;
        int used;
        int capabilities;
        unsigned int rcvbuf;
    } clientTable[MAX_CLIENTS];
};

typedef struct participantsDb *participantsDb_t;

int udpc_isParticipantValid(participantsDb_t ,int32_t slot);
int udpc_removeParticipant(participantsDb_t, int32_t slot);
int32_t udpc_lookupParticipant(participantsDb_t, struct sockaddr_in *addr);
int32_t udpc_nrParticipants(participantsDb_t);
int32_t udpc_addParticipant(participantsDb_t,
                             struct sockaddr_in *addr,
                             int capabilities,
                             unsigned int rcvbuf,
                             int pointopoint);
participantsDb_t udpc_makeParticipantsDb(void);
int udpc_getParticipantCapabilities(participantsDb_t db, int32_t i);
unsigned int udpc_getParticipantRcvBuf(participantsDb_t db, int32_t i);
struct sockaddr_in *udpc_getParticipantIp(participantsDb_t db, int32_t i);
void udpc_printNotSet(participantsDb_t db, char *d);
void udpc_printSet(participantsDb_t db, char *d);

#endif
