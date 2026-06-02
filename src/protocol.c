#include "protocol.h"
#include "player.h"
#include "level.h"
#include "entities.h"
#include "weapons.h"
#include "game.h"
#include "interact.h"
#include "net.h"
#include "menu.h"
#include <string.h>
#include <stdio.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t active, alive;
    char    name[32];
    float   px, py, pz;
    float   yaw, pitch;
    int16_t hp;
    int32_t points;
    uint8_t currentSlot;
    uint8_t perksMask;
    uint8_t invOwned[INV_SLOTS];
    uint8_t invPacked[INV_SLOTS];
    uint8_t invWeapon[INV_SLOTS];
    int16_t invAmmo[INV_SLOTS];
    int16_t invReserve[INV_SLOTS];
    float   invReloadTimer[INV_SLOTS];
    // Stats
    int16_t kills, headshots, meleeKills, revives;
    int32_t shotsFired, shotsHit;
    float   reviveAsTarget;
    uint8_t downed;
    float   bleedTimer;
    uint8_t highestRound;
    uint8_t lethals;
    uint8_t tacticals;
} SerPlayer;

typedef struct {
    float    px, py, pz;
    int16_t  hp, maxHp;
    uint8_t  state;
    uint8_t  type;
    uint8_t  targetWindow;
    float    bobPhase;
    float    stunTimer;
} SerEnemy;

typedef struct {
    float    px, py, pz;
    float    vx, vy, vz;
    float    fuse;
    uint8_t  kind;
    uint8_t  ownerPlayer;
} SerThrowable;

typedef struct {
    float    px, py, pz;
    float    vx, vy, vz;
    int16_t  damage;
    float    life;
    uint8_t  ownerPlayer;
} SerBullet;

typedef struct {
    uint8_t  type;
    uint8_t  roundNum;
    uint8_t  gameState;
    float    breakTimer;
    uint16_t enemiesAlive, enemiesToSpawn;
    uint8_t  windowBoards[MAX_WINDOWS];
    float    windowRepair[MAX_WINDOWS];
    uint8_t  papActive;
    float    papTimer;
    int8_t   papSlotInProgress;
    int8_t   papOwnerPlayer;
    uint8_t  doorsOpened;
    float    doublePointsTimer;
    float    instaKillTimer;
    // Mystery Box
    uint8_t  mboxState;
    float    mboxTimer;
    uint8_t  mboxShowingWeapon;
    uint8_t  mboxFinalWeapon;
    int8_t   mboxOwnerPlayer;
    uint16_t numEnemies;
    uint16_t numBullets;
    uint16_t numPowerUps;
    uint16_t numThrowables;
    SerPlayer players[NET_MAX_PLAYERS];
} PktSnapshotHeader;

typedef struct {
    uint8_t type;
    float   px, py, pz;
    float   bob;
    float   lifetime;
} SerPowerUp;
#pragma pack(pop)

static uint8_t snapshotBuf[16384];

static void SerializePlayer(SerPlayer *dst, Player *src) {
    dst->active = src->active;
    dst->alive  = src->alive;
    memcpy(dst->name, src->name, 32);
    dst->px = src->pos.x; dst->py = src->pos.y; dst->pz = src->pos.z;
    dst->yaw = src->yaw; dst->pitch = src->pitch;
    dst->hp = (int16_t)src->hp;
    dst->points = (int32_t)src->points;
    dst->currentSlot = (uint8_t)src->currentSlot;
    uint8_t mask = 0;
    for (int k = 0; k < PERK_COUNT; k++) if (src->hasPerk[k]) mask |= (uint8_t)(1u << k);
    dst->perksMask = mask;
    for (int s = 0; s < INV_SLOTS; s++) {
        dst->invOwned[s] = src->inventory[s].owned;
        dst->invPacked[s] = src->inventory[s].packed;
        dst->invWeapon[s] = (uint8_t)src->inventory[s].weaponIdx;
        dst->invAmmo[s]   = (int16_t)src->inventory[s].ammo;
        dst->invReserve[s]= (int16_t)src->inventory[s].reserve;
        dst->invReloadTimer[s] = src->inventory[s].reloadTimer;
    }
    dst->kills      = (int16_t)src->kills;
    dst->headshots  = (int16_t)src->headshots;
    dst->meleeKills = (int16_t)src->meleeKills;
    dst->revives    = (int16_t)src->revives;
    dst->shotsFired = (int32_t)src->shotsFired;
    dst->shotsHit   = (int32_t)src->shotsHit;
    dst->reviveAsTarget = src->reviveAsTarget;
    dst->downed       = src->downed ? 1 : 0;
    dst->bleedTimer   = src->bleedTimer;
    dst->highestRound = (uint8_t)(src->highestRound > 255 ? 255 : src->highestRound);
    dst->lethals      = (uint8_t)(src->lethals   > 255 ? 255 : (src->lethals   < 0 ? 0 : src->lethals));
    dst->tacticals    = (uint8_t)(src->tacticals > 255 ? 255 : (src->tacticals < 0 ? 0 : src->tacticals));
}

