// ============================================================================
//  eng_ui.h — the house UI toolkit: dark/gold style + scale-aware helpers on
//  top of raygui. Engine-owned, game-clean (raygui only, no game headers), so
//  BOTH the game menus (src/game/menu.c) and the editor (src/editor/) share one
//  look and one set of layout primitives instead of each re-rolling them.
//
//  This is NOT a widget framework — raygui already is the widget layer. eng_ui
//  is a thin layer over it: apply our theme once, draw scaled text, and a couple
//  of house widgets (accent-bar tool button, panel background). Callers still
//  call GuiButton / GuiSlider / … directly for everything else.
//
//  Scale: every helper multiplies a base pixel size by a module-global UI scale
//  (default 1.0). The editor drives this from a slider/recommendation so the
//  whole toolbar grows on hi-dpi displays; the game can leave it at 1.0.
// ============================================================================
#ifndef ENG_UI_H
#define ENG_UI_H

#include "raylib.h"

// ---- house palette ---------------------------------------------------------
#define ENGUI_GOLD  (Color){ 255, 206, 84,  255 }
#define ENGUI_RED   (Color){ 222, 52,  52,  255 }
#define ENGUI_TEXT  (Color){ 223, 227, 236, 255 }
#define ENGUI_DIM   (Color){ 150, 156, 172, 255 }
#define ENGUI_PANEL (Color){ 22,  26,  34,  235 }

// ---- theme -----------------------------------------------------------------

// Apply the dark/gold raygui style to every control. Idempotent — safe to call
// each frame or once at init.
void EngUi_ApplyTheme(void);

// ---- scale -----------------------------------------------------------------

void  EngUi_SetScale(float s);      // set the global UI scale (clamped >= 0.1)
float EngUi_Scale(void);            // current global UI scale
float EngUi_RecommendedScale(void); // ~1.0 at 720p, ~1.5 at 1080p, up to 3.0 at 4K

// Set raygui's control font to `basePx * scale`. Call before a group of raygui
// controls so their text matches the scaled layout.
void EngUi_ApplyFont(int basePx);

// ---- scale-aware text ------------------------------------------------------
// `basePx` is multiplied by the current scale; coordinates are real pixels.
// Argument order mirrors raylib's DrawText (text, x, y, size, color).

void EngUi_Text(const char *t, float x, float y, int basePx, Color c);          // plain
void EngUi_TextShadow(const char *t, float x, float y, int basePx, Color c);    // soft drop shadow
void EngUi_TextCentered(const char *t, float cx, float y, int basePx, Color c); // centered + shadow

// ---- house widgets ---------------------------------------------------------

// Full-width stacked tool button with a gold accent bar on its left when
// `active`. `r` is in real pixels; the accent/font follow the global scale.
// Returns true the frame it is clicked.
bool EngUi_ToolButton(Rectangle r, const char *label, bool active);

// Filled panel/background rectangle in the house panel color (or a custom one).
void EngUi_PanelBg(Rectangle r, Color c);

#endif // ENG_UI_H
