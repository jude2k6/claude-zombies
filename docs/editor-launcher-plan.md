# Editor Start Screen (Launcher) — design + WIP state

An IDE-style **welcome screen** as the editor's front door: a list of **recent
games** to reopen, plus **New Game** (pick a preset → scaffold → drop into the
editor) and **Open Game / Open Map**. This is the JetBrains/VS "recent projects"
window, and the entry point for the games-as-projects model
([game-projects.md](game-projects.md) §8). It supersedes nothing — the in-editor
File ▸ New/Open Game items stay; this is just the launch-time front door.

> **Status: LIVE (2026-06-18).** The launcher is wired into the editor loop and
> in CMake. `./build/editor` with no args opens the start screen; an explicit map
> arg, `--shot`, and `--check` bypass it straight into the editor (CLI + CI
> preserved). The four wiring steps below are done — kept as a record.

## Built so far

- **`src/editor/edlauncher.{c,h}`** — the screen itself. `EdLauncher_Init` loads
  the recent games from `editor.cfg` (`game.0..7`, resolving each display name via
  `EdProject_Read`); `EdLauncher_Draw` renders the welcome page (actions left,
  Recent Games right) + a New Game template page, and returns an
  `EdLauncherResult { action, gameDir, templateName, gameName, path }` where
  `action ∈ {NONE, OPEN_GAME, NEW_GAME, OPEN_MAP, QUIT}`. Templates: **Empty**
  works; **FPS** is shown disabled (`library/templates/fps` is still TODO). Uses
  `Eng_Ui*` chrome + raygui buttons + `EdFileDialog_Select Folder/Open`.
- **`EdScene_SaveSettings` recents fix** (`src/editor/edscene.c`) — it did a
  truncating full-rewrite of `editor.cfg` with scene keys only, so a clean exit
  **wiped the `recent.*` and `game.*` keys** the panels plugin owns (recent games
  would never persist across sessions — load-bearing for the launcher). It now
  snapshots those keys from disk before the truncate and re-emits them. (Root
  cause: two modules write the same cfg via truncating `BeginSave` with different
  key sets — the launcher work just made the bug matter.)

## Wiring (done — how it landed)

1. **`editor_main.c` app state machine.** `static EdLauncher s_launcher;` +
   `static bool s_editing`. `main()` sets `s_editing = hasMapArg || (s_shotAt >= 0)`
   so an explicit map arg and `--shot` go straight to the editor while a bare
   launch opens the start screen (`--check` still returns before the window). The
   old `EdInit` body (`EdScene_Init` + `EdHost_Create` + register builtins + load
   plugins) is factored into `EnterEditor()`, which also flips `s_editing` true.
   `EdInit` applies the theme/UI scale once, then either `EnterEditor()` or
   `EdLauncher_Init`. `EdDraw`: when `!s_editing`, `EdLauncher_Draw` + dispatch the
   action; else `EdHost_Frame` (+ the `--shot` capture). `EdShutdown` early-returns
   unless the editor was actually entered (`s_host` may be NULL on launcher quit).
2. **CMake.** `src/editor/edlauncher.c` added to the `editor` target.
3. **`builtins` recents wrapper.** `void EdBuiltins_RememberGame(EdHost *h, const
   char *dir)` (in `builtins.h`/`builtins.c`) wraps the static `GameRecents_Push`
   via `EdHost_Scene`. Called after `EnterEditor()` in the OPEN_GAME / NEW_GAME
   paths, so a launcher-opened game lands in recents like File ▸ Open Game. (Order
   matters: the panels plugin's `Recents_Load` runs inside `EnterEditor`, before
   this push.)
4. **Action handlers** in `editor_main.c` (`EnterEditorForGame()` shared by the
   first two):
   - **OPEN_GAME**: `EdProject_Open(dir)` → resolve `default_map`
     (`EdProject_Read` → `<dir>/<default_map>`, fallback `maps/default.map`) into
     `s_scene.path` → `EnterEditor()` → `EdBuiltins_RememberGame`.
   - **NEW_GAME**: `EdProject_New(dir, "empty", name)` → `EdProject_Open(dir)` →
     same as OPEN_GAME.
   - **OPEN_MAP**: `s_scene.path = path` → `EnterEditor()`.
   - **QUIT**: `Eng_RequestClose()`.

## Verify

Done (2026-06-18): `cmake --build build -j --target editor` is warning-clean;
`./build/editor` (no args) starts in the launcher (window stays up, no crash);
`./build/editor games/shooter/maps/nacht.map` and `--shot` start in the editor;
`--check games/shooter/maps/nacht.map` validates and exits without a window. Start
mode was confirmed per-invocation via a temporary `EDITOR_START_MODE=` stderr
breadcrumb (since removed). **Still hands-on to confirm:** opening/creating a game
from the launcher drops into the IDE on the game's default map, and a created game
persists in Recent Games across a restart (the `EdScene_SaveSettings` fix) — needs
a real interactive session with the file pickers.

## Related: editor layout audit

A critical layout audit ran alongside this — see
[editor-layout-audit.md](editor-layout-audit.md). Headline: the **Tools panel is
a junk drawer**. P0s: the **UI-scale slider** doesn't belong in Tools (kill it;
`Ctrl+=/−` already exists); VIEW + GIZMO toggle groups **duplicate** the
menu/keyboard and eat placement-palette space; the **auto-spawn checkbox** is
duplicated and silently doesn't persist from the panel; the panel's fixed
`prefH=256` will **clip** the place buttons as the mob catalog grows. P1: the
Inspector's "no selection ⇒ map-metadata" mode has **zero affordance**. Acting on
these is the next editor-layout pass (separate from this launcher work).
