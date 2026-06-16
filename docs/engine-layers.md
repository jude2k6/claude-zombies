# Engine / Toolkit / Game / Editor — the layering

> **Status: scaffold — for discussion.** This doc proposes *where things live* so we
> end up with **an engine + a toolkit you build games and tools on**, not **a game
> with an editor bolted inside it**. It complements:
> - [engine-game-separation.md](engine-game-separation.md) — the *rationale & history*
>   of the engine/game seam (the cardinal rule, where each file landed).
> - [engine-usage.md](engine-usage.md) — the *how-to* for the engine API as it is today.
> - [engine-roadmap.md](engine-roadmap.md) — the *plan* (phases) toward the Krunker-like goal.
>
> Nothing here is built solely by writing this doc; items become real when they ship a
> header + code and get documented in engine-usage.md §3.

## 1. The question this answers

Two things prompted this:

1. **"Should the engine provide a way of making UI (menus)?"** — and if so, is that the
   *engine*, or a separate *toolkit*?
2. **Where does the map/scene builder belong?** In Krunker the editor lives *in the
   game*; but if we reuse this engine for a *different* game we still want the scene
   builder. The stated goal: **"an engine with a toolkit, not a game with a built-in
   map editor."**

The answer to both is the same rule the project already runs on (see §3).

## 2. The mental model: four layers, two binaries (today: one)

```
  ┌──────────────────────────────────────────────────────────────────────┐
  │  APPLICATIONS  (binaries — each is just a GameModule on the engine)   │
  │                                                                        │
  │   shooter            editor              <your next game>             │
  │   (this game)        (scene builder)     (reuses everything below)    │
  │   menus, rounds,     panels, tool        its own rules + content      │
  │   weapons, AI        modes, inspector                                  │
  └──────────────┬───────────────┬───────────────────┬────────────────────┘
                 │  all link, all depend only downward ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  TOOLKIT  (engine-side, game-agnostic — the "SDK" surface)            │
  │                                                                        │
  │   ui.h        2D widget facade  (Eng_Ui* over raygui)   ← PROPOSED    │
  │   pick.h      screen→ray + ray/primitive tests                        │
  │   gizmo.h     translate/rotate/scale handle math                      │
  │   mapedit.h   id-addressed MapDoc edits + undo/redo                   │
  │   mapdoc.h    the neutral scene format (the editor's document)        │
  │   debugdraw.h deferred 3D overlay                                      │
  └──────────────┬───────────────────────────────────────────────────────┘
                 │  built on ▼
  ┌──────────────────────────────────────────────────────────────────────┐
  │  ENGINE CORE  (libengine.a — window/loop/time, GL facade, render,     │
  │   anim, audio, net transport, content registry, collide, input, fx)   │
  └──────────────────────────────────────────────────────────────────────┘
```

**Toolkit is not a separate library from the engine** — it is a *named tier of the
same `libengine.a`*. Everything in the toolkit box already lives in `src/engine/` and
links into `libengine.a` today (except `ui.h`, which is proposed). The split is
**conceptual** (core vs toolkit headers), not a second `.a` — see Open #1 for when/if
that ever changes.

**"Application = a `GameModule` on the engine"** is already the project's litmus:
*a second game is a second module, touching zero engine files* (engine-usage.md §4).
The **editor is just the first non-game application** to exercise that.

## 3. The rule that decides where anything goes

The project already has one rule and it answers both questions: **the engine ships
primitives, the application brings policy.** Live examples already in the tree:

| Engine owns (primitive / substrate) | Application owns (policy / content) |
|---|---|
| ray math (`pick.h`) | what counts as "solid" / selectable |
| collision sweep (`collide.h`) | the character controller / movement feel |
| net transport (`net.h`) | the wire schema (`protocol.c`) |
| action map (`pad.h`) | what each action *means* |
| 3D draw facade (`gfx.h`) | what scene to draw |
| **2D widget facade (`ui.h`, proposed)** | **what menus/panels exist and do** |

