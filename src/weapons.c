#include "weapons.h"
#include "player.h"
#include "level.h"
#include "interact.h"
#include "fx.h"
#include "particles.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// strdup is POSIX, not C11 — local copy so strict -std=c11 builds work.
static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

// ---- WEAPONS[] storage --------------------------------------------------
// PLAIN STORAGE — there are NO compiled-in weapon stats. The .weapon files
// under data/weapons/<name>/ are the single source of truth for every
// number; Weapons_Load zeroes these arrays (identity scale only), parses
// the files, and complains loudly about any W_* slot no file claimed.
// A slot with no file has all-zero stats (can't fire, never rolls in the
// mystery box) and shows its raw id token as its name.
WeaponDef WEAPONS[W_COUNT];

// Stable string id per slot. Mirrors the W_* enum order; this is the token
// a .weapon file's `id` field uses to claim the slot, and the placeholder
// display name until the file loads.
static const char *W_ID_NAMES[W_COUNT] = {
    [W_PISTOL]  = "PISTOL",
    [W_SMG]     = "SMG",
    [W_SHOTGUN] = "SHOTGUN",
    [W_RIFLE]   = "RIFLE",
    [W_RAYGUN]  = "RAYGUN",
};

// ---- weapon model storage (moved from assets.c) -------------------------
Model weaponModels[W_COUNT];
bool  weaponModelLoaded[W_COUNT];

WeaponModelTune weaponTune[W_COUNT];   // populated from model_* keys
WeaponGrip      weaponGrip[W_COUNT];   // populated from vm_grip_* keys

int Weapon_EffDamage(Player *p, WeaponSlot *s) {
    int d = WEAPONS[s->weaponIdx].damage;
    if (s->packed) d = (int)(d * 2.5f);
    if (p->hasPerk[PERK_DTAP]) d = (int)(d * 1.33f);
    return d;
}
float Weapon_EffFireCD(Player *p, WeaponSlot *s) {
    float c = WEAPONS[s->weaponIdx].fireCooldown;
    if (p->hasPerk[PERK_DTAP]) c *= 0.72f;
    return c;
}
float Weapon_EffReload(Player *p, WeaponSlot *s) {
    float r = WEAPONS[s->weaponIdx].reloadTime;
    if (p->hasPerk[PERK_SPEED]) r *= 0.5f;
    return r;
}
int Weapon_EffMagSize(WeaponSlot *s) {
    int m = WEAPONS[s->weaponIdx].magSize;
    if (s->packed) m *= 2;
    return m;
}
int Weapon_EffReserveMax(WeaponSlot *s) {
    int r = WEAPONS[s->weaponIdx].reserveMax;
    if (s->packed) r *= 2;   // Pack-a-Punch doubles the reserve pool
    return r;
}

int Weapon_FindOwnedSlot(Player *p, int weaponIdx) {
    for (int i = 0; i < INV_SLOTS; i++)
        if (p->inventory[i].owned && p->inventory[i].weaponIdx == weaponIdx) return i;
    return -1;
}
int Weapon_FirstEmptySlot(Player *p) {
    for (int i = 0; i < INV_SLOTS; i++) if (!p->inventory[i].owned) return i;
    return -1;
}

Vector3 Weapon_SpreadDir(Vector3 base, float degrees) {
    if (degrees <= 0) return base;
    float rad = degrees * DEG2RAD;
    float yaw   = Level_RandRange(-rad, rad);
    float pitch = Level_RandRange(-rad, rad);
    Vector3 up    = { 0, 1, 0 };
    Vector3 right = Vector3Normalize(Vector3CrossProduct(base, up));
    Vector3 trueUp = Vector3CrossProduct(right, base);
    Vector3 d = base;
    d = Vector3Add(d, Vector3Scale(right,  tanf(yaw)));
    d = Vector3Add(d, Vector3Scale(trueUp, tanf(pitch)));
    return Vector3Normalize(d);
}

// Forward decl for bullet spawn
extern void Bullets_Spawn(Vector3 origin, Vector3 dir, float speed, float life,
                          int damage, int weaponIdx, int ownerPlayer);

