#ifndef SHOOTER_MAPDOC_H
#define SHOOTER_MAPDOC_H

/*
 * mapdoc.h — neutral map document model for Claude Zombies
 *
 * This header and mapdoc.c depend ONLY on the C standard library so that
 * a future editor can link them standalone without pulling in raylib or any
 * game headers.
 *
 * The document model mirrors the .map grammar exactly.  mapdoc.c owns the
 * parser and serialiser; level.c owns the instantiation step that copies
 * a parsed MapDoc into the live game globals.
 */

#include <stdio.h>
#include <stdbool.h>

/* ---- document caps (independent of game caps in types.h) ---- */
#define MAPDOC_MAX_SPAWNS     64
#define MAPDOC_MAX_WALLS     128
#define MAPDOC_MAX_WINDOWS    32
#define MAPDOC_MAX_OBSTACLES  64
#define MAPDOC_MAX_PROPS      64
#define MAPDOC_MAX_WALLBUYS   16
#define MAPDOC_MAX_PERKS       8
#define MAPDOC_MAX_SECTORS    32
#define MAPDOC_NAME_LEN       64
#define MAPDOC_DOOR_NAME_LEN  24
#define MAPDOC_PROP_NAME_LEN  32

/* ---- texture name length (shared by TEX slots and per-surface names) ---- */
#define MAPDOC_TEX_NAME_LEN  64

/* Per-map TEXTURES block — all keys optional, empty string = unset. */
typedef struct {
    bool present;
    char floor   [MAPDOC_TEX_NAME_LEN];  /* "" = use boot slot */
    char ground  [MAPDOC_TEX_NAME_LEN];
    char wall_ext[MAPDOC_TEX_NAME_LEN];
    char wall_int[MAPDOC_TEX_NAME_LEN];
    char ceiling [MAPDOC_TEX_NAME_LEN];
} MapDocTextures;

/* ---- sub-structs ---- */

typedef struct {
    float x, z;
    int   sectorId;  /* -1 = ungrouped */
} MapDocSpawn;

/* Optional door embedded in a WALL line. */
typedef struct {
    bool  present;
    float center;   /* along the wall's running axis */
    float width;
    int   cost;
    char  name[MAPDOC_DOOR_NAME_LEN];   /* "" if no AS clause */
} MapDocDoorSpec;

typedef struct {
    float        x1, z1, x2, z2;
    MapDocDoorSpec door;
    int          sectorId;  /* -1 = not inside any ROOM block */
    /* optional TEX override; "" = unset */
    char         texName[MAPDOC_TEX_NAME_LEN];
} MapDocWall;

typedef struct {
    float x, z;
    /* direction stored as one of: "+x" "-x" "+z" "-z" */
    char  dir[4];
    /* optional LOCKED_BY door name; "" if none */
    char  lockedBy[MAPDOC_DOOR_NAME_LEN];
    int   sectorId;
} MapDocWindow;

typedef struct {
    float x, z, sx, sz, h;
    int   sectorId;
    /* optional TEX override; "" = unset */
    char  texName[MAPDOC_TEX_NAME_LEN];
} MapDocObstacle;

/* A sector: a named, axis-aligned floor area at a height. It is the unit of
 * authoring, placement, and nav. Entities reference their sector by index and
 * derive their world Y from its surface, so overlapping floors are unambiguous.
 *   kind FLAT: a level deck/room; the surface is yLow everywhere (yHigh == yLow).
 *   kind RAMP: a slope/stair from yLow -> yHigh along rampAxis (1 = +X, 2 = +Z),
 *              connecting two sectors (linkA, linkB) — these are the nav edges.
 * x,z = footprint centre; sx,sz = full XZ size. */
typedef enum { SECTOR_FLAT = 0, SECTOR_RAMP = 1 } MapDocSectorKind;
typedef struct {
    char  name[MAPDOC_NAME_LEN];
    int   kind;            /* MapDocSectorKind */
    float x, z, sx, sz;    /* footprint centre + full size */
    float yLow, yHigh;     /* FLAT: equal; RAMP: low/high edge */
    int   rampAxis;        /* RAMP only: 1 = +X, 2 = +Z */
    int   linkA, linkB;    /* RAMP only: connected sector indices (-1 = none) */
} MapDocSector;