So: **the engine provides the means to draw UI; it never defines a menu.** A "main
menu" or an "editor inspector panel" is content — it lives in the application that owns
that flow.

## 4. Where UI lands, specifically

UI is not one thing. Split it three ways and it stops being confusing:

- **UI substrate → engine/toolkit (`ui.h`, `Eng_Ui*`).** Buttons, labels, text,
  panels, sliders, checkboxes, text fields, basic layout. A thin facade over **raygui**
  + 2D raylib (`DrawText`/`DrawRectangle`). This is the **2D sibling of `gfx.h`** (3D)
  and `debugdraw.h` (overlay). *Both* the game menus and the editor panels need exactly
  these widgets — which is the whole reason it belongs one level down, shared.

  This is already half-true: `app.c` literally comments *"the engine owns the UI
  substrate"* — `RAYGUI_IMPLEMENTATION` is compiled into the engine and the engine sets
  the default style; the game's `menu.c`/`hud.c` just `#include "raygui.h"` and call
  `Gui*` directly. **The missing piece is the facade** so applications call `Eng_Ui*`
  instead of raygui directly (same containment we did for 3D with `Eng_Gfx*`).

- **The menus themselves → game (`src/game/ui/menu.c`, `hud.c`).** Which screens exist,
  the main→solo→loadout flow, what each button does, the round/score HUD — all game
  policy. Stays game-side, rebuilt on `Eng_Ui*`.

- **The editor's panels → the editor app.** Tool palette, entity inspector, sector
  list, save/load dialog. Tool policy. Lives in the editor binary, on the same
  `Eng_Ui*`.

This resolves the long-deferred **"Open #1 — 2D UI facade"** in
[engine-game-separation.md](engine-game-separation.md) §17: it was parked as *"low
value... but the map editor needs the same raygui anyway."* The editor becoming real is
exactly the trigger that flips it from *low value* to *worth doing* — two consumers now
share the widgets.

## 5. The scene / map builder

The builder is **a tool on `MapDoc`, with zero game knowledge** — it depends only on
the toolkit + engine. Its document is the engine-neutral `.map`/`MapDoc`; its verbs are
`pick.h` (select), `gizmo.h` (manipulate), `mapedit.h` (edit + undo/redo), `ui.h`
(panels). None of that knows what a "zombie" or a "perk machine" *is* — only that the
map has entities with ids, positions, and types.

So the editor is a **sibling binary to the game**, not a mode inside it:

```
src/
  engine/   ← core + toolkit (libengine.a)
  game/     ← shooter rules + content  → shooter binary
  editor/   ← scene builder            → editor binary   (PROPOSED, new)
```

**"But Krunker's editor is in-game!"** — that's a *packaging* choice, not an
architecture one. Two ways to ship the same editor code:

- **Separate binary** (`editor`) — best for "engine as an SDK for *other* games": the
  tool stands alone, any game's maps open in it.
- **Embedded** — the game launches the editor as an in-process *screen/mode* (its own
  `GameModule`, or a state the game switches into). The game depends on the editor
  module; the editor still depends on **neither** the game nor its rules.

Either way the editor code is identical and game-agnostic. Krunker-style "edit your map
in the game" = the embedded option; "ship an engine other people build games with" = the
standalone binary. **We can do both from one editor module** because it sits in the
toolkit layer, below any specific game. That is the difference between *an engine with a
toolkit* and *a game with a built-in editor*.

> The game *may* still own a small amount of **content-specific authoring** (e.g. a
> custom inspector widget that knows a "perk" entity has a flavour enum). The pattern:
> the editor core renders/handles the generic `MapDoc` entity; the game registers
> optional per-type extensions — same shape as the content registry already used for
> asset parsers (engine-game-separation.md §17, Phase 4).

## 6. Current state → target (migration sketch)