static void DeserializePlayer(Player *dst, SerPlayer *src, bool isLocal) {
    bool wasActive = dst->active;
    bool wasAlive  = dst->alive;
    dst->active = src->active;
    dst->alive  = src->alive;
    memcpy(dst->name, src->name, 32); dst->name[31] = 0;
    if (!isLocal) {
        dst->pos = (Vector3){ src->px, src->py, src->pz };
        dst->yaw = src->yaw; dst->pitch = src->pitch;
    } else if (!wasAlive && src->alive) {
        // Respawn for local: host moved us back to a spawn point, take it.
        dst->pos = (Vector3){ src->px, src->py, src->pz };
    }
    dst->hp = src->hp;
    dst->points = src->points;
    dst->currentSlot = src->currentSlot;
    for (int k = 0; k < PERK_COUNT; k++) dst->hasPerk[k] = (src->perksMask & (1u << k)) != 0;
    for (int s = 0; s < INV_SLOTS; s++) {
        dst->inventory[s].owned = src->invOwned[s];
        dst->inventory[s].packed = src->invPacked[s];
        dst->inventory[s].weaponIdx = src->invWeapon[s];
        dst->inventory[s].ammo = src->invAmmo[s];
        dst->inventory[s].reserve = src->invReserve[s];
        dst->inventory[s].reloadTimer = src->invReloadTimer[s];
    }
    dst->kills      = src->kills;
    dst->headshots  = src->headshots;
    dst->meleeKills = src->meleeKills;
    dst->revives    = src->revives;
    dst->shotsFired = src->shotsFired;
    dst->shotsHit   = src->shotsHit;
    dst->reviveAsTarget = src->reviveAsTarget;
    dst->downed       = src->downed != 0;
    dst->bleedTimer   = src->bleedTimer;
    dst->highestRound = src->highestRound;
    dst->lethals      = src->lethals;
    dst->tacticals    = src->tacticals;
    if (!wasActive && src->active && isLocal)
        dst->pos = (Vector3){ src->px, src->py, src->pz };
}

