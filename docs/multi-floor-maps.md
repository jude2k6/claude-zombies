# Multi-Floor / Vertical Maps — Design & Plan

> ## ▶ Status (updated 2026-06-14): approach B core is IMPLEMENTED
> Vertical + true overlapping floors now work for the **player**. Landed:
> - `FloorRegion` (axis-aligned XZ slab, flat or ramp) in `g_world.floors[]`,
>   and `Level_FloorHeightAt(x,z,feetY)` — the §2 "ground height is a query"
>   idea — returning the highest walkable surface at/just above the feet
>   (within `STEP_UP_HEIGHT`), with an implicit Y=0 ground plane fallback so
>   flat maps are unchanged. `player.c` ground snap uses it (stairs auto-climb,
>   stacked floors resolve to the right one).
> - MapDoc `FLOOR x z sx sz y` (flat) / `FLOOR x z sx sz yLow yHigh X|Z` (ramp)
>   entries: parsed, saved (canonical round-trip), `MapDoc_Equal`, and
>   instantiated into `g_world.floors`.
> - Rendering of decks (textured top + slab body) and ramps (sloped quad) in
>   `render.c DrawArena`.
> - `data/maps/multifloor.map` demo (ground + overlapping Y4 deck + ramp + Y8
>   catwalk) and a `--screenshot-map <file.map>` dev harness to eyeball any map.
>
> **Floor-slab collision + cross-floor AI — now also done:**
> - Bullets are blocked by floor-region slabs (`Level_FloorRegionBox` + the
>   bullet segment loop): no shooting through a deck/ramp between levels.
> - Enemies stand on the floor surface each tick (`Level_FloorHeightAt`), so
>   they walk up ramps and onto decks instead of floating at a fixed Y.
> - Cross-floor zombie nav (`CrossFloorGoal` in `entities.c`): when the player
>   is on another floor, the zombie routes toward a ramp that moves it one level
>   toward the player (greedy, level-by-level, reusing XZ homing toward the ramp
>   entrance — ramps are the implicit graph edges). Bites are gated to the same
>   floor. Verified headless by `--sim-navtest <map>` (zombie climbs to the deck
>   in ~1.8s on multifloor.map). On flat maps none of this fires (queries return
>   0), so AI is byte-identical.
>
> > **Map format is now the sector model** (see the SECTOR/RAMP grammar in
> `data/maps/default.map`'s header): a map is a list of sectors, every entity
> belongs to one and derives its floor Y from it, and `RAMP … LINK a b` records
> the nav edges. The flat 2D-entity + ROOM format is gone.

**Still TODO (refinements, not blockers):** the nav is *greedy* — it climbs
> level-by-level and can dead-end on maps needing down-then-up routing (a real
> region/portal BFS, §5, would fix that); ramp bullet-blocking uses one AABB
> spanning yLow..yHigh (slightly over-blocks the air beside a ramp); grenades
> still detonate on the Y=0 plane, not on decks; spawns/perks/wallbuys don't yet
> carry a floor Y. Core multi-floor *gameplay* (walk, stand, shoot, get chased
> across floors) works.

Status of the rest of this doc: **design reference** for the deferred work
(collision + AI nav) and the engine-seam framing. How to add verticality
(ramps, platforms, and true stacked floors) to a world whose collision and
navigation are currently flat (XZ-plane).

This pairs with [engine-game-separation.md](engine-game-separation.md): the
spatial queries below are **engine services** the game *and* the future map
editor consume. Building verticality and building the engine seam are the same
investment — do the spatial-query layer once.

---

## 1. Why it doesn't work today

The ground is a single global constant. In `player.c`:

```c
if (newY <= PLAYER_EYE) { newY = PLAYER_EYE; p->onGround = true; }
```

`PLAYER_EYE` (1.7) *is* the floor, everywhere. Collision is XZ-only
(`Level_ResolveXZ`, `Level_CircleHitsBoxXZ`, `Level_PointBlocked`,
`Level_PathClearXZ`); walls are full-height `Box`es; AI homes toward the player
in the XZ plane. There is no concept of "which floor am I on." That constant is
the entire reason multi-floor doesn't exist.

**What's already 3D and costs almost nothing:** rendering (`render.c` is full
3D — drawing N floors just works) and bullets (`SegmentEnemyHit` is a swept 3D
segment test — shooting between floors already works geometrically). Verticality
in this engine is an **AI + map-data problem, not a graphics problem.**

---

## 2. The one idea everything hangs off

**Ground height stops being a constant and becomes a query:**

```c
// engine spatial service
float Level_FloorHeightAt(float x, float z);             // heightfield (approach A)
float Level_FloorHeightAt(float x, float z, float curY); // stacked floors (approach B):
                                                         //   highest walkable surface BELOW curY
```

