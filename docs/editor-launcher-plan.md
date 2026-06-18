# Editor Start Screen (Launcher) — design + WIP state

An IDE-style **welcome screen** as the editor's front door: a list of **recent
games** to reopen, plus **New Game** (pick a preset → scaffold → drop into the
editor) and **Open Game / Open Map**. This is the JetBrains/VS "recent projects"
window, and the entry point for the games-as-projects model
([game-projects.md](game-projects.md) §8). It supersedes nothing — the in-editor
File ▸ New/Open Game items stay; this is just the launch-time front door.

> **Status: WIP — launcher screen built, NOT yet wired into the editor loop.**
> The repo builds (the new `edlauncher.c` is not in CMake yet, so it's an inert
> orphan). Finish the four wiring steps below to make it live.

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

## Remaining wiring (to make it live)

1. **`editor_main.c` app state machine.** Add `static EdLauncher s_launcher;` and
   a `bool s_editing`. In `main()` decide the start mode: **launcher** when no
   explicit map arg was passed; **straight to editor** for `editor <map>`,
   `--check`, `--shot` (preserve CLI + CI). Factor the current `EdInit` body
   (`EdScene_Init` + `EdHost_Create` + register builtins + load plugins) into an
   `EnterEditor()` called either at init (explicit map) or on a launcher action.
   `EdDraw`: if `!s_editing`, `EdLauncher_Draw` and dispatch its action; else
   `EdHost_Frame`. `EdShutdown`: guard `s_host` (may be NULL if quit from the
   launcher) and only `EdScene_SaveSettings`/`EdScene_Shutdown` once the editor
   was entered.
2. **CMake.** Add `src/editor/edlauncher.c` to the `editor` target's sources.
3. **`builtins` recents wrapper.** `GameRecents_Push(EdScene*, dir)` is static in
   `builtins.c`. Expose `void EdBuiltins_RememberGame(EdHost *h, const char *dir);`
   (wrapping it via `EdHost_Scene`) in `builtins.h`, and call it after
   `EnterEditor()` in the launcher's OPEN_GAME / NEW_GAME paths so a
   launcher-opened game lands in recents exactly like File ▸ Open Game.
4. **Action handlers** in `editor_main.c`:
   - **OPEN_GAME**: `EdProject_Open(dir)`; resolve the game's `default_map`
     (`EdProject_Read` → `<dir>/<default_map>`, fallback `maps/default.map`) into
     `s_scene.path`; `EnterEditor()`; `EdBuiltins_RememberGame`.
   - **NEW_GAME**: `EdProject_New(dir, "empty", name)` → `EdProject_Open(dir)` →
     same as OPEN_GAME.
   - **OPEN_MAP**: `s_scene.path = path` → `EnterEditor()`.
   - **QUIT**: `Eng_RequestClose()`.

## Verify when wired

`cmake --build build -j --target editor`, then `./build/editor` (no args) should
show the launcher; opening/creating a game drops into the IDE on its default map;
`./build/editor games/shooter/maps/nacht.map` and `--check` must still bypass the
launcher (CI depends on `--check`). Confirm a created game persists in Recent
Games across an editor restart (the `EdScene_SaveSettings` fix).

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
