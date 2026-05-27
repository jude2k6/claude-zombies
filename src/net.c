#include "net.h"

#define ENET_IMPLEMENTATION 0
#include <enet/enet.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <iphlpapi.h>
#else
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/socket.h>
#endif

static bool         g_inited = false;
static NetMode      g_mode = NET_SOLO;
static ENetHost    *g_host = NULL;
static ENetPeer    *g_serverPeer = NULL;
static ENetPeer    *g_clientPeers[NET_MAX_PLAYERS] = {0};

static void EnsureInit(void) {
    if (!g_inited) {
        if (enet_initialize() != 0) {
            fprintf(stderr, "enet_initialize failed\n");
            return;
        }
        atexit(enet_deinitialize);
        g_inited = true;
    }
}

bool Net_InitHost(uint16_t port) {
    Net_Shutdown();
    EnsureInit();
    ENetAddress address = { 0 };
    address.host = ENET_HOST_ANY;
    address.port = port;
    g_host = enet_host_create(&address, NET_MAX_PLAYERS - 1, 2, 0, 0);
    if (!g_host) {
        fprintf(stderr, "enet_host_create (server) failed on port %u\n", port);
        return false;
    }
    g_mode = NET_HOST;
    return true;
}

bool Net_InitClient(const char *ip, uint16_t port) {
    Net_Shutdown();
    EnsureInit();
    g_host = enet_host_create(NULL, 1, 2, 0, 0);
    if (!g_host) {
        fprintf(stderr, "enet_host_create (client) failed\n");
        return false;
    }
    ENetAddress address = { 0 };
    enet_address_set_host(&address, ip);
    address.port = port;
    g_serverPeer = enet_host_connect(g_host, &address, 2, 0);
    if (!g_serverPeer) {
        fprintf(stderr, "enet_host_connect failed\n");
        enet_host_destroy(g_host);
        g_host = NULL;
        return false;
    }
    g_mode = NET_CLIENT;
    return true;
}

void Net_Shutdown(void) {
    if (g_host) {
        if (g_mode == NET_CLIENT && g_serverPeer) {
            enet_peer_disconnect_now(g_serverPeer, 0);
        } else if (g_mode == NET_HOST) {
            for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                if (g_clientPeers[i]) enet_peer_disconnect_now(g_clientPeers[i], 0);
            }
        }
        enet_host_destroy(g_host);
        g_host = NULL;
    }
    g_serverPeer = NULL;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) g_clientPeers[i] = NULL;
    g_mode = NET_SOLO;
}

static int AssignClientSlot(ENetPeer *peer) {
    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
        if (!g_clientPeers[i]) {
            g_clientPeers[i] = peer;
            peer->data = (void *)(intptr_t)i;
            return i;
        }
    }
    return -1;
}

static void ReleaseClientSlot(ENetPeer *peer) {
    if (!peer) return;
    int idx = (int)(intptr_t)peer->data;
    if (idx >= 0 && idx < NET_MAX_PLAYERS && g_clientPeers[idx] == peer) {
        g_clientPeers[idx] = NULL;
    }
}

int Net_Poll(NetEvent *out, int maxEvents) {
    if (!g_host || maxEvents <= 0) return 0;
    int n = 0;
    ENetEvent ev;
    while (n < maxEvents && enet_host_service(g_host, &ev, 0) > 0) {
        switch (ev.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                int peerIdx = 0;
                if (g_mode == NET_HOST) {
                    peerIdx = AssignClientSlot(ev.peer);
                    if (peerIdx < 0) {
                        enet_peer_disconnect_now(ev.peer, 0);
                        continue;
                    }
                }
                out[n++] = (NetEvent){ .kind = NEV_CONNECT, .peerIdx = peerIdx };
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                int peerIdx = (g_mode == NET_HOST) ? (int)(intptr_t)ev.peer->data : 0;
                if (g_mode == NET_HOST) ReleaseClientSlot(ev.peer);
                out[n++] = (NetEvent){ .kind = NEV_DISCONNECT, .peerIdx = peerIdx };
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                int peerIdx = (g_mode == NET_HOST) ? (int)(intptr_t)ev.peer->data : 0;
                out[n++] = (NetEvent){
                    .kind = NEV_RECEIVE,
                    .peerIdx = peerIdx,
                    .data = ev.packet->data,
                    .len  = ev.packet->dataLength,
                    ._packet = ev.packet,
                };
                break;
            }
            default: break;
        }
    }
    return n;
}

void Net_FreeEvent(NetEvent *ev) {
    if (ev && ev->_packet) {
        enet_packet_destroy((ENetPacket *)ev->_packet);
        ev->_packet = NULL;
        ev->data = NULL;
    }
}

void Net_SendTo(int peerIdx, const void *data, size_t len, bool reliable) {
    if (!g_host) return;
    ENetPeer *peer = NULL;
    if (g_mode == NET_CLIENT) {
        peer = g_serverPeer;
    } else if (g_mode == NET_HOST) {
        if (peerIdx < 1 || peerIdx >= NET_MAX_PLAYERS) return;
        peer = g_clientPeers[peerIdx];
    }
    if (!peer) return;
    ENetPacket *pkt = enet_packet_create(data, len,
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_peer_send(peer, reliable ? 0 : 1, pkt);
}

void Net_Broadcast(const void *data, size_t len, bool reliable) {
    if (!g_host) return;
    if (g_mode != NET_HOST) return;
    ENetPacket *pkt = enet_packet_create(data, len,
        reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    enet_host_broadcast(g_host, reliable ? 0 : 1, pkt);
}

bool Net_IsConnected(void) {
    if (g_mode == NET_CLIENT) {
        return g_serverPeer && g_serverPeer->state == ENET_PEER_STATE_CONNECTED;
    }
    return g_host != NULL;
}

int Net_GetLocalIPs(char ips[][64], int maxIps) {
    int n = 0;
#ifdef _WIN32
    ULONG bufLen = 16384;
    PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)malloc(bufLen);
    if (!addrs) return 0;
    ULONG ret = GetAdaptersAddresses(AF_INET,
        GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST,
        NULL, addrs, &bufLen);
    if (ret == NO_ERROR) {
        for (PIP_ADAPTER_ADDRESSES a = addrs; a && n < maxIps; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (PIP_ADAPTER_UNICAST_ADDRESS ua = a->FirstUnicastAddress; ua && n < maxIps; ua = ua->Next) {
                if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
                struct sockaddr_in *sin = (struct sockaddr_in *)ua->Address.lpSockaddr;
                char ipstr[64];
                if (inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof ipstr)) {
                    strncpy(ips[n], ipstr, 63);
                    ips[n][63] = 0;
                    n++;
                }
            }
        }
    }
    free(addrs);
#else
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) != 0) return 0;
    for (ifa = ifap; ifa && n < maxIps; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        if (!(ifa->ifa_flags & IFF_UP)) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        char ipstr[64];
        if (inet_ntop(AF_INET, &sin->sin_addr, ipstr, sizeof ipstr)) {
            strncpy(ips[n], ipstr, 63);
            ips[n][63] = 0;
            n++;
        }
    }
    freeifaddrs(ifap);
#endif
    return n;
}
