#include "settings.h"
#include "pad.h"
#include "menu.h"

#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Default bindings (mirror the hardcoded layout pre-rebinding).
int bindButton[BA_COUNT] = {
    [BA_FIRE]     = BIND_TRIG_R,
    [BA_ADS]      = BIND_TRIG_L,
    [BA_RELOAD]   = PAD_Y,
    [BA_INTERACT] = PAD_X,
    [BA_MELEE]    = PAD_RB,
    [BA_SWAP]     = PAD_LB,
    [BA_SLOT1]    = PAD_DP_LEFT,
    [BA_SLOT2]    = PAD_DP_RIGHT,
    [BA_JUMP]     = PAD_A,
    [BA_CROUCH]   = PAD_B,
    [BA_SPRINT]   = PAD_L3,
    [BA_PAUSE]    = PAD_START,
    [BA_SCORE]    = PAD_BACK,
    [BA_NOCLIP]   = PAD_R3,
};

const char *Bind_ActionName(BindAction a) {
    switch (a) {
        case BA_FIRE:     return "Fire";
        case BA_ADS:      return "Aim Down Sights";
        case BA_RELOAD:   return "Reload";
        case BA_INTERACT: return "Interact / Revive";
        case BA_MELEE:    return "Melee";
        case BA_SWAP:     return "Swap Weapon";
        case BA_SLOT1:    return "Slot 1";
        case BA_SLOT2:    return "Slot 2";
        case BA_JUMP:     return "Jump";
        case BA_CROUCH:   return "Crouch";
        case BA_SPRINT:   return "Sprint";
        case BA_PAUSE:    return "Pause";
        case BA_SCORE:    return "Scoreboard";
        case BA_NOCLIP:   return "Noclip Toggle";
        default:          return "?";
    }
}

const char *Bind_ButtonLabel(int btn) {
    switch (btn) {
        case BIND_NONE:      return "(unbound)";
        case BIND_TRIG_L:    return "LT";
        case BIND_TRIG_R:    return "RT";
        case PAD_A:          return "A";
        case PAD_B:          return "B";
        case PAD_X:          return "X";
        case PAD_Y:          return "Y";
        case PAD_DP_UP:      return "DPad Up";
        case PAD_DP_DOWN:    return "DPad Down";
        case PAD_DP_LEFT:    return "DPad Left";
        case PAD_DP_RIGHT:   return "DPad Right";
        case PAD_LB:         return "LB";
        case PAD_RB:         return "RB";
        case PAD_LT_BTN:     return "LT (digital)";
        case PAD_RT_BTN:     return "RT (digital)";
        case PAD_BACK:       return "Back";
        case PAD_START:      return "Start";
        case PAD_L3:         return "L3 (stick click)";
        case PAD_R3:         return "R3 (stick click)";
        default:             return "?";
    }
}

// Trigger edge tracking so BIND_TRIG_L/R can be used for press-style actions
// (e.g. fire/ads-as-press). Down-style use case still works via Pad_TriggerX().
static bool lastTrigL = false;
static bool lastTrigR = false;

static bool TriggerPressed(int btn) {
    bool now = (btn == BIND_TRIG_L) ? Pad_TriggerL() : Pad_TriggerR();
    bool was = (btn == BIND_TRIG_L) ? lastTrigL : lastTrigR;
    return now && !was;
}

bool Bind_Pressed(BindAction a) {
    int btn = bindButton[a];
    if (btn == BIND_NONE) return false;
    if (btn == BIND_TRIG_L || btn == BIND_TRIG_R) return TriggerPressed(btn);
    return Pad_Pressed(btn);
}

bool Bind_Down(BindAction a) {
    int btn = bindButton[a];
    if (btn == BIND_NONE) return false;
    if (btn == BIND_TRIG_L) return Pad_TriggerL();
    if (btn == BIND_TRIG_R) return Pad_TriggerR();
    return Pad_Down(btn);
}

// Caller must advance the trigger edge state once per frame (after all input
// handlers have read it). Wired from main.c at end-of-frame.
void Settings_TickTriggerEdges(void) {
    lastTrigL = Pad_TriggerL();
    lastTrigR = Pad_TriggerR();
}

