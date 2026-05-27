#include "raylib.h"
#include "raymath.h"
#include "rcamera.h"
#include "rlgl.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#include "net.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

// ============================================================================
//  Constants
// ============================================================================

#define WINDOW_W_DEFAULT 1280
#define WINDOW_H_DEFAULT 720

#define ARENA_HALF        40.0f
#define WALL_HEIGHT        5.0f
#define WALL_THICK         1.0f
#define PLAYER_EYE         1.7f
#define PLAYER_RADIUS      0.4f
#define BASE_MOVE_SPEED    7.0f

#define MAX_ENEMIES       48
#define ENEMY_RADIUS       0.6f
#define ENEMY_HEIGHT       1.7f
#define ENEMY_TOUCH_COOLDOWN 0.7f
#define ENEMY_DAMAGE       8
#define ENEMY_BOARD_ATK    2.4f

#define MAX_BULLETS      128
#define BULLET_SPEED      90.0f
#define BULLET_LIFE        1.0f

#define MAX_OBSTACLES     12
#define MAX_INTERIOR_WALLS 8
#define MAX_DOORS          4

#define INV_SLOTS          2
#define INTERACT_DIST      3.0f
#define DOOR_INTERACT_DIST 3.5f

#define HIT_POINTS        10
#define KILL_POINTS       50
#define BOARD_REPAIR_PTS  10
#define BOARD_REPAIR_TIME 0.5f

#define PAP_COST          5000
#define PAP_DURATION      4.0f

#define MAX_WINDOWS        4
#define MAX_BOARDS_PER_WIN 5
#define WINDOW_WIDTH       4.0f

#define SNAPSHOT_HZ        20.0f
#define INPUT_HZ           30.0f

// ============================================================================
//  Weapons / perks (same as before)
// ============================================================================

typedef struct {
    const char *name;
    const char *packedName;
    int    damage;
    int    magSize;
    int    reserveMax;
    float  fireCooldown;
    float  reloadTime;
    int    pellets;
    float  spreadDeg;
    bool   automatic;
    int    buyPrice;
    int    ammoPrice;
    Color  tint;
} WeaponDef;

enum { W_PISTOL = 0, W_SMG, W_SHOTGUN, W_RIFLE, W_RAYGUN, W_COUNT };

static const WeaponDef WEAPONS[W_COUNT] = {
    [W_PISTOL]  = { "M1911",   "Mustang",     12, 12, 120, 0.20f, 1.3f, 1, 0.0f, false,     0,   100, (Color){200,200,200,255} },
    [W_SMG]     = { "MP5",     "MP5-K+",       9, 30, 180, 0.07f, 1.8f, 1, 1.5f, true,   1000,   600, (Color){220,170, 70,255} },
    [W_SHOTGUN] = { "Olympia", "Hades",       22,  2,  24, 0.55f, 2.0f, 6, 6.0f, false,   500,   300, (Color){170, 90, 50,255} },
    [W_RIFLE]   = { "M14",     "Mnesia",      34, 10,  90, 0.28f, 1.5f, 1, 0.4f, false,  1200,   600, (Color){100,140,100,255} },
    [W_RAYGUN]  = { "Ray Gun", "Porter's X2", 95, 20,  80, 0.22f, 2.6f, 1, 0.0f, true,  10000,  4500, (Color){120,255,160,255} },
};

typedef struct {
    int   weaponIdx;
    int   ammo;
    int   reserve;
    float fireTimer;
    float reloadTimer;
    bool  owned;
    bool  packed;
} WeaponSlot;

enum { PERK_JUG = 0, PERK_SPEED, PERK_DTAP, PERK_STAMIN, PERK_COUNT };

typedef struct {
    const char *name;
    int    cost;
    Color  tint;
} PerkDef;

static const PerkDef PERKS[PERK_COUNT] = {
    [PERK_JUG]    = { "Juggernog",   2500, (Color){220, 40, 40, 255} },
    [PERK_SPEED]  = { "Speed Cola",  3000, (Color){ 40,180, 80, 255} },
    [PERK_DTAP]   = { "Double Tap",  2000, (Color){240,180, 40, 255} },
    [PERK_STAMIN] = { "Stamin-Up",   2000, (Color){ 60,140,220, 255} },
};

typedef struct {
    Vector3 pos;
    int     perkIdx;
} PerkMachine;

// ============================================================================
//  Player
// ============================================================================

typedef struct {
    bool       active;     // slot in use (assigned to a connected client or host)
    bool       alive;
    char       name[32];
    Vector3    pos;
    float      yaw, pitch;
    int        hp;
    int        points;
    WeaponSlot inventory[INV_SLOTS];
    int        currentSlot;
    bool       hasPerk[PERK_COUNT];

    // Server-side input state (set from local input or received PKT_INPUT)
    bool       fireHeld;
    bool       interactHeld;   // E held (for repair)

    // Effects
    float      damageFlash;
} Player;

static const Color PLAYER_COLORS[NET_MAX_PLAYERS] = {
    {220, 80, 80, 255},
    {80, 120, 220, 255},
    {90, 200, 90, 255},
    {220, 200, 80, 255},
};

// ============================================================================
//  Game world
// ============================================================================

typedef enum { ZS_OUTSIDE, ZS_AT_WINDOW, ZS_INSIDE } ZombieState;

typedef struct {
    Vector3      pos;
    int          hp;
    int          maxHp;
    bool         alive;
    float        touchTimer;
    float        bobPhase;
    float        speed;
    ZombieState  state;
    int          targetWindow;
    float        attackTimer;
    int          targetPlayer;     // chase this player while INSIDE
} Enemy;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    int     damage;
    float   life;
    bool    alive;
    int     ownerPlayer;            // who fired this bullet (for points)
} Bullet;

typedef struct {
    Vector3 center;
    Vector3 size;
} Box;

typedef struct {
    Vector3 pos;
    Vector3 normal;
    Vector3 tangent;
    int     boards;
    float   repairProgress;         // per-window; only one player repairs at a time
    int     repairPlayer;
    int     lockedByDoor;           // -1 if always accessible
} Window3D;

typedef struct {
    Box  box;
    int  cost;
    bool opened;
} Door;

typedef struct {
    Vector3 pos;
    Vector3 normal;
    int     weaponIdx;
} WallBuy;

typedef struct {
    Vector3 pos;
    float   activeTimer;
    int     slotInProgress;        // -1 = idle
    int     ownerPlayer;           // -1 = idle
    float   bob;
} PackAPunch;

typedef enum {
    GS_PRE_GAME, GS_PLAY, GS_ROUND_BREAK, GS_GAME_OVER
} GamePhase;

typedef enum {
    UI_MENU,
    UI_SETTINGS,
    UI_JOIN_INPUT,
    UI_CONNECTING,
    UI_HOST_LOBBY,
    UI_CLIENT_LOBBY,
    UI_PLAY,
    UI_PAUSE,
} UiState;

typedef enum {
    IK_NONE, IK_WALLBUY, IK_PERK, IK_PAP, IK_WINDOW, IK_DOOR
} InteractKind;

typedef struct { InteractKind kind; int idx; float dist; } Interact;

// ============================================================================
//  Globals
// ============================================================================

static Box         obstacles[MAX_OBSTACLES];
static int         obstacleCount = 0;
static Box         interiorWalls[MAX_INTERIOR_WALLS];
static int         interiorWallCount = 0;
static Door        doors[MAX_DOORS];
static int         doorCount = 0;
static WallBuy     wallBuys[8];
static int         wallBuyCount = 0;
static Window3D    windows[MAX_WINDOWS];
static int         windowCount = 0;
static PerkMachine perkMachines[PERK_COUNT];
static int         perkMachineCount = 0;
static PackAPunch  pap;

static Enemy       enemies[MAX_ENEMIES];
static Bullet      bullets[MAX_BULLETS];

static Player      players[NET_MAX_PLAYERS];

static int         roundNum = 0;
static int         enemiesAlive = 0;
static int         enemiesToSpawn = 0;
static float       spawnTimer = 0.0f;
static float       roundBreakTimer = 0.0f;
static GamePhase   gamePhase = GS_PRE_GAME;

static UiState     uiState = UI_MENU;
static UiState     prevUi = UI_PAUSE;

static NetMode     netMode = NET_SOLO;
static int         localPlayerIdx = 0;

static float       snapshotAccum = 0.0f;
static float       inputAccum = 0.0f;

// Settings
static float       mouseSens   = 0.0025f;
static float       fovSetting  = 75.0f;
static bool        fullscreen  = false;
static int         windowedW   = WINDOW_W_DEFAULT;
static int         windowedH   = WINDOW_H_DEFAULT;
static char        playerName[32] = "Player";
static bool        nameEditing = false;
static char        joinIp[64]  = "127.0.0.1";
static bool        joinIpEditing = false;
static char        statusMsg[128] = "";

static char        hostIps[8][64];
static int         hostIpCount = 0;

// Cheat: toggled with F3. Only fully effective in solo or as host
// (clients can toggle the indicator but the server is authoritative on HP).
static bool        godMode = false;

static Camera      camera;

// Local fire-edge tracker (for non-auto weapons we still rely on key-press
// semantics on the server side; we send fireHeld and let server cooldown-gate)
static float       muzzleFlash = 0.0f;

// ============================================================================
//  Helpers
// ============================================================================

static float RandRange(float a, float b) {
    return a + ((float)rand() / (float)RAND_MAX) * (b - a);
}

static bool IsHost(void)    { return netMode != NET_CLIENT; } // host or solo
static bool IsClient(void)  { return netMode == NET_CLIENT; }

static int ActivePlayerCount(void) {
    int n = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (players[i].active) n++;
    return n;
}
static int AliveActiveCount(void) {
    int n = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) if (players[i].active && players[i].alive) n++;
    return n;
}

static BoundingBox BoxToBB(Box b) {
    return (BoundingBox){
        (Vector3){ b.center.x - b.size.x*0.5f, b.center.y - b.size.y*0.5f, b.center.z - b.size.z*0.5f },
        (Vector3){ b.center.x + b.size.x*0.5f, b.center.y + b.size.y*0.5f, b.center.z + b.size.z*0.5f }
    };
}

static bool CircleHitsBoxXZ(Vector3 p, float r, Box b) {
    float minX = b.center.x - b.size.x*0.5f, maxX = b.center.x + b.size.x*0.5f;
    float minZ = b.center.z - b.size.z*0.5f, maxZ = b.center.z + b.size.z*0.5f;
    float cx = Clamp(p.x, minX, maxX);
    float cz = Clamp(p.z, minZ, maxZ);
    float dx = p.x - cx, dz = p.z - cz;
    return (dx*dx + dz*dz) < (r*r);
}