void Protocol_HostBroadcastSnapshot(void) {
    PktSnapshotHeader *hdr = (PktSnapshotHeader *)snapshotBuf;
    memset(hdr, 0, sizeof *hdr);
    hdr->type = PKT_SNAPSHOT;
    hdr->roundNum = (uint8_t)roundNum;
    hdr->gameState = (uint8_t)gamePhase;
    hdr->breakTimer = roundBreakTimer;
    hdr->enemiesAlive = (uint16_t)enemiesAlive;
    hdr->enemiesToSpawn = (uint16_t)enemiesToSpawn;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        hdr->windowBoards[i] = (uint8_t)windows[i].boards;
        hdr->windowRepair[i] = windows[i].repairProgress;
    }
    hdr->papActive = (pap.activeTimer > 0) ? 1 : 0;
    hdr->papTimer = pap.activeTimer;
    hdr->papSlotInProgress = (int8_t)pap.slotInProgress;
    hdr->papOwnerPlayer = (int8_t)pap.ownerPlayer;
    {
        uint8_t mask = 0;
        for (int i = 0; i < doorCount && i < 8; i++) if (doors[i].opened) mask |= (uint8_t)(1u << i);
        hdr->doorsOpened = mask;
    }
    hdr->doublePointsTimer = doublePointsTimer;
    hdr->instaKillTimer    = instaKillTimer;
    hdr->mboxState         = (uint8_t)mbox.state;
    hdr->mboxTimer         = mbox.timer;
    hdr->mboxShowingWeapon = (uint8_t)mbox.showingWeapon;
    hdr->mboxFinalWeapon   = (uint8_t)mbox.finalWeapon;
    hdr->mboxOwnerPlayer   = (int8_t)mbox.ownerPlayer;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) SerializePlayer(&hdr->players[i], &players[i]);

    size_t off = sizeof *hdr;
    uint16_t ne = 0;
    for (int i = 0; i < MAX_ENEMIES && off + sizeof(SerEnemy) <= sizeof snapshotBuf; i++) {
        if (!enemies[i].alive) continue;
        SerEnemy se = {
            .px = enemies[i].pos.x, .py = enemies[i].pos.y, .pz = enemies[i].pos.z,
            .hp = (int16_t)enemies[i].hp, .maxHp = (int16_t)enemies[i].maxHp,
            .state = (uint8_t)enemies[i].state, .type = (uint8_t)enemies[i].type,
            .targetWindow = (uint8_t)enemies[i].targetWindow,
            .bobPhase = enemies[i].bobPhase,
            .stunTimer = enemies[i].stunTimer,
        };
        memcpy(snapshotBuf + off, &se, sizeof se);
        off += sizeof se; ne++;
    }
    hdr->numEnemies = ne;

    uint16_t nb = 0;
    for (int i = 0; i < MAX_BULLETS && off + sizeof(SerBullet) <= sizeof snapshotBuf; i++) {
        if (!bullets[i].alive) continue;
        SerBullet sb = {
            .px = bullets[i].pos.x, .py = bullets[i].pos.y, .pz = bullets[i].pos.z,
            .vx = bullets[i].vel.x, .vy = bullets[i].vel.y, .vz = bullets[i].vel.z,
            .damage = (int16_t)bullets[i].damage,
            .life = bullets[i].life,
            .ownerPlayer = (uint8_t)bullets[i].ownerPlayer,
        };
        memcpy(snapshotBuf + off, &sb, sizeof sb);
        off += sizeof sb; nb++;
    }
    hdr->numBullets = nb;

    uint16_t np = 0;
    for (int i = 0; i < MAX_POWERUPS && off + sizeof(SerPowerUp) <= sizeof snapshotBuf; i++) {
        if (!powerUps[i].active) continue;
        SerPowerUp sp = {
            .type = (uint8_t)powerUps[i].type,
            .px = powerUps[i].pos.x, .py = powerUps[i].pos.y, .pz = powerUps[i].pos.z,
            .bob = powerUps[i].bob, .lifetime = powerUps[i].lifetime,
        };
        memcpy(snapshotBuf + off, &sp, sizeof sp);
        off += sizeof sp; np++;
    }
    hdr->numPowerUps = np;

    uint16_t nt = 0;
    for (int i = 0; i < MAX_THROWABLES && off + sizeof(SerThrowable) <= sizeof snapshotBuf; i++) {
        if (!throwables[i].alive) continue;
        SerThrowable st = {
            .px = throwables[i].pos.x, .py = throwables[i].pos.y, .pz = throwables[i].pos.z,
            .vx = throwables[i].vel.x, .vy = throwables[i].vel.y, .vz = throwables[i].vel.z,
            .fuse = throwables[i].fuse,
            .kind = (uint8_t)throwables[i].kind,
            .ownerPlayer = (uint8_t)throwables[i].ownerPlayer,
        };
        memcpy(snapshotBuf + off, &st, sizeof st);
        off += sizeof st; nt++;
    }
    hdr->numThrowables = nt;

    Net_Broadcast(snapshotBuf, off, false);
}