int Bind_PollAny(void) {
    if (!Pad_Connected()) return BIND_NONE;
    // Scan the named buttons we know about (raylib's enum has gaps).
    static const int scan[] = {
        PAD_A, PAD_B, PAD_X, PAD_Y,
        PAD_DP_UP, PAD_DP_DOWN, PAD_DP_LEFT, PAD_DP_RIGHT,
        PAD_LB, PAD_RB, PAD_LT_BTN, PAD_RT_BTN,
        PAD_BACK, PAD_START, PAD_L3, PAD_R3,
    };
    for (size_t i = 0; i < sizeof scan / sizeof scan[0]; i++) {
        if (Pad_Pressed(scan[i])) return scan[i];
    }
    if (TriggerPressed(BIND_TRIG_L)) return BIND_TRIG_L;
    if (TriggerPressed(BIND_TRIG_R)) return BIND_TRIG_R;
    return BIND_NONE;
}

// ---------------------------------------------------------------------------
//  settings.cfg load/save
// ---------------------------------------------------------------------------

static char loadedPath[256] = "";

static const char *ActionKey(BindAction a) {
    switch (a) {
        case BA_FIRE:     return "bind.fire";
        case BA_ADS:      return "bind.ads";
        case BA_RELOAD:   return "bind.reload";
        case BA_INTERACT: return "bind.interact";
        case BA_MELEE:    return "bind.melee";
        case BA_SWAP:     return "bind.swap";
        case BA_SLOT1:    return "bind.slot1";
        case BA_SLOT2:    return "bind.slot2";
        case BA_JUMP:     return "bind.jump";
        case BA_CROUCH:   return "bind.crouch";
        case BA_SPRINT:   return "bind.sprint";
        case BA_PAUSE:    return "bind.pause";
        case BA_SCORE:    return "bind.score";
        case BA_NOCLIP:   return "bind.noclip";
        default:          return "bind.unknown";
    }
}

static void TrimEol(char *s) {
    char *nl = strchr(s, '\n'); if (nl) *nl = 0;
    char *cr = strchr(s, '\r'); if (cr) *cr = 0;
}

void Settings_Load(void) {
    const char *paths[] = { "settings.cfg", "../settings.cfg", "./settings.cfg" };
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        f = fopen(paths[i], "r");
        if (f) { strncpy(loadedPath, paths[i], sizeof loadedPath - 1); break; }
    }
    if (!f) {
        // No file yet - write defaults so the user has something to edit and
        // can immediately see where the file lives.
        Settings_Save();
        return;
    }

    char line[256];
    while (fgets(line, sizeof line, f)) {
        TrimEol(line);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line; char *val = eq + 1;
        while (*key && isspace((unsigned char)*key)) key++;
        while (*val && isspace((unsigned char)*val)) val++;
        char *kend = key + strlen(key);
        while (kend > key && isspace((unsigned char)kend[-1])) *--kend = 0;

        if      (strcmp(key, "mouseSens")    == 0) mouseSens = (float)atof(val);
        else if (strcmp(key, "fov")          == 0) fovSetting = (float)atof(val);
        else if (strcmp(key, "fullscreen")   == 0) fullscreen = (atoi(val) != 0);
        else if (strcmp(key, "playerName")   == 0) {
            strncpy(playerName, val, sizeof playerName - 1);
            playerName[sizeof playerName - 1] = 0;
        }
        else if (strcmp(key, "padLookYaw")   == 0) padLookYawRate   = (float)atof(val);
        else if (strcmp(key, "padLookPitch") == 0) padLookPitchRate = (float)atof(val);
        else if (strcmp(key, "padDeadzone")  == 0) padStickDeadzone = (float)atof(val);
        else {
            for (int a = 0; a < BA_COUNT; a++) {
                if (strcmp(key, ActionKey((BindAction)a)) == 0) {
                    bindButton[a] = atoi(val);
                    break;
                }
            }
        }
    }
    fclose(f);
}

void Settings_Save(void) {
    const char *path = loadedPath[0] ? loadedPath : "settings.cfg";
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Claude Zombies persistent settings\n");
    fprintf(f, "playerName=%s\n", playerName);
    fprintf(f, "mouseSens=%.6f\n", mouseSens);
    fprintf(f, "fov=%.1f\n", fovSetting);
    fprintf(f, "fullscreen=%d\n", fullscreen ? 1 : 0);
    fprintf(f, "padLookYaw=%.3f\n",   padLookYawRate);
    fprintf(f, "padLookPitch=%.3f\n", padLookPitchRate);
    fprintf(f, "padDeadzone=%.3f\n",  padStickDeadzone);
    for (int a = 0; a < BA_COUNT; a++) {
        fprintf(f, "%s=%d\n", ActionKey((BindAction)a), bindButton[a]);
    }
    fclose(f);
    if (!loadedPath[0]) strncpy(loadedPath, path, sizeof loadedPath - 1);
}