static Vector3 ResolveXZ(Vector3 from, Vector3 to, float radius, bool clampArena) {
    Vector3 candidate = to;
    if (clampArena) {
        float lim = ARENA_HALF - radius;
        if (candidate.x >  lim) candidate.x =  lim;
        if (candidate.x < -lim) candidate.x = -lim;
        if (candidate.z >  lim) candidate.z =  lim;
        if (candidate.z < -lim) candidate.z = -lim;
    }
    Vector3 stepX = (Vector3){ candidate.x, from.y, from.z };
    for (int i = 0; i < obstacleCount; i++)
        if (CircleHitsBoxXZ(stepX, radius, obstacles[i])) { stepX.x = from.x; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (CircleHitsBoxXZ(stepX, radius, interiorWalls[i])) { stepX.x = from.x; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && CircleHitsBoxXZ(stepX, radius, doors[i].box)) { stepX.x = from.x; break; }

    Vector3 stepZ = (Vector3){ stepX.x, from.y, candidate.z };
    for (int i = 0; i < obstacleCount; i++)
        if (CircleHitsBoxXZ(stepZ, radius, obstacles[i])) { stepZ.z = from.z; break; }
    for (int i = 0; i < interiorWallCount; i++)
        if (CircleHitsBoxXZ(stepZ, radius, interiorWalls[i])) { stepZ.z = from.z; break; }
    for (int i = 0; i < doorCount; i++)
        if (!doors[i].opened && CircleHitsBoxXZ(stepZ, radius, doors[i].box)) { stepZ.z = from.z; break; }

    return stepZ;
}

static bool PointBlocked(Vector3 p, float pad) {
    for (int i = 0; i < obstacleCount; i++) {
        Box b = obstacles[i];
        b.size.x += pad*2; b.size.z += pad*2;
        if (CircleHitsBoxXZ(p, 0.01f, b)) return true;
    }
    return false;
}

static Vector3 LookDir(float yaw, float pitch) {
    float cp = cosf(pitch);
    return (Vector3){ sinf(yaw) * cp, sinf(pitch), -cosf(yaw) * cp };
}

// ============================================================================
//  Effective stats
// ============================================================================

static int EffDamage(Player *p, WeaponSlot *s) {
    int d = WEAPONS[s->weaponIdx].damage;
    if (s->packed) d = (int)(d * 2.5f);
    if (p->hasPerk[PERK_DTAP]) d = (int)(d * 1.33f);
    return d;
}
static float EffFireCD(Player *p, WeaponSlot *s) {
    float c = WEAPONS[s->weaponIdx].fireCooldown;
    if (p->hasPerk[PERK_DTAP]) c *= 0.72f;
    return c;
}
static float EffReload(Player *p, WeaponSlot *s) {
    float r = WEAPONS[s->weaponIdx].reloadTime;
    if (p->hasPerk[PERK_SPEED]) r *= 0.5f;
    return r;
}
static int EffMagSize(WeaponSlot *s) {
    int m = WEAPONS[s->weaponIdx].magSize;
    if (s->packed) m *= 2;
    return m;
}
static int EffReserveMax(WeaponSlot *s) {
    int r = WEAPONS[s->weaponIdx].reserveMax;
    if (s->packed) r += 60;
    return r;
}
static int EffMaxHP(Player *p)        { return p->hasPerk[PERK_JUG]    ? 250 : 100; }
static float EffMoveSpeed(Player *p)  { return BASE_MOVE_SPEED * (p->hasPerk[PERK_STAMIN] ? 1.4f : 1.0f); }

static int FindOwnedSlot(Player *p, int weaponIdx) {
    for (int i = 0; i < INV_SLOTS; i++)
        if (p->inventory[i].owned && p->inventory[i].weaponIdx == weaponIdx) return i;
    return -1;
}
static int FirstEmptySlot(Player *p) {
    for (int i = 0; i < INV_SLOTS; i++) if (!p->inventory[i].owned) return i;
    return -1;
}

// ============================================================================
//  Level / spawns / setup
// ============================================================================

static void BuildLevel(void) {
    obstacleCount = 0;
    Box layout[] = {
        { (Vector3){  10,  1.5f,   5 }, (Vector3){ 4, 3, 4 } },
        { (Vector3){ -12,  1.0f,  -8 }, (Vector3){ 6, 2, 3 } },
        { (Vector3){   0,  1.5f, -18 }, (Vector3){ 8, 3, 2 } },
        { (Vector3){  18,  1.0f, -15 }, (Vector3){ 3, 2, 6 } },
        { (Vector3){ -20,  1.5f,  12 }, (Vector3){ 5, 3, 5 } },
        { (Vector3){   8,  1.0f,  20 }, (Vector3){ 7, 2, 2 } },
        { (Vector3){  -6,  1.5f,  26 }, (Vector3){ 3, 3, 3 } },
        { (Vector3){  25,  1.0f,   2 }, (Vector3){ 2, 2, 8 } },
    };
    int n = (int)(sizeof(layout) / sizeof(layout[0]));
    for (int i = 0; i < n && obstacleCount < MAX_OBSTACLES; i++) obstacles[obstacleCount++] = layout[i];

    wallBuyCount = 0;
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){ 15.0f, 2.0f,  ARENA_HALF - 0.55f }, (Vector3){ 0,0,-1}, W_SHOTGUN };
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){ ARENA_HALF - 0.55f, 2.0f, -10.0f }, (Vector3){-1,0, 0}, W_SMG };
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){-15.0f, 2.0f, -ARENA_HALF + 0.55f }, (Vector3){ 0,0, 1}, W_RIFLE };
    wallBuys[wallBuyCount++] = (WallBuy){ (Vector3){-ARENA_HALF + 0.55f, 2.0f, 18.0f }, (Vector3){ 1,0, 0}, W_RAYGUN };

    windowCount = 0;
    windows[windowCount++] = (Window3D){
        .pos = { -18.0f, WALL_HEIGHT*0.5f,  ARENA_HALF }, .normal = { 0, 0, -1 }, .tangent = { 1, 0, 0 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 };
    windows[windowCount++] = (Window3D){
        .pos = { ARENA_HALF, WALL_HEIGHT*0.5f, 18.0f }, .normal = { -1, 0, 0 }, .tangent = { 0, 0, 1 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 };
    windows[windowCount++] = (Window3D){
        .pos = { 18.0f, WALL_HEIGHT*0.5f, -ARENA_HALF }, .normal = { 0, 0, 1 }, .tangent = { 1, 0, 0 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = 0 };  // south-room window
    windows[windowCount++] = (Window3D){
        .pos = { -ARENA_HALF, WALL_HEIGHT*0.5f, -8.0f }, .normal = { 1, 0, 0 }, .tangent = { 0, 0, 1 },
        .boards = MAX_BOARDS_PER_WIN, .repairPlayer = -1, .lockedByDoor = -1 };

    // Interior wall splitting the arena north/south at z = -20, with a 6m door gap at x=0
    interiorWallCount = 0;
    interiorWalls[interiorWallCount++] = (Box){ (Vector3){ -21.5f, WALL_HEIGHT*0.5f, -20.0f }, (Vector3){ 37.0f, WALL_HEIGHT, 1.0f } };
    interiorWalls[interiorWallCount++] = (Box){ (Vector3){  21.5f, WALL_HEIGHT*0.5f, -20.0f }, (Vector3){ 37.0f, WALL_HEIGHT, 1.0f } };

    // Doors (filling the openings in the interior wall)
    doorCount = 0;
    doors[doorCount++] = (Door){
        .box = { (Vector3){ 0, WALL_HEIGHT*0.5f, -20.0f }, (Vector3){ 6.0f, WALL_HEIGHT, 1.0f } },
        .cost = 1500, .opened = false,
    };

    perkMachineCount = 0;
    perkMachines[perkMachineCount++] = (PerkMachine){ { -8.0f, 0, 12.0f }, PERK_JUG };
    perkMachines[perkMachineCount++] = (PerkMachine){ {  4.0f, 0, -8.0f }, PERK_SPEED };
    perkMachines[perkMachineCount++] = (PerkMachine){ { 22.0f, 0, 25.0f }, PERK_DTAP };
    perkMachines[perkMachineCount++] = (PerkMachine){ {-25.0f, 0,-22.0f }, PERK_STAMIN };

    pap = (PackAPunch){ .pos = { 0, 0, -28.0f }, .activeTimer = 0, .slotInProgress = -1, .ownerPlayer = -1 };
}

static Vector3 PlayerSpawn(int idx) {
    static const Vector3 SP[NET_MAX_PLAYERS] = {
        { 0, PLAYER_EYE,  0 },
        { 2, PLAYER_EYE,  0 },
        {-2, PLAYER_EYE,  0 },
        { 0, PLAYER_EYE,  2 },
    };
    return SP[idx];
}

static void InitPlayerInventory(Player *p) {
    p->inventory[0] = (WeaponSlot){ .weaponIdx = W_PISTOL,
                                    .ammo = WEAPONS[W_PISTOL].magSize,
                                    .reserve = WEAPONS[W_PISTOL].reserveMax,
                                    .owned = true };
    p->inventory[1] = (WeaponSlot){ .owned = false };
    p->currentSlot = 0;
    for (int k = 0; k < PERK_COUNT; k++) p->hasPerk[k] = false;
    p->hp = 100;
    p->points = 500;
    p->alive = true;
}

static void ResetPlayerForGame(int idx, const char *name) {
    Player *p = &players[idx];
    memset(p, 0, sizeof *p);
    p->active = true;
    p->alive = true;
    if (name) strncpy(p->name, name, sizeof p->name - 1);
    p->pos = PlayerSpawn(idx);
    p->yaw = 0.0f; p->pitch = 0.0f;
    InitPlayerInventory(p);
}

// ============================================================================
//  Spawning / enemies / bullets
// ============================================================================

static int   RoundEnemyHP(int r)    { return 30 + r * 22; }
static float RoundEnemySpeed(int r) { float s = 1.6f + r * 0.10f; return s > 4.5f ? 4.5f : s; }
static int   RoundSpawnCount(int r) {
    int base = 6 + r * 2;
    int active = ActivePlayerCount();
    if (active < 1) active = 1;
    return base + (active - 1) * 4;  // a bit more per player
}

static int CountAlive(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) n++;
    return n;
}

static int NearestAlivePlayer(Vector3 pos) {
    int best = -1; float bestD = 1e9f;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!players[i].active || !players[i].alive) continue;
        float dx = players[i].pos.x - pos.x, dz = players[i].pos.z - pos.z;
        float d = dx*dx + dz*dz;
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

static void TrySpawnEnemy(void) {
    if (windowCount == 0) return;
    int accessible[MAX_WINDOWS]; int na = 0;
    for (int i = 0; i < windowCount; i++) {
        int lk = windows[i].lockedByDoor;
        if (lk >= 0 && lk < doorCount && !doors[lk].opened) continue;
        accessible[na++] = i;
    }
    if (na == 0) return;
    int wi = accessible[rand() % na];
    Window3D *w = &windows[wi];

    Vector3 spawn = Vector3Subtract(w->pos, Vector3Scale(w->normal, RandRange(4.0f, 6.0f)));
    spawn = Vector3Add(spawn, Vector3Scale(w->tangent, RandRange(-1.2f, 1.2f)));
    spawn.y = ENEMY_HEIGHT * 0.5f;

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) {
            enemies[i] = (Enemy){
                .pos = spawn,
                .hp = RoundEnemyHP(roundNum), .maxHp = RoundEnemyHP(roundNum),
                .alive = true,
                .bobPhase = RandRange(0, 6.28f),
                .speed = RoundEnemySpeed(roundNum),
                .state = ZS_OUTSIDE,
                .targetWindow = wi,
                .targetPlayer = -1,
            };
            enemiesAlive++;
            enemiesToSpawn--;
            return;
        }
    }
}

static void StartRound(int r) {
    roundNum = r;
    enemiesToSpawn = RoundSpawnCount(r);
    enemiesAlive   = 0;
    spawnTimer = 1.0f;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].alive = false;
    for (int i = 0; i < MAX_BULLETS; i++) bullets[i].alive = false;
    for (int i = 0; i < windowCount; i++) {
        windows[i].boards = MAX_BOARDS_PER_WIN;
        windows[i].repairProgress = 0;
        windows[i].repairPlayer = -1;
    }
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (players[i].active && !players[i].alive) {
            players[i].alive = true;
            players[i].hp = EffMaxHP(&players[i]);
            // free top-up of current weapon mag
            WeaponSlot *s = &players[i].inventory[players[i].currentSlot];
            if (s->owned) {
                int need = EffMagSize(s) - s->ammo;
                int take = (need < s->reserve) ? need : s->reserve;
                s->ammo += take; s->reserve -= take;
            }
        }
    }
    gamePhase = GS_PLAY;
}

