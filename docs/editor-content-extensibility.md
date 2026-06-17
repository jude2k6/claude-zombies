# Editor Content Extensibility — entities, mobs, assets, behaviours

**Status: agreed design, NOT yet implemented.** This captures the design settled in
discussion for how users add their own entities/mobs/assets to the map editor (and how the
game consumes the same content). It is the plan behind roadmap item #3 in
[scene-builder.md](scene-builder.md) ("place all entity kinds") and the broader backlog in
[editor-feature-ideas.md](editor-feature-ideas.md). It builds on the editor's plugin shell
([scene-builder.md](scene-builder.md) §3) and the engine/game seam
([engine-layers.md](engine-layers.md)).

---

## 1. The core distinction: primitives vs definitions

There are two things people call "an entity"; conflating them is the trap.

- **Primitive kinds** — the 8 fixed things the `.map` grammar and `MapDoc` know about:
  `sector, wall, window, obstacle, prop, spawn, wallbuy, perk`. Adding a *new* primitive
  (say a trigger volume) means changing the grammar + parser + engine structs — an **engine
  change**, kept rare and curated. Users do **not** add primitives.
- **Entity definitions (templates)** — "a Dog spawn", "a barrel prop", "a Juggernog perk".
  Each is just *a primitive plus preset field values*. The document model is **already
  built for this**: the flavor fields are free-form strings the engine never interprets —
  `spawn.mob`, `prop.name`, `wallbuy.weapon`, `perk.perk`, texture names. `mob=DOG` is a
  spawn; `name=barrel.glb` is a prop. "A new mob is a new tag" already proves it needs zero
  engine changes.

**Users add definitions, never primitives.** The editor's place palette is built from a
**registry of definitions**, not the hardcoded handful of tools it has today.

This is the Quake/Source `.fgd` model more than the Unity model: a level editor reads a
declaration of entity classes + their keyvalues (so it knows what to place and what fields
to show); the engine/game reads the map and supplies behaviour by class/tag. Our editor is
behaviour-free by design — see §4.

## 2. The entity registry

One concept the place palette, Hierarchy, and proxy colours all read from:

```
EdEntityDef {
    name        "Dog spawn"      // palette label
    category    "Spawns"         // palette grouping
    color       red              // proxy / marker tint
    kind        SPAWN            // which MapDoc primitive it instantiates
    placement   POINT            // gesture: POINT | LINE | RECT  (see §5)
    defaults    mob=DOG          // preset flavor fields written on placement
    asset       (optional)       // model/texture this def references
}
```

The current "Player spawn / Zombie spawn / Barricade" tools become three built-in defs
instead of a hardcoded enum.

### Where defs come from — three tiers (decision: "both, data-first")

1. **Built-in, engine-generic** — the editor ships defs for the raw primitives (sector,
   wall, obstacle, generic prop/spawn, window). Always available, no game knowledge.
2. **Data-driven, no code** — the editor scans a folder (e.g. `data/mobs/`, and a general
   `data/editor/entities/`) and turns each def/catalog file into a palette entry. **This is
   the primary user path** — add content by dropping a text file, no recompile.
3. **Plugin, code** — a built-in or `.so` plugin calls an `EdHost_AddEntityDef(...)` API for
   defs that need custom placement interaction or inspector fields. This is also the
   seam-clean way the **game** hands the editor its game-specific catalog without the editor
   ever depending on game code.

## 3. Mobs as data files (like `.weapon`)

A mob is content, authored exactly like a weapon. Layout mirrors `data/weapons/`:

```
data/mobs/
  zombie/
    zombie.mob        # key=value, house style (see data/weapons/*.weapon)
    zombie.glb        # rigged model + animation clips
  dog/
    dog.mob
    dog.glb
```

### `zombie.mob` (annotated)

```ini
# Zombie — the baseline shambler. `id` is what a spawn's `mob` tag stores.

id              ZOMBIE
name            Shambler
category        mob              # vs PLAYER; groups it in the editor palette

# ---- render (same fields as .weapon) ----
model           zombie.glb
model_scale     1.0
model_yaw       0.0
tint            150 200 150 255  # editor marker / proxy colour

# ---- animation: logical state -> clip name inside the .glb ----
anim_walk       Walk
anim_attack     Attack
anim_death      Death
anim_idle       Idle

# ---- behaviour: the hinge between reuse and custom (see §4) ----
behaviour       chaser           # names a GAME-side AI archetype (code)
move_speed      3.2
path_repath     0.5

# ---- combat / stats (data the game reads; engine never does) ----
health_base     100              # round-1 HP; game applies its round curve
damage          50
attack_windup   0.5
attack_range    1.6

# ---- audio ----
sfx_idle        ZOMBIE_GROAN 0.6 0.8
sfx_attack      ZOMBIE_HIT   0.9 1.0
```

### `dog.mob` — the "reuse" case (new mob, zero code)

```ini
id              DOG
name            Hellhound
category        mob
model           dog.glb
model_scale     0.8
tint            200 120 80 255
anim_walk       Run
anim_attack     Bite
anim_death      Death
behaviour       chaser           # REUSES the zombie's AI archetype, just faster
move_speed      8.0
health_base     60
damage          40
attack_windup   0.3
attack_range    1.2
```

