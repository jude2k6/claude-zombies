# Multi-Floor / Vertical Maps

Vertical and true overlapping floors. Status: **working end-to-end** — player
movement, collision, rendering, and cross-floor zombie AI. This doc is the
current-state reference plus the open refinements.

## What works

- **Floors are a query, not a constant.** `FloorRegion` (axis-aligned XZ slab,
  flat or ramp) in `g_world.floors[]`, and `Level_FloorHeightAt(x, z, feetY)`
  returns the highest walkable surface at/just above the feet (within
  `STEP_UP_HEIGHT`), with an implicit Y=0 ground fallback so flat maps are
  unchanged. `player.c` ground-snap uses it (stairs auto-climb; stacked floors
  resolve to the right one).
- **Map data is the sector model** (see `data/maps/default.map`'s header for the
  grammar): `SECTOR`/`RAMP` blocks; every entity belongs to a sector and derives
  its Y from it; `RAMP … LINK a b` records the nav edges. Parsed, saved
  (canonical round-trip), `MapDoc_Equal`, instantiated into `g_world.floors`.
- **Rendering** of decks (textured top + slab body) and ramps (sloped quad) in
  `render.c:DrawArena`/`DrawFloors`. Rendering was always full 3D — verticality
  here was an AI + map-data problem, not a graphics one.
- **Floor-slab collision:** bullets are blocked by floor-region slabs
  (`Level_FloorRegionBox` + the bullet segment loop) — no shooting through a deck.
- **Enemies stand on the surface** each tick (`Level_FloorHeightAt`), walking up
  ramps and onto decks.
- **Cross-floor zombie nav — region BFS** (`CrossFloorGoalFull` in `entities.c`):
  FLAT sectors are nav nodes, `RAMP … LINK a b` ramps are edges
  (`g_world.navNodes/navEdges`, built in `Level_InstantiateDoc`). Two-phase: on a
  ramp, head for the end with the shorter BFS hop-distance to the target's region
  (handles up / down / **down-then-up**); on a flat sector, BFS and head for the
  first ramp's near entrance. `RampAscentAhead` makes a zombie **steer around
  ramps it isn't mounting** (detour in Z past an X-ramp blocking the path), via
  the existing fan-probe. Flat maps have no nav nodes → AI byte-identical.

Verify headless: `--sim-tick`, `--screenshot-map <file.map>`, `--sim-navtest
data/maps/multifloor.map` (climb), `--sim-navtest-dtu data/maps/navtest_dtu.map`
(down-then-up, RAMP_Z) and `data/maps/navtest_xramp.map` (X-ramp across the path).

## Demo / test maps

`data/maps/multifloor.map` — ground + overlapping Y4 deck + ramp + Y8 catwalk.
`navtest_dtu.map` / `navtest_xramp.map` — down-then-up routing fixtures.

## Open refinements (not blockers)

- **Ramp bullet-blocking** uses one AABB spanning yLow..yHigh, slightly
  over-blocking the air beside a ramp.
- **Grenades** still detonate on the Y=0 plane, not on decks.
- **Spawns / perks / wallbuys** derive their Y from their sector at instantiation
  but don't otherwise carry a runtime floor for gameplay queries.

## Engine-seam note

`Level_FloorHeightAt`, the region nav graph, floor-slab collision, and
segment-vs-world are spatial-query services the game *and* a future map editor
both consume. The editor authors `SECTOR`/`RAMP` into the `MapDoc`; the engine/
game instantiate it into the collision/nav world. Build the spatial-query layer
once. (See `docs/engine-usage.md` for the MapDoc seam.)
