/*
 * mapdoc.c — neutral map document model for Claude Zombies
 *
 * Parses and serialises .map files.  No game state is touched here;
 * level.c calls MapDoc_Parse then Level_InstantiateDoc to push data into
 * the live game globals.
 *
 * Dependencies: C standard library only (no raylib, no game headers).
 */

#include "mapdoc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>

/* ---- internal helpers ---- */

/* Tokenise an in-place buffer into NUL-terminated whitespace-delimited
   tokens.  Modifies `s`.  Returns token count capped at `max`. */
static int Tokenise(char *s, char **out, int max) {
    int n = 0;
    while (*s) {
        while (*s && isspace((unsigned char)*s)) *s++ = '\0';
        if (!*s) break;
        if (n < max) out[n] = s;
        n++;
        while (*s && !isspace((unsigned char)*s)) s++;
    }
    return n;
}

static bool ParseFloat(const char *s, float *out) {
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

static bool ParseInt(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    *out = (int)v;
    return true;
}

/* "+x", "-x", "+z", "-z" — writes into dir[4]. */
static bool ParseDir(const char *s, char dir[4]) {
    if (strcmp(s, "+x") == 0 || strcmp(s, "-x") == 0 ||
        strcmp(s, "+z") == 0 || strcmp(s, "-z") == 0) {
        strncpy(dir, s, 3); dir[3] = '\0';
        return true;
    }
    return false;
}

static float Fabsf(float x) { return x < 0.0f ? -x : x; }

/* Assign the next monotonic runtime id during parsing. */
static int NextParseId(MapDoc *doc) {
    return doc->nextId++;
}

/* Add a new sector; returns its index, -1 on cap overflow. */
static int AddSector(MapDoc *doc, const char *name, FILE *errs, int lineNo) {
    if (doc->sectorCount >= MAPDOC_MAX_SECTORS) {
        fprintf(errs, "map: line %d: too many sectors (max %d)\n", lineNo, MAPDOC_MAX_SECTORS);
        return -1;
    }
    MapDocSector *s = &doc->sectors[doc->sectorCount];
    memset(s, 0, sizeof *s);
    strncpy(s->name, name, MAPDOC_NAME_LEN - 1);
    s->name[MAPDOC_NAME_LEN - 1] = '\0';
    s->linkA = s->linkB = -1;
    s->id = NextParseId(doc);
    return doc->sectorCount++;
}

/* Find a sector index by name; -1 if none. */
static int FindSector(const MapDoc *doc, const char *name) {
    for (int i = 0; i < doc->sectorCount; i++)
        if (strcmp(doc->sectors[i].name, name) == 0) return i;
    return -1;
}

/* ---- MapDoc_Parse ---- */

int MapDoc_Parse(const char *path, MapDoc *out, FILE *errs) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(errs, "map: cannot open %s\n", path);
        return 1;
    }

    memset(out, 0, sizeof *out);
    strncpy(out->name, "Unnamed", MAPDOC_NAME_LEN - 1);
    out->arenaHalfX = 40.0f;
    out->arenaHalfZ = 40.0f;
    out->nextId = 1;  /* ids are 1-based; monotonic counter for this parse */

    int errors = 0;
    enum { BLK_NONE, BLK_ATMOS, BLK_SECTOR, BLK_TEX } block = BLK_NONE;
    int  curSectorIdx = -1;

    /* deferred window→door name resolution */
    struct { int winIdx; char name[MAPDOC_DOOR_NAME_LEN]; } pending[MAPDOC_MAX_WINDOWS];
    int pendingCount = 0;

    char raw[512];
    int  lineNo = 0;
    while (fgets(raw, sizeof raw, f)) {
        lineNo++;
        char *hash = strchr(raw, '#');
        if (hash) *hash = '\0';

        char *toks[20];
        int n = Tokenise(raw, toks, 20);
        if (n == 0) continue;

        const char *key = toks[0];

        /* --- block management --- */
        if (strcmp(key, "ATMOSPHERE") == 0) {
            if (block != BLK_NONE) {
                fprintf(errs, "map: line %d: ATMOSPHERE inside another block\n", lineNo);
                errors++; continue;
            }
            block = BLK_ATMOS; continue;
        }
        if (strcmp(key, "TEXTURES") == 0) {
            if (block != BLK_NONE) {
                fprintf(errs, "map: line %d: TEXTURES inside another block\n", lineNo);
                errors++; continue;
            }
            block = BLK_TEX; continue;
        }
        /* SECTOR <name> <x> <z> <sx> <sz> <y>
         * RAMP   <name> <x> <z> <sx> <sz> <yLow> <yHigh> <X|Z> [LINK <a> <b>]
         * Opens a block; placed entities until END belong to this sector. */
        if (strcmp(key, "SECTOR") == 0 || strcmp(key, "RAMP") == 0) {
            bool isRamp = (key[0] == 'R');
            if (block != BLK_NONE) {
                fprintf(errs, "map: line %d: %s inside another block\n", lineNo, key);
                errors++; continue;
            }
            int need = isRamp ? 9 : 7;
            if (n < need) {
                fprintf(errs, "map: line %d: %s expects %s\n", lineNo, key,
                        isRamp ? "name x z sx sz yLow yHigh X|Z [LINK a b]"
                               : "name x z sx sz y");
                errors++; continue;
            }
            float x, z, sx, sz, yLow, yHigh;
            if (!ParseFloat(toks[2], &x)  || !ParseFloat(toks[3], &z) ||
                !ParseFloat(toks[4], &sx) || !ParseFloat(toks[5], &sz) ||
                !ParseFloat(toks[6], &yLow)) {
                fprintf(errs, "map: line %d: %s bad number\n", lineNo, key);
                errors++; continue;
            }
            int rampAxis = 0, linkA = -1, linkB = -1;
            yHigh = yLow;
            if (isRamp) {
                if (!ParseFloat(toks[7], &yHigh)) {
                    fprintf(errs, "map: line %d: RAMP bad yHigh\n", lineNo);
                    errors++; continue;
                }
                if      (strcmp(toks[8], "X") == 0) rampAxis = 1;
                else if (strcmp(toks[8], "Z") == 0) rampAxis = 2;
                else {
                    fprintf(errs, "map: line %d: RAMP axis must be X or Z\n", lineNo);
                    errors++; continue;
                }
                /* optional LINK a b — sectors must already be defined above */
                if (n >= 12 && strcmp(toks[9], "LINK") == 0) {
                    linkA = FindSector(out, toks[10]);
                    linkB = FindSector(out, toks[11]);
                    if (linkA < 0 || linkB < 0) {
                        fprintf(errs, "map: line %d: RAMP LINK references unknown sector "
                                "(define linked sectors before the ramp)\n", lineNo);
                        errors++; continue;
                    }
                }
            }
            int si = AddSector(out, toks[1], errs, lineNo);
            if (si < 0) { errors++; continue; }
            MapDocSector *s = &out->sectors[si];
            s->kind = isRamp ? SECTOR_RAMP : SECTOR_FLAT;
            s->x = x; s->z = z; s->sx = sx; s->sz = sz;
            s->yLow = yLow; s->yHigh = yHigh;
            s->rampAxis = rampAxis; s->linkA = linkA; s->linkB = linkB;
            curSectorIdx = si;
            block = BLK_SECTOR; continue;
        }
        if (strcmp(key, "END") == 0) {
            if (block == BLK_NONE) {
                fprintf(errs, "map: line %d: END outside a block\n", lineNo);
                errors++;
            }
            block = BLK_NONE;
            curSectorIdx = -1;
            continue;
        }

        /* --- textures sub-keys --- */
        if (block == BLK_TEX) {
            if (n < 2) {
                fprintf(errs, "map: line %d: TEXTURES key expects a texture name\n", lineNo);
                errors++; continue;
            }
            out->textures.present = true;
            char *dst = NULL;
            if      (strcmp(key, "floor")    == 0) dst = out->textures.floor;
            else if (strcmp(key, "ground")   == 0) dst = out->textures.ground;
            else if (strcmp(key, "wall_ext") == 0) dst = out->textures.wall_ext;
            else if (strcmp(key, "wall_int") == 0) dst = out->textures.wall_int;
            else if (strcmp(key, "ceiling")  == 0) dst = out->textures.ceiling;
            else {
                fprintf(errs, "map: line %d: unknown TEXTURES key '%s'\n", lineNo, key);
                errors++; continue;
            }
            strncpy(dst, toks[1], MAPDOC_TEX_NAME_LEN - 1);
            dst[MAPDOC_TEX_NAME_LEN - 1] = '\0';
            continue;
        }

        /* --- atmosphere sub-keys --- */
        if (block == BLK_ATMOS) {
            if (strcmp(key, "fog") == 0 && n == 6) {
                float r, g, b, s, e;
                if (!ParseFloat(toks[1], &r) || !ParseFloat(toks[2], &g) ||
                    !ParseFloat(toks[3], &b) || !ParseFloat(toks[4], &s) ||
                    !ParseFloat(toks[5], &e)) {
                    fprintf(errs, "map: line %d: fog expects r g b start end\n", lineNo);
                    errors++; continue;
                }
                out->atmosphere.present = true;
                out->atmosphere.fogR = r; out->atmosphere.fogG = g; out->atmosphere.fogB = b;
                out->atmosphere.fogStart = s; out->atmosphere.fogEnd = e;
            } else if (strcmp(key, "sky_tint") == 0 && n == 4) {
                float r, g, b;
                if (ParseFloat(toks[1], &r) && ParseFloat(toks[2], &g) &&
                    ParseFloat(toks[3], &b)) {
                    out->atmosphere.hasSkyTint = true;
                    out->atmosphere.skyR = r; out->atmosphere.skyG = g; out->atmosphere.skyB = b;
                }
            } else if (strcmp(key, "music") == 0 && n == 2) {
                out->atmosphere.hasMusic = true;
                strncpy(out->atmosphere.music, toks[1], sizeof out->atmosphere.music - 1);
            } else {
                fprintf(errs, "map: line %d: unknown atmosphere key '%s'\n", lineNo, key);
                errors++;
            }
            continue;
        }

        /* --- top-level / room-level entries --- */
        if (strcmp(key, "NAME") == 0) {
            out->name[0] = '\0';
            for (int i = 1; i < n; i++) {
                if (i > 1) strncat(out->name, " ", MAPDOC_NAME_LEN - strlen(out->name) - 1);
                strncat(out->name, toks[i], MAPDOC_NAME_LEN - strlen(out->name) - 1);
            }
            continue;
        }

        if (strcmp(key, "ARENA") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: ARENA expects halfX halfZ\n", lineNo);
                errors++; continue;
            }
            float hx, hz;
            if (!ParseFloat(toks[1], &hx) || !ParseFloat(toks[2], &hz) ||
                hx <= 0.0f || hz <= 0.0f) {
                fprintf(errs, "map: line %d: ARENA bad values (must be positive)\n", lineNo);
                errors++; continue;
            }
            out->arenaHalfX = hx;
            out->arenaHalfZ = hz;
            continue;
        }

        /* Everything below is a placed entity and must live inside a sector,
           which is where it gets its floor Y from. */
        if (block != BLK_SECTOR) {
            fprintf(errs, "map: line %d: '%s' must be inside a SECTOR or RAMP block\n",
                    lineNo, key);
            errors++; continue;
        }

        if (strcmp(key, "SPAWN") == 0) {
            /* SPAWN PLAYER x z   |   SPAWN MOB <mob> x z [LOCKED_BY <door>] */
            char  mob[MAPDOC_SPAWN_MOB_LEN]      = {0};
            char  lockedBy[MAPDOC_DOOR_NAME_LEN] = {0};
            float x, z;
            if (n < 2) {
                fprintf(errs, "map: line %d: SPAWN expects PLAYER or MOB\n", lineNo);
                errors++; continue;
            }
            if (strcmp(toks[1], "PLAYER") == 0) {
                if (n != 4) {
                    fprintf(errs, "map: line %d: SPAWN PLAYER expects x z\n", lineNo);
                    errors++; continue;
                }
                if (!ParseFloat(toks[2], &x) || !ParseFloat(toks[3], &z)) {
                    fprintf(errs, "map: line %d: SPAWN bad number\n", lineNo);
                    errors++; continue;
                }
                strcpy(mob, "PLAYER");
            } else if (strcmp(toks[1], "MOB") == 0) {
                if (n < 5) {
                    fprintf(errs, "map: line %d: SPAWN MOB expects <mob> x z\n", lineNo);
                    errors++; continue;
                }
                strncpy(mob, toks[2], MAPDOC_SPAWN_MOB_LEN - 1);
                if (!ParseFloat(toks[3], &x) || !ParseFloat(toks[4], &z)) {
                    fprintf(errs, "map: line %d: SPAWN bad number\n", lineNo);
                    errors++; continue;
                }
                if (n >= 7 && strcmp(toks[5], "LOCKED_BY") == 0) {
                    strncpy(lockedBy, toks[6], MAPDOC_DOOR_NAME_LEN - 1);
                } else if (n != 5) {
                    fprintf(errs, "map: line %d: SPAWN MOB trailing tokens\n", lineNo);
                    errors++; continue;
                }
            } else {
                fprintf(errs, "map: line %d: SPAWN expects PLAYER or MOB, got '%s'\n",
                        lineNo, toks[1]);
                errors++; continue;
            }
            if (out->spawnCount >= MAPDOC_MAX_SPAWNS) {
                fprintf(errs, "map: line %d: too many spawns (max %d)\n",
                        lineNo, MAPDOC_MAX_SPAWNS);
                errors++; continue;
            }
            MapDocSpawn sp;
            memset(&sp, 0, sizeof sp);
            sp.x = x; sp.z = z;
            snprintf(sp.mob, sizeof sp.mob, "%s", mob);
            snprintf(sp.lockedBy, sizeof sp.lockedBy, "%s", lockedBy);
            sp.sectorId = curSectorIdx;
            sp.id = NextParseId(out);
            out->spawns[out->spawnCount++] = sp;

            /* defer LOCKED_BY validation (door must exist); reuses pending list */
            if (lockedBy[0] && pendingCount < MAPDOC_MAX_WINDOWS) {
                pending[pendingCount].winIdx = -1;   /* -1 marks a spawn ref */
                snprintf(pending[pendingCount].name, sizeof pending[pendingCount].name, "%s", lockedBy);
                pendingCount++;
            }
            continue;
        }

        if (strcmp(key, "WALL") == 0) {
            if (n < 5) {
                fprintf(errs, "map: line %d: WALL expects x1 z1 x2 z2\n", lineNo);
                errors++; continue;
            }
            float x1, z1, x2, z2;
            if (!ParseFloat(toks[1], &x1) || !ParseFloat(toks[2], &z1) ||
                !ParseFloat(toks[3], &x2) || !ParseFloat(toks[4], &z2)) {
                fprintf(errs, "map: line %d: WALL bad number\n", lineNo);
                errors++; continue;
            }
            bool xRun = Fabsf(z1 - z2) < 0.001f;
            bool zRun = Fabsf(x1 - x2) < 0.001f;
            if (!xRun && !zRun) {
                fprintf(errs, "map: line %d: WALL must be axis-aligned\n", lineNo);
                errors++; continue;
            }
            /* normalise ordering */
            if (xRun && x2 < x1) { float t = x1; x1 = x2; x2 = t; }
            if (zRun && z2 < z1) { float t = z1; z1 = z2; z2 = t; }

            if (out->wallCount >= MAPDOC_MAX_WALLS) {
                fprintf(errs, "map: line %d: too many walls (max %d)\n",
                        lineNo, MAPDOC_MAX_WALLS);
                errors++; continue;
            }

            MapDocWall w;
            memset(&w, 0, sizeof w);
            w.x1 = x1; w.z1 = z1; w.x2 = x2; w.z2 = z2;
            w.sectorId = curSectorIdx;
            w.id = NextParseId(out);

            /* optional DOOR clause */
            int ti = 5;
            if (ti < n && strcmp(toks[ti], "DOOR") == 0) {
                if (n < ti + 4) {
                    fprintf(errs, "map: line %d: DOOR expects center width cost\n", lineNo);
                    errors++; continue;
                }
                float center, width;
                int cost;
                if (!ParseFloat(toks[ti+1], &center) ||
                    !ParseFloat(toks[ti+2], &width) ||
                    !ParseInt  (toks[ti+3], &cost)) {
                    fprintf(errs, "map: line %d: DOOR bad number\n", lineNo);
                    errors++; continue;
                }
                /* validate door fits inside wall */
                float dl = center - width * 0.5f;
                float dr = center + width * 0.5f;
                bool fits;
                if (xRun) fits = (dl >= x1 && dr <= x2);
                else       fits = (dl >= z1 && dr <= z2);
                if (!fits) {
                    fprintf(errs, "map: line %d: DOOR extends past wall ends\n", lineNo);
                    errors++; continue;
                }
                w.door.present = true;
                w.door.center  = center;
                w.door.width   = width;
                w.door.cost    = cost;
                ti += 4;   /* advance past DOOR center width cost */
                if (ti + 1 < n && strcmp(toks[ti], "AS") == 0) {
                    strncpy(w.door.name, toks[ti+1], MAPDOC_DOOR_NAME_LEN - 1);
                    ti += 2;
                }
            }
            /* optional TEX clause (after DOOR ... [AS name]) */
            if (ti + 1 < n && strcmp(toks[ti], "TEX") == 0) {
                strncpy(w.texName, toks[ti+1], MAPDOC_TEX_NAME_LEN - 1);
                w.texName[MAPDOC_TEX_NAME_LEN - 1] = '\0';
                ti += 2;
            }
            out->walls[out->wallCount++] = w;
            continue;
        }

        if (strcmp(key, "DOOR") == 0) {
            fprintf(errs, "map: line %d: DOOR is only valid as part of a WALL line\n", lineNo);
            errors++; continue;
        }

        if (strcmp(key, "OBSTACLE") == 0) {
            if (n < 5) {
                fprintf(errs, "map: line %d: OBSTACLE expects x z sx sz [h]\n", lineNo);
                errors++; continue;
            }
            float x, z, sx, sz, h = 2.0f;
            if (!ParseFloat(toks[1], &x)  || !ParseFloat(toks[2], &z) ||
                !ParseFloat(toks[3], &sx) || !ParseFloat(toks[4], &sz)) {
                fprintf(errs, "map: line %d: OBSTACLE bad number\n", lineNo);
                errors++; continue;
            }
            int oi = 5;  /* next token index after mandatory args */
            if (oi < n) {
                float htmp;
                if (ParseFloat(toks[oi], &htmp)) { h = htmp; oi++; }
            }
            if (out->obstacleCount >= MAPDOC_MAX_OBSTACLES) {
                fprintf(errs, "map: line %d: too many obstacles (max %d)\n",
                        lineNo, MAPDOC_MAX_OBSTACLES);
                errors++; continue;
            }
            MapDocObstacle ob;
            memset(&ob, 0, sizeof ob);
            ob.x = x; ob.z = z; ob.sx = sx; ob.sz = sz; ob.h = h;
            ob.sectorId = curSectorIdx;
            ob.id = NextParseId(out);
            /* optional TEX clause */
            if (oi + 1 < n && strcmp(toks[oi], "TEX") == 0) {
                strncpy(ob.texName, toks[oi+1], MAPDOC_TEX_NAME_LEN - 1);
                ob.texName[MAPDOC_TEX_NAME_LEN - 1] = '\0';
            } else if (oi < n && strcmp(toks[oi], "TEX") == 0) {
                fprintf(errs, "map: line %d: TEX expects a texture name\n", lineNo);
                errors++; continue;
            }
            out->obstacles[out->obstacleCount++] = ob;
            continue;
        }

        if (strcmp(key, "WALLBUY") == 0) {
            if (n != 5) {
                fprintf(errs, "map: line %d: WALLBUY expects x z <dir> WEAPON\n", lineNo);
                errors++; continue;
            }
            float x, z;
            char dir[4];
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z) ||
                !ParseDir(toks[3], dir)) {
                fprintf(errs, "map: line %d: WALLBUY bad arg\n", lineNo);
                errors++; continue;
            }
            /* validate weapon name */
            const char *wn = toks[4];
            if (strcmp(wn, "PISTOL") != 0 && strcmp(wn, "SMG") != 0 &&
                strcmp(wn, "SHOTGUN") != 0 && strcmp(wn, "RIFLE") != 0 &&
                strcmp(wn, "RAYGUN") != 0) {
                fprintf(errs, "map: line %d: unknown weapon '%s'\n", lineNo, wn);
                errors++; continue;
            }
            if (out->wallbuyCount >= MAPDOC_MAX_WALLBUYS) {
                fprintf(errs, "map: line %d: too many wall buys (max %d)\n",
                        lineNo, MAPDOC_MAX_WALLBUYS);
                errors++; continue;
            }
            MapDocWallbuy wb;
            memset(&wb, 0, sizeof wb);
            wb.x = x; wb.z = z;
            memcpy(wb.dir, dir, 4);
            strncpy(wb.weapon, wn, sizeof wb.weapon - 1);
            wb.sectorId = curSectorIdx;
            wb.id = NextParseId(out);
            out->wallbuys[out->wallbuyCount++] = wb;
            continue;
        }

        if (strcmp(key, "PERK") == 0) {
            if (n != 4) {
                fprintf(errs, "map: line %d: PERK expects x z NAME\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: PERK bad coord\n", lineNo);
                errors++; continue;
            }
            const char *pn = toks[3];
            if (strcmp(pn, "JUG") != 0 && strcmp(pn, "SPEED") != 0 &&
                strcmp(pn, "DTAP") != 0 && strcmp(pn, "STAMIN") != 0) {
                fprintf(errs, "map: line %d: unknown perk '%s'\n", lineNo, pn);
                errors++; continue;
            }
            if (out->perkCount >= MAPDOC_MAX_PERKS) {
                fprintf(errs, "map: line %d: too many perk machines (max %d)\n",
                        lineNo, MAPDOC_MAX_PERKS);
                errors++; continue;
            }
            MapDocPerk pk;
            memset(&pk, 0, sizeof pk);
            pk.x = x; pk.z = z;
            strncpy(pk.perk, pn, sizeof pk.perk - 1);
            pk.sectorId = curSectorIdx;
            pk.id = NextParseId(out);
            out->perks[out->perkCount++] = pk;
            continue;
        }

        if (strcmp(key, "PAP") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: PAP expects x z\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: PAP bad coord\n", lineNo);
                errors++; continue;
            }
            out->hasPap = true;
            out->pap = (MapDocPap){ x, z, curSectorIdx };
            continue;
        }

        if (strcmp(key, "MBOX") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: MBOX expects x z\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: MBOX bad coord\n", lineNo);
                errors++; continue;
            }
            out->mbox.present = true;
            out->mbox.x = x; out->mbox.z = z;
            out->mbox.sectorId = curSectorIdx;
            continue;
        }

        if (strcmp(key, "WINDOW") == 0) {
            if (n < 4) {
                fprintf(errs, "map: line %d: WINDOW expects x z <dir>\n", lineNo);
                errors++; continue;
            }
            float x, z;
            char dir[4];
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z) ||
                !ParseDir(toks[3], dir)) {
                fprintf(errs, "map: line %d: WINDOW bad arg\n", lineNo);
                errors++; continue;
            }
            char lockedBy[MAPDOC_DOOR_NAME_LEN] = {0};
            if (n >= 6 && strcmp(toks[4], "LOCKED_BY") == 0) {
                strncpy(lockedBy, toks[5], MAPDOC_DOOR_NAME_LEN - 1);
            } else if (n != 4) {
                fprintf(errs, "map: line %d: WINDOW trailing tokens\n", lineNo);
                errors++; continue;
            }
            if (out->windowCount >= MAPDOC_MAX_WINDOWS) {
                fprintf(errs, "map: line %d: too many windows (max %d)\n",
                        lineNo, MAPDOC_MAX_WINDOWS);
                errors++; continue;
            }
            MapDocWindow win;
            memset(&win, 0, sizeof win);
            win.x = x; win.z = z;
            memcpy(win.dir, dir, 4);
            snprintf(win.lockedBy, sizeof win.lockedBy, "%s", lockedBy);
            win.sectorId = curSectorIdx;
            win.id = NextParseId(out);
            int widx = out->windowCount++;
            out->windows[widx] = win;

            /* deferred door-name resolution for LOCKED_BY */
            if (lockedBy[0] && pendingCount < MAPDOC_MAX_WINDOWS) {
                pending[pendingCount].winIdx = widx;
                snprintf(pending[pendingCount].name, sizeof pending[pendingCount].name, "%s", lockedBy);
                pendingCount++;
            }
            continue;
        }

        if (strcmp(key, "PROP") == 0) {
            if (n < 4) {
                fprintf(errs, "map: line %d: PROP expects <name> x z [yaw d] [scale s]\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[2], &x) || !ParseFloat(toks[3], &z)) {
                fprintf(errs, "map: line %d: PROP bad coord\n", lineNo);
                errors++; continue;
            }
            float yaw = 0.0f, scale = 1.0f;
            bool ok = true;
            int i = 4;
            while (i + 1 < n) {
                if (strcmp(toks[i], "yaw") == 0) {
                    if (!ParseFloat(toks[i+1], &yaw)) {
                        fprintf(errs, "map: line %d: PROP bad yaw\n", lineNo);
                        errors++; ok = false; break;
                    }
                } else if (strcmp(toks[i], "scale") == 0) {
                    if (!ParseFloat(toks[i+1], &scale)) {
                        fprintf(errs, "map: line %d: PROP bad scale\n", lineNo);
                        errors++; ok = false; break;
                    }
                } else {
                    fprintf(errs, "map: line %d: PROP unknown option '%s'\n", lineNo, toks[i]);
                    errors++; ok = false; break;
                }
                i += 2;
            }
            if (!ok) continue;
            if (out->propCount >= MAPDOC_MAX_PROPS) {
                fprintf(errs, "map: line %d: too many props (max %d)\n",
                        lineNo, MAPDOC_MAX_PROPS);
                errors++; continue;
            }
            MapDocProp pr;
            memset(&pr, 0, sizeof pr);
            strncpy(pr.name, toks[1], MAPDOC_PROP_NAME_LEN - 1);
            pr.x = x; pr.z = z;
            pr.yawDeg = yaw; pr.scale = scale;
            pr.sectorId = curSectorIdx;
            pr.id = NextParseId(out);
            out->props[out->propCount++] = pr;
            continue;
        }

        fprintf(errs, "map: line %d: unknown key '%s'\n", lineNo, key);
        errors++;
    }
    fclose(f);

    if (block != BLK_NONE) {
        fprintf(errs, "map: end of file inside an open block\n");
        errors++;
    }

    /* Resolve LOCKED_BY names — just validate the door name exists in the
       wall list.  The actual door→index mapping happens in Level_InstantiateDoc
       after the game door array is built.  Here we just confirm the name is
       referenced by a WALL DOOR AS <name> line so the document is self-consistent. */
    for (int pi = 0; pi < pendingCount; pi++) {
        const char *wanted = pending[pi].name;
        bool found = false;
        for (int wi = 0; wi < out->wallCount && !found; wi++)
            if (out->walls[wi].door.present && strcmp(out->walls[wi].door.name, wanted) == 0)
                found = true;
        if (!found) {
            fprintf(errs, "map: window references unknown door '%s'\n", wanted);
            errors++;
        }
    }

    /* Ensure at least one PLAYER spawn exists (sector -1 => ground at Y0) */
    {
        bool hasPlayer = false;
        for (int i = 0; i < out->spawnCount; i++)
            if (strcmp(out->spawns[i].mob, "PLAYER") == 0) { hasPlayer = true; break; }
        if (!hasPlayer && out->spawnCount < MAPDOC_MAX_SPAWNS) {
            MapDocSpawn sp;
            memset(&sp, 0, sizeof sp);
            strcpy(sp.mob, "PLAYER");
            sp.sectorId = -1;
            sp.id = NextParseId(out);
            out->spawns[out->spawnCount++] = sp;
        }
    }

    return errors;
}