The player snap becomes:

```c
float groundY = Level_FloorHeightAt(p->pos.x, p->pos.z, p->pos.y);
if (newY <= groundY + PLAYER_EYE) { newY = groundY + PLAYER_EYE; p->onGround = true; }
```

Once "where is the floor here?" is a function instead of `1.7`, ramps,
platforms, and stacked floors all become *data*. Everything else is: what data
backs that query, and how AI navigates it.

---

## 3. Three ways to back the query (pick by how much verticality you need)

### A. Heightfield — one height per XZ point
A function/grid returning floor Y at any (x,z). Gives **ramps, raised
platforms, catwalks, hills, sunken areas**. Cheap: existing XZ collision plus a
height lookup; ramps are walkable for free; AI nav barely changes (still XZ,
just walks up/down slopes). **Limit:** one height per point — no room directly
*above* another room. This is the 80/20: most verticality feel, least work.

### B. Stacked regions / sectors — multiple heights per XZ point
True multi-floor (an upstairs over a downstairs). The map is a set of **convex
floor polygons, each at a Y**, connected by **stairs/ramps as links**. "Which
floor am I on" = the region whose surface is highest *below* the player. This is
**how real CoD-zombies maps actually work** — Nacht/Verrückt/Kino are connected
planar areas at different heights, not volumetric BSP. Best fit for this game
and a tractable editor.

### C. Full volumetric brushes (Quake/Source style)
Arbitrary 3D solids, true 6-DOF collision. Most powerful, most work, overkill
for a zombies arena. **Skip it.**

**Plan: A first** (a strict subset of B, ships verticality fast), then **B**
when overlapping floors are needed.

---

## 4. Per-subsystem impact

| Subsystem | Change | Effort |
|-----------|--------|--------|
| **Player ground** (`player.c`) | `Level_FloorHeightAt` query instead of the `PLAYER_EYE` constant; step-up tolerance so stairs/curbs auto-climb | small |
| **Collision** (`level.c`) | keep XZ wall resolve; add floor slab as a vertical blocker (can't fall through); region membership for B | small (A) / medium (B) |
| **AI navigation** (`entities.c`) | **the hard part** — see §5. XZ homing must become floor-aware via a nav graph | medium–large |
| **Bullets** (`entities.c`) | already 3D; add floor slabs as segment blockers so shots don't pass through floors | small |
| **Rendering** (`render.c`) | already full 3D — **no change** | none |
| **MapDoc** (`mapdoc.c`) | add `FLOOR`/`REGION`/`STAIR` entries with Y/height; walls/props gain optional floor or base-Y | medium |
| **Spawns / windows / perks / wallbuys** | gain a Y (which floor) | small |

Headline: rendering and bullets are nearly free. The real work is **navigation**
then **data format**.

---

## 5. AI navigation (the actual cost)

Today `Enemy` homes toward `player.pos` with `Level_PathClearXZ` avoidance. That
breaks the instant the target is on another floor — the zombie walks into the
wall beneath the player.

Fix: a **portal/waypoint graph**.

- Each floor region is a node; each stair/ramp is an edge linking two regions.
- A zombie finds the player's region, runs a small BFS/Dijkstra over the region
  graph for the next region, homes toward **that region's connecting stair**
  until it's on the player's floor, then homes on the player directly.
- This **reuses** the existing XZ homing + avoidance *within* a floor; the graph
  only decides "which stair/doorway next." It generalizes the door pathfinding
  zombies already do (routing through doorways) to include vertical links.

So this extends an existing pattern, not a new system.

---

## 6. Recommended path

1. **Heightfield first (A).** Add `Level_FloorHeightAt(x,z)`, swap the
   `player.c` constant, add stairs/ramps as height ramps. Vertical maps that
   *play* without major AI work (zombies walk up slopes). High payoff, low risk.
2. **Region nav graph (§5)** when zombies must chase *across* true floors —
   generalize the doorway routing to include stair links.
3. **Stacked regions (B)** only when a room must sit literally above another.
   That's when `Level_FloorHeightAt` takes the player's Y to pick the floor
   below them.

---

## 7. How it lands in the engine seam

`Level_FloorHeightAt`, the region nav graph, floor-slab collision, and
segment-vs-world are all **engine spatial-query services** (`engine/level` or
`engine/collision`): generic, game-agnostic, headless-testable. The game and the
map editor both call them. The editor authors `FLOOR`/`STAIR` entries into the
`MapDoc`; the engine instantiates them into the collision/nav world.

Build the spatial-query layer once; both the game and the editor consume it.
```
