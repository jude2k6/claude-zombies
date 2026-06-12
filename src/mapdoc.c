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

/* Find or add a room name; returns index, -1 on cap overflow. */
static int FindOrAddRoom(MapDoc *doc, const char *name, FILE *errs, int lineNo) {
    for (int i = 0; i < doc->roomCount; i++)
        if (strcmp(doc->rooms[i], name) == 0) return i;
    if (doc->roomCount >= MAPDOC_MAX_ROOMS) {
        fprintf(errs, "map: line %d: too many rooms (max %d)\n", lineNo, MAPDOC_MAX_ROOMS);
        return -1;
    }
    strncpy(doc->rooms[doc->roomCount], name, MAPDOC_NAME_LEN - 1);
    doc->rooms[doc->roomCount][MAPDOC_NAME_LEN - 1] = '\0';
    return doc->roomCount++;
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

    int errors = 0;
    enum { BLK_NONE, BLK_ATMOS, BLK_ROOM } block = BLK_NONE;
    int  curRoomIdx = -1;

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
        if (strcmp(key, "ROOM") == 0) {
            if (block != BLK_NONE) {
                fprintf(errs, "map: line %d: ROOM inside another block\n", lineNo);
                errors++; continue;
            }
            if (n < 2) {
                fprintf(errs, "map: line %d: ROOM expects a name\n", lineNo);
                errors++; continue;
            }
            curRoomIdx = FindOrAddRoom(out, toks[1], errs, lineNo);
            block = BLK_ROOM; continue;
        }
        if (strcmp(key, "END") == 0) {
            if (block == BLK_NONE) {
                fprintf(errs, "map: line %d: END outside a block\n", lineNo);
                errors++;
            }
            block = BLK_NONE;
            curRoomIdx = -1;
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

        if (strcmp(key, "SPAWN") == 0) {
            if (n != 3) {
                fprintf(errs, "map: line %d: SPAWN expects x z\n", lineNo);
                errors++; continue;
            }
            float x, z;
            if (!ParseFloat(toks[1], &x) || !ParseFloat(toks[2], &z)) {
                fprintf(errs, "map: line %d: SPAWN bad number\n", lineNo);
                errors++; continue;
            }
            if (out->spawnCount >= MAPDOC_MAX_SPAWNS) {
                fprintf(errs, "map: line %d: too many spawns (max %d)\n",
                        lineNo, MAPDOC_MAX_SPAWNS);
                errors++; continue;
            }
            out->spawns[out->spawnCount++] = (MapDocSpawn){ x, z, curRoomIdx };
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
            w.roomIdx = curRoomIdx;

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
                if (n >= ti + 6 && strcmp(toks[ti+4], "AS") == 0)
                    strncpy(w.door.name, toks[ti+5], MAPDOC_DOOR_NAME_LEN - 1);
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
            if (n >= 6 && !ParseFloat(toks[5], &h)) {
                fprintf(errs, "map: line %d: OBSTACLE bad height\n", lineNo);
                errors++; continue;
            }
            if (out->obstacleCount >= MAPDOC_MAX_OBSTACLES) {
                fprintf(errs, "map: line %d: too many obstacles (max %d)\n",
                        lineNo, MAPDOC_MAX_OBSTACLES);
                errors++; continue;
            }
            out->obstacles[out->obstacleCount++] =
                (MapDocObstacle){ x, z, sx, sz, h, curRoomIdx };
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
            wb.roomIdx = curRoomIdx;
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
            pk.roomIdx = curRoomIdx;
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
            out->pap = (MapDocPap){ x, z, curRoomIdx };
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
            out->mbox.roomIdx = curRoomIdx;
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
            strncpy(win.lockedBy, lockedBy, MAPDOC_DOOR_NAME_LEN - 1);
            win.roomIdx = curRoomIdx;
            int widx = out->windowCount++;
            out->windows[widx] = win;

            /* deferred door-name resolution for LOCKED_BY */
            if (lockedBy[0] && pendingCount < MAPDOC_MAX_WINDOWS) {
                pending[pendingCount].winIdx = widx;
                strncpy(pending[pendingCount].name, lockedBy, MAPDOC_DOOR_NAME_LEN - 1);
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
            pr.roomIdx = curRoomIdx;
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

    /* Ensure at least one spawn exists */
    if (out->spawnCount == 0)
        out->spawns[out->spawnCount++] = (MapDocSpawn){ 0.0f, 0.0f };

    return errors;
}

/* ---- MapDoc_Save ---- */

static const char *DirStr(const char dir[4]) {
    return dir; /* already "+x"/"-x"/"+z"/"-z" */
}

/* Emit all entities for a given roomIdx (-1 = ungrouped). */
static void EmitEntities(FILE *f, const MapDoc *doc, int roomIdx) {
    const char *ind = (roomIdx == -1) ? "" : "    ";

    for (int i = 0; i < doc->spawnCount; i++) {
        if (doc->spawns[i].roomIdx != roomIdx) continue;
        fprintf(f, "%sSPAWN %.4g %.4g\n", ind, doc->spawns[i].x, doc->spawns[i].z);
    }

    for (int i = 0; i < doc->wallCount; i++) {
        const MapDocWall *w = &doc->walls[i];
        if (w->roomIdx != roomIdx) continue;
        if (w->door.present) {
            fprintf(f, "%sWALL %.4g %.4g  %.4g %.4g  DOOR %.4g %.4g %d",
                    ind, w->x1, w->z1, w->x2, w->z2,
                    w->door.center, w->door.width, w->door.cost);
            if (w->door.name[0])
                fprintf(f, "  AS %s", w->door.name);
            fprintf(f, "\n");
        } else {
            fprintf(f, "%sWALL %.4g %.4g  %.4g %.4g\n",
                    ind, w->x1, w->z1, w->x2, w->z2);
        }
    }

    for (int i = 0; i < doc->obstacleCount; i++) {
        const MapDocObstacle *o = &doc->obstacles[i];
        if (o->roomIdx != roomIdx) continue;
        if (Fabsf(o->h - 2.0f) < 0.001f)
            fprintf(f, "%sOBSTACLE %.4g %.4g  %.4g %.4g\n",
                    ind, o->x, o->z, o->sx, o->sz);
        else
            fprintf(f, "%sOBSTACLE %.4g %.4g  %.4g %.4g %.4g\n",
                    ind, o->x, o->z, o->sx, o->sz, o->h);
    }

    for (int i = 0; i < doc->windowCount; i++) {
        const MapDocWindow *w = &doc->windows[i];
        if (w->roomIdx != roomIdx) continue;
        if (w->lockedBy[0])
            fprintf(f, "%sWINDOW %.4g %.4g  %s  LOCKED_BY %s\n",
                    ind, w->x, w->z, DirStr(w->dir), w->lockedBy);
        else
            fprintf(f, "%sWINDOW %.4g %.4g  %s\n",
                    ind, w->x, w->z, DirStr(w->dir));
    }

    for (int i = 0; i < doc->wallbuyCount; i++) {
        const MapDocWallbuy *wb = &doc->wallbuys[i];
        if (wb->roomIdx != roomIdx) continue;
        fprintf(f, "%sWALLBUY %.4g %.4g  %s  %s\n",
                ind, wb->x, wb->z, DirStr(wb->dir), wb->weapon);
    }

    for (int i = 0; i < doc->perkCount; i++) {
        const MapDocPerk *pk = &doc->perks[i];
        if (pk->roomIdx != roomIdx) continue;
        fprintf(f, "%sPERK %.4g %.4g %s\n", ind, pk->x, pk->z, pk->perk);
    }

    if (doc->hasPap && doc->pap.roomIdx == roomIdx)
        fprintf(f, "%sPAP %.4g %.4g\n", ind, doc->pap.x, doc->pap.z);

    if (doc->mbox.present && doc->mbox.roomIdx == roomIdx)
        fprintf(f, "%sMBOX %.4g %.4g\n", ind, doc->mbox.x, doc->mbox.z);

    for (int i = 0; i < doc->propCount; i++) {
        const MapDocProp *pr = &doc->props[i];
        if (pr->roomIdx != roomIdx) continue;
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

    /* ungrouped entities (roomIdx == -1) */
    fprintf(f, "# --- ungrouped ---\n");
    EmitEntities(f, doc, -1);

    /* per-room blocks */
    for (int r = 0; r < doc->roomCount; r++) {
        fprintf(f, "\n# --- ROOM %s ---\n", doc->rooms[r]);
        fprintf(f, "ROOM %s\n", doc->rooms[r]);
        EmitEntities(f, doc, r);
        fprintf(f, "END\n");
    }

    fclose(f);
    return 0;
}

/* ---- MapDoc_Equal ---- */

static bool FEq(float a, float b) { return Fabsf(a - b) < 1e-3f; }
static bool SEq(const char *a, const char *b) { return strcmp(a, b) == 0; }

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

    /* spawns */
    if (a->spawnCount != b->spawnCount) return false;
    for (int i = 0; i < a->spawnCount; i++) {
        if (!FEq(a->spawns[i].x, b->spawns[i].x)) return false;
        if (!FEq(a->spawns[i].z, b->spawns[i].z)) return false;
        if (a->spawns[i].roomIdx != b->spawns[i].roomIdx) return false;
    }

    /* walls */
    if (a->wallCount != b->wallCount) return false;
    for (int i = 0; i < a->wallCount; i++) {
        const MapDocWall *wa = &a->walls[i], *wb = &b->walls[i];
        if (!FEq(wa->x1, wb->x1) || !FEq(wa->z1, wb->z1)) return false;
        if (!FEq(wa->x2, wb->x2) || !FEq(wa->z2, wb->z2)) return false;
        if (wa->door.present != wb->door.present) return false;
        if (wa->door.present) {
            if (!FEq(wa->door.center, wb->door.center)) return false;
            if (!FEq(wa->door.width,  wb->door.width))  return false;
            if (wa->door.cost != wb->door.cost) return false;
            if (!SEq(wa->door.name, wb->door.name)) return false;
        }
    }

    /* windows */
    if (a->windowCount != b->windowCount) return false;
    for (int i = 0; i < a->windowCount; i++) {
        const MapDocWindow *wa = &a->windows[i], *wb = &b->windows[i];
        if (!FEq(wa->x, wb->x) || !FEq(wa->z, wb->z)) return false;
        if (!SEq(wa->dir, wb->dir)) return false;
        if (!SEq(wa->lockedBy, wb->lockedBy)) return false;
    }

    /* obstacles */
    if (a->obstacleCount != b->obstacleCount) return false;
    for (int i = 0; i < a->obstacleCount; i++) {
        const MapDocObstacle *oa = &a->obstacles[i], *ob = &b->obstacles[i];
        if (!FEq(oa->x, ob->x) || !FEq(oa->z, ob->z)) return false;
        if (!FEq(oa->sx, ob->sx) || !FEq(oa->sz, ob->sz)) return false;
        if (!FEq(oa->h, ob->h)) return false;
    }

    /* wallbuys */
    if (a->wallbuyCount != b->wallbuyCount) return false;
    for (int i = 0; i < a->wallbuyCount; i++) {
        if (!FEq(a->wallbuys[i].x, b->wallbuys[i].x)) return false;
        if (!FEq(a->wallbuys[i].z, b->wallbuys[i].z)) return false;
        if (!SEq(a->wallbuys[i].dir, b->wallbuys[i].dir)) return false;
        if (!SEq(a->wallbuys[i].weapon, b->wallbuys[i].weapon)) return false;
    }

    /* perks */
    if (a->perkCount != b->perkCount) return false;
    for (int i = 0; i < a->perkCount; i++) {
        if (!FEq(a->perks[i].x, b->perks[i].x)) return false;
        if (!FEq(a->perks[i].z, b->perks[i].z)) return false;
        if (!SEq(a->perks[i].perk, b->perks[i].perk)) return false;
    }

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
    for (int i = 0; i < a->propCount; i++) {
        if (!SEq(a->props[i].name, b->props[i].name)) return false;
        if (!FEq(a->props[i].x, b->props[i].x)) return false;
        if (!FEq(a->props[i].z, b->props[i].z)) return false;
        if (!FEq(a->props[i].yawDeg, b->props[i].yawDeg)) return false;
        if (!FEq(a->props[i].scale, b->props[i].scale)) return false;
    }

    /* rooms (names and count) */
    if (a->roomCount != b->roomCount) return false;
    for (int i = 0; i < a->roomCount; i++)
        if (!SEq(a->rooms[i], b->rooms[i])) return false;

    return true;
}