void Weapon_Fire(Player *p) {
    if (!p->alive) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned) return;
    if (s->reloadTimer > 0 || s->fireTimer > 0 || s->ammo <= 0) return;
    if (PaP_SlotLocked((int)(p - players), p->currentSlot)) return;  // in the PaP machine

    const WeaponDef *w = &WEAPONS[s->weaponIdx];
    Vector3 dir = Player_LookDir(p->yaw, p->pitch);
    // Spawn from the gun muzzle, not the eye, so tracers visibly leave the
    // weapon instead of the crosshair. Offsets mirror the viewmodel anchor in
    // render.c (forward/right/down from camera); ADS pulls the gun to center.
    Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, (Vector3){0,1,0}));
    Vector3 up    = Vector3CrossProduct(right, dir);
    float adsBlend = p->adsHeld ? 0.15f : 1.0f;
    float fwdOff   = 0.55f;                  // muzzle sits in front of the eye either way
    float rightOff = 0.22f * adsBlend;
    float downOff  = 0.18f * adsBlend;
    Vector3 origin = p->pos;
    origin = Vector3Add(origin, Vector3Scale(dir,    fwdOff));
    origin = Vector3Add(origin, Vector3Scale(right,  rightOff));
    origin = Vector3Add(origin, Vector3Scale(up,    -downOff));
    int dmg = Weapon_EffDamage(p, s);
    int ownerIdx = (int)(p - players);
    // Movement throws off accuracy: walking adds a little bloom, sprinting a
    // lot. ADS settles everything (and you can't sprint while aiming). For
    // remote players on the host, moveBlend/sprintBlend stay 0 (local-only
    // fields), so their hip-fire isn't movement-penalised — same MP caveat as
    // the per-weapon recoil.
    float adsMul = p->adsHeld ? 0.25f : 1.0f;
    float moveSpread = p->moveBlend * 1.2f + p->sprintBlend * 3.5f;
    float spread = (w->spreadDeg + moveSpread) * adsMul;
    for (int k = 0; k < w->pellets; k++) {
        Vector3 d = Weapon_SpreadDir(dir, spread);
        Bullets_Spawn(origin, d, w->bulletSpeed, w->bulletLife, dmg, s->weaponIdx, ownerIdx);
    }
    // Muzzle flash sparks + casing eject at the bullet's origin.
    Particles_MuzzleFlash(origin, dir);
    Particles_CasingEject(origin, right);

    s->ammo--;
    s->fireTimer = Weapon_EffFireCD(p, s);
    p->shotsFired += w->pellets;

    // Recoil — kick pitch (up) and random yaw after the shot, so the first
    // round of each burst/click lands where aimed. ADS halves the kick.
    float recoilMul = p->adsHeld ? 0.5f : 1.0f;
    p->pitch += w->recoilPitch * DEG2RAD * recoilMul;
    if (p->pitch >  1.55f) p->pitch =  1.55f;
    if (w->recoilYaw > 0.001f) {
        float yawKick = Level_RandRange(-w->recoilYaw, w->recoilYaw) * DEG2RAD * recoilMul;
        p->yaw += yawKick;
    }

    if (ownerIdx == localPlayerIdx) {
        // Per-weapon haptic punch (camera shake + rumble — distinct from the
        // aim-kick recoil applied above). Data-driven via the `haptic` key.
        Fx_PunchAndRumble(w->hapticShake, w->hapticTime, w->rumbleLow, w->rumbleHigh);
    }
}

void Weapon_StartReload(Player *p) {
    if (!p->alive) return;
    WeaponSlot *s = &p->inventory[p->currentSlot];
    if (!s->owned) return;
    if (s->reloadTimer > 0 || s->ammo >= Weapon_EffMagSize(s) || s->reserve <= 0) return;
    s->reloadTimer = Weapon_EffReload(p, s);
}

void Weapon_FinishReloadIfReady(Player *p, WeaponSlot *s) {
    (void)p;
    if (s->reloadTimer > 0) return;
    int mag = Weapon_EffMagSize(s);
    int need = mag - s->ammo;
    if (need <= 0) return;
    int take = (need < s->reserve) ? need : s->reserve;
    s->ammo += take; s->reserve -= take;
}

void Weapon_SwapSlot(Player *p, int target) {
    if (target < 0 || target >= INV_SLOTS) return;
    if (p->inventory[target].owned) p->currentSlot = target;
}

// Forward decls into entities
extern Enemy enemies[MAX_ENEMIES];
extern int   enemiesAlive;

#define MELEE_RANGE       1.8f
#define MELEE_DAMAGE      150
#define MELEE_KILL_POINTS 130
#define MELEE_COOLDOWN    0.6f