static void UpdateSpawns(float dt) {
    if (enemiesToSpawn <= 0) return;
    if (CountAlive() >= 24 + (ActivePlayerCount() - 1) * 4) return;
    spawnTimer -= dt;
    if (spawnTimer <= 0) {
        TrySpawnEnemy();
        float gap = 1.2f - roundNum * 0.05f;
        if (gap < 0.35f) gap = 0.35f;
        spawnTimer = gap;
    }
}

static void SeparateEnemies(void) {
    float minD = ENEMY_RADIUS * 1.9f;
    float minD2 = minD * minD;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        if (enemies[i].state == ZS_AT_WINDOW) continue;
        for (int j = i + 1; j < MAX_ENEMIES; j++) {
            if (!enemies[j].alive) continue;
            if (enemies[j].state == ZS_AT_WINDOW) continue;
            float dx = enemies[i].pos.x - enemies[j].pos.x;
            float dz = enemies[i].pos.z - enemies[j].pos.z;
            float d2 = dx*dx + dz*dz;
            if (d2 < minD2 && d2 > 1e-6f) {
                float d = sqrtf(d2);
                float push = (minD - d) * 0.5f;
                float nx = dx / d, nz = dz / d;
                enemies[i].pos.x += nx * push;
                enemies[i].pos.z += nz * push;
                enemies[j].pos.x -= nx * push;
                enemies[j].pos.z -= nz * push;
            }
        }
    }
    // Re-clamp INSIDE zombies to the arena after separation
    float lim = ARENA_HALF - ENEMY_RADIUS;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive || enemies[i].state != ZS_INSIDE) continue;
        enemies[i].pos.x = Clamp(enemies[i].pos.x, -lim, lim);
        enemies[i].pos.z = Clamp(enemies[i].pos.z, -lim, lim);
    }
}

static void UpdateEnemies(float dt) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        Enemy *e = &enemies[i];
        e->bobPhase += dt * 4.0f;
        if (e->touchTimer > 0) e->touchTimer -= dt;

        if (e->state == ZS_OUTSIDE) {
            Window3D *w = &windows[e->targetWindow];
            Vector3 to = Vector3Subtract(w->pos, e->pos);
            to.y = 0;
            float d = Vector3Length(to);
            if (d < 1.2f) {
                e->state = ZS_AT_WINDOW;
                e->attackTimer = ENEMY_BOARD_ATK * 0.4f;
                e->pos = (Vector3){ w->pos.x - w->normal.x * 0.5f, ENEMY_HEIGHT*0.5f, w->pos.z - w->normal.z * 0.5f };
            } else {
                Vector3 dir = Vector3Scale(to, 1.0f / d);
                e->pos = Vector3Add(e->pos, Vector3Scale(dir, e->speed * 0.7f * dt));
            }
        }
        else if (e->state == ZS_AT_WINDOW) {
            Window3D *w = &windows[e->targetWindow];
            if (w->boards <= 0) {
                e->pos = Vector3Add(w->pos, Vector3Scale(w->normal, 1.6f));
                e->pos.y = ENEMY_HEIGHT*0.5f;
                e->state = ZS_INSIDE;
                e->targetPlayer = NearestAlivePlayer(e->pos);
            } else {
                e->attackTimer -= dt;
                if (e->attackTimer <= 0) {
                    w->boards--;
                    e->attackTimer = ENEMY_BOARD_ATK;
                }
            }
        }
        else { // ZS_INSIDE
            // Re-pick target periodically
            if (e->targetPlayer < 0 || !players[e->targetPlayer].active || !players[e->targetPlayer].alive
                || (rand() % 60) == 0) {
                e->targetPlayer = NearestAlivePlayer(e->pos);
            }
            if (e->targetPlayer < 0) continue; // no living targets
            Player *tp = &players[e->targetPlayer];
            Vector3 to = Vector3Subtract(tp->pos, e->pos);
            to.y = 0;
            float d = Vector3Length(to);
            if (d > 0.01f) {
                Vector3 dir = Vector3Scale(to, 1.0f / d);
                Vector3 want = Vector3Add(e->pos, Vector3Scale(dir, e->speed * dt));
                e->pos = ResolveXZ(e->pos, want, ENEMY_RADIUS, true);
            }

            Vector2 pXZ = { tp->pos.x, tp->pos.z };
            Vector2 eXZ = { e->pos.x, e->pos.z };
            if (Vector2Distance(pXZ, eXZ) < PLAYER_RADIUS + ENEMY_RADIUS + 0.1f && e->touchTimer <= 0) {
                bool cheatProtected = godMode && (int)(tp - players) == localPlayerIdx;
                if (!cheatProtected) {
                    tp->hp -= ENEMY_DAMAGE;
                    tp->damageFlash = 0.5f;
                    if (tp->hp <= 0) { tp->hp = 0; tp->alive = false; }
                }
                e->touchTimer = ENEMY_TOUCH_COOLDOWN;
            }
        }
    }
}

static void SpawnBullet(Vector3 origin, Vector3 dir, int damage, int ownerPlayer) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) {
            bullets[i].pos    = Vector3Add(origin, Vector3Scale(dir, 0.4f));
            bullets[i].vel    = Vector3Scale(dir, BULLET_SPEED);
            bullets[i].damage = damage;
            bullets[i].life   = BULLET_LIFE;
            bullets[i].alive  = true;
            bullets[i].ownerPlayer = ownerPlayer;
            return;
        }
    }
}

static Vector3 SpreadDir(Vector3 base, float degrees) {
    if (degrees <= 0) return base;
    float rad = degrees * DEG2RAD;
    float yaw   = RandRange(-rad, rad);
    float pitch = RandRange(-rad, rad);
    Vector3 up    = { 0, 1, 0 };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(base, up));
    Vector3 trueUp = Vector3CrossProduct(right, base);
    Vector3 d = base;
    d = Vector3Add(d, Vector3Scale(right,  tanf(yaw)));
    d = Vector3Add(d, Vector3Scale(trueUp, tanf(pitch)));
    return Vector3Normalize(d);
}

static void UpdateBullets(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) continue;
        bullets[i].life -= dt;
        if (bullets[i].life <= 0) { bullets[i].alive = false; continue; }
        bullets[i].pos = Vector3Add(bullets[i].pos, Vector3Scale(bullets[i].vel, dt));

        for (int j = 0; j < obstacleCount; j++) {
            if (CheckCollisionBoxSphere(BoxToBB(obstacles[j]), bullets[i].pos, 0.1f)) {
                bullets[i].alive = false; break;
            }
        }
        if (!bullets[i].alive) continue;

        float ax = fabsf(bullets[i].pos.x), az = fabsf(bullets[i].pos.z);
        if (ax > ARENA_HALF + 4.0f || az > ARENA_HALF + 4.0f) { bullets[i].alive = false; continue; }

        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].alive) continue;
            float dx = bullets[i].pos.x - enemies[e].pos.x;
            float dz = bullets[i].pos.z - enemies[e].pos.z;
            float dy = bullets[i].pos.y - enemies[e].pos.y;
            float r  = ENEMY_RADIUS + 0.15f;
            if (dx*dx + dz*dz < r*r && fabsf(dy) < ENEMY_HEIGHT*0.6f) {
                enemies[e].hp -= bullets[i].damage;
                bullets[i].alive = false;
                int op = bullets[i].ownerPlayer;
                if (op >= 0 && op < NET_MAX_PLAYERS && players[op].active) {
                    players[op].points += HIT_POINTS;
                    if (enemies[e].hp <= 0) {
                        enemies[e].alive = false;
                        enemiesAlive--;
                        players[op].points += KILL_POINTS;
                    }
                } else if (enemies[e].hp <= 0) {
                    enemies[e].alive = false;
                    enemiesAlive--;
                }
                break;
            }
        }
    }
}

// ============================================================================
//  Weapon actions (server-side; called for any player)
// ============================================================================

static void FireWeaponServer(Player *p) {
    if (!p->alive) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned) return;
    if (s->reloadTimer > 0 || s->fireTimer > 0 || s->ammo <= 0) return;
    if (pap.activeTimer > 0 && pap.slotInProgress == p->currentSlot && pap.ownerPlayer == (int)(p - players)) return;

    const WeaponDef *w = &WEAPONS[s->weaponIdx];
    Vector3 origin = (Vector3){ p->pos.x, PLAYER_EYE, p->pos.z };
    Vector3 dir = LookDir(p->yaw, p->pitch);
    int dmg = EffDamage(p, s);
    int ownerIdx = (int)(p - players);
    for (int k = 0; k < w->pellets; k++) {
        Vector3 d = SpreadDir(dir, w->spreadDeg);
        SpawnBullet(origin, d, dmg, ownerIdx);
    }
    s->ammo--;
    s->fireTimer = EffFireCD(p, s);
    if (ownerIdx == localPlayerIdx) muzzleFlash = 0.05f;
}

static void StartReloadServer(Player *p) {
    if (!p->alive) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned) return;
    if (s->reloadTimer > 0 || s->ammo >= EffMagSize(s) || s->reserve <= 0) return;
    s->reloadTimer = EffReload(p, s);
}

static void FinishReloadIfReady(Player *p, WeaponSlot *s) {
    (void)p;
    if (s->reloadTimer > 0) return;
    int mag = EffMagSize(s);
    int need = mag - s->ammo;
    if (need <= 0) return;
    int take = (need < s->reserve) ? need : s->reserve;
    s->ammo += take; s->reserve -= take;
}

static void SwapSlotServer(Player *p, int target) {
    if (target < 0 || target >= INV_SLOTS) return;
    if (p->inventory[target].owned) p->currentSlot = target;
}

// ============================================================================
//  Interactables (per-player)
// ============================================================================

static Interact FindInteractFor(Player *p) {
    Interact best = { IK_NONE, -1, INTERACT_DIST };
    Vector2 pXZ = { p->pos.x, p->pos.z };

    for (int i = 0; i < wallBuyCount; i++) {
        Vector2 q = { wallBuys[i].pos.x, wallBuys[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < best.dist) best = (Interact){ IK_WALLBUY, i, d };
    }
    for (int i = 0; i < perkMachineCount; i++) {
        Vector2 q = { perkMachines[i].pos.x, perkMachines[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < best.dist) best = (Interact){ IK_PERK, i, d };
    }
    Vector2 papXZ = { pap.pos.x, pap.pos.z };
    float dp = Vector2Distance(pXZ, papXZ);
    if (dp < best.dist) best = (Interact){ IK_PAP, 0, dp };

    for (int i = 0; i < windowCount; i++) {
        if (windows[i].boards >= MAX_BOARDS_PER_WIN) continue;
        Vector2 q = { windows[i].pos.x, windows[i].pos.z };
        float d = Vector2Distance(pXZ, q);
        if (d < best.dist) best = (Interact){ IK_WINDOW, i, d };
    }
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].opened) continue;
        Vector2 q = { doors[i].box.center.x, doors[i].box.center.z };
        float d = Vector2Distance(pXZ, q);
        if (d < DOOR_INTERACT_DIST && d < best.dist + 0.5f)
            best = (Interact){ IK_DOOR, i, d };
    }
    return best;
}

static void BuyDoorServer(Player *p, int doorIdx) {
    if (doorIdx < 0 || doorIdx >= doorCount) return;
    Door *d = &doors[doorIdx];
    if (d->opened) return;
    if (p->points < d->cost) return;
    p->points -= d->cost;
    d->opened = true;
}

