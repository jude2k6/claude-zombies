#ifndef SHOOTER_PROTOCOL_H
#define SHOOTER_PROTOCOL_H

#include "types.h"

enum {
    PKT_HELLO = 1, PKT_WELCOME, PKT_REJECT, PKT_LOBBY, PKT_START,
    PKT_INPUT, PKT_ACTION, PKT_SNAPSHOT,
};

enum { ACT_RELOAD = 1, ACT_SWAP_SLOT, ACT_INTERACT_F, ACT_MELEE };
enum { REJECT_PROTO = 1, REJECT_FULL };

#pragma pack(push, 1)
typedef struct { uint8_t type, proto; char name[32]; } PktHello;
typedef struct { uint8_t type, playerIdx; } PktWelcome;
typedef struct { uint8_t type, reason; } PktReject;
typedef struct {
    uint8_t type, numPlayers;
    uint8_t active[NET_MAX_PLAYERS];
    char    names[NET_MAX_PLAYERS][32];
} PktLobby;
typedef struct { uint8_t type; } PktStart;
typedef struct {
    uint8_t type;
    float   px, py, pz;
    float   yaw, pitch;
    uint8_t currentSlot;
    uint8_t fireDown;
    uint8_t interactHeld;
    uint8_t adsHeld;
} PktInput;
typedef struct { uint8_t type, action, arg; } PktAction;
#pragma pack(pop)

void  Protocol_HostBroadcastSnapshot(void);
void  Protocol_ClientApplySnapshot(uint8_t *data, size_t len);
void  Protocol_HostSendLobby(void);
void  Protocol_ClientSendInput(Player *p);

// Network dispatchers
void  Protocol_HostHandlePacket(int peerIdx, uint8_t *data, size_t len);
void  Protocol_ClientHandlePacket(uint8_t *data, size_t len, const char *playerName);

#endif