Nothing below is required to *finish the current game*; it is the cleanup that makes the
engine reusable as an SDK.

| Area | Today | Target |
|---|---|---|
| UI substrate | raygui `#include`d + called directly by `game/ui/*` | `ui.h` / `Eng_Ui*` facade in engine; apps stop touching raygui directly |
| Game menus/HUD | `src/game/menu.c`, `hud.c` on raw raygui + `DrawText` | same files, rebuilt on `Eng_Ui*` |
| Editor | does not exist; primitives (`pick`/`gizmo`/`mapedit`/`mapdoc`) all shipped | `src/editor/` app (own `GameModule`) → `editor` binary |
| Editor packaging | n/a | standalone binary first; optional in-game embed later |

**Suggested order** (small, independently shippable, none blocks the game):
1. `ui.h` facade — wrap the raygui calls already in use (button, label, panel, slider,
   text field, toggle) as `Eng_Ui*`; port `menu.c`/`hud.c` onto it. *(Closes Open #1.)*
2. `src/editor/` skeleton — a `GameModule` that loads a `MapDoc`, flies a camera, draws
   the scene via `Eng_Gfx*`, and selects with `pick.h`. Produces the `editor` binary.
3. Wire the editor verbs — `gizmo.h` drag → `mapedit.h` mutator → `EngMapHistory`
   commit; an `Eng_Ui*` inspector panel for the selected entity.
4. Save/load + a tool palette; then consider the in-game embed.

## 7. Open decisions (pick later; defaults in **bold**)

1. **One library, two documented tiers** *(recommended)* vs. split a `libtoolkit.a` out
   of `libengine.a`. One lib is lowest-churn and raygui is *already* an engine dep; only
   split if a consumer genuinely wants the core without raygui/editor — no such consumer
   is in view.
2. **Immediate-mode `Eng_Ui*`** *(recommended, matches raygui + the existing code)* vs.
   a retained widget tree. Immediate-mode is what menu/hud already are; a retained model
   is a big jump for no current need.
3. Editor packaging: **standalone `editor` binary first**, in-game embed later — vs.
   embed-only from the start. Standalone first keeps the "engine as SDK" property honest
   and is easier to test headlessly.
4. Editor tree location: **`src/editor/`** as a peer of `src/game/` — vs. `src/game/editor/`
   (rejected: that re-couples the tool to this game, the exact thing we're avoiding).
5. **Do we need a menu *builder* tool?** (open — revisit later) Distinguish a tool to
   *draw* menus (= the `ui.h` facade, already step 1) from a tool to *build* menus (a
   visual menu/layout editor, the menu analogue of the scene builder). A builder looks
   tempting but the trade is poor today: menus are few (~5–10 screens vs. hundreds of
   maps), behaviour-coupled (a button *is* a callback — a layout tool only captures the
   visual half, the wiring stays code), and immediate-mode (raygui = UI-as-code, which
   doesn't serialise to a document — a builder really wants a *retained* UI tree, i.e.
   Open #2). So: **no dedicated menu builder until menus must be moddable *data*** (custom
   community HUDs / cosmetic UI) — the same trigger that would flip Open #2 to retained-mode.
   Cheaper middle ground to reach for first if the itch is reskinning: a **theme/style
   asset as data** (colors/fonts/spacing the `ui.h` facade reads — raygui already has a
   style system `app.c` drives), plus letting the editor app **dogfood `ui.h`** as proof
   the facade is enough for real panels. Revisit a true UI-document + layout tool only at
   the moddable-UI trigger.

## 8. One-line summary

**Engine** = the runtime. **Toolkit** = the engine's game-agnostic SDK surface (UI
widgets + the editor primitives + the scene format), shipped *in* `libengine.a`.
**Game** and **Editor** = sibling applications, each a `GameModule`, each owning only
its own policy. The editor is a tool on the scene format, not a feature of the game —
which is what makes this *an engine with a toolkit* rather than *a game with an editor*.
