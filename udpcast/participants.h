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
    int16_t nrParticipants;

    struct clientDesc {
        struct sockaddr_in addr;
        int used;
        uint32_t capabilities;
        uint32_t rcvbuf;
    } clientTable[MAX_CLIENTS];
};

typedef struct participantsDb *participantsDb_t;

int udpc_isParticipantValid(participantsDb_t ,int32_t slot);
int udpc_removeParticipant(participantsDb_t, int32_t slot);
int16_t udpc_lookupParticipant(participantsDb_t, struct sockaddr_in *addr);
int16_t udpc_nrParticipants(participantsDb_t);
int16_t udpc_addParticipant(participantsDb_t,
                             struct sockaddr_in *addr,
                             uint32_t capabilities,
                             uint32_t rcvbuf,
                             int pointopoint);
participantsDb_t udpc_makeParticipantsDb(void);
uint32_t udpc_getParticipantCapabilities(participantsDb_t db, int16_t i);
uint32_t udpc_getParticipantRcvBuf(participantsDb_t db, int16_t i);
struct sockaddr_in *udpc_getParticipantIp(participantsDb_t db, int16_t i);
void udpc_printNotSet(participantsDb_t db, char *d);
void udpc_printSet(participantsDb_t db, char *d);

#endif
