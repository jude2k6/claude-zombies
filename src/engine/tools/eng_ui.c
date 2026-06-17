// eng_ui.c — implementation of the house UI toolkit. raygui only; never a game
// header (this lives under src/engine/ and is checked by scripts/check-seam.sh).
#include "eng_ui.h"
#include "raygui.h"   // RAYGUI_IMPLEMENTATION lives in engine/app.c

static float g_scale = 1.0f;

void EngUi_ApplyTheme(void) {
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR,     ColorToInt((Color){ 12, 14, 20, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,    ColorToInt((Color){ 24, 28, 38, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,   ColorToInt((Color){ 38, 44, 58, 255 }));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,   ColorToInt((Color){ 58, 48, 26, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL,  ColorToInt((Color){ 66, 72, 86, 255 }));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(ENGUI_GOLD));
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(ENGUI_GOLD));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,    ColorToInt((Color){ 210, 214, 224, 255 }));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,   ColorToInt(ENGUI_GOLD));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,   ColorToInt(WHITE));
    GuiSetStyle(BUTTON, BORDER_WIDTH, 2);
}

void  EngUi_SetScale(float s) { g_scale = (s < 0.1f) ? 0.1f : s; }
float EngUi_Scale(void)       { return g_scale; }

float EngUi_RecommendedScale(void) {
    int mh = GetMonitorHeight(GetCurrentMonitor());
    if (mh <= 0) mh = GetScreenHeight();
    float s = (float)mh / 720.0f;
    if (s < 1.0f) s = 1.0f;
    if (s > 3.0f) s = 3.0f;
    return s;
}

void EngUi_ApplyFont(int basePx) {
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(basePx * g_scale));
}

// ---- text ------------------------------------------------------------------

void EngUi_Text(const char *t, float x, float y, int basePx, Color c) {
    DrawText(t, (int)x, (int)y, (int)(basePx * g_scale), c);
}

void EngUi_TextShadow(const char *t, float x, float y, int basePx, Color c) {
    int fs = (int)(basePx * g_scale);
    DrawText(t, (int)x + 1, (int)y + 2, fs, (Color){ 0, 0, 0, 170 });
    DrawText(t, (int)x, (int)y, fs, c);
}

void EngUi_TextCentered(const char *t, float cx, float y, int basePx, Color c) {
    int fs = (int)(basePx * g_scale);
    EngUi_TextShadow(t, cx - MeasureText(t, fs) / 2.0f, y, basePx, c);
}

// ---- house widgets ---------------------------------------------------------

bool EngUi_ToolButton(Rectangle r, const char *label, bool active) {
    if (active)
        DrawRectangleRec((Rectangle){ r.x - 5 * g_scale, r.y, 3 * g_scale, r.height }, ENGUI_GOLD);
    return GuiButton(r, label);
}

void EngUi_PanelBg(Rectangle r, Color c) {
    DrawRectangleRec(r, c);
}