void Protocol_ClientApplySnapshot(uint8_t *data, size_t len) {
    if (len < sizeof(PktSnapshotHeader)) return;
    PktSnapshotHeader *hdr = (PktSnapshotHeader *)data;
    roundNum = hdr->roundNum;
    gamePhase = (GamePhase)hdr->gameState;
    roundBreakTimer = hdr->breakTimer;
    enemiesAlive = hdr->enemiesAlive;
    enemiesToSpawn = hdr->enemiesToSpawn;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].boards = hdr->windowBoards[i];
        windows[i].repairProgress = hdr->windowRepair[i];
    }
    pap.activeTimer = hdr->papTimer;
    pap.slotInProgress = hdr->papSlotInProgress;
    pap.ownerPlayer = hdr->papOwnerPlayer;
    for (int i = 0; i < doorCount && i < 8; i++) doors[i].opened = (hdr->doorsOpened & (1u << i)) != 0;
    doublePointsTimer = hdr->doublePointsTimer;
    instaKillTimer    = hdr->instaKillTimer;
    mbox.state         = hdr->mboxState;
    mbox.timer         = hdr->mboxTimer;
    mbox.showingWeapon = hdr->mboxShowingWeapon;
    mbox.finalWeapon   = hdr->mboxFinalWeapon;
    mbox.ownerPlayer   = hdr->mboxOwnerPlayer;

    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        DeserializePlayer(&players[i], &hdr->players[i], i == localPlayerIdx);

    size_t off = sizeof *hdr;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = false;
    for (int i = 0; i < hdr->numEnemies && off + sizeof(SerEnemy) <= len && i < MAX_ENEMIES; i++) {
        SerEnemy se;
        memcpy(&se, data + off, sizeof se); off += sizeof se;
        enemies[i].pos = (Vector3){ se.px, se.py, se.pz };
        enemies[i].hp = se.hp; enemies[i].maxHp = se.maxHp;
        enemies[i].state = (ZombieState)se.state;
        enemies[i].type  = (ZombieType)se.type;
        enemies[i].targetWindow = se.targetWindow;
        enemies[i].bobPhase = se.bobPhase;
        enemies[i].stunTimer = se.stunTimer;
        enemies[i].alive = true;
    }
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].alive = false;
    for (int i = 0; i < hdr->numBullets && off + sizeof(SerBullet) <= len && i < MAX_BULLETS; i++) {
        SerBullet sb;
        memcpy(&sb, data + off, sizeof sb); off += sizeof sb;
        bullets[i].pos = (Vector3){ sb.px, sb.py, sb.pz };
        bullets[i].vel = (Vector3){ sb.vx, sb.vy, sb.vz };
        bullets[i].damage = sb.damage;
        bullets[i].life = sb.life;
        bullets[i].ownerPlayer = sb.ownerPlayer;
        bullets[i].alive = true;
    }
    for (int i = 0; i < MAX_POWERUPS; i++) powerUps[i].active = false;
    for (int i = 0; i < hdr->numPowerUps && off + sizeof(SerPowerUp) <= len && i < MAX_POWERUPS; i++) {
        SerPowerUp sp;
        memcpy(&sp, data + off, sizeof sp); off += sizeof sp;
        powerUps[i].type = (PowerUpType)sp.type;
        powerUps[i].pos = (Vector3){ sp.px, sp.py, sp.pz };
        powerUps[i].bob = sp.bob;
        powerUps[i].lifetime = sp.lifetime;
        powerUps[i].active = true;
    }
    for (int i = 0; i < MAX_THROWABLES; i++) throwables[i].alive = false;
    for (int i = 0; i < hdr->numThrowables && off + sizeof(SerThrowable) <= len && i < MAX_THROWABLES; i++) {
        SerThrowable st;
        memcpy(&st, data + off, sizeof st); off += sizeof st;
        throwables[i].pos  = (Vector3){ st.px, st.py, st.pz };
        throwables[i].vel  = (Vector3){ st.vx, st.vy, st.vz };
        throwables[i].fuse = st.fuse;
        throwables[i].kind = (ThrowableKind)st.kind;
        throwables[i].ownerPlayer = st.ownerPlayer;
        throwables[i].alive = true;
    }
}

void Protocol_HostSendLobby(void) {
    PktLobby lob = { .type = PKT_LOBBY, .numPlayers = (uint8_t)Player_ActiveCount() };
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        lob.active[i] = players[i].active;
        memcpy(lob.names[i], players[i].name, 32);
    }
    Net_Broadcast(&lob, sizeof lob, true);
}

void Protocol_ClientSendInput(Player *p) {
    PktInput in = {
        .type = PKT_INPUT,
        .px = p->pos.x, .py = p->pos.y, .pz = p->pos.z,
        .yaw = p->yaw, .pitch = p->pitch,
        .currentSlot = (uint8_t)p->currentSlot,
        .fireDown = (uint8_t)(p->fireHeld ? 1 : 0),
        .interactHeld = (uint8_t)(p->interactHeld ? 1 : 0),
        .adsHeld = (uint8_t)(p->adsHeld ? 1 : 0),
    };
    Net_SendTo(0, &in, sizeof in, false);
}

