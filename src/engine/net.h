#ifndef SHOOTER_NET_H
#define SHOOTER_NET_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define NET_MAX_PLAYERS   4
#define NET_PORT_DEFAULT  7777
#define NET_PROTO_VERSION 11

typedef enum {
    NET_SOLO = 0,
    NET_HOST,
    NET_CLIENT,
} NetMode;

typedef enum {
    NEV_NONE = 0,
    NEV_CONNECT,
    NEV_DISCONNECT,
    NEV_RECEIVE,
} NetEventKind;

typedef struct {
    NetEventKind kind;
    int          peerIdx;  // host: 1..NET_MAX_PLAYERS-1 (clients); client: 0 (server)
    uint8_t     *data;
    size_t       len;
    void        *_packet; // internal
} NetEvent;

bool Net_InitHost(uint16_t port);
bool Net_InitClient(const char *ip, uint16_t port);
void Net_Shutdown(void);

// Poll up to `max` events. Returns count. After processing each event,
// call Net_FreeEvent(&events[i]) to release packet memory.
int  Net_Poll(NetEvent *events, int maxEvents);
void Net_FreeEvent(NetEvent *ev);

void Net_SendTo(int peerIdx, const void *data, size_t len, bool reliable);
void Net_Broadcast(const void *data, size_t len, bool reliable);

// Receive observer: fired for every received packet from inside Net_Poll, IN
// ADDITION to the NetEvent returned to the poller. This lets a second consumer
// (e.g. a voice-chat plugin) see incoming packets WITHOUT owning the single
// Net_Poll drain — it piggybacks on whoever already polls. `data` is valid only
// for the duration of the call; copy what you need. Game-clean (bytes only); one
// observer slot, NULL clears.
typedef void (*NetReceiveObserver)(int peerIdx, const uint8_t *data, size_t len);
void Net_SetReceiveObserver(NetReceiveObserver fn);

bool Net_IsConnected(void);

// Fills `ips` with up to `maxIps` non-loopback IPv4 strings. Returns count.
int  Net_GetLocalIPs(char ips[][64], int maxIps);

#endif // SHOOTER_NET_H