void Weapon_Melee(Player *p) {
    if (!p->alive) return;
    if (p->meleeTimer > 0) return;
    p->meleeTimer = MELEE_COOLDOWN;

    Vector3 fwd = { sinf(p->yaw), 0, -cosf(p->yaw) };
    int ownerIdx = (int)(p - players);
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) continue;
        float dx = enemies[i].pos.x - p->pos.x;
        float dz = enemies[i].pos.z - p->pos.z;
        float d2 = dx*dx + dz*dz;
        if (d2 > MELEE_RANGE * MELEE_RANGE) continue;
        float d = sqrtf(d2);
        if (d < 0.0001f) d = 0.0001f;
        float dot = (dx * fwd.x + dz * fwd.z) / d;
        if (dot < 0.3f) continue;   // ~70deg forward arc
        enemies[i].hp -= MELEE_DAMAGE;
        if (enemies[i].hp <= 0) {
            enemies[i].dyingTimer = ENEMY_DEATH_WINDOW;
            enemies[i].alive = false;
            enemiesAlive--;
            if (ownerIdx >= 0 && ownerIdx < NET_MAX_PLAYERS && players[ownerIdx].active) {
                players[ownerIdx].points += MELEE_KILL_POINTS;
                players[ownerIdx].kills++;
                players[ownerIdx].meleeKills++;
            }
        } else if (ownerIdx >= 0 && ownerIdx < NET_MAX_PLAYERS && players[ownerIdx].active) {
            players[ownerIdx].points += HIT_POINTS;
        }
    }
}

// ============================================================================
//  .weapon file loader
// ============================================================================
//
// File format (line-based, "#" starts a comment, blank lines ignored):
//
//     id              PISTOL
//     name            M1911
//     packed_name     Mustang
//     category        PRIMARY              # PRIMARY | SPECIAL | MELEE | LETHAL | TACTICAL
//     fire_mode       SEMI                 # SEMI | BURST | AUTO
//     damage          12
//     mag_size        12
//     reserve_max     120
//     fire_cooldown   0.20
//     reload_time     1.3
//     pellets         1
//     spread_deg      0.0
//     burst_count     3                    # only required for BURST
//     burst_interval  0.07
//     buy_price       0
//     ammo_price      100
//     tint            255 220 140 255
//     bullet_speed    320.0
//     bullet_life     0.35
//     tracer_width    0.025
//     recoil_pitch    0.55
//     recoil_yaw      0.18
//     dropoff         15.0 35.0 0.55       # start  end  minMul
//     splash          3.0 70               # radius  peakDamage (linear falloff)
//     model           pistol.obj           # path relative to the .weapon file
//     model_scale     1.2                  # viewmodel framing (legacy path + DrawWeaponDisplay)
//     model_yaw       90.0
//     model_offset    0.05 0.09 0.0
//     sfx             SHOT 0.55 1.10       # bank (SHOT|SHOTGUN|RAYGUN)  vol  pitch
//     haptic          0.10 0.30 0.25 0.06  # shake  time  rumbleLow  rumbleHigh
//     mbox_weight     1.0                  # mystery-box roll weight (0 = never)
//     vm_grip_pos     0.04 -0.12 0.05      # arms-path grip nudge (+x right, +y muzzle, +z up)
//     vm_grip_rot     0 0 0                # fine rotation (deg) after the base +90X
//     vm_grip_scale   1.0                  # gun size relative to the arms
//
// There are NO compiled-in defaults — a field not present in the file stays
// ZERO (identity 1.0 for the scale keys), so every gameplay field should be
// specified. The `id` field is REQUIRED and must match one of the W_* enum
// names (without the prefix).

static int IdNameToIdx(const char *s) {
    if (!s) return -1;
    for (int i = 0; i < W_COUNT; i++)
        if (strcmp(s, W_ID_NAMES[i]) == 0) return i;
    return -1;
}

static int SfxKindFromStr(const char *s, int fallback) {
    if (!s) return fallback;
    if (strcmp(s, "SHOT")    == 0) return WSFX_SHOT;
    if (strcmp(s, "SHOTGUN") == 0) return WSFX_SHOTGUN;
    if (strcmp(s, "RAYGUN")  == 0) return WSFX_RAYGUN;
    return fallback;
}

static FireMode FireModeFromStr(const char *s, FireMode fallback) {
    if (!s) return fallback;
    if (strcmp(s, "SEMI")  == 0) return FM_SEMI;
    if (strcmp(s, "BURST") == 0) return FM_BURST;
    if (strcmp(s, "AUTO")  == 0) return FM_AUTO;
    return fallback;
}