void Protocol_HostHandlePacket(int peerIdx, uint8_t *data, size_t len) {
    if (len < 1) return;
    uint8_t type = data[0];
    if (type == PKT_HELLO && len >= sizeof(PktHello)) {
        PktHello *h = (PktHello *)data;
        if (h->proto != NET_PROTO_VERSION) {
            PktReject r = { .type = PKT_REJECT, .reason = REJECT_PROTO };
            Net_SendTo(peerIdx, &r, sizeof r, true);
            return;
        }
        Player_ResetForGame(peerIdx, h->name);
        PktWelcome w = { .type = PKT_WELCOME, .playerIdx = (uint8_t)peerIdx };
        Net_SendTo(peerIdx, &w, sizeof w, true);
        Protocol_HostSendLobby();
    }
    else if (type == PKT_INPUT && len >= sizeof(PktInput)) {
        PktInput *in = (PktInput *)data;
        if (peerIdx < 0 || peerIdx >= NET_MAX_PLAYERS) return;
        Player *p = &players[peerIdx];
        if (!p->active) return;
        p->pos = (Vector3){ in->px, in->py, in->pz };
        p->yaw = in->yaw; p->pitch = in->pitch;
        p->currentSlot = in->currentSlot < INV_SLOTS ? in->currentSlot : 0;
        p->fireHeld = in->fireDown != 0;
        p->interactHeld = in->interactHeld != 0;
        p->adsHeld = in->adsHeld != 0;
    }
    else if (type == PKT_ACTION && len >= sizeof(PktAction)) {
        PktAction *a = (PktAction *)data;
        if (peerIdx < 0 || peerIdx >= NET_MAX_PLAYERS) return;
        Player *p = &players[peerIdx];
        if (!p->active) return;
        if      (a->action == ACT_RELOAD)     Weapon_StartReload(p);
        else if (a->action == ACT_SWAP_SLOT)  Weapon_SwapSlot(p, a->arg);
        else if (a->action == ACT_INTERACT_F) Interact_Do(p);
        else if (a->action == ACT_MELEE)      Weapon_Melee(p);
        else if (a->action == ACT_THROW_LETHAL)   Throwables_Throw(p, TH_FRAG);
        else if (a->action == ACT_THROW_TACTICAL) Throwables_Throw(p, TH_STUN);
    }
}

void Protocol_ClientHandlePacket(uint8_t *data, size_t len, const char *playerName) {
    if (len < 1) return;
    uint8_t type = data[0];
    if (type == PKT_WELCOME && len >= sizeof(PktWelcome)) {
        PktWelcome *w = (PktWelcome *)data;
        localPlayerIdx = w->playerIdx;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
        Player_ResetForGame(localPlayerIdx, playerName);
        uiState = UI_CLIENT_LOBBY;
    }
    else if (type == PKT_REJECT && len >= sizeof(PktReject)) {
        PktReject *r = (PktReject *)data;
        snprintf(statusMsg, sizeof statusMsg,
                 (r->reason == REJECT_PROTO) ? "Protocol mismatch" : "Server full");
        Net_Shutdown();
        uiState = UI_MENU;
    }
    else if (type == PKT_LOBBY && len >= sizeof(PktLobby)) {
        PktLobby *l = (PktLobby *)data;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            players[i].active = l->active[i];
            memcpy(players[i].name, l->names[i], 32);
            players[i].name[31] = 0;
        }
    }
    else if (type == PKT_START && len >= sizeof(PktStart)) {
        PktStart *s = (PktStart *)data;
        // Match the host's selected map by name. If we don't have it locally,
        // fall back to whatever Level_Build picked up at startup.
        if (s->mapName[0]) {
            bool loaded = false;
            for (int i = 0; i < mapListCount; i++) {
                if (strncmp(mapList[i].name, s->mapName, sizeof mapList[0].name) == 0) {
                    if (Level_LoadFromFile(mapList[i].path)) { loaded = true; selectedMapIdx = i; }
                    break;
                }
            }
            if (!loaded) Level_Build();
            Level_Reset();
        }
        uiState = UI_PLAY;
    }
    else if (type == PKT_SNAPSHOT) {
        Protocol_ClientApplySnapshot(data, len);
        if (uiState == UI_CLIENT_LOBBY) uiState = UI_PLAY;
    }
}
