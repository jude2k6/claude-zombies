# Content Packs — stdlib + importable packs, copied into a game

> **Status: Phases 1–5 SHIPPED** (palette badges + per-item import + provenance sidecars
> landed; only "update from pack" remains as polish). Captures the content-packs model worked
> out 2026-06-20. It extends the games-as-projects overlay
> ([game-projects.md](game-projects.md)) with a *pack* — a distributable bundle of placeables —
> and a copy-on-import flow at whole-pack or per-item granularity.
>
> **Done (phase 1):** the content tree is reorganized — `library/ → stdlib/` (commit
> 29a2747), and the themed content extracted into `packs/zombies/` and imported (copied) into
> `games/shooter/` (commit 1aef637). Runtime resolves **game → stdlib** unchanged; packs are
> import sources, not runtime-loaded.
>
> **Done (phases 2–4):** the engine reads/enumerates packs (`src/engine/pack.{c,h}`:
> `Eng_PackReadManifest`, `Eng_PackList`, `Eng_PackDirs`), a reusable copy-on-import core copies
> a def + its referenced model/`.mtl`/textures into a game (`src/editor/edimport.{c,h}`:
> `EdImport_Item`, `EdImport_Pack`), and the editor exposes **File ▸ Game project ▸ Import Pack… /
> Install Pack…** plus one-click **Import: \<pack\>** entries for the packs found at launch
> (`edmenus.c`). `EdProject_CopyTree` was made public to back Install Pack. The importer mirrors
> the source layout: weapon models copy beside their def, mob/perk/prop models copy into the
> shared `models/` tier — matching how the game resolves each.
> Verified: clean `-Wall -Wextra` build; a link-test imports the zombies pack (21 files,
> incl. the raygun viewmodel) into a fresh game folder; editor `--check` still parses maps.
>
> **Done (palette UX + sidecars):** the content browser (`src/editor/edpanels.c`) now lists
> items from installed packs the open game hasn't imported, dimmed with a **source-pack chip**;
> clicking one copy-imports it (`EdImport_Item`), re-scans, and arms it for placement
> (`EdScene_ScanMobs`/`EdScanIdNameCatalog` add a pack pass carrying provenance into
> `EdMobDef`/`EdPropDef`). The palette search also matches the pack id (browse-by-pack). Every
> import writes a `.import` **provenance sidecar** (source pack + version + path) beside the
> copied def. The launcher also has **Install Pack…** (`src/editor/edlauncher.c`).
> Verified: link-test shows an empty game surfaces 2 importable mobs + 4 importable perks tagged
> `pack='zombies'`, per-item import writes the sidecar, and an already-imported item is de-duped
> (no badge); editor `--check` still parses maps.
>
> **Not yet built:** "update from pack" / "show changes" using the sidecars (read-back side of
> §8 phase 5). The resolved-design decisions (§9) are settled.

---

## 1. The idea in one screen

Placeable content (mobs, perks, wallbuy weapons, props) is split by **theme**, not just by
type, into **packs** — so "the zombies pack" bundles the zombie + dog mobs, the four perk
machines, the ray gun, and zombie-specific props as one downloadable unit. Generic,
universal placeables (base guns, basic obstacles) are **not** a pack — they live in a
first-party **stdlib** that ships with the engine and is always available.

```
stdlib/            ← universal first-party base (ships with engine; always importable)
  weapons/{pistol,smg,shotgun,rifle}/      props/{crate,barrel,sandbag}/
  shaders/  templates/                     (geometry primitives are code, not files)

packs/             ← the editor-wide pack catalog (lives alongside stdlib)
  zombies/         ← first-party themed pack (ships with the reference game)
    pack.manifest
    mobs/{zombie,dog}/  perks/{juggernog,speed_cola,double_tap,staminup}/
    weapons/raygun/     props/...     models/  textures/   ← its OWN unique assets
  <community-pack>/      ← a pack you installed permanently (see §4)

games/<game>/      ← a specific game: copy-on-import TARGET (self-contained for packs)
  game.project
  maps/
  mobs/ perks/ weapons/ props/ models/ textures/   ← copies imported from stdlib/packs
```

Three **sources** you import *from* (stdlib, installed packs, a one-off external folder),
one **target** you import *into* (the game). Importing = **copy into the game folder**.

---

## 2. The three sources + the game