typedef struct {
    char  name[MAPDOC_PROP_NAME_LEN];
    float x, z;
    float yawDeg;
    float scale;
    int   sectorId;
} MapDocProp;

typedef struct {
    float x, z;
    /* direction stored as one of: "+x" "-x" "+z" "-z" */
    char  dir[4];
    /* weapon name: "PISTOL" "SMG" "SHOTGUN" "RIFLE" "RAYGUN" */
    char  weapon[16];
    int   sectorId;
} MapDocWallbuy;

typedef struct {
    float x, z;
    /* perk name: "JUG" "SPEED" "DTAP" "STAMIN" */
    char  perk[16];
    int   sectorId;
} MapDocPerk;

typedef struct {
    float x, z;
    int   sectorId;
} MapDocPap;

typedef struct {
    bool  present;
    float x, z;
    int   sectorId;
} MapDocMbox;

typedef struct {
    bool  present;
    /* fog */
    float fogR, fogG, fogB;  /* 0-255 */
    float fogStart, fogEnd;
    /* sky tint (reserved, store if present) */
    bool  hasSkyTint;
    float skyR, skyG, skyB;
    /* music (reserved) */
    bool  hasMusic;
    char  music[64];
} MapDocAtmosphere;

/* ---- top-level document ---- */

typedef struct {
    char name[MAPDOC_NAME_LEN];

    /* Per-map arena half-extents (defaults 40 40 when absent). */
    float arenaHalfX;
    float arenaHalfZ;

    MapDocAtmosphere atmosphere;
    MapDocTextures   textures;  /* per-map texture slot overrides */

    /* Sector table. Entities reference a sector by index; every placed entity
       belongs to exactly one sector and takes its floor Y from it. */
    int          sectorCount;
    MapDocSector sectors[MAPDOC_MAX_SECTORS];

    int          spawnCount;
    MapDocSpawn  spawns[MAPDOC_MAX_SPAWNS];

    int          wallCount;
    MapDocWall   walls[MAPDOC_MAX_WALLS];

    int          windowCount;
    MapDocWindow windows[MAPDOC_MAX_WINDOWS];

    int             obstacleCount;
    MapDocObstacle  obstacles[MAPDOC_MAX_OBSTACLES];

    int          propCount;
    MapDocProp   props[MAPDOC_MAX_PROPS];

    int            wallbuyCount;
    MapDocWallbuy  wallbuys[MAPDOC_MAX_WALLBUYS];

    int          perkCount;
    MapDocPerk   perks[MAPDOC_MAX_PERKS];

    bool         hasPap;
    MapDocPap    pap;

    MapDocMbox   mbox;
} MapDoc;

/* ---- API ---- */

/*
 * MapDoc_Parse — parse `path` into `*out`.
 * Returns the number of errors found.  Line-numbered error messages are
 * written to `errs` (pass stderr normally).  On return, `*out` contains
 * whatever was parsed successfully up to the first fatal cap-overflow;
 * callers should treat any non-zero return as a potentially incomplete doc.
 */
int  MapDoc_Parse(const char *path, MapDoc *out, FILE *errs);

/*
 * MapDoc_Save — serialise `*doc` to `path`.
 * Emits NAME, ARENA, ATMOSPHERE/TEXTURES blocks, then one SECTOR/RAMP block
 * per sector with its placed entities.  Re-readable by MapDoc_Parse (hand-
 * written comments are not preserved).  Returns 0 on success, -1 on I/O error.
 */
int  MapDoc_Save(const char *path, const MapDoc *doc);

/*
 * MapDoc_Equal — field-wise equality with float tolerance ~1e-3.
 * Used by the --map-roundtrip dev tool.
 */
bool MapDoc_Equal(const MapDoc *a, const MapDoc *b);

#endif /* SHOOTER_MAPDOC_H */
