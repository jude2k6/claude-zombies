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

/* ARENA_HALF is no longer a compile-time constant; runtime values are
   arenaHalfX / arenaHalfZ in level.c (extern'd in level.h).
   Keep this fallback define only for Level_LoadHardcodedFallback. */
#define ARENA_HALF_DEFAULT 40.0f
#define WALL_HEIGHT         5.0f
#define WALL_THICK          1.0f
#define DOOR_HEIGHT         2.5f   // height of door + matching wall opening
#define PLAYER_EYE          1.7f
#define PLAYER_RADIUS       0.4f
#define STEP_UP_HEIGHT      0.6f   // max ledge the player auto-climbs (stairs/curbs)
#define BASE_MOVE_SPEED     7.0f
#define PLAYER_GRAVITY     36.0f
#define PLAYER_JUMP_VEL     7.0f
#define ADS_FOV_MUL         0.65f
#define ADS_MOVE_MUL        0.55f

#define MAX_ENEMIES        48
#define ENEMY_RADIUS        0.6f
#define ENEMY_HEIGHT        1.7f
#define ENEMY_TOUCH_COOLDOWN 0.7f
#define ENEMY_DAMAGE        50     // CoD: ~2 hits down at 100 HP, ~5 with Juggernog (250)
#define ENEMY_BOARD_ATK     2.4f

// Death animation window: keep the corpse for the full death clip duration
// so the rigged 'death' one-shot finishes before the slot is freed.
#define ENEMY_DEATH_WINDOW  1.34f  // seconds (matches zombie.glb 'death' clip)
// Sim attack timer: set on each player bite; render plays 'attack_a' while > 0.
#define ENEMY_ATTACK_TIMER  0.61f  // seconds (matches zombie.glb 'attack_a' clip)

// Health regeneration (CoD-style). After REGEN_DELAY seconds without taking
// damage, an upright player regenerates REGEN_RATE HP/sec back to their max.
#define REGEN_DELAY         4.0f
#define REGEN_RATE          110.0f

#define MAX_BULLETS       192
#define BULLET_TAIL_MAX     2.0f   // tracer visual length cap (m)
// MAX_DECALS now lives in engine/decals.h (decals is an engine subsystem).
#define MAX_THROWABLES     16

#define MAX_OBSTACLES      64
#define MAX_INTERIOR_WALLS 64   // header walls above doors count too
#define MAX_DOORS          16
#define MAX_WALLBUYS       16
#define MAX_MAP_PROPS      64
#define MAX_FLOORS         32   // walkable floor regions (multi-floor maps)

#define INV_SLOTS           2
#define INTERACT_DIST       3.0f
#define DOOR_INTERACT_DIST  3.5f
#define BLEED_TIME         30.0f
#define ROUND_RESPAWN_DELAY 1.0f

#define HIT_POINTS         10
#define KILL_POINTS        60
#define BOARD_REPAIR_PTS   10
#define BOARD_REPAIR_TIME   0.5f

#define PAP_COST           5000

#define MAX_WINDOWS        16
#define MAX_BOARDS_PER_WIN  5
#define WINDOW_WIDTH        4.0f

#define SNAPSHOT_HZ        20.0f
#define INPUT_HZ           30.0f

#define MAX_POWERUPS       16
#define POWERUP_LIFETIME   30.0f
#define POWERUP_PICKUP_R    1.4f
#define POWERUP_DROP_CHANCE 0.06f

// Equipment (lethals + tacticals). Counts live on Player; throwable
// projectiles spawn into throwables[]. Frag = AoE damage, stun = AoE
// zombie freeze (Enemy.stunTimer > 0 -> slow + can't bite).
#define STARTING_LETHALS    2
#define STARTING_TACTICALS  2
#define MAX_LETHALS         4
#define MAX_TACTICALS       4
#define FRAG_FUSE           2.0f
#define FRAG_RADIUS         5.0f
#define FRAG_DAMAGE         260
#define STUN_RADIUS         5.5f
#define STUN_DURATION       4.5f
#define THROW_SPEED        14.0f
#define THROW_UPKICK        3.5f
#define THROWABLE_RADIUS    0.10f

// ============================================================================
//  Weapons
// ============================================================================

typedef enum { FM_SEMI = 0, FM_BURST, FM_AUTO } FireMode;

// Which procedural fire-SFX bank in audio.c a weapon's shots route to.
typedef enum { WSFX_SHOT = 0, WSFX_SHOTGUN, WSFX_RAYGUN } WeaponSfxKind;

// Which arms-viewmodel hold the weapon uses at idle. LONG = two-hand
// foregrip hold (the shared `idle` clip); PISTOL = support hand cups the
// gun hand (`idle_pistol` clip, falls back to `idle` if the clip is
// missing from arms_vm.glb).
typedef enum { VMPOSE_LONG = 0, VMPOSE_PISTOL } WeaponVmPose;