static void BuyAtWallServer(Player *p, int wbIdx) {
    if (wbIdx < 0) return;
    WallBuy *wb = &wallBuys[wbIdx];
    const WeaponDef *w = &WEAPONS[wb->weaponIdx];
    int ownedSlot = FindOwnedSlot(p, wb->weaponIdx);

    if (ownedSlot >= 0) {
        WeaponSlot *s = &p->inventory[ownedSlot];
        int cap = EffReserveMax(s);
        if (s->reserve >= cap) return;
        if (p->points < w->ammoPrice) return;
        p->points -= w->ammoPrice;
        s->reserve = cap;
    } else {
        if (p->points < w->buyPrice) return;
        p->points -= w->buyPrice;
        int slot = FirstEmptySlot(p);
        if (slot < 0) slot = p->currentSlot;
        p->inventory[slot] = (WeaponSlot){
            .weaponIdx = wb->weaponIdx, .ammo = w->magSize,
            .reserve = w->reserveMax, .owned = true,
        };
        p->currentSlot = slot;
    }
}

static void BuyPerkServer(Player *p, int pmIdx) {
    if (pmIdx < 0) return;
    int pIdx = perkMachines[pmIdx].perkIdx;
    if (p->hasPerk[pIdx]) return;
    if (p->points < PERKS[pIdx].cost) return;
    p->points -= PERKS[pIdx].cost;
    p->hasPerk[pIdx] = true;
    if (pIdx == PERK_JUG) p->hp = EffMaxHP(p);
}

static void UsePackAPunchServer(Player *p) {
    if (pap.activeTimer > 0) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned || s->packed) return;
    if (p->points < PAP_COST) return;
    p->points -= PAP_COST;
    pap.activeTimer = PAP_DURATION;
    pap.slotInProgress = p->currentSlot;
    pap.ownerPlayer = (int)(p - players);
}

static void InteractServer(Player *p) {
    Interact ix = FindInteractFor(p);
    if      (ix.kind == IK_WALLBUY) BuyAtWallServer(p, ix.idx);
    else if (ix.kind == IK_PERK)    BuyPerkServer(p, ix.idx);
    else if (ix.kind == IK_PAP)     UsePackAPunchServer(p);
    else if (ix.kind == IK_DOOR)    BuyDoorServer(p, ix.idx);
}

static void UpdatePaP(float dt) {
    pap.bob += dt * 3.0f;
    if (pap.activeTimer > 0) {
        pap.activeTimer -= dt;
        if (pap.activeTimer <= 0) {
            pap.activeTimer = 0;
            if (pap.ownerPlayer >= 0 && pap.slotInProgress >= 0) {
                WeaponSlot *s = &players[pap.ownerPlayer].inventory[pap.slotInProgress];
                if (s->owned) {
                    s->packed = true;
                    s->ammo = EffMagSize(s);
                    s->reserve = EffReserveMax(s);
                    s->reloadTimer = 0;
                }
            }
            pap.slotInProgress = -1;
            pap.ownerPlayer = -1;
        }
    }
}

static void UpdateRepairsServer(float dt) {
    // Each player who is holding E gets repair progress on nearest window
    for (int i = 0; i < windowCount; i++) windows[i].repairProgress = 0;

    for (int pi = 0; pi < NET_MAX_PLAYERS; pi++) {
        Player *p = &players[pi];
        if (!p->active || !p->alive || !p->interactHeld) continue;
        Interact ix = FindInteractFor(p);
        if (ix.kind != IK_WINDOW) continue;
        Window3D *w = &windows[ix.idx];
        if (w->boards >= MAX_BOARDS_PER_WIN) continue;
        if (w->repairPlayer < 0 || w->repairPlayer == pi) {
            w->repairProgress += dt / BOARD_REPAIR_TIME;
            w->repairPlayer = pi;
            if (w->repairProgress >= 1.0f) {
                w->boards++;
                w->repairProgress = 0;
                p->points += BOARD_REPAIR_PTS;
                w->repairPlayer = -1;
            }
        }
    }
}

// ============================================================================
//  World tick (host/solo)
// ============================================================================

static void WorldTick(float dt) {
    // Apply firing inputs and weapon timers for every active player
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Player *p = &players[i];
        if (!p->active) continue;
        for (int s = 0; s < INV_SLOTS; s++) {
            if (!p->inventory[s].owned) continue;
            if (p->inventory[s].fireTimer   > 0) p->inventory[s].fireTimer   -= dt;
            if (p->inventory[s].reloadTimer > 0) {
                p->inventory[s].reloadTimer -= dt;
                if (p->inventory[s].reloadTimer <= 0) {
                    p->inventory[s].reloadTimer = 0;
                    FinishReloadIfReady(p, &p->inventory[s]);
                }
            }
        }
        if (p->alive && p->fireHeld) {
            WeaponSlot *cs = &p->inventory[p->currentSlot];
            const WeaponDef *cw = &WEAPONS[cs->weaponIdx];
            if (cw->automatic || cs->fireTimer <= 0) {
                if (cs->ammo <= 0 && cs->reserve > 0 && cs->reloadTimer <= 0) StartReloadServer(p);
                else FireWeaponServer(p);
            }
        }
        if (p->damageFlash > 0) p->damageFlash -= dt * 1.5f;
    }

    UpdateBullets(dt);
    UpdateEnemies(dt);
    SeparateEnemies();
    UpdateSpawns(dt);
    UpdatePaP(dt);
    UpdateRepairsServer(dt);

    if (gamePhase == GS_PLAY) {
        if (AliveActiveCount() == 0 && ActivePlayerCount() > 0) {
            gamePhase = GS_GAME_OVER;
        } else if (enemiesAlive <= 0 && enemiesToSpawn <= 0) {
            gamePhase = GS_ROUND_BREAK;
            roundBreakTimer = 4.0f;
            int bonus = 50 + roundNum * 10;
            for (int i = 0; i < NET_MAX_PLAYERS; i++)
                if (players[i].active) players[i].points += bonus;
            for (int i = 0; i < windowCount; i++) windows[i].boards = MAX_BOARDS_PER_WIN;
        }
    } else if (gamePhase == GS_ROUND_BREAK) {
        roundBreakTimer -= dt;
        if (roundBreakTimer <= 0) StartRound(roundNum + 1);
    }
}

// ============================================================================
//  Networking protocol
// ============================================================================

enum {
    PKT_HELLO = 1, PKT_WELCOME, PKT_REJECT, PKT_LOBBY, PKT_START,
    PKT_INPUT, PKT_ACTION, PKT_SNAPSHOT,
};

enum {
    ACT_RELOAD = 1, ACT_SWAP_SLOT, ACT_INTERACT_F,
};

enum {
    REJECT_PROTO = 1, REJECT_FULL,
};

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
} PktInput;
typedef struct { uint8_t type, action, arg; } PktAction;

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
} SerPlayer;

typedef struct {
    float    px, py, pz;
    int16_t  hp, maxHp;
    uint8_t  state;
    uint8_t  targetWindow;
    float    bobPhase;
} SerEnemy;

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
    uint8_t  doorsOpened;             // bit i = door i opened
    uint16_t numEnemies;
    uint16_t numBullets;
    SerPlayer players[NET_MAX_PLAYERS];
} PktSnapshotHeader;
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
    for (int k = 0; k < PERK_COUNT; k++) if (src->hasPerk[k]) mask |= (1 << k);
    dst->perksMask = mask;
    for (int s = 0; s < INV_SLOTS; s++) {
        dst->invOwned[s] = src->inventory[s].owned;
        dst->invPacked[s] = src->inventory[s].packed;
        dst->invWeapon[s] = (uint8_t)src->inventory[s].weaponIdx;
        dst->invAmmo[s]   = (int16_t)src->inventory[s].ammo;
        dst->invReserve[s]= (int16_t)src->inventory[s].reserve;
        dst->invReloadTimer[s] = src->inventory[s].reloadTimer;
    }
}

static void DeserializePlayer(Player *dst, SerPlayer *src, bool isLocal) {
    bool wasActive = dst->active;
    dst->active = src->active;
    dst->alive  = src->alive;
    memcpy(dst->name, src->name, 32); dst->name[31] = 0;
    if (!isLocal) {
        dst->pos = (Vector3){ src->px, src->py, src->pz };
        dst->yaw = src->yaw; dst->pitch = src->pitch;
    }
    dst->hp = src->hp;
    dst->points = src->points;
    dst->currentSlot = src->currentSlot;
    for (int k = 0; k < PERK_COUNT; k++) dst->hasPerk[k] = (src->perksMask & (1 << k)) != 0;
    for (int s = 0; s < INV_SLOTS; s++) {
        dst->inventory[s].owned = src->invOwned[s];
        dst->inventory[s].packed = src->invPacked[s];
        dst->inventory[s].weaponIdx = src->invWeapon[s];
        dst->inventory[s].ammo = src->invAmmo[s];
        dst->inventory[s].reserve = src->invReserve[s];
        dst->inventory[s].reloadTimer = src->invReloadTimer[s];
    }
    if (!wasActive && src->active && isLocal) {
        // first sync of local player position too
        dst->pos = (Vector3){ src->px, src->py, src->pz };
    }
}

static void HostBroadcastSnapshot(void) {
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

    for (int i = 0; i < NET_MAX_PLAYERS; i++) SerializePlayer(&hdr->players[i], &players[i]);

    size_t off = sizeof *hdr;
    uint16_t ne = 0;
    for (int i = 0; i < MAX_ENEMIES && off + sizeof(SerEnemy) <= sizeof snapshotBuf; i++) {
        if (!enemies[i].alive) continue;
        SerEnemy se = {
            .px = enemies[i].pos.x, .py = enemies[i].pos.y, .pz = enemies[i].pos.z,
            .hp = (int16_t)enemies[i].hp, .maxHp = (int16_t)enemies[i].maxHp,
            .state = (uint8_t)enemies[i].state, .targetWindow = (uint8_t)enemies[i].targetWindow,
            .bobPhase = enemies[i].bobPhase,
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

    Net_Broadcast(snapshotBuf, off, false);
}

static void ClientApplySnapshot(uint8_t *data, size_t len) {
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
        enemies[i].targetWindow = se.targetWindow;
        enemies[i].bobPhase = se.bobPhase;
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
}

static void HostSendLobby(void) {
    PktLobby lob = { .type = PKT_LOBBY, .numPlayers = (uint8_t)ActivePlayerCount() };
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        lob.active[i] = players[i].active;
        memcpy(lob.names[i], players[i].name, 32);
    }
    Net_Broadcast(&lob, sizeof lob, true);
}

// ============================================================================
//  Local input
// ============================================================================

static void ApplyLocalLook(Player *p, float dt) {
    (void)dt;
    Vector2 md = GetMouseDelta();
    p->yaw   += md.x * mouseSens;
    p->pitch -= md.y * mouseSens;
    if (p->pitch >  1.55f) p->pitch =  1.55f;
    if (p->pitch < -1.55f) p->pitch = -1.55f;
}

static void ApplyLocalMove(Player *p, float dt) {
    Vector3 fwd   = { sinf(p->yaw),  0, -cosf(p->yaw) };
    Vector3 right = { cosf(p->yaw),  0,  sinf(p->yaw) };

    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, fwd);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, fwd);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (Vector3LengthSqr(move) > 0.0001f) {
        move = Vector3Scale(Vector3Normalize(move), EffMoveSpeed(p) * dt);
    }
    Vector3 newPos = Vector3Add(p->pos, move);
    newPos.y = PLAYER_EYE;
    p->pos = ResolveXZ(p->pos, newPos, PLAYER_RADIUS, true);
}

// ============================================================================
//  Network packet dispatch
// ============================================================================