static WeaponCategory CategoryFromStr(const char *s, WeaponCategory fallback) {
    if (!s) return fallback;
    if (strcmp(s, "PRIMARY")  == 0) return WC_PRIMARY;
    if (strcmp(s, "SPECIAL")  == 0) return WC_SPECIAL;
    if (strcmp(s, "MELEE")    == 0) return WC_MELEE;
    if (strcmp(s, "LETHAL")   == 0) return WC_LETHAL;
    if (strcmp(s, "TACTICAL") == 0) return WC_TACTICAL;
    return fallback;
}

// Tokenize whitespace-separated, in place. Returns token count, fills toks[].
static int Tokenize(char *line, char *toks[], int maxToks) {
    int n = 0;
    char *p = line;
    while (*p && n < maxToks) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        toks[n++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (*p) { *p = 0; p++; }
    }
    return n;
}

static bool ParseFloatTok(const char *s, float *out) {
    if (!s) return false;
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s) return false;
    *out = v;
    return true;
}
static bool ParseIntTok(const char *s, int *out) {
    if (!s) return false;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    *out = (int)v;
    return true;
}

// Returns the directory of `path` (everything up to and including the last
// '/'). Writes into `out`, caps at outSize. Empty string if no slash.
static void DirOf(const char *path, char *out, size_t outSize) {
    const char *slash = strrchr(path, '/');
    if (!slash) { if (outSize > 0) out[0] = 0; return; }
    size_t n = (size_t)(slash - path) + 1;
    if (n >= outSize) n = outSize - 1;
    memcpy(out, path, n);
    out[n] = 0;
}