// High-level slot category. Drives HUD ordering, wallbuy filter,
// equipment-slot rules (lethal/tactical occupy their own slots).
typedef enum {
    WC_PRIMARY = 0,   // pistol/smg/shotgun/rifle — fills inventory[]
    WC_SPECIAL,       // wonder weapons / heavies — also inventory[]
    WC_MELEE,         // knife/bowie — own slot (planned)
    WC_LETHAL,        // frag/semtex — own slot (planned)
    WC_TACTICAL,      // stun/flash — own slot (planned)
} WeaponCategory;

typedef struct {
    // Stable string id (e.g. "PISTOL") that .weapon files use to claim
    // a slot. Mirrors the WeaponId enum; left non-NULL once the loader
    // has filled this entry. Used for diagnostics.
    const char *idName;
    const char *name;
    const char *packedName;
    WeaponCategory category;
    int    damage;
    int    magSize;
    int    reserveMax;
    float  fireCooldown;   // post-shot (or post-burst) cooldown
    float  reloadTime;
    int    pellets;
    float  spreadDeg;
    FireMode fireMode;
    int    burstCount;     // shots per click for FM_BURST
    float  burstInterval;  // delay between shots within a burst (s)
    int    buyPrice;
    int    ammoPrice;
    Color  tint;
    float  bulletSpeed;    // m/s — sets tracer feel + travel time
    float  bulletLife;     // seconds before despawn
    float  tracerWidth;    // capsule radius for the visible tracer (m)
    // Recoil — degrees added to pitch (up) and ±range on yaw per shot.
    // ADS halves the effective kick.
    float  recoilPitch;
    float  recoilYaw;
    // Damage dropoff (linear from dropoffStart to dropoffEnd meters,
    // scaling damage from 1.0 down to dropoffMinMul). At or past end,
    // damage is `damage * dropoffMinMul`.
    float  dropoffStart;
    float  dropoffEnd;
    float  dropoffMinMul;
    // Splash / AoE damage on bullet impact. splashRadius=0 disables.
    // Damage falls off linearly to 0 at radius; never applied to the
    // directly-hit enemy (they already took full damage). Headshot
    // multiplier does NOT apply to splash.
    float  splashRadius;
    int    splashDamage;
    // Fire SFX routing — bank + volume + pitch (consumed by audio.c).
    // .weapon key: `sfx SHOT|SHOTGUN|RAYGUN vol pitch`
    WeaponSfxKind sfxKind;
    float  sfxVol;
    float  sfxPitch;
    // Haptic punch on fire, local player only (weapons.c:Weapon_Fire):
    // camera shake amount/time + pad rumble low/high motor strengths.
    // .weapon key: `haptic shake time rumbleLow rumbleHigh`
    float  hapticShake;
    float  hapticTime;
    float  rumbleLow;
    float  rumbleHigh;
    // Mystery Box roll weighting (relative; 1.0 = baseline, 0 = never
    // rolls). .weapon key: `mbox_weight w`
    float  mboxWeight;
    // Arms-viewmodel idle hold. .weapon key: `vm_pose LONG|PISTOL`
    WeaponVmPose vmPose;
} WeaponDef;

// Adding a 6th weapon? Checklist (everything else is data-driven via the
// data/weapons/<name>/<name>.weapon file — see weapons.c for the format):
//   1. Extend this enum before W_COUNT.
//   2. Add a minimal fallback entry to WEAPONS[] + weaponGrip[] in weapons.c
//      (idName at minimum — the .weapon file supplies the real values).
//   3. Add the id token to IdNameToIdx (weapons.c), WeaponNameToIdx
//      (level.c), and the WALLBUY token list in mapdoc.c.
//   4. Bump NET_PROTO_VERSION in net.h — weapon indices are serialized raw
//      (inventory, mystery box, PaP) and old clients would mis-decode the
//      new index.
//   5. Author data/weapons/<name>/{<name>.weapon,<name>.obj,<name>.mtl}
//      (~0.5–1.5 m long, origin at the grip, forward_axis='Z' export) and
//      tune vm_grip_* via --screenshot-viewmodels.
enum { W_PISTOL = 0, W_SMG, W_SHOTGUN, W_RIFLE, W_RAYGUN, W_COUNT };