static void HostHandlePacket(int peerIdx, uint8_t *data, size_t len) {
    if (len < 1) return;
    uint8_t type = data[0];
    if (type == PKT_HELLO && len >= sizeof(PktHello)) {
        PktHello *h = (PktHello *)data;
        if (h->proto != NET_PROTO_VERSION) {
            PktReject r = { .type = PKT_REJECT, .reason = REJECT_PROTO };
            Net_SendTo(peerIdx, &r, sizeof r, true);
            return;
        }
        if (gamePhase != GS_PRE_GAME) {
            // mid-game join: place player but they'll spawn at round break with starter
            // For simplicity we still let them join.
        }
        ResetPlayerForGame(peerIdx, h->name);
        PktWelcome w = { .type = PKT_WELCOME, .playerIdx = (uint8_t)peerIdx };
        Net_SendTo(peerIdx, &w, sizeof w, true);
        HostSendLobby();
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
    }
    else if (type == PKT_ACTION && len >= sizeof(PktAction)) {
        PktAction *a = (PktAction *)data;
        if (peerIdx < 0 || peerIdx >= NET_MAX_PLAYERS) return;
        Player *p = &players[peerIdx];
        if (!p->active) return;
        if      (a->action == ACT_RELOAD)      StartReloadServer(p);
        else if (a->action == ACT_SWAP_SLOT)   SwapSlotServer(p, a->arg);
        else if (a->action == ACT_INTERACT_F)  InteractServer(p);
    }
}