/* ---- MapDoc_Save ---- */

static const char *DirStr(const char dir[4]) {
    return dir; /* already "+x"/"-x"/"+z"/"-z" */
}

/* Emit all entities belonging to a given sector (indented inside its block). */
static void EmitEntities(FILE *f, const MapDoc *doc, int sectorId) {
    const char *ind = "    ";

    for (int i = 0; i < doc->spawnCount; i++) {
        const MapDocSpawn *s = &doc->spawns[i];
        if (s->sectorId != sectorId) continue;
        if (strcmp(s->mob, "PLAYER") == 0) {
            fprintf(f, "%sSPAWN PLAYER %.4g %.4g\n", ind, s->x, s->z);
        } else if (s->lockedBy[0]) {
            fprintf(f, "%sSPAWN MOB %s %.4g %.4g LOCKED_BY %s\n",
                    ind, s->mob, s->x, s->z, s->lockedBy);
        } else {
            fprintf(f, "%sSPAWN MOB %s %.4g %.4g\n", ind, s->mob, s->x, s->z);
        }
    }

    for (int i = 0; i < doc->wallCount; i++) {
        const MapDocWall *w = &doc->walls[i];
        if (w->sectorId != sectorId) continue;
        if (w->door.present) {
            fprintf(f, "%sWALL %.4g %.4g  %.4g %.4g  DOOR %.4g %.4g %d",
                    ind, w->x1, w->z1, w->x2, w->z2,
                    w->door.center, w->door.width, w->door.cost);
            if (w->door.name[0])
                fprintf(f, "  AS %s", w->door.name);
        } else {
            fprintf(f, "%sWALL %.4g %.4g  %.4g %.4g",
                    ind, w->x1, w->z1, w->x2, w->z2);
        }
        if (w->texName[0])
            fprintf(f, "  TEX %s", w->texName);
        fprintf(f, "\n");
    }

    for (int i = 0; i < doc->obstacleCount; i++) {
        const MapDocObstacle *o = &doc->obstacles[i];
        if (o->sectorId != sectorId) continue;
        if (Fabsf(o->h - 2.0f) < 0.001f)
            fprintf(f, "%sOBSTACLE %.4g %.4g  %.4g %.4g",
                    ind, o->x, o->z, o->sx, o->sz);
        else
            fprintf(f, "%sOBSTACLE %.4g %.4g  %.4g %.4g %.4g",
                    ind, o->x, o->z, o->sx, o->sz, o->h);
        if (o->texName[0])
            fprintf(f, "  TEX %s", o->texName);
        fprintf(f, "\n");
    }

    for (int i = 0; i < doc->windowCount; i++) {
        const MapDocWindow *w = &doc->windows[i];
        if (w->sectorId != sectorId) continue;
        if (w->lockedBy[0])
            fprintf(f, "%sWINDOW %.4g %.4g  %s  LOCKED_BY %s\n",
                    ind, w->x, w->z, DirStr(w->dir), w->lockedBy);
        else
            fprintf(f, "%sWINDOW %.4g %.4g  %s\n",
                    ind, w->x, w->z, DirStr(w->dir));
    }

    for (int i = 0; i < doc->wallbuyCount; i++) {
        const MapDocWallbuy *wb = &doc->wallbuys[i];
        if (wb->sectorId != sectorId) continue;
        fprintf(f, "%sWALLBUY %.4g %.4g  %s  %s\n",
                ind, wb->x, wb->z, DirStr(wb->dir), wb->weapon);
    }

    for (int i = 0; i < doc->perkCount; i++) {
        const MapDocPerk *pk = &doc->perks[i];
        if (pk->sectorId != sectorId) continue;
        fprintf(f, "%sPERK %.4g %.4g %s\n", ind, pk->x, pk->z, pk->perk);
    }

    if (doc->hasPap && doc->pap.sectorId == sectorId)
        fprintf(f, "%sPAP %.4g %.4g\n", ind, doc->pap.x, doc->pap.z);

    if (doc->mbox.present && doc->mbox.sectorId == sectorId)
        fprintf(f, "%sMBOX %.4g %.4g\n", ind, doc->mbox.x, doc->mbox.z);

    for (int i = 0; i < doc->propCount; i++) {
        const MapDocProp *pr = &doc->props[i];
        if (pr->sectorId != sectorId) continue;
        fprintf(f, "%sPROP %s %.4g %.4g", ind, pr->name, pr->x, pr->z);
        if (Fabsf(pr->yawDeg) > 0.001f)
            fprintf(f, " yaw %.4g", pr->yawDeg);
        if (Fabsf(pr->scale - 1.0f) > 0.001f)
            fprintf(f, " scale %.4g", pr->scale);
        fprintf(f, "\n");
    }
}

