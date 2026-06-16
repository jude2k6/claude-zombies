#include "debugdraw.h"
#include <string.h>

// ============================================================================
//  Engine debug-draw — implementation.
//
//  Module-level state: four fixed-size pools (lines/boxes/spheres/labels)
//  plus their live counts and the global enable flag. See debugdraw.h for
//  the deferred-queue model and draw-order requirement.
// ============================================================================

#define ENG_DEBUG_MAX_LINES   1024
#define ENG_DEBUG_MAX_BOXES   256
#define ENG_DEBUG_MAX_SPHERES 256
#define ENG_DEBUG_MAX_LABELS  128
#define ENG_DEBUG_LABEL_LEN   64

typedef struct {
    Vector3 a, b;
    Color   c;
} DDLine;

typedef struct {
    Vector3 center, size; // wireframe cube — boxes are normalized to this at submit time
    Color   c;
} DDBox;

typedef struct {
    Vector3 center;
    float   radius;
    Color   c;
} DDSphere;

typedef struct {
    Vector3 pos;
    char    text[ENG_DEBUG_LABEL_LEN];
    Color   c;
} DDLabel;

static bool s_enabled = false;

static DDLine   s_lines[ENG_DEBUG_MAX_LINES];
static int      s_lineCount = 0;

static DDBox    s_boxes[ENG_DEBUG_MAX_BOXES];
static int      s_boxCount = 0;

static DDSphere s_spheres[ENG_DEBUG_MAX_SPHERES];
static int      s_sphereCount = 0;

static DDLabel  s_labels[ENG_DEBUG_MAX_LABELS];
static int      s_labelCount = 0;

// ---- Enable flag ------------------------------------------------------------

void Eng_DebugSetEnabled(bool on) { s_enabled = on; }
bool Eng_DebugEnabled(void)       { return s_enabled; }

// ---- Submission ---------------------------------------------------------

void Eng_DebugLine(Vector3 a, Vector3 b, Color c) {
    if (!s_enabled) return;
    if (s_lineCount >= ENG_DEBUG_MAX_LINES) return; // silently drop on overflow
    s_lines[s_lineCount++] = (DDLine){ a, b, c };
}

void Eng_DebugBox(BoundingBox box, Color c) {
    if (!s_enabled) return;
    if (s_boxCount >= ENG_DEBUG_MAX_BOXES) return;
    Vector3 center = {
        (box.min.x + box.max.x) * 0.5f,
        (box.min.y + box.max.y) * 0.5f,
        (box.min.z + box.max.z) * 0.5f,
    };
    Vector3 size = {
        box.max.x - box.min.x,
        box.max.y - box.min.y,
        box.max.z - box.min.z,
    };
    s_boxes[s_boxCount++] = (DDBox){ center, size, c };
}

void Eng_DebugCube(Vector3 center, Vector3 size, Color c) {
    if (!s_enabled) return;
    if (s_boxCount >= ENG_DEBUG_MAX_BOXES) return;
    s_boxes[s_boxCount++] = (DDBox){ center, size, c };
}

void Eng_DebugSphere(Vector3 center, float radius, Color c) {
    if (!s_enabled) return;
    if (s_sphereCount >= ENG_DEBUG_MAX_SPHERES) return;
    s_spheres[s_sphereCount++] = (DDSphere){ center, radius, c };
}

void Eng_DebugText3D(Vector3 worldPos, const char *text, Color c) {
    if (!s_enabled) return;
    if (!text) return;
    if (s_labelCount >= ENG_DEBUG_MAX_LABELS) return;
    DDLabel *l = &s_labels[s_labelCount++];
    l->pos = worldPos;
    l->c   = c;
    strncpy(l->text, text, ENG_DEBUG_LABEL_LEN - 1);
    l->text[ENG_DEBUG_LABEL_LEN - 1] = '\0';
}

// ---- Flush ----------------------------------------------------------------

void Eng_DebugDraw3D(Camera3D cam) {
    (void)cam; // unused directly — shapes are already in world space
    if (!s_enabled) {
        // Still clear so stale data never lingers across an enable toggle.
        s_lineCount = s_boxCount = s_sphereCount = 0;
        return;
    }

    for (int i = 0; i < s_lineCount; i++) {
        DrawLine3D(s_lines[i].a, s_lines[i].b, s_lines[i].c);
    }
    for (int i = 0; i < s_boxCount; i++) {
        DrawCubeWiresV(s_boxes[i].center, s_boxes[i].size, s_boxes[i].c);
    }
    for (int i = 0; i < s_sphereCount; i++) {
        DrawSphereWires(s_spheres[i].center, s_spheres[i].radius, 8, 8, s_spheres[i].c);
    }

    s_lineCount = 0;
    s_boxCount = 0;
    s_sphereCount = 0;
}

void Eng_DebugDrawLabels(Camera3D cam) {
    if (!s_enabled) {
        s_labelCount = 0;
        return;
    }

    for (int i = 0; i < s_labelCount; i++) {
        DDLabel *l = &s_labels[i];

        // Skip labels behind the camera (would otherwise project to a
        // mirrored/garbage screen position).
        Vector3 fwd = {
            cam.target.x - cam.position.x,
            cam.target.y - cam.position.y,
            cam.target.z - cam.position.z,
        };
        Vector3 toLabel = {
            l->pos.x - cam.position.x,
            l->pos.y - cam.position.y,
            l->pos.z - cam.position.z,
        };
        float dot = fwd.x * toLabel.x + fwd.y * toLabel.y + fwd.z * toLabel.z;
        if (dot <= 0.0f) continue;

        Vector2 screen = GetWorldToScreen(l->pos, cam);
        DrawText(l->text, (int)screen.x, (int)screen.y, 10, l->c);
    }

    s_labelCount = 0;
}
