# Games as Projects — self-contained game folders + a read-only engine library

> **Status: MOSTLY IMPLEMENTED (2026-06-18).** The overlay resolver (Steps 1–2),
> the `data/` → `library/` + `games/shooter/` filesystem split (Step 3), and the
> runtime root wiring (Step 5) are built and committed. Remaining: the editor's
> New/Open-Game management UI (Step 4 remainder) and import/origin polish
> (Step 6). See the [Implementation progress](#implementation-progress-2026-06-18)
> section appended to §10 for the exact done/remaining breakdown. Original design
> intent:
> Captures the architecture Jude wants:
> the editor (and the runtime) operate on a **game** — a self-contained folder
> holding that game's *skeleton code*, content, and a manifest — that links the
> engine. "shooter" (the current zombies game) is one such folder; a "csgo-style"
> game would be another, side by side. The engine ships a **read-only library**
> of stock content; using a stock asset in a game **copies it into the game
> folder**, where it becomes editable for that game only. Builds on
> [engine-game-separation.md](engine-game-separation.md),
> [engine-layers.md](engine-layers.md), [scene-builder.md](scene-builder.md), and
> [editor-content-extensibility.md](editor-content-extensibility.md).

---

## 1. The idea in one screen

```
<install>/                         the engine + its tools + the stock library
  bin/  editor, engine runtime
  library/        ← READ-ONLY. What the engine "ships with".
    mobs/zombie/zombie.mob + zombie.glb
    weapons/pistol/...   models/  textures/  shaders/  prefabs/  templates/

~/games/                           the user's games (anywhere on disk)
  shooter/                         ← a GAME = a self-contained folder
    game.project                   manifest (deffile format)
    src/        main.c + GameModule skeleton (links libengine.a)
    maps/       arena.map ...
    mobs/zombie/zombie.mob         ← imported (copied) from library, now EDITABLE here
    weapons/  models/  textures/   ← only the assets THIS game imported/authored
    build/
  csgo/                            ← another game, totally independent
    game.project  src/  maps/  mobs/  ...
```

Three rules make the whole thing work:

1. **A game is a folder.** Code + content + manifest, self-contained, links the
   engine. Nothing about "shooter" leaks into "csgo".
2. **The library is read-only and lives with the engine, not the game.** It is
   the stock catalog the engine ships (mobs, weapons, models, textures, shaders,
   prefabs, and *code templates*). Games never write to it.
3. **Use = import = copy-into-the-game.** Pulling a stock mob into your game
   copies `library/mobs/zombie/` → `<game>/mobs/zombie/`. From then on it is the
   game's own file: edit it freely, and the change affects *only this game*.
   Another game that imports the same zombie gets its own pristine copy.

This is the Unity/Unreal/Godot split (engine/package content vs project content),
specialised to our file-based catalogs.

## 2. Terminology

| Term | Meaning |
|---|---|
| **Game** (a.k.a. project) | A self-contained folder: skeleton code + content + `game.project`. Links the engine, runs standalone. |
| **Library** | The engine's read-only stock store (`<install>/library/`). What "comes included". |
| **Import** | Copy a library asset's folder into a game so it becomes game-local and editable (copy-on-write). |
| **Override** | A game file at the same relative path as a library file shadows it during resolution. Import *creates* an override. |
| **Resolve** | Turn a relative content path into a real file by searching **game-first, then library**. |

## 3. Content resolution — the overlay (the heart of it)

Every content lookup resolves against an ordered **root stack**:

```
1. <game>/        the active game folder        (writable, wins)
2. <library>/     the engine stock library      (read-only, fallback)
```

`resolve("mobs/zombie/zombie.mob")` → return `<game>/mobs/zombie/zombie.mob` if
it exists, else `<library>/mobs/zombie/zombie.mob`, else not-found. The same
relative path means "the zombie", and whether you've imported it just decides
which copy answers. Unimported stock still resolves (from the library), so a game
can *reference* stock it hasn't customised; importing is only required to *edit*.

This already has a home: `src/engine/content.c` centralises path probing
(`Eng_ResolveAssetPath` + the `MODEL_PREFIXES` / `TEXTURE_PREFIXES` /
`CONTENT_PREFIXES` tables). Today those tables hardcode `data/`, `../data/`,
`./data/`. The change is to make the roots **configurable** and **two-layered**:

```c
// New engine API (content.h). The host sets these once at startup.
void Eng_SetGameRoot(const char *dir);     // <game>/      (NULL = none)
void Eng_SetLibraryRoot(const char *dir);  // <library>/   (discovered near the exe)
// Eng_ResolveAssetPath then tries: <game>/<rel>, then <library>/<rel>.
```

Loaders that currently roll their own `data/<x>` scans —
`weapons.c`, `mobs.c`, `level.c`, `audio.c`/`audio_director.c`, `menu.c` (map
list), and the editor's `edscene.c` mob scan — all route through the resolver
instead of duplicating prefix lists. **One resolver, two roots, every loader.**
(Directory *scans* — "list every .mob" — also become two-pass: union the game's
dir over the library's, game entries shadowing same-named library entries.)

> Seam: the resolver stays in the engine and is game-clean (it knows roots and
> relative paths, never `TEX_FLOOR` or `ZOMBIE`). The editor reuses it unchanged
> — which is *why* `deffile` and the resolver live engine-side.

## 4. The read-only library

- **Location:** beside the installed engine/editor, discovered relative to the
  executable (`GetApplicationDirectory()`), not the CWD — with dev fallbacks for
  running from the build/source tree. Never assumed writable.
- **Layout:** mirrors a game's content layout so resolution is a straight
  relative-path overlay: `library/mobs/ weapons/ models/ textures/ shaders/
  prefabs/`, plus `library/templates/` (§7, the skeleton-code templates).
- **Read-only by contract:** the editor offers *Import* (copy out), never *Save*
  into the library. A future "asset pack" mechanism can add *more* read-only
  layers below the library (engine builtins < installed packs < game) without
  changing the resolution model.

## 5. A game folder

Scaffolded by **New Game** (§8). Minimum runnable game:

```
<game>/
  game.project          # manifest (deffile: key value, like .mob/.weapon)
  src/
    main.c              # GameModule skeleton: init/frame/fixed/draw + Eng_Run
    CMakeLists.txt      # finds the engine, builds this game (+ its behaviours)
  maps/                 # the game's own .map files (starter map seeded)
  mobs/  weapons/  models/  textures/   # empty until you import/author
  behaviours/           # optional Tier-2 .so AI archetypes (see mob_ai.h)
  build/                # out-of-source build output (gitignore'd)
```

### `game.project` (deffile format)

```ini
# csgo — a defusal shooter built on the engine.
id              csgo
name            Counter-Offensive
engine_version  1
default_map     maps/dust.map
# Imports are IMPLICIT: anything present under <game>/ that shadows a library
# path is an override. The manifest stays metadata, not a dependency lockfile —
# the filesystem is the source of truth (no list to drift out of sync).
```

Decision: **imports are implicit by presence**, not a manifest list. The
filesystem *is* the import set, so it can never disagree with what's on disk.
(Optional later: record each import's source + library version in a sidecar so
the editor can offer "reset to library" / "show my changes" — §6.)

## 6. Import semantics

- **Import (copy-on-write):** editor action "Add to game / Edit copy" copies the
  asset's whole folder `library/mobs/zombie/` → `<game>/mobs/zombie/`. Now it
  resolves game-first and is editable. Per-game by construction — two games hold
  two independent copies.
- **Use without import:** referencing a stock path you haven't imported just
  resolves from the library at load time (read-only). Importing is the act of
  saying "I want to change this for my game."
- **Un-import:** delete the game copy → resolution falls back to the library
  (revert to stock).
- **Origin tracking (decided — written from day one):** every import stamps the
  copied folder with a tiny `.import` sidecar (`source library/mobs/zombie`,
  `library_version 1`) so the editor can later show "edited vs stock", offer
  **Reset to library**, and warn when a library update diverges from your edited
  copy. The copy is self-sufficient without it, but recording the metadata at
  import time is nearly free and avoids a backfill migration when those features
  land — so we write it now even though the UI for them comes later.

## 7. The skeleton code (what "New Game" generates)

A game is *code* too, not only content — it implements the engine's `GameModule`
(`init/frame/fixed/draw/shutdown` + an `EngConfig`) and calls `Eng_Run`
(`app.h`). New Game stamps a working minimal module from `library/templates/`:

```c
// <game>/src/main.c  (generated)
#include "app.h"
static void Init(void)              { /* Mobs_Load(); load your starter map */ }
static void Frame(float dt,int w,int h) { /* input, camera */ }
static void Fixed(float dt)         { /* authoritative sim */ }
static void Draw(int w,int h)       { /* issue draws */ }
int main(void) {
    Eng_SetLibraryRoot(/* discovered */);
    Eng_SetGameRoot(".");           // run from the game folder
    EngConfig cfg = { .w=1280,.h=720,.title="Counter-Offensive",.vsync=true };
    Eng_Run(&cfg, (GameModule){ Init, Frame, Fixed, Draw, NULL });
    return 0;
}
```

### How a game's code is built and run

**A game is an executable that statically links the engine** — the standard,
simplest model, and exactly what `src/game` is today. No plugin indirection, no
engine↔game ABI to freeze, normal build/debug/ship. We split only by *whether a
game has custom code*:

| Case | New Game produces | Run | Notes |
|---|---|---|---|
| **Content-only game** | content + `game.project` (no `src/`) | `engine <gamedir>` (the prebuilt **default runtime** that ships with the engine) | No compile at all — the "mostly content, csgo-style" case. The default runtime is one executable that loads any game folder. |
| **Game with custom code** | `src/main.c` (GameModule) + CMake emitting **two targets**: a static standalone exe (ship) **and** a `.so` (editor Play, §7 "Play from the editor" Tier 2) | build → `<game>/build/<id>` exe + `<id>.so` | Ships as its own standalone executable; the editor loads the `.so` for in-process Play. One source, two outputs (the Unity exe-vs-assemblies split). |

The editor's **Play** just `posix_spawn`s the right binary pointed at the game
folder — the default runtime for content-only games, or the game's built
executable — as a child process. No `.so` for the game itself.

> The **shipping form** of a game is always the static executable — you don't
> *distribute* a game as an `.so`. But a game `.so` reappears legitimately for one
> thing: **in-editor Play mode** (next).

### Play from the editor (the Unity model)

Unity can hit Play without building because (a) the **editor IS the runtime** and
(b) the game's code is a **loadable module** (a managed DLL), not statically
welded in; Build later bakes the *same* code into a standalone player. Our editor
**already links `libengine.a`** — it is an engine host too — so the same approach
applies, with a `.so` as the native equivalent of Unity's DLL. Three tiers, in
the order we'd build them:

| Tier | Mechanism | Unity analogue |
|---|---|---|
| **1. Spawn a child process** *(first)* | editor builds + `posix_spawn`s the game exe (or the default runtime) pointed at the game folder | "Build And Run" |
| **2. Host the game `.so` in a Play viewport** *(Unity-grade target)* | editor dlopens the game's module and runs its `GameModule` loop in-editor; rebuild → reload the `.so` | **Play mode** |
| **3. Scripting** *(deferred Tier-3)* | game logic in embedded Lua/Wren the editor interprets in-process; live reload is free | GDScript-style |

So the game `.so` is **not the game's identity or shipping form** — the same game
source compiles to *both* a static standalone executable (ship) *and* a `.so`
(the editor's in-process Play module), exactly as Unity emits a baked Player and
editor assemblies from one codebase. The ABI worry is small: the editor and the
`.so` are built from the **same engine version in lockstep** (same install), an
internal build artifact, not a frozen third-party boundary. Caveat vs Unity:
C# domain-reload preserves state across a reload; native C does not, so our first
hot-reload is "rebuild → reload → re-init" (a Play restart), not live code swap.

> Orthogonal to the Tier-2 mob-behaviour `.so`s (`mob_ai.c`): those stay `.so`
> extension points a game loads from its own `behaviours/`. The *game* ships as an
> executable; its *behaviours* are plugins; and for *editor Play* the game is also
> built as a loadable module.

## 8. Editor integration

- **File ▸ New Game…** → choose a **template** (`empty` or `fps`, from
  `library/templates/`) → native folder picker (defaults to `~/games/`, any
  location allowed) → scaffold §5/§7 from that template → set it as the active
  game → open its starter map. (Replaces today's single-map "New".) The `fps`
  template is a working first-person game (controller + HUD) you reshape; `empty`
  is a bare GameModule. The current zombies game is the basis of the `fps`
  template.
- **File ▸ Open Game…** → pick a folder containing `game.project` → set roots →
  populate maps/mobs/etc. **Recents become recent *games***, not maps (the
  `editor.cfg` recents list, [editor-settings-and-validation]).
- **Maps belong to the game:** the map list (`menu.c` scan, editor Open) reads
  `<game>/maps/` over `library/prefabs/`. Save targets the game folder.
- **Asset browser** ([editor-feature-ideas] 5.3) becomes the import surface: it
  shows the library (importable, marked read-only) and the game's own assets
  (editable); the action on a library asset is **Import to game**, after which it
  appears under the game and is editable in the Inspector. The mob palette
  (`EdScene_ScanMobs`) already scans `<roots>/mobs/`; with two roots it lists
  library + game mobs, and placing/editing one imports it on first edit.
- The editor is still engine-only: it drives all of this through the engine
  resolver + `deffile` + filesystem copies; no game headers.

## 9. Game runtime integration

- `shooter` becomes `engine <gamedir>` (or a per-game binary, per §7): set
  `Eng_SetLibraryRoot` (near the exe) + `Eng_SetGameRoot(<gamedir>)`, then every
  existing loader (`Mobs_Load`, `Weapons_Load`, `Level_*`, audio) resolves
  through the overlay with zero per-loader changes beyond dropping their private
  prefix lists.
- The **mob behaviour registry** already scans `behaviours/` + `data/mobs/*/`
  (`mob_ai.c`); it simply scans the game's roots instead — so a game ships its
  own AI `.so`s in its own folder, exactly the "locality follows ownership"
  rule from [editor-content-extensibility] §4.
- A **default game** ships in the repo so `engine` with no arg still runs
  something (the migrated "shooter", §10).

## 10. Migration plan (phased; each step builds + runs)

Per [[no-backwards-compat]] we can restructure freely.

1. **Resolver becomes root-driven.** Add `Eng_SetGameRoot` / `Eng_SetLibraryRoot`;
   make `Eng_ResolveAssetPath` + the category prefixes consult them. Keep the
   current `data/` working by defaulting both roots to `data/` — no behaviour
   change, pure plumbing.
2. **Unify loaders onto the resolver.** Delete the duplicated `data/<x>` scans in
   `weapons.c`/`mobs.c`/`level.c`/`audio*`/`menu.c`/`edscene.c`; route through the
   resolver + a two-root directory-scan helper.
3. **Split `data/` into `library/` + a sample game.** Move stock (models,
   textures, shaders, base mobs/weapons, prefab maps) to `library/`; move the
   zombies game's own maps + any game-specific tuning into `games/shooter/`
   with a `game.project` + scaffolded `src/`. Point the default run at it.
4. **Editor: New/Open Game + scaffolding + import.** Folder picker, template
   stamping, recents-as-games, asset-browser import action.
5. **Runtime: `engine <gamedir>`** (and/or per-game binary) per the §7 decision;
   wire behaviour/`.so` scans to the game roots.
6. **Polish:** origin sidecars + "reset to library" / "show changes"; a second
   sample game (the csgo-style template) to prove independence.

### Implementation progress (2026-06-18)

Built and committed in this order; each commit builds wall-clean and the
headless smoke tests (`--list-mobs`, `--validate`, `--map-roundtrip`) pass.

**✅ Step 1 — Resolver is root-driven** (commit `eeeb43a`).
`src/engine/content.{c,h}` now owns the overlay:
- `Eng_SetGameRoot(dir)` / `Eng_SetLibraryRoot(dir)` / `Eng_GetGameRoot()` /
  `Eng_GetLibraryRoot()` — set/clear the two roots (NULL/"" clears).
- The internal root stack is **game → library → legacy `data/` dev fallbacks**
  (`data`, `../data`, `./data`). Both roots default to unset, so with nothing
  configured only the `data/` fallbacks apply — behaviour is byte-identical to
  before (pure plumbing).
- All asset loaders (`Eng_LoadModel`/`Texture`/`Shader`/`AnimModel`) and
  `Eng_ResolveAssetPath` probe the stack; a full `data/...`/absolute path is
  still honoured as-is first, so legacy callers keep working.
- New `int Eng_ContentDirs(relSubdir, dirs[][512], maxDirs)` — the two-pass
  directory-scan primitive (§3): returns the existing dirs for a content subdir
  (`"maps"`, `"mobs"`, …), game-root first, for callers to union + de-dup.

**✅ Step 2 — Loaders unified onto the resolver** (commit `a4288be`).
Deleted every private `data/<x>` prefix list; each loader/scan now routes
through the resolver:
- `mobs.c`, `menu.c`, `audio_director.c`: `Eng_ContentDirs` union scans with
  de-dup (by mob id / map basename; the first = game root shadows library).
- `mobs.c` ModelResolves, `level.c` default-map, `weapons.c` weapon-dir scan,
  `mob_ai.c` behaviour-`.so` scan, engine `anim.c` + `audio.c`: route through
  `Eng_ResolveAssetPath` / `Eng_ContentDirs`.

**🟡 Step 4 — Editor: PARTIAL** (the scan half, committed in `a4288be`).
- Editor content scans are root-aware: `edscene.c` `EdScene_ScanMobs` union +
  de-dup, `builtins.c` map-dialog start dirs (new `MapsStartDir` helper),
  `editor_main.c` default map via the resolver.
- `edfiledialog`: added `EdFileDialog_SelectFolder` (native folder picker) ready
  for Open/New Game.
- **NOT yet done:** File ▸ New Game… / Open Game… actions, `game.project`
  read/write + scaffolding, recents-as-games, asset-browser import action.

**✅ Step 3 — Split `data/` → `library/` + `games/shooter/`** (this wave). The
filesystem reorg landed as a single coherent edit:
- `git mv` of all stock content into **`library/`** (`models/`, `textures/`,
  `shaders/`, `mobs/`, `weapons/`, `ANIMATIONS.md`) + a `library/templates/`
  placeholder for the New-Game skeletons.
- The zombies game's maps moved to **`games/shooter/maps/`** under a
  `games/shooter/game.project` deffile manifest (`id`/`name`/`default_map`).
- CMake's post-build copy now mirrors **`library/` + `games/`** next to the
  binary (was `data/`); `.gitignore`'s `*.obj` keep-exception follows to
  `library/`/`games/`. `data/` no longer exists.

**✅ Step 5 — Runtime root wiring** (this wave). Both `src/game/main.c` (before
`Devtools_HandleCLI`) and `src/editor/editor_main.c` (before the default-map
resolve) call the new engine helper **`Eng_LocateRoot(relName, buf, n)`** — it
probes `<exe-dir>/relName` (the install layout) then the CWD and its parent (dev
tree) — to set the library + `games/shooter` roots. A `<gamedir>` positional
override is still future (arrives with Open Game). CI + README + HANDOFF map
paths updated to `games/shooter/maps/`.
- *Bonus fix found by the post-move smoke test:* engine `Anim_Load` only probed
  `models/<file>`, so combined viewmodel rigs at `weapons/<id>/<id>_vm.glb`
  (incl. the MP5) silently failed since `a4288be`. It now falls back to the raw
  root-relative path like `Eng_LoadAnimModel`, restoring combined-rig loading.

#### Remaining work

- **Step 4 remainder** — the New/Open Game UI: File ▸ New Game… / Open Game…,
  `game.project` read/write + scaffolding from `library/templates/`,
  recents-as-games, asset-browser import action (the `EdFileDialog_SelectFolder`
  primitive is already in place to build on).
- **Step 6 — Polish** — `.import` origin sidecars + reset-to-library/show-changes
  UI; a second sample game to prove independence.

> Integration note: Steps 1, 2, and the Step-4 scans were built as one wave (two
> parallel sub-agents on disjoint file sets, integrated from the main session).
> Steps 3 + 5 were then done together by the main session as one coherent edit
> (filesystem move + CMake + root wiring + doc/CI sweep), exactly as flagged —
> not parallelized, since the move touches one shared tree. The remaining Step 4
> UI + Step 6 polish are again parallel-friendly (disjoint editor surfaces).

## 11. Decisions (all settled — 2026-06-18)

- **A game is a folder** with content + a `game.project` manifest and **optional**
  code. A game with code is an **executable that statically links the engine**
  (never shipped as an `.so`); content-only games run on a prebuilt default
  runtime (§7).
- **Library** = read-only stock store named **`library/`**, discovered near the
  exe. Resolution = **game-over-library** by relative path. **Import** =
  copy-on-write of the asset folder; **imports are implicit by presence**.
  Manifest + sidecars in **`deffile`** format.
- **Play mode = Tier 2** (in-editor, in-process Play via a loadable game `.so`)
  is the target; **Tier 1** (spawn-a-process) ships first as a stepping stone.
  New Game's CMake emits **both** a standalone exe and a `.so` (§7).
- **New Game** offers **templates** (`empty`, `fps`) from `library/templates/`;
  the picker defaults to **`~/games/`** (any location allowed). The current
  zombies game becomes the `fps` template.
- **Origin tracking**: every import writes an `.import` sidecar **from day one**
  (§6); the reset/diff UI it enables comes later.

All revisable per [[no-backwards-compat]] — but these are the defaults the
migration (§10) builds toward.
```