### How each side consumes it

- **Game** loads `data/mobs/*/*.mob` at startup into a mob table (replacing the hardcoded
  `ZT_RUNNER/CRAWLER/BOSS` constants in `entities.c`). At runtime: spawn's `mob` tag → look
  up the def → load model/anims → run the named `behaviour` archetype with the data params.
- **Editor** scans the same folder; each `.mob` becomes a **spawn entity-def** in the
  palette ("Shambler", "Hellhound"). Placing one writes `SPAWN MOB <id>`. The editor uses
  `model`/`tint` to draw the marker (or the real model once textured rendering lands —
  [editor-textured-rendering-plan.md](editor-textured-rendering-plan.md)) and shows the
  stats read-only in the Inspector. **The editor never reads `behaviour`** — that's
  game-only — it just carries the tag. Seam stays clean.

The `.mob` file *is* the shared catalog: game reads behaviour, editor reads the placeable
identity. Everything in the file is **data** except one word — `behaviour` — which *names*
code (§4).

### Validating a `.mob` (a checker, like `MapDoc_Validate`)

A `.mob` gets a validator mirroring `MapDoc_Validate`: a `Mob_Validate` returning an issue
list (WARN/ERROR). It backs an editor **"Validate" pass** (issues listed in the Console,
click to jump to the offending mob), a headless **`--check` analogue** for CI, and lets the
game **refuse/warn** on a bad `.mob` at load instead of crashing. It splits along the seam:

- **Structural — shared/engine-side, the editor CAN run it:** required fields present
  (`id`, `name`, `model`); `id` non-empty and unique across the catalog; numeric sanity
  (`health_base > 0`, `move_speed >= 0`, `model_scale > 0`); `category` known; the
  referenced `model`/asset file actually resolves on disk; anim fields non-empty. This is
  pure data/file checking — no game knowledge.
- **Semantic / behaviour — game-side (needs the archetype registry):** `behaviour` names a
  **registered** archetype (ERROR if unknown — the *same* missing-archetype signal that
  nudges "open / stub the source" in §4), and the params that archetype requires are
  present and in range. The editor can surface "this behaviour isn't registered" from the
  catalog, but can't validate archetype-specific params without the game.

Reuse the shared def-file loader (§7) and an issue type like `MapDocIssue`, so `.mob`,
`.weapon`, etc. all validate through one mechanism.

**Linter and formatter — don't build either as a separate tool:**
- **Linter = the WARN tier of `Mob_Validate`.** Lint findings (unknown/misspelled/deprecated
  keys, legal-but-suspicious values, redundant explicit defaults) are just warnings, so they
  ride the same validator + issue list — one checker, two severities (ERROR = invalid,
  WARN = lint). A separate linter would only duplicate the parse + issue plumbing.
- **Formatter = the canonical serializer you need anyway.** Authoring a `.mob` from the
  Inspector requires *writing* one; that writer (canonical key order, aligned columns, like
  `MapDoc_Save` does for `.map`) IS the formatter. "Reformat / Save canonical" is a parse →
  write round-trip through it, keeping hand-edited and tool-written files identical.

This mirrors `.map` exactly: `MapDoc_Validate` (errors + warnings) + `MapDoc_Save` (the
canonical writer / de-facto formatter) — no new concepts.

## 4. Behaviour model — Tier 1-2, programmer-extends

Decision: behaviours are programmer-extended for now; user-scripting (Tier 3) is deferred.
Behaviours compile with the **game**, never the engine (the engine is behaviour-free).

A behaviour is **not** a hardcoded function call — it is a **name → archetype registry
lookup**. `behaviour chaser` resolves against a registry populated at game startup from two
provider tiers:

```
.mob:  behaviour chaser
                  │ lookup by name
                  ▼
        name → archetype registry
         ├─ Tier 1: compiled-in C   (src/game/ai/, Game_RegisterBehaviour("chaser",...);
         │                            needs a game rebuild)
         └─ Tier 2: dynamic .so     (same dlopen-by-name pattern as the editor plugin
                                      loader — drop a .so, NO engine/game rebuild)
   (Tier 3, deferred: scripts/*.lua — no compile at all; the eventual modder story.)
```

- **Reuse** = point `behaviour` at an existing archetype (`dog` → `chaser`). Pure data, no
  code.
- **Custom native** = a programmer adds an archetype (Tier 1 compiled-in, or Tier 2 as a
  `.so` with no main rebuild), then it's selectable from data.
- **Parameters live in the `.mob`**, never in the behaviour. The archetype is generic code
  that reads a param bag (so one `chaser` serves a slow zombie and a fast dog). The
  archetype declares which keys it reads; the mob supplies the values.

### Where a behaviour file lives — locality follows ownership

The rule that resolves "it feels weird to edit zombie behaviour outside the zombie folder":

- **Tuning a mob is local (the 90%).** "Faster / hits harder / use crawler AI" = params and
  the archetype *choice* — all in `<id>.mob`, in the mob's folder. Nothing leaves the folder.