static void ClientHandlePacket(uint8_t *data, size_t len) {
    if (len < 1) return;
    uint8_t type = data[0];
    if (type == PKT_WELCOME && len >= sizeof(PktWelcome)) {
        PktWelcome *w = (PktWelcome *)data;
        localPlayerIdx = w->playerIdx;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
        ResetPlayerForGame(localPlayerIdx, playerName);
        uiState = UI_CLIENT_LOBBY;
    }
    else if (type == PKT_REJECT && len >= sizeof(PktReject)) {
        PktReject *r = (PktReject *)data;
        snprintf(statusMsg, sizeof statusMsg,
                 (r->reason == REJECT_PROTO) ? "Protocol mismatch" : "Server full");
        Net_Shutdown();
        netMode = NET_SOLO;
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
    else if (type == PKT_START) {
        uiState = UI_PLAY;
    }
    else if (type == PKT_SNAPSHOT) {
        ClientApplySnapshot(data, len);
        if (uiState == UI_CLIENT_LOBBY) uiState = UI_PLAY;
    }
}

static void PollNetwork(void) {
    NetEvent events[32];
    int n = Net_Poll(events, 32);
    for (int i = 0; i < n; i++) {
        NetEvent *ev = &events[i];
        if (netMode == NET_HOST) {
            if (ev->kind == NEV_CONNECT) {
                // wait for HELLO
            } else if (ev->kind == NEV_DISCONNECT) {
                if (ev->peerIdx >= 0 && ev->peerIdx < NET_MAX_PLAYERS) {
                    players[ev->peerIdx].active = false;
                }
                HostSendLobby();
            } else if (ev->kind == NEV_RECEIVE) {
                HostHandlePacket(ev->peerIdx, ev->data, ev->len);
            }
        } else if (netMode == NET_CLIENT) {
            if (ev->kind == NEV_CONNECT) {
                PktHello h = { .type = PKT_HELLO, .proto = NET_PROTO_VERSION };
                strncpy(h.name, playerName, 31);
                Net_SendTo(0, &h, sizeof h, true);
            } else if (ev->kind == NEV_DISCONNECT) {
                snprintf(statusMsg, sizeof statusMsg, "Disconnected from server");
                Net_Shutdown();
                netMode = NET_SOLO;
                uiState = UI_MENU;
            } else if (ev->kind == NEV_RECEIVE) {
                ClientHandlePacket(ev->data, ev->len);
            }
        }
        Net_FreeEvent(ev);
    }
}

static void ClientSendInput(Player *p) {
    PktInput in = {
        .type = PKT_INPUT,
        .px = p->pos.x, .py = p->pos.y, .pz = p->pos.z,
        .yaw = p->yaw, .pitch = p->pitch,
        .currentSlot = (uint8_t)p->currentSlot,
        .fireDown = (uint8_t)(p->fireHeld ? 1 : 0),
        .interactHeld = (uint8_t)(p->interactHeld ? 1 : 0),
    };
    Net_SendTo(0, &in, sizeof in, false);
}

// ============================================================================
//  Rendering
// ============================================================================

static void DrawWallXSeg(float x0, float x1, float fixedZ, Color c) {
    if (x1 <= x0) return;
    float cx = (x0 + x1) * 0.5f;
    DrawCube((Vector3){cx, WALL_HEIGHT*0.5f, fixedZ}, (x1 - x0), WALL_HEIGHT, WALL_THICK, c);
}
static void DrawWallZSeg(float z0, float z1, float fixedX, Color c) {
    if (z1 <= z0) return;
    float cz = (z0 + z1) * 0.5f;
    DrawCube((Vector3){fixedX, WALL_HEIGHT*0.5f, cz}, WALL_THICK, WALL_HEIGHT, (z1 - z0), c);
}

static void DrawArena(void) {
    DrawPlane((Vector3){0,0,0}, (Vector2){ARENA_HALF*2, ARENA_HALF*2}, (Color){55,65,75,255});
    rlPushMatrix(); rlTranslatef(0, 0.01f, 0); DrawGrid(40, 2.0f); rlPopMatrix();
    DrawPlane((Vector3){0, -0.01f, 0}, (Vector2){ARENA_HALF*2 + 16, ARENA_HALF*2 + 16}, (Color){30,35,40,255});

    Color wc = (Color){90,100,110,255};
    float a = ARENA_HALF;
    for (int side = 0; side < 4; side++) {
        float gapPos = 0; bool hasGap = false;
        for (int i = 0; i < windowCount; i++) {
            if (side == 0 && windows[i].pos.z >  a - 0.5f) { gapPos = windows[i].pos.x; hasGap = true; }
            if (side == 1 && windows[i].pos.z < -a + 0.5f) { gapPos = windows[i].pos.x; hasGap = true; }
            if (side == 2 && windows[i].pos.x >  a - 0.5f) { gapPos = windows[i].pos.z; hasGap = true; }
            if (side == 3 && windows[i].pos.x < -a + 0.5f) { gapPos = windows[i].pos.z; hasGap = true; }
        }
        float gMin = gapPos - WINDOW_WIDTH*0.5f, gMax = gapPos + WINDOW_WIDTH*0.5f;
        if (side == 0) { if (hasGap) { DrawWallXSeg(-a, gMin,  a, wc); DrawWallXSeg(gMax, a,  a, wc); } else DrawWallXSeg(-a, a,  a, wc); }
        else if (side == 1) { if (hasGap) { DrawWallXSeg(-a, gMin, -a, wc); DrawWallXSeg(gMax, a, -a, wc); } else DrawWallXSeg(-a, a, -a, wc); }
        else if (side == 2) { if (hasGap) { DrawWallZSeg(-a, gMin,  a, wc); DrawWallZSeg(gMax, a,  a, wc); } else DrawWallZSeg(-a, a,  a, wc); }
        else                 { if (hasGap) { DrawWallZSeg(-a, gMin, -a, wc); DrawWallZSeg(gMax, a, -a, wc); } else DrawWallZSeg(-a, a, -a, wc); }
    }

    for (int i = 0; i < obstacleCount; i++) {
        DrawCubeV(obstacles[i].center, obstacles[i].size, (Color){120,90,70,255});
        DrawCubeWiresV(obstacles[i].center, obstacles[i].size, (Color){40,30,20,255});
    }
}

static void DrawInteriorWalls(void) {
    for (int i = 0; i < interiorWallCount; i++) {
        DrawCubeV(interiorWalls[i].center, interiorWalls[i].size, (Color){80, 90, 100, 255});
        DrawCubeWiresV(interiorWalls[i].center, interiorWalls[i].size, (Color){40, 50, 60, 255});
    }
}

static void DrawDoors(void) {
    for (int i = 0; i < doorCount; i++) {
        if (doors[i].opened) continue;
        Box b = doors[i].box;
        DrawCubeV(b.center, b.size, (Color){130, 80, 50, 255});
        DrawCubeWiresV(b.center, b.size, (Color){40, 25, 15, 255});
        // Decorative planks
        for (int k = 0; k < 3; k++) {
            float y = b.center.y - b.size.y*0.3f + k * (b.size.y*0.3f);
            Vector3 c2 = { b.center.x, y, b.center.z };
            Vector3 sz = { b.size.x * 0.96f, 0.08f, b.size.z * 1.05f };
            DrawCubeV(c2, sz, (Color){90, 55, 30, 255});
        }
    }
}

static void DrawWindows(void) {
    for (int i = 0; i < windowCount; i++) {
        Window3D *w = &windows[i];
        for (int b = 0; b < w->boards; b++) {
            float t = (b + 1.0f) / (MAX_BOARDS_PER_WIN + 1.0f);
            float by = t * WALL_HEIGHT;
            Vector3 planeCenter = { w->pos.x, by, w->pos.z };
            Vector3 planeSize = (fabsf(w->normal.x) > 0.5f)
                ? (Vector3){ WALL_THICK + 0.2f, 0.25f, WINDOW_WIDTH }
                : (Vector3){ WINDOW_WIDTH, 0.25f, WALL_THICK + 0.2f };
            DrawCubeV(planeCenter, planeSize, (Color){150, 100, 50, 255});
            DrawCubeWiresV(planeCenter, planeSize, (Color){60, 40, 20, 255});
        }
    }
}

static void DrawWallBuys(void) {
    for (int i = 0; i < wallBuyCount; i++) {
        WallBuy *wb = &wallBuys[i];
        const WeaponDef *w = &WEAPONS[wb->weaponIdx];
        Vector3 size = (fabsf(wb->normal.x) > 0.5f)
            ? (Vector3){ 0.2f, 1.0f, 1.6f }
            : (Vector3){ 1.6f, 1.0f, 0.2f };
        DrawCubeV(wb->pos, size, w->tint);
        DrawCubeWiresV(wb->pos, size, BLACK);
    }
}

static void DrawPerkMachines(void) {
    for (int i = 0; i < perkMachineCount; i++) {
        PerkMachine *pm = &perkMachines[i];
        const PerkDef *pd = &PERKS[pm->perkIdx];
        DrawCube((Vector3){ pm->pos.x, 0.6f, pm->pos.z }, 1.0f, 1.2f, 1.0f, (Color){30,30,30,255});
        DrawCube((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, pd->tint);
        DrawCubeWires((Vector3){ pm->pos.x, 1.7f, pm->pos.z }, 0.8f, 1.0f, 0.8f, BLACK);
        DrawCube((Vector3){ pm->pos.x, 2.5f, pm->pos.z }, 0.6f, 0.4f, 0.6f, (Color){40,40,40,255});
        bool myOwn = players[localPlayerIdx].hasPerk[pm->perkIdx];
        DrawSphere((Vector3){ pm->pos.x, 2.95f, pm->pos.z }, 0.18f,
                   myOwn ? (Color){240,240,240,255} : pd->tint);
    }
}

static void DrawPaP(void) {
    DrawCube(pap.pos, 2.5f, 0.6f, 2.5f, (Color){25,25,30,255});
    DrawCubeWires(pap.pos, 2.5f, 0.6f, 2.5f, BLACK);
    DrawCube((Vector3){pap.pos.x, 1.0f, pap.pos.z}, 2.0f, 0.8f, 2.0f, (Color){50,50,60,255});
    Vector3 top = { pap.pos.x, 1.7f + sinf(pap.bob) * 0.05f, pap.pos.z };
    DrawCube(top, 1.6f, 0.5f, 1.6f, (Color){80, 50, 130, 255});
    DrawCubeWires(top, 1.6f, 0.5f, 1.6f, (Color){200,150,255,255});
    DrawSphere((Vector3){pap.pos.x, 2.2f, pap.pos.z}, 0.15f, (Color){220, 180, 255, 255});

    if (pap.activeTimer > 0 && pap.ownerPlayer >= 0 && pap.slotInProgress >= 0) {
        float spin = pap.bob * 4.0f;
        Vector3 weaponPos = { pap.pos.x, 2.6f + sinf(pap.bob*2) * 0.15f, pap.pos.z };
        rlPushMatrix();
        rlTranslatef(weaponPos.x, weaponPos.y, weaponPos.z);
        rlRotatef(spin * 60.0f, 0, 1, 0);
        int w = players[pap.ownerPlayer].inventory[pap.slotInProgress].weaponIdx;
        DrawCube((Vector3){0,0,0}, 0.8f, 0.3f, 0.2f, WEAPONS[w].tint);
        rlPopMatrix();
    }
}

static void DrawEnemy(Enemy *e) {
    float bob = sinf(e->bobPhase) * 0.06f;
    Vector3 body = { e->pos.x, e->pos.y + bob, e->pos.z };
    float t = (float)e->hp / (float)(e->maxHp > 0 ? e->maxHp : 1);
    Color tint;
    if      (t > 0.66f) tint = (Color){80, 140, 60, 255};
    else if (t > 0.33f) tint = (Color){180,160, 40, 255};
    else                tint = (Color){200, 60, 50, 255};
    DrawCube(body, ENEMY_RADIUS*2, ENEMY_HEIGHT, ENEMY_RADIUS*2, tint);
    DrawCubeWires(body, ENEMY_RADIUS*2, ENEMY_HEIGHT, ENEMY_RADIUS*2, BLACK);
    DrawSphere((Vector3){body.x, body.y + ENEMY_HEIGHT*0.5f + 0.3f, body.z}, 0.28f, tint);
}

static void DrawOtherPlayer(int idx) {
    Player *p = &players[idx];
    if (!p->active) return;
    Color c = PLAYER_COLORS[idx];
    if (!p->alive) c = (Color){ 100,100,100, 200 };
    Vector3 body = { p->pos.x, ENEMY_HEIGHT*0.5f, p->pos.z };
    DrawCube(body, 0.55f, 1.6f, 0.55f, c);
    DrawCubeWires(body, 0.55f, 1.6f, 0.55f, BLACK);
    DrawSphere((Vector3){ body.x, body.y + 0.95f, body.z }, 0.28f, c);
    // Direction indicator
    Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
    Vector3 nose = Vector3Add((Vector3){body.x, body.y + 0.5f, body.z}, Vector3Scale(fwd, 0.45f));
    DrawSphere(nose, 0.10f, BLACK);
}

static void DrawPerkIcons(int sw, int sh) {
    (void)sw;
    Player *p = &players[localPlayerIdx];
    int sz = 36, gap = 6, x = 14, y = sh - sz - 14;
    for (int i = 0; i < PERK_COUNT; i++) {
        if (!p->hasPerk[i]) continue;
        DrawRectangle(x, y, sz, sz, PERKS[i].tint);
        DrawRectangleLines(x, y, sz, sz, BLACK);
        DrawText(PERKS[i].name, x, y - 16, 12, RAYWHITE);
        x += sz + gap;
    }
}

static void DrawHUD(int sw, int sh, Interact ix) {
    int cx = sw / 2, cy = sh / 2;
    Color cross = (muzzleFlash > 0) ? YELLOW : RAYWHITE;
    DrawLine(cx - 10, cy, cx + 10, cy, cross);
    DrawLine(cx, cy - 10, cx, cy + 10, cross);
    DrawCircleLines(cx, cy, 2, cross);

    Player *me = &players[localPlayerIdx];
    if (me->damageFlash > 0) {
        unsigned char a = (unsigned char)(me->damageFlash * 180);
        DrawRectangle(0, 0, sw, sh, (Color){180, 0, 0, a});
    }

    DrawRectangle(10, 10, 280, 120, (Color){0,0,0,140});
    DrawRectangleLines(10, 10, 280, 120, (Color){200,200,200,180});

    char buf[96];
    DrawText("HP", 22, 22, 18, RAYWHITE);
    float hpf = (float)me->hp, hpMax = (float)EffMaxHP(me);
    GuiProgressBar((Rectangle){62, 22, 215, 18}, NULL, NULL, &hpf, 0.0f, hpMax);
    snprintf(buf, sizeof buf, "%d / %d", me->hp, EffMaxHP(me));
    DrawText(buf, 145, 22, 16, RAYWHITE);
    snprintf(buf, sizeof buf, "POINTS  %d", me->points);
    DrawText(buf, 22, 52, 22, YELLOW);
    snprintf(buf, sizeof buf, "ROUND  %d", roundNum);
    DrawText(buf, 22, 80, 22, SKYBLUE);
    snprintf(buf, sizeof buf, "ZOMBIES LEFT  %d", enemiesAlive + enemiesToSpawn);
    DrawText(buf, 22, 106, 16, RAYWHITE);

    WeaponSlot *cur = &me->inventory[me->currentSlot];
    const WeaponDef *cw = &WEAPONS[cur->weaponIdx];
    const char *displayName = cur->packed ? cw->packedName : cw->name;

    int panelW = 340, panelH = 110;
    int px = sw - panelW - 10, py = sh - panelH - 10;
    DrawRectangle(px, py, panelW, panelH, (Color){0,0,0,160});
    DrawRectangleLines(px, py, panelW, panelH,
                       cur->packed ? (Color){200,150,255,255} : (Color){200,200,200,180});
    DrawRectangle(px + 8, py + 8, 14, panelH - 16, cw->tint);

    if (cur->reloadTimer > 0) {
        DrawText("RELOADING...", px + 32, py + 14, 22, ORANGE);
        float t = 1.0f - (cur->reloadTimer / EffReload(me, cur));
        GuiProgressBar((Rectangle){px + 32, py + 46, panelW - 50, 14}, NULL, NULL, &t, 0.0f, 1.0f);
    } else if (pap.activeTimer > 0 && pap.ownerPlayer == localPlayerIdx && pap.slotInProgress == me->currentSlot) {
        DrawText("IN PACK-A-PUNCH...", px + 32, py + 14, 20, (Color){200,150,255,255});
        float t = 1.0f - (pap.activeTimer / PAP_DURATION);
        GuiProgressBar((Rectangle){px + 32, py + 46, panelW - 50, 14}, NULL, NULL, &t, 0.0f, 1.0f);
    } else {
        DrawText(displayName, px + 32, py + 14, 22, cur->packed ? (Color){220,180,255,255} : RAYWHITE);
        snprintf(buf, sizeof buf, "%d / %d", cur->ammo, cur->reserve);
        DrawText(buf, px + 32, py + 42, 28, (cur->ammo == 0) ? RED : YELLOW);
    }

    WeaponSlot *other = &me->inventory[(me->currentSlot + 1) % INV_SLOTS];
    if (other->owned) {
        const WeaponDef *ow = &WEAPONS[other->weaponIdx];
        const char *on = other->packed ? ow->packedName : ow->name;
        snprintf(buf, sizeof buf, "[Q] %s  %d/%d", on, other->ammo, other->reserve);
        DrawText(buf, px + 32, py + 80, 16, GRAY);
    } else {
        DrawText("[Q] (empty slot)", px + 32, py + 80, 16, (Color){90,90,90,255});
    }

    DrawPerkIcons(sw, sh);

    if (godMode) {
        const char *gm = "GOD MODE";
        int gw = MeasureText(gm, 22);
        DrawRectangle(sw - gw - 30, 140, gw + 20, 30, (Color){0,0,0,160});
        DrawRectangleLines(sw - gw - 30, 140, gw + 20, 30, (Color){255,220,100,255});
        DrawText(gm, sw - gw - 20, 145, 22, (Color){255,220,100,255});
    }

    // Other player roster (top right)
    int rx = sw - 220, ry = 10, rh = 28;
    int actCount = ActivePlayerCount();
    if (actCount > 1) {
        DrawRectangle(rx, ry, 210, 8 + actCount * rh, (Color){0,0,0,140});
        DrawRectangleLines(rx, ry, 210, 8 + actCount * rh, (Color){200,200,200,180});
        int row = 0;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (!players[i].active) continue;
            DrawRectangle(rx + 6, ry + 4 + row*rh + 4, 14, 14, PLAYER_COLORS[i]);
            char ln[64];
            snprintf(ln, sizeof ln, "%s", players[i].name[0] ? players[i].name : "Player");
            Color tc = players[i].alive ? RAYWHITE : (Color){180,80,80,255};
            DrawText(ln, rx + 26, ry + 4 + row*rh + 2, 16, tc);
            char hpStr[32]; snprintf(hpStr, sizeof hpStr, "%d", players[i].hp);
            DrawText(hpStr, rx + 150, ry + 4 + row*rh + 2, 16, tc);
            row++;
        }
    }

    if (ix.kind != IK_NONE) {
        char prompt[128] = {0};
        Color promptColor = RAYWHITE;
        Color border = (Color){200,200,200,200};

        if (ix.kind == IK_WALLBUY) {
            WallBuy *wb = &wallBuys[ix.idx];
            const WeaponDef *w = &WEAPONS[wb->weaponIdx];
            int owned = FindOwnedSlot(me, wb->weaponIdx);
            border = w->tint;
            if (owned >= 0) {
                WeaponSlot *s = &me->inventory[owned];
                int cap = EffReserveMax(s);
                if (s->reserve >= cap) { snprintf(prompt, sizeof prompt, "%s ammo: FULL", w->name); promptColor = GRAY; }
                else { snprintf(prompt, sizeof prompt, "[F]  %s AMMO  -  %d", w->name, w->ammoPrice);
                       if (me->points < w->ammoPrice) promptColor = (Color){200,80,80,255}; }
            } else {
                snprintf(prompt, sizeof prompt, "[F]  BUY %s  -  %d", w->name, w->buyPrice);
                if (me->points < w->buyPrice) promptColor = (Color){200,80,80,255};
            }
        } else if (ix.kind == IK_PERK) {
            PerkMachine *pm = &perkMachines[ix.idx];
            const PerkDef *pd = &PERKS[pm->perkIdx];
            border = pd->tint;
            if (me->hasPerk[pm->perkIdx]) { snprintf(prompt, sizeof prompt, "%s : owned", pd->name); promptColor = GRAY; }
            else { snprintf(prompt, sizeof prompt, "[F]  BUY %s  -  %d", pd->name, pd->cost);
                   if (me->points < pd->cost) promptColor = (Color){200,80,80,255}; }
        } else if (ix.kind == IK_PAP) {
            border = (Color){200,150,255,255};
            WeaponSlot *s = &me->inventory[me->currentSlot];
            if (pap.activeTimer > 0) { snprintf(prompt, sizeof prompt, "Upgrading... %.1fs", pap.activeTimer); promptColor = (Color){200,150,255,255}; }
            else if (s->packed) { snprintf(prompt, sizeof prompt, "Already Pack-a-Punched"); promptColor = GRAY; }
            else { snprintf(prompt, sizeof prompt, "[F]  PACK-A-PUNCH  -  %d", PAP_COST);
                   if (me->points < PAP_COST) promptColor = (Color){200,80,80,255}; }
        } else if (ix.kind == IK_WINDOW) {
            Window3D *w = &windows[ix.idx];
            border = (Color){200, 160, 80, 255};
            if (w->boards >= MAX_BOARDS_PER_WIN) { snprintf(prompt, sizeof prompt, "Window sealed"); promptColor = GRAY; }
            else snprintf(prompt, sizeof prompt, "[Hold E]  REPAIR BOARD");
        } else if (ix.kind == IK_DOOR) {
            Door *d = &doors[ix.idx];
            border = (Color){200, 150, 100, 255};
            if (d->opened) { snprintf(prompt, sizeof prompt, "Door open"); promptColor = GRAY; }
            else {
                snprintf(prompt, sizeof prompt, "[F]  OPEN DOOR  -  %d", d->cost);
                if (me->points < d->cost) promptColor = (Color){200,80,80,255};
            }
        }

        if (prompt[0]) {
            int tw = MeasureText(prompt, 26);
            int by = sh / 2 + 80;
            DrawRectangle(cx - tw/2 - 16, by - 8, tw + 32, 40, (Color){0,0,0,180});
            DrawRectangleLines(cx - tw/2 - 16, by - 8, tw + 32, 40, border);
            DrawText(prompt, cx - tw/2, by, 26, promptColor);
            if (ix.kind == IK_WINDOW) {
                float p = windows[ix.idx].repairProgress;
                if (p > 0) GuiProgressBar((Rectangle){cx - tw/2, by + 36, tw, 8}, NULL, NULL, &p, 0.0f, 1.0f);
            }
        }
    }

    if (gamePhase == GS_ROUND_BREAK) {
        char rb[64]; snprintf(rb, sizeof rb, "ROUND  %d", roundNum + 1);
        int rs = 70;
        int rw = MeasureText(rb, rs);
        DrawRectangle(0, sh/2 - 70, sw, 140, (Color){0,0,0,160});
        DrawText(rb, cx - rw/2, sh/2 - 50, rs, (Color){220, 50, 50, 255});
        const char *sub = "Get ready...";
        int sw2 = MeasureText(sub, 24);
        DrawText(sub, cx - sw2/2, sh/2 + 30, 24, RAYWHITE);
    }

    if (!me->alive && gamePhase != GS_GAME_OVER) {
        DrawRectangle(0, sh/2 - 40, sw, 80, (Color){80, 0, 0, 160});
        const char *down = "YOU ARE DOWN  —  revive at next round";
        int dw = MeasureText(down, 28);
        DrawText(down, sw/2 - dw/2, sh/2 - 14, 28, RAYWHITE);
    }
}

// ============================================================================
//  Menus
// ============================================================================

static void ToggleFullscreenSafe(void) {
    int monitor = GetCurrentMonitor();
    if (!IsWindowFullscreen()) {
        windowedW = GetScreenWidth(); windowedH = GetScreenHeight();
        SetWindowSize(GetMonitorWidth(monitor), GetMonitorHeight(monitor));
        ToggleFullscreen();
    } else {
        ToggleFullscreen();
        SetWindowSize(windowedW, windowedH);
    }
    fullscreen = IsWindowFullscreen();
}

static void StartSoloGame(void) {
    netMode = NET_SOLO;
    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    ResetPlayerForGame(0, playerName);
    StartRound(1);
    gamePhase = GS_ROUND_BREAK;
    roundBreakTimer = 3.0f;
    uiState = UI_PLAY;
}

static void StartHosting(void) {
    if (!Net_InitHost(NET_PORT_DEFAULT)) {
        snprintf(statusMsg, sizeof statusMsg, "Failed to start server on port %d", NET_PORT_DEFAULT);
        return;
    }
    netMode = NET_HOST;
    localPlayerIdx = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    ResetPlayerForGame(0, playerName);
    gamePhase = GS_PRE_GAME;
    uiState = UI_HOST_LOBBY;
    hostIpCount = Net_GetLocalIPs(hostIps, 8);
    snprintf(statusMsg, sizeof statusMsg, "Hosting on port %d", NET_PORT_DEFAULT);
}

static void StartHostedGame(void) {
    StartRound(1);
    gamePhase = GS_ROUND_BREAK;
    roundBreakTimer = 3.0f;
    PktStart s = { .type = PKT_START };
    Net_Broadcast(&s, sizeof s, true);
    uiState = UI_PLAY;
}

static void StartConnecting(void) {
    if (!Net_InitClient(joinIp, NET_PORT_DEFAULT)) {
        snprintf(statusMsg, sizeof statusMsg, "Failed to start client");
        return;
    }
    netMode = NET_CLIENT;
    snprintf(statusMsg, sizeof statusMsg, "Connecting to %s...", joinIp);
    uiState = UI_CONNECTING;
}

static void DrawMenu(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "CLAUDE  ZOMBIES";
    int ts = 72;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2 + 3, sh/6 + 3, ts, (Color){80, 0, 0, 255});
    DrawText(title, sw/2 - tw/2,     sh/6,     ts, (Color){220, 40, 40, 255});

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 - 80;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by,        bw, bh}, "HOST GAME"))   StartHosting();
    if (GuiButton((Rectangle){bx, by + 64,   bw, bh}, "JOIN GAME"))   { uiState = UI_JOIN_INPUT; statusMsg[0]=0; }
    if (GuiButton((Rectangle){bx, by + 128,  bw, bh}, "SOLO PLAY"))   StartSoloGame();
    if (GuiButton((Rectangle){bx, by + 192,  bw, bh}, "SETTINGS"))    uiState = UI_SETTINGS;
    if (GuiButton((Rectangle){bx, by + 256,  bw, bh}, "QUIT"))        { CloseWindow(); exit(0); }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    if (statusMsg[0]) {
        int tw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - tw2/2, sh - 60, 18, (Color){200,180,180,255});
    }
}

