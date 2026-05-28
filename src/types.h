#ifndef SHOOTER_TYPES_H
#define SHOOTER_TYPES_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#include "net.h"   // NET_MAX_PLAYERS

// ============================================================================
//  Constants
// ============================================================================

#define WINDOW_W_DEFAULT 1280
#define WINDOW_H_DEFAULT 720

#define ARENA_HALF         40.0f
#define WALL_HEIGHT         5.0f
#define WALL_THICK          1.0f
#define PLAYER_EYE          1.7f
#define PLAYER_RADIUS       0.4f
#define BASE_MOVE_SPEED     7.0f
#define PLAYER_GRAVITY     22.0f
#define PLAYER_JUMP_VEL     7.5f
#define ADS_FOV_MUL         0.65f
#define ADS_MOVE_MUL        0.55f

#define MAX_ENEMIES        48
#define ENEMY_RADIUS        0.6f
#define ENEMY_HEIGHT        1.7f
#define ENEMY_TOUCH_COOLDOWN 0.7f
#define ENEMY_DAMAGE        8
#define ENEMY_BOARD_ATK     2.4f

#define MAX_BULLETS       128
#define BULLET_SPEED       90.0f
#define BULLET_LIFE         1.0f

#define MAX_OBSTACLES      12
#define MAX_INTERIOR_WALLS  8
#define MAX_DOORS           4

#define INV_SLOTS           2
#define INTERACT_DIST       3.0f
#define DOOR_INTERACT_DIST  3.5f

#define HIT_POINTS         10
#define KILL_POINTS        50
#define BOARD_REPAIR_PTS   10
#define BOARD_REPAIR_TIME   0.5f

#define PAP_COST           5000
#define PAP_DURATION        4.0f

#define MAX_WINDOWS         4
#define MAX_BOARDS_PER_WIN  5
#define WINDOW_WIDTH        4.0f

#define SNAPSHOT_HZ        20.0f
#define INPUT_HZ           30.0f

#define MAX_POWERUPS       16
#define POWERUP_LIFETIME   30.0f
#define POWERUP_PICKUP_R    1.4f
#define POWERUP_DROP_CHANCE 0.06f

// ============================================================================
//  Weapons
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

typedef struct {
    int   weaponIdx;
    int   ammo;
    int   reserve;
    float fireTimer;
    float reloadTimer;
    bool  owned;
    bool  packed;
} WeaponSlot;

// ============================================================================
//  Perks
// ============================================================================

enum { PERK_JUG = 0, PERK_SPEED, PERK_DTAP, PERK_STAMIN, PERK_COUNT };

typedef struct {
    const char *name;
    int    cost;
    Color  tint;
} PerkDef;

// ============================================================================
//  Player
// ============================================================================

typedef struct {
    bool       active;
    bool       alive;
    char       name[32];
    Vector3    pos;
    float      yaw, pitch;
    int        hp;
    int        points;
    WeaponSlot inventory[INV_SLOTS];
    int        currentSlot;
    bool       hasPerk[PERK_COUNT];

    bool       fireHeld;
    bool       interactHeld;
    bool       adsHeld;

    float      damageFlash;
    float      meleeTimer;

    // Vertical movement (gravity / jump)
    float      velY;
    bool       onGround;

    // Stats
    int        kills;
    int        headshots;
    int        shotsFired;
    int        shotsHit;
    int        meleeKills;
    int        revives;

    // Locally-tracked (not in snapshot)
    float      stamina;       // 0..100

    // Target for hold-action (revive)
    float      reviveAsTarget;  // progress while another player revives THIS one
    int        reviverIdx;       // who is reviving us, -1 if none
} Player;

// ============================================================================
//  World entities
// ============================================================================

typedef enum { ZS_OUTSIDE, ZS_AT_WINDOW, ZS_INSIDE } ZombieState;
typedef enum { ZT_NORMAL = 0, ZT_RUNNER, ZT_CRAWLER, ZT_BOSS, ZT_COUNT } ZombieType;

typedef struct {
    Vector3      pos;
    int          hp;
    int          maxHp;
    bool         alive;
    float        touchTimer;
    float        bobPhase;
    float        speed;
    ZombieState  state;
    ZombieType   type;
    int          targetWindow;
    float        attackTimer;
    int          targetPlayer;
    Vector3      waypoint;       // intermediate path target (door)
    bool         hasWaypoint;
    // Host-only AI escape: when stuck, sidestep perpendicular for a beat.
    float        escapeTimer;
    Vector3      escapeDir;
    uint8_t      stuckBias;
} Enemy;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    int     damage;
    float   life;
    bool    alive;
    int     ownerPlayer;
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
    float   repairProgress;
    int     repairPlayer;
    int     lockedByDoor;
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
    int     perkIdx;
} PerkMachine;

typedef struct {
    Vector3 pos;
    float   activeTimer;
    int     slotInProgress;
    int     ownerPlayer;
    float   bob;
} PackAPunch;

#define MBOX_IDLE       0
#define MBOX_ROLLING    1
#define MBOX_WAITING    2
#define MBOX_COST       950
#define MBOX_ROLL_TIME   4.0f
#define MBOX_WAIT_TIME   8.0f

typedef struct {
    bool    placed;
    Vector3 pos;
    int     state;
    float   timer;
    int     showingWeapon;
    int     finalWeapon;
    int     ownerPlayer;
    float   bob;
} MysteryBox;

// ============================================================================
//  Game flow
// ============================================================================

typedef enum {
    GS_PRE_GAME, GS_PLAY, GS_ROUND_BREAK, GS_GAME_OVER
} GamePhase;

typedef enum {
    UI_MENU, UI_SETTINGS, UI_JOIN_INPUT, UI_CONNECTING,
    UI_HOST_LOBBY, UI_CLIENT_LOBBY, UI_PLAY, UI_PAUSE,
} UiState;

typedef enum {
    IK_NONE, IK_WALLBUY, IK_PERK, IK_PAP, IK_WINDOW, IK_DOOR,
    IK_REVIVE, IK_MBOX
} InteractKind;

#define REVIVE_TIME 4.0f

typedef struct { InteractKind kind; int idx; float dist; } Interact;

// ============================================================================
//  Power-ups
// ============================================================================

typedef enum {
    PU_MAX_AMMO = 0, PU_NUKE, PU_DOUBLE_POINTS, PU_INSTAKILL, PU_CARPENTER,
    PU_COUNT
} PowerUpType;

typedef struct {
    bool        active;
    PowerUpType type;
    Vector3     pos;
    float       bob;
    float       lifetime;
} PowerUp;

#endif