// Parse one .weapon file. On success returns the resolved idx; -1 on error.
static int Weapons_ParseFile(const char *path) {
    char *text = LoadFileText(path);
    if (!text) {
        fprintf(stderr, "weapon: failed to read %s\n", path);
        return -1;
    }

    int idx = -1;
    WeaponDef d = {0};   // accumulator — copied into WEAPONS[idx] only if we find a valid id
    WeaponModelTune tune = { .scale = 1.0f, .yawDeg = 0.0f, .offset = {0,0,0} };
    WeaponGrip grip = { .scale = 1.0f };
    char modelFile[128] = {0};
    bool tuneSet = false, gripSet = false;

    char *line = text;
    int lineNo = 0;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        lineNo++;

        // Strip comments
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;

        char *toks[8];
        int n = Tokenize(line, toks, 8);
        if (n == 0) goto next;
        const char *k = toks[0];

        if      (strcmp(k, "id") == 0 && n >= 2) {
            idx = IdNameToIdx(toks[1]);
            if (idx < 0) {
                fprintf(stderr, "weapon: %s line %d: unknown id '%s'\n",
                        path, lineNo, toks[1]);
                free(text);
                return -1;
            }
            d = WEAPONS[idx];   // zeroed storage (+ idName) — file supplies everything
            tune = weaponTune[idx];
            grip = weaponGrip[idx];
            d.idName = W_ID_NAMES[idx];
        }
        else if (idx < 0) {
            // any non-id key before id is a structural error
            fprintf(stderr, "weapon: %s line %d: '%s' before 'id'\n",
                    path, lineNo, k);
            free(text);
            return -1;
        }
        else if (strcmp(k, "name") == 0 && n >= 2)        { d.name       = xstrdup(toks[1]); }
        else if (strcmp(k, "packed_name") == 0 && n >= 2) { d.packedName = xstrdup(toks[1]); }
        else if (strcmp(k, "category") == 0 && n >= 2)    { d.category = CategoryFromStr(toks[1], d.category); }
        else if (strcmp(k, "fire_mode") == 0 && n >= 2)   { d.fireMode = FireModeFromStr(toks[1], d.fireMode); }
        else if (strcmp(k, "damage") == 0 && n >= 2)      ParseIntTok(toks[1], &d.damage);
        else if (strcmp(k, "mag_size") == 0 && n >= 2)    ParseIntTok(toks[1], &d.magSize);
        else if (strcmp(k, "reserve_max") == 0 && n >= 2) ParseIntTok(toks[1], &d.reserveMax);
        else if (strcmp(k, "fire_cooldown") == 0 && n >= 2) ParseFloatTok(toks[1], &d.fireCooldown);
        else if (strcmp(k, "reload_time") == 0 && n >= 2) ParseFloatTok(toks[1], &d.reloadTime);
        else if (strcmp(k, "pellets") == 0 && n >= 2)     ParseIntTok(toks[1], &d.pellets);
        else if (strcmp(k, "spread_deg") == 0 && n >= 2)  ParseFloatTok(toks[1], &d.spreadDeg);
        else if (strcmp(k, "burst_count") == 0 && n >= 2) ParseIntTok(toks[1], &d.burstCount);
        else if (strcmp(k, "burst_interval") == 0 && n >= 2) ParseFloatTok(toks[1], &d.burstInterval);
        else if (strcmp(k, "buy_price") == 0 && n >= 2)   ParseIntTok(toks[1], &d.buyPrice);
        else if (strcmp(k, "ammo_price") == 0 && n >= 2)  ParseIntTok(toks[1], &d.ammoPrice);
        else if (strcmp(k, "tint") == 0 && n >= 5) {
            int r=0,g=0,b=0,a=255;
            ParseIntTok(toks[1], &r); ParseIntTok(toks[2], &g);
            ParseIntTok(toks[3], &b); ParseIntTok(toks[4], &a);
            d.tint = (Color){(unsigned char)r,(unsigned char)g,(unsigned char)b,(unsigned char)a};
        }
        else if (strcmp(k, "bullet_speed") == 0 && n >= 2) ParseFloatTok(toks[1], &d.bulletSpeed);
        else if (strcmp(k, "bullet_life") == 0 && n >= 2)  ParseFloatTok(toks[1], &d.bulletLife);
        else if (strcmp(k, "tracer_width") == 0 && n >= 2) ParseFloatTok(toks[1], &d.tracerWidth);
        else if (strcmp(k, "recoil_pitch") == 0 && n >= 2) ParseFloatTok(toks[1], &d.recoilPitch);
        else if (strcmp(k, "recoil_yaw") == 0 && n >= 2)   ParseFloatTok(toks[1], &d.recoilYaw);
        else if (strcmp(k, "dropoff") == 0 && n >= 4) {
            ParseFloatTok(toks[1], &d.dropoffStart);
            ParseFloatTok(toks[2], &d.dropoffEnd);
            ParseFloatTok(toks[3], &d.dropoffMinMul);
        }
        else if (strcmp(k, "splash") == 0 && n >= 3) {
            ParseFloatTok(toks[1], &d.splashRadius);
            ParseIntTok  (toks[2], &d.splashDamage);
        }
        else if (strcmp(k, "model") == 0 && n >= 2) {
            strncpy(modelFile, toks[1], sizeof modelFile - 1);
        }
        else if (strcmp(k, "model_scale") == 0 && n >= 2) {
            ParseFloatTok(toks[1], &tune.scale); tuneSet = true;
        }
        else if (strcmp(k, "model_yaw") == 0 && n >= 2) {
            ParseFloatTok(toks[1], &tune.yawDeg); tuneSet = true;
        }
        else if (strcmp(k, "model_offset") == 0 && n >= 4) {
            ParseFloatTok(toks[1], &tune.offset.x);
            ParseFloatTok(toks[2], &tune.offset.y);
            ParseFloatTok(toks[3], &tune.offset.z);
            tuneSet = true;
        }
        else if (strcmp(k, "sfx") == 0 && n >= 4) {
            d.sfxKind = (WeaponSfxKind)SfxKindFromStr(toks[1], d.sfxKind);
            ParseFloatTok(toks[2], &d.sfxVol);
            ParseFloatTok(toks[3], &d.sfxPitch);
        }
        else if (strcmp(k, "haptic") == 0 && n >= 5) {
            ParseFloatTok(toks[1], &d.hapticShake);
            ParseFloatTok(toks[2], &d.hapticTime);
            ParseFloatTok(toks[3], &d.rumbleLow);
            ParseFloatTok(toks[4], &d.rumbleHigh);
        }
        else if (strcmp(k, "mbox_weight") == 0 && n >= 2) {
            ParseFloatTok(toks[1], &d.mboxWeight);
        }
        else if (strcmp(k, "vm_grip_pos") == 0 && n >= 4) {
            ParseFloatTok(toks[1], &grip.pos.x);
            ParseFloatTok(toks[2], &grip.pos.y);
            ParseFloatTok(toks[3], &grip.pos.z);
            gripSet = true;
        }
        else if (strcmp(k, "vm_grip_rot") == 0 && n >= 4) {
            ParseFloatTok(toks[1], &grip.rotDeg.x);
            ParseFloatTok(toks[2], &grip.rotDeg.y);
            ParseFloatTok(toks[3], &grip.rotDeg.z);
            gripSet = true;
        }
        else if (strcmp(k, "vm_grip_scale") == 0 && n >= 2) {
            ParseFloatTok(toks[1], &grip.scale);
            gripSet = true;
        }
        else {
            fprintf(stderr, "weapon: %s line %d: unknown key '%s'\n",
                    path, lineNo, k);
        }

    next:
        if (!nl) break;
        line = nl + 1;
    }

    if (idx < 0) {
        fprintf(stderr, "weapon: %s missing 'id' field\n", path);
        free(text);
        return -1;
    }

    WEAPONS[idx] = d;
    if (tuneSet) weaponTune[idx] = tune;
    if (gripSet) weaponGrip[idx] = grip;

    // Load the model (relative to the .weapon file's directory).
    if (modelFile[0]) {
        char dir[512]; DirOf(path, dir, sizeof dir);
        char fullPath[768];
        snprintf(fullPath, sizeof fullPath, "%s%s", dir, modelFile);
        if (FileExists(fullPath)) {
            // Unload any prior model in this slot first
            if (weaponModelLoaded[idx]) {
                UnloadModel(weaponModels[idx]);
                weaponModelLoaded[idx] = false;
            }
            weaponModels[idx] = LoadModel(fullPath);
            if (weaponModels[idx].meshCount > 0) {
                weaponModelLoaded[idx] = true;
                fprintf(stderr, "weapon: loaded %s (meshes=%d)\n",
                        fullPath, weaponModels[idx].meshCount);
            } else {
                fprintf(stderr, "weapon: %s: model %s loaded but empty\n",
                        path, fullPath);
            }
        } else {
            fprintf(stderr, "weapon: %s: model %s not found\n", path, fullPath);
        }
    }

    free(text);
    return idx;
}