int MapDoc_Save(const char *path, const MapDoc *doc) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    /* NAME */
    fprintf(f, "NAME %s\n\n", doc->name);

    /* ARENA (only emit if non-default) */
    if (Fabsf(doc->arenaHalfX - 40.0f) > 0.001f || Fabsf(doc->arenaHalfZ - 40.0f) > 0.001f)
        fprintf(f, "ARENA %.4g %.4g\n\n", doc->arenaHalfX, doc->arenaHalfZ);

    /* ATMOSPHERE */
    if (doc->atmosphere.present) {
        fprintf(f, "ATMOSPHERE\n");
        fprintf(f, "    fog  %.4g %.4g %.4g  %.4g %.4g\n",
                doc->atmosphere.fogR, doc->atmosphere.fogG, doc->atmosphere.fogB,
                doc->atmosphere.fogStart, doc->atmosphere.fogEnd);
        if (doc->atmosphere.hasSkyTint)
            fprintf(f, "    sky_tint  %.4g %.4g %.4g\n",
                    doc->atmosphere.skyR, doc->atmosphere.skyG, doc->atmosphere.skyB);
        if (doc->atmosphere.hasMusic)
            fprintf(f, "    music  %s\n", doc->atmosphere.music);
        fprintf(f, "END\n\n");
    }

    /* TEXTURES (only emit when at least one slot is set) */
    if (doc->textures.present) {
        fprintf(f, "TEXTURES\n");
        if (doc->textures.floor[0])    fprintf(f, "    floor     %s\n", doc->textures.floor);
        if (doc->textures.ground[0])   fprintf(f, "    ground    %s\n", doc->textures.ground);
        if (doc->textures.wall_ext[0]) fprintf(f, "    wall_ext  %s\n", doc->textures.wall_ext);
        if (doc->textures.wall_int[0]) fprintf(f, "    wall_int  %s\n", doc->textures.wall_int);
        if (doc->textures.ceiling[0])  fprintf(f, "    ceiling   %s\n", doc->textures.ceiling);
        fprintf(f, "END\n\n");
    }

    /* One block per sector: header line carries the floor surface + footprint,
       then the entities placed on it. */
    for (int s = 0; s < doc->sectorCount; s++) {
        const MapDocSector *sc = &doc->sectors[s];
        if (sc->kind == SECTOR_RAMP) {
            fprintf(f, "\nRAMP %s  %.4g %.4g  %.4g %.4g  %.4g %.4g %s",
                    sc->name, sc->x, sc->z, sc->sx, sc->sz,
                    sc->yLow, sc->yHigh, sc->rampAxis == 1 ? "X" : "Z");
            if (sc->linkA >= 0 && sc->linkB >= 0)
                fprintf(f, "  LINK %s %s",
                        doc->sectors[sc->linkA].name, doc->sectors[sc->linkB].name);
            fprintf(f, "\n");
        } else {
            fprintf(f, "\nSECTOR %s  %.4g %.4g  %.4g %.4g  %.4g\n",
                    sc->name, sc->x, sc->z, sc->sx, sc->sz, sc->yLow);
        }
        EmitEntities(f, doc, s);
        fprintf(f, "END\n");
    }

    fclose(f);
    return 0;
}