static void DrawSettings(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "SETTINGS";
    int ts = 56;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, 60, ts, RAYWHITE);

    int x = sw/2 - 200, y = 160, lh = 66;

    DrawText("Player Name", x, y + 6, 22, RAYWHITE);
    if (GuiTextBox((Rectangle){x + 280, y + 4, 240, 28}, playerName, sizeof playerName, nameEditing)) nameEditing = !nameEditing;

    y += lh;
    DrawText("Fullscreen", x, y + 6, 22, RAYWHITE);
    bool fs = fullscreen;
    GuiCheckBox((Rectangle){x + 280, y + 8, 24, 24}, NULL, &fs);
    if (fs != fullscreen) ToggleFullscreenSafe();

    y += lh;
    DrawText("Mouse Sensitivity", x, y + 6, 22, RAYWHITE);
    char sbuf[32]; snprintf(sbuf, sizeof sbuf, "%.4f", mouseSens);
    GuiSlider((Rectangle){x + 280, y + 8, 240, 24}, NULL, sbuf, &mouseSens, 0.0005f, 0.006f);

    y += lh;
    DrawText("Field of View", x, y + 6, 22, RAYWHITE);
    char fbuf[32]; snprintf(fbuf, sizeof fbuf, "%.0f", fovSetting);
    GuiSlider((Rectangle){x + 280, y + 8, 240, 24}, NULL, fbuf, &fovSetting, 60.0f, 110.0f);
    camera.fovy = fovSetting;

    y += lh + 30;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){sw/2 - 130, y, 260, 50}, "BACK")) uiState = UI_MENU;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    DrawText("WASD move, mouse look, LMB shoot, R reload,",
             sw/2 - 280, sh - 100, 18, GRAY);
    DrawText("Q swap, F buy / use, hold E repair, ESC pause",
             sw/2 - 280, sh - 76, 18, GRAY);
}

static void DrawJoinInput(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *title = "JOIN GAME";
    int ts = 56;
    int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, 80, ts, RAYWHITE);

    int x = sw/2 - 200, y = 220;
    DrawText("Server IP", x, y, 22, RAYWHITE);
    if (GuiTextBox((Rectangle){x, y + 32, 400, 36}, joinIp, sizeof joinIp, joinIpEditing)) joinIpEditing = !joinIpEditing;
    DrawText("Player Name", x, y + 90, 22, RAYWHITE);
    if (GuiTextBox((Rectangle){x, y + 122, 400, 36}, playerName, sizeof playerName, nameEditing)) nameEditing = !nameEditing;

    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){sw/2 - 200, y + 200, 180, 48}, "CONNECT")) StartConnecting();
    if (GuiButton((Rectangle){sw/2 +  20, y + 200, 180, 48}, "BACK"))    uiState = UI_MENU;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    if (statusMsg[0]) {
        int tw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - tw2/2, sh - 60, 18, (Color){200,180,180,255});
    }
}

static void DrawConnecting(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *t = "CONNECTING...";
    int ts = 48; int tw = MeasureText(t, ts);
    DrawText(t, sw/2 - tw/2, sh/2 - 30, ts, RAYWHITE);
    if (statusMsg[0]) {
        int sw2 = MeasureText(statusMsg, 18);
        DrawText(statusMsg, sw/2 - sw2/2, sh/2 + 40, 18, GRAY);
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);
    if (GuiButton((Rectangle){sw/2 - 100, sh - 100, 200, 44}, "CANCEL")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

static void DrawLobby(int sw, int sh, bool isHost) {
    DrawRectangle(0, 0, sw, sh, (Color){15, 18, 25, 255});
    const char *t = isHost ? "HOST  LOBBY" : "WAITING FOR HOST";
    int ts = 48; int tw = MeasureText(t, ts);
    DrawText(t, sw/2 - tw/2, 50, ts, RAYWHITE);

    char info[128];
    snprintf(info, sizeof info, "Port %d  -  %d / %d players",
             NET_PORT_DEFAULT, ActivePlayerCount(), NET_MAX_PLAYERS);
    int iw = MeasureText(info, 20);
    DrawText(info, sw/2 - iw/2, 108, 20, GRAY);

    int listY = 280;
    if (isHost) {
        const char *hint = "Players join by entering one of these addresses:";
        int hw = MeasureText(hint, 18);
        DrawText(hint, sw/2 - hw/2, 140, 18, (Color){200,200,200,200});
        if (hostIpCount == 0) {
            const char *na = "(no network interface found - use 127.0.0.1 on this machine)";
            int nw = MeasureText(na, 18);
            DrawText(na, sw/2 - nw/2, 168, 18, GRAY);
        } else {
            int boxW = 380, boxH = 30 * hostIpCount + 16;
            int bx = sw/2 - boxW/2, by = 164;
            DrawRectangle(bx, by, boxW, boxH, (Color){25,30,40,255});
            DrawRectangleLines(bx, by, boxW, boxH, (Color){200,200,200,180});
            for (int i = 0; i < hostIpCount; i++) {
                char line[80];
                snprintf(line, sizeof line, "%s : %d", hostIps[i], NET_PORT_DEFAULT);
                int lw = MeasureText(line, 24);
                DrawText(line, sw/2 - lw/2, by + 8 + i*30, 24, YELLOW);
            }
            listY = by + boxH + 24;
        }
    }

    int rowH = 44;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        int ry = listY + i * rowH;
        DrawRectangle(sw/2 - 220, ry, 440, rowH - 6, (Color){25,30,40,255});
        DrawRectangle(sw/2 - 210, ry + 8, 20, 20, PLAYER_COLORS[i]);
        char line[64];
        if (players[i].active) snprintf(line, sizeof line, "%s", players[i].name[0] ? players[i].name : "Player");
        else                   snprintf(line, sizeof line, "(empty)");
        Color tc = players[i].active ? RAYWHITE : GRAY;
        DrawText(line, sw/2 - 180, ry + 8, 22, tc);
        if (i == localPlayerIdx) DrawText("(you)", sw/2 + 140, ry + 10, 18, YELLOW);
    }

    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (isHost) {
        if (GuiButton((Rectangle){sw/2 - 200, sh - 110, 180, 50}, "START GAME")) StartHostedGame();
    }
    if (GuiButton((Rectangle){sw/2 + 20, sh - 110, 180, 50}, "LEAVE")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

static void DrawPause(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 170});
    const char *title = "PAUSED";
    int ts = 64; int tw = MeasureText(title, ts);
    DrawText(title, sw/2 - tw/2, sh/4, ts, RAYWHITE);
    if (netMode != NET_SOLO) {
        const char *sub = "(other players keep playing)";
        int sw2 = MeasureText(sub, 18);
        DrawText(sub, sw/2 - sw2/2, sh/4 + ts + 12, 18, GRAY);
    }

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 - 20;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by,        bw, bh}, "RESUME"))     uiState = UI_PLAY;
    if (GuiButton((Rectangle){bx, by + 64,   bw, bh}, "SETTINGS"))   uiState = UI_SETTINGS;
    if (GuiButton((Rectangle){bx, by + 128,  bw, bh}, "QUIT TO MENU")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

static void DrawGameOver(int sw, int sh) {
    DrawRectangle(0, 0, sw, sh, (Color){120, 0, 0, 170});
    const char *msg = "GAME OVER";
    int fs = 96; int tw = MeasureText(msg, fs);
    DrawText(msg, sw/2 - tw/2, sh/3, fs, RAYWHITE);
    char rb[96]; snprintf(rb, sizeof rb, "Round %d reached", roundNum);
    int rw = MeasureText(rb, 28);
    DrawText(rb, sw/2 - rw/2, sh/3 + fs + 20, 28, RAYWHITE);

    int bw = 260, bh = 50, bx = sw/2 - bw/2, by = sh/2 + 100;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 22);
    if (GuiButton((Rectangle){bx, by, bw, bh}, "MAIN MENU")) {
        Net_Shutdown(); netMode = NET_SOLO; uiState = UI_MENU;
    }
    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);
}