void Weapons_Load(void) {
    // Reset to a blank slate: zero stats, identity scales, id token as the
    // placeholder display name. Everything real comes from the files.
    for (int i = 0; i < W_COUNT; i++) {
        weaponModelLoaded[i] = false;
        memset(&WEAPONS[i], 0, sizeof WEAPONS[i]);
        WEAPONS[i].idName     = W_ID_NAMES[i];
        WEAPONS[i].name       = W_ID_NAMES[i];
        WEAPONS[i].packedName = W_ID_NAMES[i];
        WEAPONS[i].tint       = WHITE;
        weaponTune[i] = (WeaponModelTune){ .scale = 1.0f };
        weaponGrip[i] = (WeaponGrip){ .scale = 1.0f };
    }

    // Search the same prefix list as other assets so the binary runs from
    // either the build dir (data/weapons/) or the source tree (../data/...).
    static const char *prefixes[] = {
        "data/weapons",
        "../data/weapons",
        "./data/weapons",
    };

    bool claimed[W_COUNT] = { false };
    int parsed = 0;
    for (size_t p = 0; p < sizeof prefixes / sizeof prefixes[0]; p++) {
        if (!DirectoryExists(prefixes[p])) continue;
        FilePathList files = LoadDirectoryFilesEx(prefixes[p], ".weapon", true);
        for (unsigned i = 0; i < files.count; i++) {
            int idx = Weapons_ParseFile(files.paths[i]);
            if (idx >= 0) { claimed[idx] = true; parsed++; }
        }
        UnloadDirectoryFiles(files);
        if (parsed > 0) break;     // first existing root wins
    }

    // No compiled-in fallbacks exist: any unclaimed slot is a broken weapon
    // (zero damage / zero mag, never rolls in the box). Say so loudly.
    for (int i = 0; i < W_COUNT; i++) {
        if (!claimed[i])
            fprintf(stderr, "weapon: ERROR: no .weapon file claimed id %s "
                            "— weapon is unusable (data/weapons/ missing or broken)\n",
                    W_ID_NAMES[i]);
    }
    fprintf(stderr, "weapon: parsed %d .weapon file(s)\n", parsed);
}

void Weapons_Unload(void) {
    for (int i = 0; i < W_COUNT; i++) {
        if (weaponModelLoaded[i]) {
            UnloadModel(weaponModels[i]);
            weaponModelLoaded[i] = false;
        }
    }
}