/* ---- MapDoc_Equal ---- */

static bool FEq(float a, float b) { return Fabsf(a - b) < 1e-3f; }
static bool SEq(const char *a, const char *b) { return strcmp(a, b) == 0; }

/* Entity order is not semantically meaningful (MapDoc_Save canonicalises it:
 * ungrouped first, then per-room), so each per-type comparison below is a
 * greedy unordered match: every element of a must pair with a distinct,
 * field-equal element of b. Room order IS preserved by Save, so sectorId
 * values remain directly comparable. */

static bool SpawnEq(const MapDocSpawn *x, const MapDocSpawn *y) {
    return FEq(x->x, y->x) && FEq(x->z, y->z) && x->sectorId == y->sectorId &&
           SEq(x->mob, y->mob) && SEq(x->lockedBy, y->lockedBy);
}
static bool WallEq(const MapDocWall *x, const MapDocWall *y) {
    if (!FEq(x->x1, y->x1) || !FEq(x->z1, y->z1)) return false;
    if (!FEq(x->x2, y->x2) || !FEq(x->z2, y->z2)) return false;
    if (x->sectorId != y->sectorId) return false;
    if (x->door.present != y->door.present) return false;
    if (x->door.present) {
        if (!FEq(x->door.center, y->door.center)) return false;
        if (!FEq(x->door.width,  y->door.width))  return false;
        if (x->door.cost != y->door.cost) return false;
        if (!SEq(x->door.name, y->door.name)) return false;
    }
    if (!SEq(x->texName, y->texName)) return false;
    return true;
}
static bool WindowEq(const MapDocWindow *x, const MapDocWindow *y) {
    return FEq(x->x, y->x) && FEq(x->z, y->z) && x->sectorId == y->sectorId &&
           SEq(x->dir, y->dir) && SEq(x->lockedBy, y->lockedBy);
}
static bool ObstacleEq(const MapDocObstacle *x, const MapDocObstacle *y) {
    return FEq(x->x, y->x) && FEq(x->z, y->z) && x->sectorId == y->sectorId &&
           FEq(x->sx, y->sx) && FEq(x->sz, y->sz) && FEq(x->h, y->h) &&
           SEq(x->texName, y->texName);
}
static bool SectorEq(const MapDocSector *x, const MapDocSector *y) {
    return SEq(x->name, y->name) && x->kind == y->kind &&
           FEq(x->x, y->x) && FEq(x->z, y->z) &&
           FEq(x->sx, y->sx) && FEq(x->sz, y->sz) &&
           FEq(x->yLow, y->yLow) && FEq(x->yHigh, y->yHigh) &&
           x->rampAxis == y->rampAxis &&
           x->linkA == y->linkA && x->linkB == y->linkB;
}
static bool WallbuyEq(const MapDocWallbuy *x, const MapDocWallbuy *y) {
    return FEq(x->x, y->x) && FEq(x->z, y->z) && x->sectorId == y->sectorId &&
           SEq(x->dir, y->dir) && SEq(x->weapon, y->weapon);
}
static bool PerkEq(const MapDocPerk *x, const MapDocPerk *y) {
    return FEq(x->x, y->x) && FEq(x->z, y->z) && x->sectorId == y->sectorId &&
           SEq(x->perk, y->perk);
}
static bool PropEq(const MapDocProp *x, const MapDocProp *y) {
    return SEq(x->name, y->name) && x->sectorId == y->sectorId &&
           FEq(x->x, y->x) && FEq(x->z, y->z) &&
           FEq(x->yawDeg, y->yawDeg) && FEq(x->scale, y->scale);
}