typedef struct {
    int   weaponIdx;
    int   ammo;
    int   reserve;
    float fireTimer;
    float reloadTimer;
    bool  owned;
    bool  packed;
    // Burst state (FM_BURST only). Local — not serialized.
    int   burstRemaining;
    float burstTimer;
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
    bool       prevFireHeld;   // edge-detect for FM_SEMI / FM_BURST
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
    float      sprintBlend;   // 0 = walk, 1 = full sprint (eased)
    float      moveBlend;     // 0 = still, 1 = moving (eased; drives bob/spread)

    // Target for hold-action (revive)
    float      reviveAsTarget;  // progress while another player revives THIS one
    int        reviverIdx;       // who is reviving us, -1 if none

    // Downed/spectator state. `alive` stays true while downed; `downed` lets
    // the player be revived. `bleedTimer` ticks while downed, and on expiry
    // we transition to alive=false (full death + spectator until next round).
    bool       downed;
    float      bleedTimer;
    int        highestRound;  // peak round this player reached this match

    // Equipment counts (single type each for now — frag + stun).
    int        lethals;
    int        tacticals;

    // Seconds since this player last took damage; drives HP regen. Local to
    // the authoritative sim (not serialized — clients see the resulting hp).
    float      regenTimer;
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
    // Per-type AI scratch. Runner: lunge timer (>0 = lunge active,
    // <-cooldown = ready). Crawler/Boss: unused for now.
    float        specialTimer;
    // Stun grenade effect: while >0, speed scales to 0.3x and bite is
    // suppressed. Decrements each tick. Set by Throwables_Detonate
    // (TH_STUN). Local-only — not serialized; clients infer from a
    // visual hint instead.
    float        stunTimer;
    // Death window: set to ENEMY_DEATH_WINDOW at the kill moment. While > 0
    // the slot is occupied by a falling corpse (alive=false, no AI, no
    // collision, not counted toward round/spawns). Decremented each tick;
    // when it reaches 0 the slot is fully free. Serialized so clients play
    // the death clip in sync.
    float        dyingTimer;
    // Sim attack timer: set to ENEMY_ATTACK_TIMER when the zombie bites a
    // player. Render plays the 'attack_a' one-shot while > 0. Serialized.
    float        simAttackTimer;
} Enemy;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    Vector3 origin;       // spawn position for damage-dropoff distance
    int     weaponIdx;    // for per-weapon dropoff curve at hit
    int     damage;
    float   life;
    bool    alive;
    int     ownerPlayer;
} Bullet;

typedef enum { TH_FRAG = 0, TH_STUN } ThrowableKind;

typedef struct {
    bool          alive;
    ThrowableKind kind;
    Vector3       pos;
    Vector3       vel;
    float         fuse;         // seconds until detonation (frag); stun
                                // detonates on first wall/floor contact
    int           ownerPlayer;
    float         spinPhase;    // visual tumble
} Throwable;

typedef struct {
    Vector3 center;
    Vector3 size;
} Box;

// A walkable floor surface over an axis-aligned XZ rectangle (multi-floor
// maps). Flat when yLow == yHigh; a ramp/stair otherwise — the surface Y
// interpolates yLow -> yHigh along rampAxis. Stacked regions over the same
// XZ give true overlapping floors (an upstairs above a downstairs); the
// player stands on the highest surface at or just above their feet, found by
// Level_FloorHeightAt(). An implicit ground plane at Y=0 is always present, so
// maps with no FloorRegions behave exactly as the old flat world.
typedef enum { RAMP_FLAT = 0, RAMP_X = 1, RAMP_Z = 2 } RampAxis;
typedef struct {
    float cx, cz;          // XZ center
    float halfX, halfZ;    // XZ half-extents
    float yLow, yHigh;     // surface Y (flat: equal; ramp: low/high edge)
    RampAxis rampAxis;     // RAMP_FLAT, or slope along +X / +Z
} FloorRegion;

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
    char name[24];   // empty if unnamed; used by WINDOW LOCKED_BY
} Door;

// Authored props placed via map file (PROP lines).  Distinct from
// OBSTACLE: props get rendered as models and use a per-prop collider
// looked up by name in PROP_DEFS[].
typedef struct {
    int     propId;     // PropId from assets.h
    Vector3 pos;
    float   yawDeg;
    float   scale;
    Box     collider;   // XZ collision box centred on pos
} MapProp;

typedef struct {
    Vector3 pos;
    Vector3 normal;
    int     weaponIdx;
} WallBuy;

typedef struct {
    Vector3 pos;
    int     perkIdx;
} PerkMachine;

// Pack-a-Punch phases. See docs/pack-a-punch-spec.md.
#define PAP_IDLE        0   // free, ready to take a weapon
#define PAP_INSERT      1   // gun animating from hands into the chamber
#define PAP_WORK        2   // chamber closed, upgrading
#define PAP_READY       3   // upgraded gun presented, awaiting manual pickup

#define PAP_INSERT_TIME  0.8f
#define PAP_WORK_TIME    3.5f

typedef struct {
    Vector3 pos;
    int     phase;          // PAP_IDLE / INSERT / WORK / READY
    float   timer;          // counts down within the current timed phase
    int     slotInProgress; // owner's inventory slot being upgraded (locked)
    int     ownerPlayer;
    int     weaponIdx;      // weapon being upgraded (for render + retrieval)
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
    UI_MENU, UI_SETTINGS, UI_BINDINGS, UI_JOIN_INPUT, UI_CONNECTING,
    UI_SOLO_LOBBY, UI_MP_MENU,
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