| Tier | What | Lifetime | Loaded at runtime? |
|---|---|---|---|
| **`stdlib/`** | Universal first-party base: base guns, basic obstacles/props, shaders, templates | Ships with the engine | **Yes — runtime fallback** (a game references stdlib it hasn't imported; see §5) |
| **`packs/<name>/`** | Themed bundles, first-party (`zombies`) or permanently-installed third-party | Ships and/or installed by the user | **No** — an import *source* only |
| **external folder** | A pack downloaded anywhere (e.g. `~/Downloads/coolpack/`) | Transient | No — imported per-game via a file picker |
| **`games/<game>/`** | The game's own content + overrides | Per game | **Yes — wins** (game-first overlay) |

**stdlib is the renamed, refocused `library/`.** It keeps the games-as-projects role of a
read-only base that resolves at runtime, so a game stays lean and isn't forced to duplicate
the whole base. Packs are different: they are **import-only**, so a game that uses a pack
ends up with that pack's content **copied in** — which is exactly the "self-contained game"
property and the "copied to the game folder" behaviour we want.

> **Key decision (this reconciles your copy model with the existing overlay):**
> *stdlib = runtime fallback (import only to edit); packs = always copy-on-import.* So the
> runtime resolution order is unchanged — **game → stdlib** (`Eng_ContentDirs`) — because a
> used pack's content lives *in the game* after import. We do **not** add packs as a third
> runtime tier; that keeps the overlay simple and makes game folders distributable. The one
> refinement to "stdlib is copied into the game": stdlib content is copied in **only when you
> import an item to edit it** (per game-projects §3); unedited base content resolves from
> stdlib without bloating every game. If you'd rather stdlib *also* always copy in (fully
> self-contained, no fallback), that's the alternative — noted in §8.

---

## 3. Pack structure + `pack.manifest`

A pack is a self-contained folder: the type subdirs it provides, plus its **own** unique
models/textures. It may *reference* stdlib for universal assets (stdlib always ships), so a
pack only bundles what's unique to it.

```
packs/zombies/
  pack.manifest
  mobs/zombie/zombie.mob          models/zombie.glb
  mobs/dog/dog.mob                models/dog.glb
  perks/juggernog/juggernog.perk  models/perk_jugg.glb
  weapons/raygun/raygun.weapon    models/raygun.glb
  props/...                       textures/...
```

`pack.manifest` (deffile format — `key value`, like `.mob`/`game.project`):

```
id            zombies
name          Zombies
version       1
author        first-party
description   CoD-style zombies: enemies, perk machines, the ray gun.
# Optional: declared dependency on stdlib items a pack references but doesn't bundle.
requires      stdlib
```

The manifest is **metadata, not a lockfile** — the filesystem (the type subdirs present) is
the source of truth for what the pack provides, matching the game-projects principle.

---

## 4. Installing a pack source (two scopes)

Both gestures make a pack importable; they differ in *how long* the source sticks around.

- **One-off / external** — a pack folder you downloaded sits anywhere (e.g. system Downloads).
  In the editor: **Import pack… → file picker → choose the folder → it imports into the
  current game** (copy, §5). The source is not retained; the copies in the game are.
- **Permanent / editor-wide** — for a pack you use a lot: **Install pack…** copies the pack
  folder into `packs/` so it lives alongside stdlib and shows up in the editor's import UI
  for **every** project, with no filesystem hunting. (This is a copy of the *source*, not an
  import into a game.)

The shipped first-party `zombies` pack is simply a pack that's pre-present in `packs/`.

---

## 5. Import = copy into the game (whole pack or items)

Importing is the games-as-projects copy-on-import ([game-projects.md](game-projects.md) §6),
now also driven at pack granularity.

- **Granularity:** import a **whole pack** (all its mobs/perks/weapons/props) **or pick
  individual items** from it (just the zombie mob, just one perk).
- **What gets copied:** each imported item's def file **plus the unique models/textures it
  references** that live in the pack, into the matching `games/<game>/` subdirs
  (`mobs/`, `perks/`, `models/`, `textures/`, …). Assets the item references from **stdlib**
  are *not* copied (stdlib ships; they resolve at runtime).
- **Result:** the game folder now owns those copies; the editor's catalog scan
  (`EdScene_ScanMobs` & siblings, via `Eng_ContentDirs`) picks them up game-first, and the
  game runs off them. A game using the zombies pack is self-contained for that content.
- **Provenance (optional, later):** record each import's source pack + version in a `.import`
  sidecar so the editor can later offer "update from pack" / show origin — the deferred
  games-as-projects sidecar item. Not required for v1.

Runtime is **unchanged**: `Eng_ContentDirs` still resolves **game → stdlib**. Packs never
load at runtime; they only feed the import.

---

## 6. Editor UX

The content browser (PALETTE, [editor-ux-review.md](editor-ux-review.md) §4) becomes the
import surface as well as the placement surface:

- **Source switch / badge** — tiles show whether they're already in the game (placeable) or
  available to import from stdlib / a pack, with a per-tile **pack badge** and an **Import**
  affordance (mirrors the planned `Import ⤓` in the UX review).
- **Browse by pack** — a pack filter/grouping so you can see "everything in the zombies pack"
  and **Import pack** as a unit, or import individual tiles.
- **Install / Import pack…** menu items — **Install pack…** (permanent → `packs/`) and
  **Import pack…** (file picker → this game), per §4. Both reuse the native file picker
  already in the editor (`edfiledialog`).
- Placement itself is unchanged — a tile arms the same `placeTool` state once its content
  is in the game.

This is the UI half of the content-import work flagged in
[game-projects.md](game-projects.md) §8 and
[editor-content-extensibility.md](editor-content-extensibility.md) §6.

---

## 7. Migration of today's `library/` (load-bearing — one careful step)

The game currently **runs** off `library/` via the overlay, so the split must keep every map
loading. The move:

| Today (`library/`) | Goes to |
|---|---|
| `weapons/{pistol,smg,shotgun,rifle}` | `stdlib/weapons/` |
| `props/{obstacle_crate,obstacle_barrel,sandbag_stack}` | `stdlib/props/` |
| `shaders/`, `templates/` | `stdlib/` (shared engine resources) |
| `mobs/{zombie,dog}` | `packs/zombies/mobs/` |
| `perks/{juggernog,speed_cola,double_tap,staminup}` | `packs/zombies/perks/` |
| `weapons/raygun` | `packs/zombies/weapons/` |
| `models/`, `textures/` | split: universal → `stdlib/`; zombie-only → `packs/zombies/` |

Then, because **packs are copy-on-import** and the reference game *uses* the zombies pack:

- **Import the `zombies` pack into `games/shooter/`** — copy its mobs/perks/weapons/props +
  unique models/textures into `games/shooter/`. After this, `games/shooter/` is self-contained
  for zombie content and runs off its own copies.
- **Repoint the engine root wiring**: `Eng_LocateRoot("library", …)` → `Eng_LocateRoot("stdlib", …)`;
  the game root stays `games/shooter`. Update `Eng_ContentDirs` callers/comments from
  "library" to "stdlib". (No behavioural change — same game→base fallback, base just renamed.)
- **CI / build**: any path or copy step referencing `library/` (CMake asset staging,
  `build/library/…` shader copy seen in editor logs) repoints to `stdlib/`.
- **Verify**: `--sim-tick`/`--screenshot` on every map + the editor `--check`/`--shot` stay
  green (the §15 / CI litmus), proving no map lost its content in the move.

This is a no-backwards-compat move (per the project rule) — one clean cut, not a compat shim.

---

## 8. Implementation phases

1. **Layout + manifest + migration (§7) — ✅ SHIPPED** (commits 29a2747 + 1aef637): created
   `stdlib/` + `packs/zombies/`, moved files, imported the pack into `games/shooter/`, repointed
   roots (`Eng_LocateRoot("stdlib")`) + CMake staging (`stdlib/ packs/ games/`). Verified maps +
   render + editor catalog. *No new runtime code beyond root-name wiring.*
2. **Pack scan + manifest read — ✅ SHIPPED** (`src/engine/pack.{c,h}`): `Eng_PackReadManifest`
   reads a `pack.manifest` (deffile), `Eng_PackList` enumerates `packs/` (located via
   `Eng_LocateRoot`) as **import sources** distinct from the runtime overlay, and `Eng_PackDirs`
   is the per-pack analogue of `Eng_ContentDirs`. Engine-clean (raylib + deffile + content.h).
3. **Copy-on-import core — ✅ SHIPPED** (`src/editor/edimport.{c,h}`): `EdImport_Item` copies one
   def + the unique model/`.mtl`/`map_Kd` textures it references; `EdImport_Pack` does a whole
   pack. Binary-safe (LoadFileData/SaveFileData). Mirrors the source layout (weapon model beside
   the def vs. mob/perk/prop model in shared `models/`) so the game resolves the copy unchanged.
4. **Editor UX — ✅ SHIPPED** — menus (`src/editor/edmenus.c`): **File ▸ Game project ▸
   Import Pack…** (folder picker → import into the open game, then rescan the palette),
   **Install Pack…** (folder picker → copy a source into `packs/`, reusing the now-public
   `EdProject_CopyTree`), and dynamic **Import: \<pack\>** quick-entries from `Eng_PackList`;
   plus **Install Pack…** on the launcher (`edlauncher.c`). **Palette** (`edpanels.c`): pack
   items the game lacks show as dimmed tiles with a source-pack chip; a click copy-imports +
   re-scans + arms; the search matches the pack id (browse-by-pack). Provenance flows from the
   catalog scan (`edcatalog.c`) through `EdMobDef`/`EdPropDef`.
5. **Provenance sidecars — ✅ SHIPPED (write side)** — `EdImport_Item` writes a `.import`
   sidecar (source pack + version + path) beside each copied def. *Remaining:* the read-back
   ("update from pack" / "show origin") UI.

Phases 1–5 are shipped; only "update from pack" (reading the sidecars) remains as polish.

---

## 9. Decisions (all RESOLVED 2026-06-20 — chosen value in **bold**)

1. **Source-tier name:** **`packs/`** *(chosen — first-party packs live there too)* vs.
   `3rdparty/` (only fits externally-installed ones).
2. **stdlib copy policy:** **fallback + import-to-edit** *(recommended — matches the existing
   game-projects overlay; games stay lean)* vs. always-copy stdlib into every game (fully
   self-contained, no fallback; simpler mental model, fatter games, stdlib updates don't
   propagate).
3. **"Add pack to the editor overall" semantics:** **install the source into `packs/`**
   *(available to import everywhere)* vs. also auto-import into a chosen set of games. The
   former is simpler and was the intent ("so the editor just has it").
4. **Whole-pack vs item import default gesture:** offer **both** (Import pack button +
   per-tile import) — already decided.
```