/* Greedy unordered set-match over parallel arrays of `count` elements of
 * `size` bytes, using `eq` as the pairing predicate. */
static bool UnorderedMatch(const void *arrA, const void *arrB, int count,
                           size_t size, bool (*eq)(const void *, const void *)) {
    bool used[MAPDOC_MAX_WALLS] = { false };   /* largest cap */
    for (int i = 0; i < count; i++) {
        const void *ea = (const char *)arrA + (size_t)i * size;
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (used[j]) continue;
            const void *eb = (const char *)arrB + (size_t)j * size;
            if (eq(ea, eb)) { used[j] = true; found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

/* Thin void* adapters for UnorderedMatch. */
static bool SpawnEqV(const void *a, const void *b)    { return SpawnEq(a, b); }
static bool WallEqV(const void *a, const void *b)     { return WallEq(a, b); }
static bool WindowEqV(const void *a, const void *b)   { return WindowEq(a, b); }
static bool ObstacleEqV(const void *a, const void *b) { return ObstacleEq(a, b); }
static bool WallbuyEqV(const void *a, const void *b)  { return WallbuyEq(a, b); }
static bool PerkEqV(const void *a, const void *b)     { return PerkEq(a, b); }
static bool PropEqV(const void *a, const void *b)     { return PropEq(a, b); }

bool MapDoc_Equal(const MapDoc *a, const MapDoc *b) {
    if (!SEq(a->name, b->name)) return false;
    if (!FEq(a->arenaHalfX, b->arenaHalfX)) return false;
    if (!FEq(a->arenaHalfZ, b->arenaHalfZ)) return false;

    /* atmosphere */
    if (a->atmosphere.present != b->atmosphere.present) return false;
    if (a->atmosphere.present) {
        if (!FEq(a->atmosphere.fogR, b->atmosphere.fogR)) return false;
        if (!FEq(a->atmosphere.fogG, b->atmosphere.fogG)) return false;
        if (!FEq(a->atmosphere.fogB, b->atmosphere.fogB)) return false;
        if (!FEq(a->atmosphere.fogStart, b->atmosphere.fogStart)) return false;
        if (!FEq(a->atmosphere.fogEnd, b->atmosphere.fogEnd)) return false;
    }

    /* textures */
    if (a->textures.present != b->textures.present) return false;
    if (a->textures.present) {
        if (!SEq(a->textures.floor,    b->textures.floor))    return false;
        if (!SEq(a->textures.ground,   b->textures.ground))   return false;
        if (!SEq(a->textures.wall_ext, b->textures.wall_ext)) return false;
        if (!SEq(a->textures.wall_int, b->textures.wall_int)) return false;
        if (!SEq(a->textures.ceiling,  b->textures.ceiling))  return false;
    }

    /* entities: unordered per-type match (Save canonicalises order) */
    if (a->spawnCount != b->spawnCount) return false;
    if (!UnorderedMatch(a->spawns, b->spawns, a->spawnCount,
                        sizeof a->spawns[0], SpawnEqV)) return false;

    if (a->wallCount != b->wallCount) return false;
    if (!UnorderedMatch(a->walls, b->walls, a->wallCount,
                        sizeof a->walls[0], WallEqV)) return false;

    if (a->windowCount != b->windowCount) return false;
    if (!UnorderedMatch(a->windows, b->windows, a->windowCount,
                        sizeof a->windows[0], WindowEqV)) return false;

    if (a->obstacleCount != b->obstacleCount) return false;
    if (!UnorderedMatch(a->obstacles, b->obstacles, a->obstacleCount,
                        sizeof a->obstacles[0], ObstacleEqV)) return false;

    if (a->wallbuyCount != b->wallbuyCount) return false;
    if (!UnorderedMatch(a->wallbuys, b->wallbuys, a->wallbuyCount,
                        sizeof a->wallbuys[0], WallbuyEqV)) return false;

    if (a->perkCount != b->perkCount) return false;
    if (!UnorderedMatch(a->perks, b->perks, a->perkCount,
                        sizeof a->perks[0], PerkEqV)) return false;

    /* pap */
    if (a->hasPap != b->hasPap) return false;
    if (a->hasPap) {
        if (!FEq(a->pap.x, b->pap.x) || !FEq(a->pap.z, b->pap.z)) return false;
    }

    /* mbox */
    if (a->mbox.present != b->mbox.present) return false;
    if (a->mbox.present) {
        if (!FEq(a->mbox.x, b->mbox.x) || !FEq(a->mbox.z, b->mbox.z)) return false;
    }

    /* props */
    if (a->propCount != b->propCount) return false;
    if (!UnorderedMatch(a->props, b->props, a->propCount,
                        sizeof a->props[0], PropEqV)) return false;

    /* sectors (ordered: indices are stable references) */
    if (a->sectorCount != b->sectorCount) return false;
    for (int i = 0; i < a->sectorCount; i++)
        if (!SectorEq(&a->sectors[i], &b->sectors[i])) return false;

    /* NOTE: `id` fields and `nextId` are intentionally never compared above —
       they are runtime-only and not part of document content (see mapdoc.h). */

    return true;
}

/* ---- runtime id allocation ---- */

int MapDoc_AllocId(MapDoc *doc) {
    return doc->nextId++;
}

/* ---- geometry / integrity validation ---- */

/* Append one issue when room remains, and always bump the running total so the
   caller learns the real count even with a short buffer. */
static void AddIssue(MapDocIssue *out, int max, int *n, int severity, int entId,
                     const char *fmt, ...) {
    if (out && *n < max) {
        MapDocIssue *it = &out[*n];
        it->severity = severity;
        it->entId    = entId;
        va_list ap; va_start(ap, fmt);
        vsnprintf(it->msg, sizeof it->msg, fmt, ap);
        va_end(ap);
    }
    (*n)++;
}

static bool DirValid(const char *d) {
    return !strcmp(d, "+x") || !strcmp(d, "-x") || !strcmp(d, "+z") || !strcmp(d, "-z");
}

/* Is (x,z) within valid sector index si's footprint (small slack for edges)? */
static bool InSector(const MapDoc *d, int si, float x, float z) {
    const MapDocSector *s = &d->sectors[si];
    return fabsf(x - s->x) <= s->sx * 0.5f + 0.05f &&
           fabsf(z - s->z) <= s->sz * 0.5f + 0.05f;
}

/* Validate an entity's sector reference. Emits an issue and returns -1 if the
   reference is unusable, else the valid sector index. */
static int CheckSectorRef(const MapDoc *d, MapDocIssue *out, int max, int *n,
                          int entId, const char *what, int sectorId) {
    if (sectorId < 0) {
        AddIssue(out, max, n, MAPDOC_ERROR, entId, "%s #%d has no sector (ungrouped)", what, entId);
        return -1;
    }
    if (sectorId >= d->sectorCount) {
        AddIssue(out, max, n, MAPDOC_ERROR, entId, "%s #%d references invalid sector %d", what, entId, sectorId);
        return -1;
    }
    return sectorId;
}

int MapDoc_Validate(const MapDoc *d, MapDocIssue *out, int max) {
    int n = 0;

    /* ---- doc-level ---- */
    if (d->sectorCount == 0)
        AddIssue(out, max, &n, MAPDOC_ERROR, -1, "map has no sectors");

    int players = 0, mobs = 0;
    for (int i = 0; i < d->spawnCount; i++) {
        if (strcmp(d->spawns[i].mob, "PLAYER") == 0) players++; else mobs++;
    }
    if (players == 0) AddIssue(out, max, &n, MAPDOC_ERROR, -1, "no PLAYER spawn - nowhere to start");
    if (mobs == 0)    AddIssue(out, max, &n, MAPDOC_WARN,  -1, "no mob spawns - no zombies will appear");
    if (d->arenaHalfX <= 0 || d->arenaHalfZ <= 0)
        AddIssue(out, max, &n, MAPDOC_WARN, -1, "arena half-extents are non-positive");

    /* ---- sectors ---- */
    for (int i = 0; i < d->sectorCount; i++) {
        const MapDocSector *s = &d->sectors[i];
        if (s->sx <= 0 || s->sz <= 0)
            AddIssue(out, max, &n, MAPDOC_ERROR, s->id, "sector '%s' has non-positive size", s->name);
        if (s->kind == SECTOR_FLAT && fabsf(s->yHigh - s->yLow) > 1e-3f)
            AddIssue(out, max, &n, MAPDOC_WARN, s->id, "flat sector '%s' has yLow != yHigh", s->name);
        if (s->kind == SECTOR_RAMP) {
            if (s->yHigh - s->yLow <= 1e-3f)
                AddIssue(out, max, &n, MAPDOC_WARN, s->id, "ramp '%s' has no rise", s->name);
            if (s->rampAxis != 1 && s->rampAxis != 2)
                AddIssue(out, max, &n, MAPDOC_ERROR, s->id, "ramp '%s' has invalid axis", s->name);
            if (s->linkA >= d->sectorCount || s->linkB >= d->sectorCount)
                AddIssue(out, max, &n, MAPDOC_ERROR, s->id, "ramp '%s' links a non-existent sector", s->name);
        }
    }

    /* ---- placed entities: sector ref + containment + per-kind checks ---- */
    for (int i = 0; i < d->spawnCount; i++) {
        const MapDocSpawn *e = &d->spawns[i];
        int si = CheckSectorRef(d, out, max, &n, e->id, "spawn", e->sectorId);
        // Only PLAYER starts must sit inside their sector. Mob spawns are
        // legitimately placed outside the arena (zombies enter from outside),
        // so containment is not an error for them.
        if (si >= 0 && strcmp(e->mob, "PLAYER") == 0 && !InSector(d, si, e->x, e->z))
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "player spawn #%d sits outside its sector", e->id);
    }
    for (int i = 0; i < d->wallCount; i++) {
        const MapDocWall *e = &d->walls[i];
        int si = CheckSectorRef(d, out, max, &n, e->id, "wall", e->sectorId);
        if (fabsf(e->x2 - e->x1) < 1e-3f && fabsf(e->z2 - e->z1) < 1e-3f)
            AddIssue(out, max, &n, MAPDOC_ERROR, e->id, "wall #%d has zero length", e->id);
        if (si >= 0 && (!InSector(d, si, e->x1, e->z1) || !InSector(d, si, e->x2, e->z2)))
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "wall #%d extends outside its sector", e->id);
    }
    // Windows (barricades) and wallbuys live ON the perimeter / wall line, so
    // they are not containment-checked — only their sector ref + facing.
    for (int i = 0; i < d->windowCount; i++) {
        const MapDocWindow *e = &d->windows[i];
        CheckSectorRef(d, out, max, &n, e->id, "window", e->sectorId);
        if (!DirValid(e->dir))
            AddIssue(out, max, &n, MAPDOC_ERROR, e->id, "window #%d has invalid facing '%s'", e->id, e->dir);
    }
    for (int i = 0; i < d->obstacleCount; i++) {
        const MapDocObstacle *e = &d->obstacles[i];
        int si = CheckSectorRef(d, out, max, &n, e->id, "obstacle", e->sectorId);
        if (e->sx <= 0 || e->sz <= 0 || e->h <= 0)
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "obstacle #%d has non-positive size", e->id);
        if (si >= 0 && !InSector(d, si, e->x, e->z))
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "obstacle #%d sits outside its sector", e->id);
    }
    for (int i = 0; i < d->propCount; i++) {
        const MapDocProp *e = &d->props[i];
        int si = CheckSectorRef(d, out, max, &n, e->id, "prop", e->sectorId);
        if (e->scale <= 0)
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "prop #%d has non-positive scale", e->id);
        if (si >= 0 && !InSector(d, si, e->x, e->z))
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "prop #%d sits outside its sector", e->id);
    }
    for (int i = 0; i < d->wallbuyCount; i++) {
        const MapDocWallbuy *e = &d->wallbuys[i];
        CheckSectorRef(d, out, max, &n, e->id, "wallbuy", e->sectorId);
        if (!DirValid(e->dir))
            AddIssue(out, max, &n, MAPDOC_ERROR, e->id, "wallbuy #%d has invalid facing '%s'", e->id, e->dir);
    }
    for (int i = 0; i < d->perkCount; i++) {
        const MapDocPerk *e = &d->perks[i];
        int si = CheckSectorRef(d, out, max, &n, e->id, "perk", e->sectorId);
        if (si >= 0 && !InSector(d, si, e->x, e->z))
            AddIssue(out, max, &n, MAPDOC_WARN, e->id, "perk #%d sits outside its sector", e->id);
    }

    return n;
}