- **A behaviour used by ONE mob** → ships as a `.so` in *that mob's folder*, beside its
  `.mob` and `.glb`. Everything about that mob is in one place.
  ```
  data/mobs/boss/
    boss.mob          behaviour=boss_smash
    boss.glb
    boss_smash.so     # boss-only behaviour, right here with the boss
  ```
- **A behaviour SHARED by many mobs** → central `behaviours/` dir (or compiled-in), because
  editing it affects every mob that uses it. That non-locality is an honest signal — like
  Unity's shared script asset vs. per-prefab values — not a wart.
  ```
  behaviours/chaser.so        (or compiled into the game)
  ```

The loader scans **both** `behaviours/` and every mob folder; `behaviour <name>` resolves
the same regardless of where the provider was found. So a mob folder *can* contain its
behaviour — and does, precisely when that behaviour is the mob's own.

> Note: a Tier-1 compiled-in archetype is C in `src/game/` and can't sit in the data tree;
> anything you want physically next to a mob takes the Tier-2 `.so` route.

### Authoring behaviours from the editor (open in external editor)

The editor never *edits* behaviour code — it's engine-clean and edits data; code lives
game-side. Instead it **surfaces the source and launches an external editor**, matching how
Unity hands scripts off to VS Code/Rider rather than shipping a code editor:

- The Inspector shows the selected entity's `behaviour` name with an **"Open behaviour
  source"** action that launches `$EDITOR` / VS Code / `xdg-open` on the file (a small
  spawn hook — the only external-process primitive the editor needs).
- It **pushes** you toward this: if a `.mob` names a behaviour the registry doesn't have (a
  missing archetype), the editor flags it (red in the Inspector / Console) and offers to
  open — or stub out — that source. That's the nudge from "I referenced `leaper`" to "go
  write `leaper`."
- So the editor knows *which* file to open while staying engine-clean, the **behaviour
  catalog entry carries an optional `source` path hint** (data the game/plugin supplies);
  the editor just opens it — it never reads, parses, or compiles it.

This is the only point where the editor touches behaviour code: as a **launcher**, not an
editor. A full embedded code/text editor is explicitly out of scope (even Unity delegates
to an external IDE).

Structured **data** files (`.mob`, map metadata) are edited primarily via the Inspector's
fields (sliders/dropdowns/text fields — e.g. `move_speed` as a slider, `behaviour` from a
list). A **simple raw-text view of the `.mob` is a reasonable fallback** (raygui's multiline
text box → save → reparse → revalidate): a `.mob` is a short key=value file, not code, so
this is cheap and useful for quick edits or fields the form doesn't expose yet. This is the
one "simple text editor" worth having — it does *not* extend to a general/code editor.

> Future only (don't build yet): a mob mixing two behaviours (movement + attack) is the
> ECS/component road — `behaviour` becomes a *list* of names. The name-reference design
> extends to that cleanly later.

## 5. Placement semantics — "where entities sit"

Driven by the def's `placement` field, because primitives aren't all point-like:

- **POINT** (spawn, prop, perk, wallbuy, window): one ground click; Y inherited from the
  sector under the cursor (proxies already do this); snapping applies; facing-aware kinds
  auto-face the interior and `R` rotates.
- **LINE** (wall): click start → click end.
- **RECT** (sector): drag a footprint.

Invariant from the grammar: every placed entity is assigned to the **sector under the
cursor** (fallback sector 0), since `.map` has no ungrouped entities (`Eng_SetSector`).

## 6. Assets

A parallel registry. The editor **scans `data/` for models (`.glb`) and textures** and shows
an **asset browser / picker**; adding an asset = drop a file in the folder. A def with
`kind=PROP` either hard-codes its `asset` (one-click "Barrel") or leaves it blank so placing
it opens the picker, whose choice is written into `prop.name`. Rendering those assets in the
viewport uses engine asset-load APIs — see
[editor-textured-rendering-plan.md](editor-textured-rendering-plan.md). This and game-accurate
rendering converge on the same asset path.

## 7. Implementation order (when we start)

1. **Shared def-file loader** — one house key=value reader reused by `.mob`, `.weapon`, and
   future `.perk`/`.prop` catalogs, and by the editor's def-scanner. One format, one parser.
2. **Zombie refactor (proves the format end-to-end)** — move the existing hardcoded zombie's
   numbers into `data/mobs/zombie/zombie.mob` and load it. Pure refactor, no behaviour
   change.
3. **Behaviour registry** — `Game_RegisterBehaviour(name, ...)` + a `behaviours/` (and
   per-mob-folder) `.so` scan, mirroring the editor's plugin loader. Register `chaser`
   (and whatever the current zombie uses) as Tier-1 archetypes.
4. **Editor def-scanner + palette** — generalise the place palette to read entity defs (mob
   catalog first), then add POINT/LINE/RECT placement for all primitive kinds.
5. **Asset browser + picker**; then a second mob (`dog`) to validate reuse, and (optionally)
   a Tier-2 behaviour `.so` to validate the no-rebuild path.