// ============================================================================
//  Main
// ============================================================================

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W_DEFAULT, WINDOW_H_DEFAULT, "Claude Zombies");
    SetTargetFPS(60);
    SetExitKey(0);

    GuiSetStyle(DEFAULT, TEXT_SIZE, 16);

    BuildLevel();
    for (int i = 0; i < NET_MAX_PLAYERS; i++) memset(&players[i], 0, sizeof players[i]);
    ResetPlayerForGame(0, playerName); // for menu render etc.

    camera.position   = (Vector3){ 0.0f, PLAYER_EYE, 0.0f };
    camera.target     = (Vector3){ 0.0f, PLAYER_EYE, -1.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f,  0.0f };
    camera.fovy       = fovSetting;
    camera.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();

        // Cursor mode
        bool wantCursor = (uiState != UI_PLAY);
        if (uiState != prevUi) {
            if (wantCursor) EnableCursor(); else DisableCursor();
            prevUi = uiState;
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            if      (uiState == UI_PLAY)        uiState = UI_PAUSE;
            else if (uiState == UI_PAUSE)       uiState = UI_PLAY;
            else if (uiState == UI_SETTINGS)    uiState = UI_MENU;
            else if (uiState == UI_JOIN_INPUT)  uiState = UI_MENU;
        }
        if (IsKeyPressed(KEY_F11)) ToggleFullscreenSafe();
        if (IsKeyPressed(KEY_F3))  godMode = !godMode;

        // ---- Networking pump ----
        if (netMode != NET_SOLO) PollNetwork();

        Player *me = &players[localPlayerIdx];
        Interact ix = { IK_NONE, -1, 0 };

        // ---- Input + local prediction + simulation ----
        if (uiState == UI_PLAY) {
            // Always update local look/move/input (even client)
            if (me->alive) {
                ApplyLocalLook(me, dt);
                ApplyLocalMove(me, dt);
            }
            // Inputs
            me->fireHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && me->alive;
            me->interactHeld = IsKeyDown(KEY_E) && me->alive;

            // One-shot actions
            bool reloadEdge = IsKeyPressed(KEY_R) && me->alive;
            bool swapEdge   = (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_TAB)) && me->alive;
            bool slot1Edge  = IsKeyPressed(KEY_ONE) && me->alive;
            bool slot2Edge  = IsKeyPressed(KEY_TWO) && me->alive;
            bool interactEdge = IsKeyPressed(KEY_F) && me->alive;

            int swapTarget = -1;
            if (swapEdge) {
                int other = (me->currentSlot + 1) % INV_SLOTS;
                if (me->inventory[other].owned) swapTarget = other;
            } else if (slot1Edge && me->inventory[0].owned) swapTarget = 0;
            else if (slot2Edge && me->inventory[1].owned) swapTarget = 1;

            if (IsHost()) {
                if (reloadEdge)   StartReloadServer(me);
                if (swapTarget >= 0) SwapSlotServer(me, swapTarget);
                if (interactEdge) InteractServer(me);
            } else {
                // Client side: send actions to server
                if (reloadEdge) {
                    PktAction a = { .type = PKT_ACTION, .action = ACT_RELOAD };
                    Net_SendTo(0, &a, sizeof a, true);
                }
                if (swapTarget >= 0) {
                    me->currentSlot = swapTarget; // predict
                    PktAction a = { .type = PKT_ACTION, .action = ACT_SWAP_SLOT, .arg = (uint8_t)swapTarget };
                    Net_SendTo(0, &a, sizeof a, true);
                }
                if (interactEdge) {
                    PktAction a = { .type = PKT_ACTION, .action = ACT_INTERACT_F };
                    Net_SendTo(0, &a, sizeof a, true);
                }
            }

            if (IsHost()) {
                WorldTick(dt);

                if (godMode) {
                    Player *gp = &players[localPlayerIdx];
                    gp->points = 999999;
                    gp->hp = EffMaxHP(gp);
                    gp->alive = true;
                }

                snapshotAccum += dt;
                if (snapshotAccum >= 1.0f / SNAPSHOT_HZ && netMode == NET_HOST) {
                    snapshotAccum = 0;
                    HostBroadcastSnapshot();
                }
            } else {
                inputAccum += dt;
                if (inputAccum >= 1.0f / INPUT_HZ) {
                    inputAccum = 0;
                    ClientSendInput(me);
                }
            }

            ix = FindInteractFor(me);
            if (muzzleFlash > 0) muzzleFlash -= dt;
        }

        // ---- Camera setup ----
        camera.position = (Vector3){ me->pos.x, PLAYER_EYE, me->pos.z };
        Vector3 dir = LookDir(me->yaw, me->pitch);
        camera.target = Vector3Add(camera.position, dir);
        camera.fovy = fovSetting;

        // ---- Draw ----
        BeginDrawing();
        ClearBackground((Color){20,25,35,255});

        if (uiState == UI_MENU) {
            DrawMenu(sw, sh);
        } else if (uiState == UI_SETTINGS) {
            DrawSettings(sw, sh);
        } else if (uiState == UI_JOIN_INPUT) {
            DrawJoinInput(sw, sh);
        } else if (uiState == UI_CONNECTING) {
            DrawConnecting(sw, sh);
        } else if (uiState == UI_HOST_LOBBY) {
            DrawLobby(sw, sh, true);
        } else if (uiState == UI_CLIENT_LOBBY) {
            DrawLobby(sw, sh, false);
        } else {
            // In-game: render 3D
            BeginMode3D(camera);
                DrawArena();
                DrawInteriorWalls();
                DrawDoors();
                DrawWindows();
                DrawWallBuys();
                DrawPerkMachines();
                DrawPaP();
                for (int i = 0; i < NET_MAX_PLAYERS; i++) {
                    if (i == localPlayerIdx) continue;
                    DrawOtherPlayer(i);
                }
                for (int i = 0; i < MAX_ENEMIES; i++) if (enemies[i].alive) DrawEnemy(&enemies[i]);
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!bullets[i].alive) continue;
                    DrawSphere(bullets[i].pos, 0.08f, YELLOW);
                    DrawSphere(bullets[i].pos, 0.14f, (Color){255,200,0,90});
                }
            EndMode3D();

            // World-space labels for wall buys
            for (int i = 0; i < wallBuyCount; i++) {
                WallBuy *wb = &wallBuys[i];
                const WeaponDef *w = &WEAPONS[wb->weaponIdx];
                Vector3 above = { wb->pos.x, wb->pos.y + 1.2f, wb->pos.z };
                Vector3 toCam = Vector3Subtract(camera.position, wb->pos);
                if (Vector3DotProduct(toCam, wb->normal) <= 0) continue;
                Vector2 sp = GetWorldToScreen(above, camera);
                if (sp.x < -100 || sp.y < -100 || sp.x > sw + 100 || sp.y > sh + 100) continue;
                int owned = FindOwnedSlot(me, wb->weaponIdx);
                char label[64];
                snprintf(label, sizeof label, "%s  %d",
                         w->name, (owned >= 0) ? w->ammoPrice : w->buyPrice);
                int lw = MeasureText(label, 18);
                DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
                DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, w->tint);
            }
            for (int i = 0; i < perkMachineCount; i++) {
                PerkMachine *pm = &perkMachines[i];
                const PerkDef *pd = &PERKS[pm->perkIdx];
                Vector3 above = { pm->pos.x, 3.3f, pm->pos.z };
                Vector2 sp = GetWorldToScreen(above, camera);
                if (sp.x < -200 || sp.y < -100 || sp.x > sw + 200 || sp.y > sh + 100) continue;
                char label[64];
                if (me->hasPerk[pm->perkIdx]) snprintf(label, sizeof label, "%s", pd->name);
                else                          snprintf(label, sizeof label, "%s  %d", pd->name, pd->cost);
                int lw = MeasureText(label, 18);
                DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
                DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18,
                         me->hasPerk[pm->perkIdx] ? GRAY : pd->tint);
            }
            {
                Vector3 above = { pap.pos.x, 3.4f, pap.pos.z };
                Vector2 sp = GetWorldToScreen(above, camera);
                if (sp.x > -200 && sp.x < sw + 200 && sp.y > -200 && sp.y < sh + 200) {
                    char label[64];
                    snprintf(label, sizeof label, "PACK-A-PUNCH  %d", PAP_COST);
                    int lw = MeasureText(label, 18);
                    DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
                    DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, (Color){200,150,255,255});
                }
            }
            // Doors
            for (int i = 0; i < doorCount; i++) {
                if (doors[i].opened) continue;
                Vector3 above = { doors[i].box.center.x,
                                  doors[i].box.center.y + doors[i].box.size.y*0.5f + 0.8f,
                                  doors[i].box.center.z };
                Vector2 sp = GetWorldToScreen(above, camera);
                if (sp.x < -200 || sp.y < -100 || sp.x > sw + 200 || sp.y > sh + 100) continue;
                char label[64];
                snprintf(label, sizeof label, "DOOR  %d", doors[i].cost);
                int lw = MeasureText(label, 18);
                DrawRectangle((int)sp.x - lw/2 - 6, (int)sp.y - 12, lw + 12, 24, (Color){0,0,0,160});
                DrawText(label, (int)sp.x - lw/2, (int)sp.y - 9, 18, (Color){220,170,110,255});
            }

            DrawHUD(sw, sh, (uiState == UI_PLAY) ? ix : (Interact){ IK_NONE, -1, 0 });

            if (uiState == UI_PAUSE) DrawPause(sw, sh);
            if (gamePhase == GS_GAME_OVER) DrawGameOver(sw, sh);
        }

        EndDrawing();
    }

    Net_Shutdown();
    CloseWindow();
    return 0;
}
