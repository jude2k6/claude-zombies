# Asset spec — Blender → raylib

Scope: hand-authored OBJ models that the game loads at runtime (zombie,
guns). This doc fixes the aesthetic, the scale/axis conventions, and
the checklist of what to make so future assets all line up without
per-model fudge factors.

## Engine facts that dictate everything below

- **Renderer:** raylib 5.5, loads OBJ via `LoadModel("...obj")`.
- **Units:** 1 Blender meter = 1 world unit = 1 raylib unit.
- **Axes:** raylib is Y-up, right-handed, **-Z is forward**. Blender's
  default export (`Forward: -Z, Up: Y`) matches this — keep it.
- **Origin convention:** model origin sits at the model's **feet/grip**,
  not its centre. Reasons below.
- **Rigging-first.** The asset set is being re-authored so models are built
  *for animation from the start* — neutral rig pose (T/A-pose characters; guns
  with slide/bolt/charging-handle/mag as separate movable parts), edge loops at
  every joint that bends, planned skeleton. Don't model static then try to rig.
  The scale/axis/origin/palette rules in THIS doc still apply; rig-first changes
  topology and pose, not the art direction. Full rig + per-asset animation clip
  lists are in [`../ANIMATIONS.md`](../ANIMATIONS.md) — start there for anything
  that needs a skeleton. Only genuinely inert props stay static OBJ.

## Aesthetic

The current game is deliberately blocky: walls, props, and zombies are
solid-colour cubes with `DrawCubeWires` black outlines. New assets
should sit next to that **but push fidelity past pure cubes** — readable
detail at 5–10 m, not just silhouette at 20 m.

- **Stylised low-poly, NOT minimum-poly.** Target ~3000–5000 tris per
  asset (was ~1500 — bumped). Bevel every visible hard edge with a
  1–2 segment chamfer so silhouettes catch light; add secondary shapes
  (e.g. kneecaps on the zombie, ribbed cooling fins on the raygun)
  instead of leaving every limb/section as a single cuboid.
- **Hard normals on body panels, but model the detail in geometry.**
  Use Shade Flat globally so faces stay crisp; achieve "rounded" look
  by inserting more geometry, not by smoothing.
- **Flat colour, no textures.** One material per part, RGB colour set
  via `Principled BSDF → Base Color`. The OBJ + MTL exporter will emit
  a `Kd` line per material — raylib picks that up as the diffuse
  colour. No image maps; no PBR; ignore roughness/metallic.
- **Multiple material zones per asset.** Use 4–8 materials per model
  (vs. the old 3) so detail reads via colour breaks: e.g. zombie gets
  separate skin / shirt / pants / boots / blood / bone / nail
  materials; raygun gets body / grip / coil / muzzle / accent / vent /
  optic glass.
- **Palette:** desaturated, slightly muddy. Match the in-game enemy
  greens (`{80, 140, 60}` etc.) and the muted wood/metal tones already
  used for props. Save the bright tints for power-ups and tracer FX.
- **Detail to include now (was previously skipped):**
  - Zombie: separated jaw / brow ridge, knuckled hands (1 cube per
    finger group is fine), kneecaps, torn shirt strips as separate
    poly strips, exposed ribs/bone on damaged side
  - Guns: bevelled receiver edges, rib detail on barrel, iron sight
    notch geometry, vent slats as recessed inset faces, magazine or
    energy-cell as a distinct sub-mesh
  - Props (crates, sandbags, boards): chamfered edges + plank seams
    modelled as inset cuts, not painted

The line we are NOT crossing: photoreal textures, subsurf, PBR maps,
rigged deformation. Stay readable as "stylised cubes with character."

## Scale conventions

### Zombie

- **Height:** 1.7 m exactly (matches `ENEMY_HEIGHT` in `src/types.h`).
- **Footprint:** fits inside a cylinder of radius 0.6 m (`ENEMY_RADIUS`).
  Don't let outstretched arms exceed that — the collision capsule is
  what the game checks against, and overhang will clip walls.
- **Origin:** at the feet, centred between them, on the ground plane
  (`Z=0` in Blender, which becomes `Y=0` in raylib).
- **Facing:** model faces **-Z in Blender** (so it faces -Z in-world
  too, matching the "forward" convention). Code rotates around Y for
  per-zombie yaw — authoring forward as -Z means yaw=0 looks
  consistent across all assets.

### Guns (first-person viewmodel)

> **Superseded for first person:** viewmodels are now authored as **combined
> per-weapon rigs** (`data/weapons/<id>/<id>_vm.glb` — arms + gun + mechanism in
> one rigged glTF), not gun-only OBJs scaled near the camera. See
> `data/ANIMATIONS.md` and `docs/arms-rig-generalisation.md` §0. The gun OBJ +
> `model_scale` notes below still apply to **world** draws (wall-buy / Mystery
> Box / PaP display) and to the legacy bolt-on fallback. The draw code moved to
> `src/game/viewmodel.c` (`Viewmodel_DrawFirstPerson`).

The legacy gun-only viewmodel scale (`src/game/viewmodel.c`, fallback path):

```
float s = 0.025f * weaponTune[wi].scale;   // base scale × per-weapon tune
```

That `0.025` is the magic number. Measured bounds of the existing
viewmodels: rifle/shotgun/sniper are ~9–10 long on Z, smg ~11 long on
Z, pistol ~10 long on X (the +X-long outlier — see `weaponTune` yaw).
So a gun authored at ~10 Blender units long renders at ~0.25 m on
screen, which is the right viewmodel size when held near the camera.

- **Long-axis length:** 9–12 Blender units (matches existing
  `smg.obj`, `rifle.obj`, `shotgun.obj` authoring).
- **Forward axis:** model points along **+Z**. (`pistol.obj` is the
  exception — it's +X, which is why `weaponTune[W_PISTOL].yawDeg = 90`.
  Don't repeat that mistake on new guns.)
- **Up axis:** +Y. Sights/rail on top, grip hanging down.
- **Origin:** at the **grip**, where the player's hand would sit.
  That's the point the viewmodel anchor offsets are computed from.
  If the origin is at the muzzle or the receiver, the gun will appear
  to float away from the hand position.
- **Materials:** 1–3 materials per gun (body, grip, accents). Same
  flat-colour rule as the zombie.

## File layout

```
data/models/
├── ASSETS.md          ← this file
├── zombie.obj         ← new, this task
├── zombie.mtl
└── weapons/
    ├── pistol.obj     ← existing
    ├── smg.obj        ← existing
    ├── shotgun.obj    ← existing
    ├── rifle.obj      ← existing
    ├── raygun.obj     ← new, this task (W_RAYGUN currently NULL in assets.c)
    └── ...
```

Loader prefixes (see `src/assets.c:Assets_Load`):
`data/models/weapons/`, `../data/models/weapons/`,
`./data/models/weapons/`. So the binary works whether run from repo
root or `build/`.

## Blender export settings

`File → Export → Wavefront (.obj)`, then:

- Forward Axis: **-Z**
- Up Axis: **Y**
- Apply Modifiers: **on**
- Export Materials: **on** (so the .mtl is written with `Kd`)
- Export Normals: **on**
- Export UVs: **off** (we have no textures)
- Triangulate Mesh: **on** (raylib triangulates anyway, but doing it
  here makes the OBJ deterministic)
- Selection Only: **on** (export one asset at a time)

Save the `.obj` and `.mtl` side-by-side. raylib reads the `mtllib`
line from the OBJ to find the MTL — don't rename one without the
other.

## What needs to be made — checklist

### Zombie (`data/models/zombie.obj`)

> **Superseded for the live enemy:** the in-game zombie now ships as a rigged,
> animated `data/models/zombie.glb` (authored **+Y-forward** per the glTF rule
> in [`../ANIMATIONS.md`](../ANIMATIONS.md), NOT the -Z-in-Blender OBJ rule
> below). The OBJ checklist here is kept for the static fallback and for the
> palette/scale conventions, which still hold.

- [ ] Mesh: blocky humanoid, ~1.7 m tall, fits in r=0.6 m cylinder
- [ ] Pose: arms forward at shoulder height, slight slouch, neutral
      stance (no walk cycle — that's procedural)
- [ ] Origin at feet, centred, on ground plane
- [ ] Faces -Z
- [ ] Geometry detail (per the bumped aesthetic): bevelled limb edges,
      jaw + brow ridge as separate sub-meshes, knuckled hands (1 cube
      per finger group), kneecaps, torn-shirt strips as poly strips,
      exposed bone/rib on damaged side
- [ ] Materials (4–6): skin (muddy green), shirt (dark grey/brown),
      trousers (darker), boots (near-black), blood accent, bone/teeth
      (pale yellow). All flat shading
- [ ] Tri count ~3000–5000 (was ≤1500 — bumped for higher fidelity)
- [ ] Export with settings above → `data/models/zombie.obj` + `.mtl`
- [ ] Wire into engine:
  - [ ] `assets.c`: add `Model zombieModel; bool zombieModelLoaded;`
        and load it in `Assets_Load`
  - [ ] `assets.h`: extern the above
  - [ ] `render.c:DrawEnemy`: if loaded, `DrawModelEx` at `e->pos`
        with yaw from `e->bobPhase`/movement; fall back to current
        cube+sphere when not loaded. Keep HP tint by overwriting the
        material's diffuse, or modulate via the `tint` param to
        `DrawModelEx`.
  - [ ] Verify boss/runner/crawler variants still scale visibly
        (apply the same `w *= …; h *= …;` multipliers as the cube
        path)

### Gun — Ray Gun (`data/models/weapons/raygun.obj`)

Why this gun: `W_RAYGUN` is the one weapon slot in `MODEL_BASENAMES[]`
that's still `NULL` (procedural fallback). Filling it completes the
viewmodel set.

- [ ] Mesh: sci-fi blaster silhouette — chunky barrel, exposed coils,
      curved grip. Distinct enough from the realistic guns to read as
      "special weapon" at a glance
- [ ] Long axis ~10 Blender units along **+Z**
- [ ] Origin at grip
- [ ] Geometry detail: bevelled receiver edges, ribbed barrel (3+
      cooling fins), iron sight notch geometry on top rib, vent slats
      as recessed inset faces near the rear, distinct energy-cell
      sub-mesh under the receiver, trigger guard loop modelled (not
      just a slab)
- [ ] Materials (5–7): body (dark metallic grey, flat), coil/accent
      (acid green to match raygun tint in code), grip (black), muzzle
      (very dark), accent stripe (yellow), vent (near-black), optic
      glass (cyan if you add a scope)
- [ ] Tri count ~2500–4000 (was ≤1500 — bumped for higher fidelity)
- [ ] Export → `data/models/weapons/raygun.obj` + `.mtl`
- [ ] Wire into engine:
  - [ ] `assets.c:MODEL_BASENAMES[W_RAYGUN] = "raygun.obj"`
  - [ ] `assets.c:weaponTune[W_RAYGUN]` — start at
        `{ .scale = 1.0f, .yawDeg = 0, .offset = {0,0,0} }`,
        tweak after a visual check

### Mystery Box (`data/models/mystery_box.obj`)

Replaces the procedural box in `render.c:DrawMysteryBox`. Existing
collision footprint is **1.8 × 1.0 × 1.2** m (W × H × D), with a flat
yellow lid plane drawn on top. Keep the same footprint so wallbuy /
PaP / obstacle spacing keeps working.

- [ ] Mesh: wooden crate, 1.8 W × 1.0 H × 1.2 D
- [ ] Origin at the **centre of the base** (matches `mbox.pos` which is
      the centre point used by `DrawCube`); equivalently, geometry
      spans Y = 0..1.0
- [ ] Plank detail: 4–6 horizontal plank ridges per face, modelled as
      inset/extruded seams (not just stripes of geometry on the
      surface) so light catches them
- [ ] Metal corner brackets on the four vertical edges (dark grey)
      with bolt-head cubes at top/bottom of each bracket
- [ ] **Hinged lid:** modelled as part of the same mesh, in CLOSED
      pose. Lid is a 1.8 × 0.1 × 1.2 slab sitting on top, slightly
      inset so the brackets read above it
- [ ] Question-mark decal **painted** as geometry: 2–3 small yellow
      cubes shaped into a `?` on the front face. Visual only — no UVs.
- [ ] Materials: crate (warm brown `{120, 80, 50}`), lid (mustard
      yellow `{240, 200, 80}` to match the existing lid plane),
      brackets (dark grey), accent (yellow for the `?`)
- [ ] Tri count ~2000–3500 (bumped — bevels + seam insets eat budget)
- [ ] Export → `data/models/mystery_box.obj` + `.mtl`
- [ ] Wire into engine:
  - [ ] `assets.{h,c}`: add `mboxModel` / `mboxModelLoaded` (same
        pattern as zombie)
  - [ ] `render.c:DrawMysteryBox`: if loaded, `DrawModelEx` at
        `mbox.pos` with a hover bob; tint shifts brighter when
        `state != MBOX_IDLE` (matches the procedural `crate` colour
        switch). Keep the weapon spinner draw on top untouched

### Sandbags (`data/models/sandbag_stack.obj`)

Static prop used as cover / chokepoint dressing. Authored as a
**short stack** unit (≈ 3 bags wide × 2 bags tall) so the level data
can place rows of them without engine code stacking individuals.

- [ ] Mesh: pile of pillow-shaped sandbags, ~1.5 W × 0.8 H × 0.6 D
- [ ] Each bag is a bevelled cuboid (2-segment chamfer minimum) with
      a slight central pinch so the rope-tied top reads — still
      flat-faceted, no subsurf
- [ ] Stagger the upper row by half a bag so the seam reads as bricks
- [ ] Origin at the **centre of the base** so positioning in level
      data matches the obstacle convention (`Box.center`)
- [ ] Faces **+Z** (long axis along world X if it matters — symmetric
      enough that either works)
- [ ] Materials: bag (faded tan `{180, 155, 105}`), stitch/rope detail
      (darker brown). Optional bullet-hole accents in a darker tan
- [ ] Tri count ~1500–2500 (bumped — 6 bevelled bags add up)
- [ ] Export → `data/models/sandbag_stack.obj` + `.mtl`
- [ ] Wire into engine:
  - [ ] Add a generic prop-model registry to `assets.{h,c}` (so
        future props don't each get bespoke globals) — keep the API
        simple: `Model Assets_Prop(const char *name)` returning
        `NULL` if not loaded
  - [ ] Add a way to place sandbags in `.map` files (e.g. a `prop
        sandbag_stack x z yaw` line in `data/maps/nacht.map`) parsed
        in `level.c`; render via `DrawModelEx` in `render.c`. Until
        the prop loader exists, leave this as a TODO comment

### Window barricades / boards (`data/models/board.obj`)

Replaces the orange plank slabs drawn in `render.c:DrawWindows`.
Each barricade in the existing system is **WINDOW_WIDTH (4.0) wide ×
0.25 tall × WALL_THICK+0.2 (1.2) deep**, lying horizontally across a
window opening. We need a **single board** model that the renderer
positions per-board at the correct Y stack.

- [ ] Mesh: one weathered wooden plank, **4.0 × 0.25 × 0.25** m
      (slim cross-section — the current 1.2 m depth is the slab
      drawn through the wall; the new model is a real plank that the
      renderer offsets in/out as needed)
- [ ] Origin at the geometric centre (matches how
      `DrawWindows` builds `planeCenter`)
- [ ] Long axis along **+X** (so per-window orientation can be
      flipped by 90° yaw when the window normal is along ±Z)
- [ ] Asymmetric end caps: one end has a couple of bent nails (small
      dark cubes), other end has a splintered notch — sells "ripped
      from a porch"
- [ ] Materials: wood (weathered tan `{150, 100, 50}`, matches
      current colour), nail (dark grey)
- [ ] Tri count ~600–1000 (bumped — bevels on a plank are cheap, and
      this is called many times per frame so don't go wild)
- [ ] Export → `data/models/board.obj` + `.mtl`
- [ ] Wire into engine:
  - [ ] `assets.{h,c}`: add `boardModel` / `boardModelLoaded`
  - [ ] `render.c:DrawWindows`: if loaded, replace the per-board
        `DrawCubeV`/`DrawCubeWiresV` pair with a `DrawModelEx` at
        `planeCenter`, rotating yaw based on `w->normal` so the
        plank aligns with the wall it's blocking. Keep the cube
        fallback when not loaded
  - [ ] Verify spacing still reads — current loop puts up to
        MAX_BOARDS_PER_WIN (5) planks evenly distributed up the
        window. The slimmer 0.25 m board should give visible gaps

## Acceptance test

1. `cmake --build build && ./build/shooter`
2. Spawn into Nacht, buy/cycle to the new gun → viewmodel sits in
   bottom-right, level, doesn't clip the camera, doesn't float away
   from the hand
3. Wait for zombies → they render as the new model at the right size,
   feet on the ground, facing the player they're chasing
4. Crawler/runner/boss variants still distinguishable
5. Mystery box reads as a wooden crate from across the room; bob and
   "?" decal are visible; weapon spinner still appears above it
   during MBOX_ROLLING / MBOX_WAITING
6. Window boards look like individual planks (not slabs) with
   visible gaps between them; the stack reads as "more boards = more
   repair done"
7. Sandbags (where placed) sit flat on the floor, don't z-fight, and
   the staggered seam reads from a few metres away
8. No console spam from `Assets_Load` ("model: … not found")

---

# Round 2 — replacing the remaining primitive draws with props

Survey of `src/render.c` showed everything in the list below is still
being drawn with `DrawCube` / `DrawSphere`. The engine has been
re-architected to be model-first: every render site below now tries
`propModels[PROP_*]` first and falls back to the existing cube draw
only when the model isn't loaded. So each new OBJ here drops in with
no code change once it's exported to the right path.

All Round-2 assets follow the same conventions as Round 1:

- **Author with Blender Z up, -Y forward, +X right.**
  Export with `Forward: Z`, `Up: Y` (verified empirically — see
  `assets.c:Assets_Load`). After export, the OBJ ends up with feet at
  `Y=0`, forward at `-Z`. Note: this *mirrors X* — Blender +X becomes
  raylib -X. Asymmetric details should be authored on the side you
  want to end up on the player-facing right in raylib, *then mirror in
  Blender*.
- **Origin convention is asset-specific** and noted in each section.
  Get this wrong and the engine's anchor offsets won't line up.
- **Materials via Principled BSDF → Base Color.** Plain
  `material.diffuse_color` does NOT make it into the MTL `Kd`. The
  Round-2 build helper in `data/models/build_zombie.py` (etc.) shows
  the right node setup.
- **`size` in the cube helper is the final edge length.**
  `primitive_cube_add(size=1)` already gives a unit cube, so the
  scale factor should be `(sx, sy, sz)` *without* a 0.5 multiplier
  (this was the original "exploded" bug — every part came out half
  size, leaving gaps).
- **All parts must overlap** so the joined object has no visible
  cracks. Each beveled cuboid is independent geometry; nothing welds
  unless they share space.
- **Tri budgets are targets, not hard caps.** Hero props (PaP, perk
  cabinets) can spend more; tile/floor props should stay tight since
  they're drawn many times per frame.

## Prop registry

Code-side, every Round-2 asset is registered by `PropId` in
`src/assets.h`. The loader walks `PROP_FILES[]`, tries each
prefix (`data/models/`, `../data/models/`, `./data/models/`), and
sets `propModelLoaded[id] = true` on success. Render sites read
`propModelLoaded[id]` to decide whether to use the model or the
fallback cube draw.

| `PropId`             | File                                  | Replaces |
|----------------------|---------------------------------------|----------|
| `PROP_ZOMBIE`        | `zombie.obj`                          | `DrawEnemy` cube+sphere fallback |
| `PROP_MYSTERY_BOX`   | `mystery_box.obj`                     | `DrawMysteryBox` crate cubes |
| `PROP_BOARD`         | `board.obj`                           | `DrawWindows` plank slabs |
| `PROP_SANDBAG_STACK` | `sandbag_stack.obj`                   | (placed via `PROP` map line — see §"Prop map syntax") |
| `PROP_DOOR`          | `door.obj`                            | `DrawDoors` body + plank ridges |
| `PROP_DOOR_FRAME`    | `door_frame.obj`                      | (new — drawn around every door, opened or not) |
| `PROP_OBSTACLE_CRATE`| `obstacle_crate.obj`                  | `DrawArena` obstacle cubes (default) |
| `PROP_OBSTACLE_BARREL`| `obstacle_barrel.obj`                | `DrawArena` obstacle cubes (when `PROP` line tags one as `barrel`) |
| `PROP_PERK_JUG`      | `perk_juggernog.obj`                  | `DrawPerkMachines` (`PERK_JUG`) cubes |
| `PROP_PERK_SPEED`    | `perk_speed_cola.obj`                 | `DrawPerkMachines` (`PERK_SPEED`) cubes |
| `PROP_PERK_DTAP`     | `perk_double_tap.obj`                 | `DrawPerkMachines` (`PERK_DTAP`) cubes |
| `PROP_PERK_STAMIN`   | `perk_staminup.obj`                   | `DrawPerkMachines` (`PERK_STAMIN`) cubes |
| `PROP_PAP`           | `pap_machine.obj`                     | `DrawPaP` lower+upper+top cubes |
| `PROP_WALLBUY_PANEL` | `wallbuy_panel.obj`                   | `DrawWallBuys` mount-plate cube |
| `PROP_POWERUP_DROP`  | `powerup_drop.obj`                    | `DrawPowerUps` cube+sphere |
| `PROP_WALL_PANEL`    | `wall_panel.obj`                      | `DrawArena` exterior wall segments (tiled) |
| `PROP_FLOOR_TILE`    | `floor_tile.obj`                      | `DrawArena` floor `DrawPlane` (tiled) |
| `PROP_PLAYER_M`      | `player_m.obj`                        | `DrawOtherPlayer` cube+sphere |

## Prop map syntax (new)

`data/maps/*.map` gets one new line type so authored props (sandbags,
barrels, debris, …) can be placed without code:

```
PROP <name> <x> <z> <yawDeg> [scale]
```

`<name>` matches a `PROP_*` model basename (without `.obj`). `level.c`
records these into a `MapProp` array and the renderer draws each one
via `DrawProp(name, pos, yaw, scale)`. Collision is per-prop: the
loader looks up a hand-tuned `Box` from a small `PROP_COLLIDERS[]`
table (e.g. `sandbag_stack` = `0.75 × 0.40 × 0.30`).

## Models to make

### Door (`data/models/door.obj`)

Replaces the brown cube + 3 plank ridges in `render.c:DrawDoors`.
Doors are purchasable barricades; they sit *inside* the gap between
two wall segments and disappear when bought open.

- **Footprint:** matches the door's collision `Box` from `.map` —
  varies, but the typical size is **1.6 W × 2.5 H × 0.20 D** for a
  long-axis-X door and the transpose for a long-axis-Z door. Author
  one canonical orientation: long axis along **+X**, then yaw 90°
  in the renderer when the door is along Z.
- **Origin:** geometric centre (matches `Box.center`).
- **Construction:** 5 vertical wood planks visible on each face,
  modelled as inset seams; 2 horizontal cross-braces (the "Z" diagonal
  is optional but reads well); 4 corner brackets with bolts; one
  central deadbolt/handle plate.
- **Materials (3):** `D_Wood` weathered brown `{120, 80, 50}`, `D_Iron`
  dark grey `{30, 30, 35}`, `D_Brass` deadbolt `{180, 140, 60}`.
- **Tri budget:** ~1500–2200.
- **Engine wiring (already in place):** `render.c:DrawDoors` calls
  `DrawProp(PROP_DOOR, b.center, yawForLongAxis, …)`.

### Door frame (`data/models/door_frame.obj`)

Sits in the wall gap whether or not the door is purchased — gives the
opening real chamfered wood/metal trim instead of a clean rectangular
hole.

- **Footprint:** **1.8 W × 2.7 H × 0.4 D**, with a 1.6 × 2.5 cutout in
  the middle. Author as a flat-front trim ring.
- **Origin:** centre of the cutout, on the floor (Y=0). The renderer
  drops it at the same XZ as the door's collision Box but with
  `pos.y = 0`.
- **Construction:** beveled wooden 2×6 trim on the two vertical sides
  and the top; small metal hinge plates baked into the inside of the
  trim (visible even with the door open).
- **Materials (2):** `D_Wood` (same as door), `D_Iron`.
- **Tri budget:** ~600–900.
- **Engine wiring:** `DrawDoors` draws the frame *before* the door so
  the door reads as sitting inside the trim.

### Obstacle crate (`data/models/obstacle_crate.obj`)

Generic "stuff in the way" prop — sized variably from
`.map`-authored `OBSTACLE` lines.

- **Canonical size:** **1.0 × 1.0 × 1.0**. Renderer scales to the
  obstacle Box size, so author at unit dimensions and bevel
  conservatively (uniform scaling will distort big bevels).
- **Origin:** centre of the cube.
- **Construction:** military supply crate — plank top + bottom, metal
  banding around the middle, 4 corner reinforcers, "AMMO" or similar
  stencilled as small extruded geometry on the front face.
- **Materials (3):** `O_Wood` `{120, 90, 70}`, `O_Iron` `{40, 40, 45}`,
  `O_Stencil` faded white `{210, 200, 180}`.
- **Tri budget:** ~1200–1800. Called many times per frame — don't
  over-bevel.
- **Engine wiring:** `DrawArena` obstacle loop calls
  `DrawProp(PROP_OBSTACLE_CRATE, c, 0, sizeAsScale, WHITE)`.

### Obstacle barrel (`data/models/obstacle_barrel.obj`)

For when the obstacle should read as a barrel instead. Selected via the
new `PROP` line so map authors can mix crates and barrels.

- **Canonical size:** **0.7 W × 1.1 H × 0.7 D** (cylinder approximated
  as a 10-sided faceted prism for the low-poly look).
- **Origin:** centre of the base, on Y=0.
- **Construction:** 10-sided body + top and bottom caps + 2 metal
  bands at 1/3 and 2/3 height + bunghole bump on top + skull stencil
  on side (extruded geometry, no decals).
- **Materials (3):** `O_Barrel` rust orange `{140, 70, 40}`,
  `O_BarrelBand` dark grey `{40, 40, 45}`, `O_Stencil` `{210, 200, 180}`.
- **Tri budget:** ~800–1400.
- **Engine wiring:** drawn when the `OBSTACLE` line is paired with a
  `PROP <id> barrel` tag (parsed in `level.c`), otherwise the crate
  is used.

### Perk machines — one per perk

Each is a CoD-style vending-cabinet ~1.0 W × 2.4 H × 0.8 D, lit-up
faceplate showing the perk logo. All four follow the same silhouette
so I can share most of the geometry; the **faceplate insets, accent
trim, and top sign differ per perk** to match the colours already in
`src/perks.c`.

Shared structure (author once, swap materials per variant):

- **Footprint:** **1.0 W × 2.4 H × 0.8 D**.
- **Origin:** centre of the base, on Y=0. (Existing code anchors at
  `pm->pos` with offsets Y=0.6 / 1.7 / 2.5 — that becomes a single
  `DrawProp(PROP_PERK_*, {pm->pos.x, 0, pm->pos.z}, yawToPlayer)`.)
- **Construction:** lower base cabinet (0.0–1.2), main body with
  recessed faceplate (1.2–2.0), tilted top sign with the perk logo
  extruded (2.0–2.4), dispenser slot below the faceplate, twin glowing
  side rails along the body.
- **Tri budget per cabinet:** ~2500–3500.

Per-perk variants:

| File | Body | Faceplate / Rails | Sign |
|------|------|-------------------|------|
| `perk_juggernog.obj`   | dark steel `{40,40,45}` | crimson `{220,40,40}` | "JUG" stencil yellow `{240,200,80}` |
| `perk_speed_cola.obj`  | dark steel              | leaf green `{40,180,80}` | "SPD" stencil white |
| `perk_double_tap.obj`  | dark steel              | gold `{240,180,40}` | "2x" stencil black |
| `perk_staminup.obj`    | dark steel              | sky blue `{60,140,220}` | "STM" stencil white |

`render.c:DrawPerkMachines` switches on `pm->perkIdx` to pick the
right `PROP_*` id.

### Pack-a-Punch (`data/models/pap_machine.obj`)

Replaces the `DrawPaP` cube stack. Different silhouette from the perk
cabinets — wider, taller, with a feed chute on top.

- **Footprint:** **2.5 W × 2.4 H × 2.5 D** (matches the existing
  `DrawCube` extents).
- **Origin:** centre of the base, on Y=0. (Existing code calls
  `DrawCube(pap.pos, …)` with `pos.y` already at the machine centre;
  the wiring needs `pos.y = 0` after switching to the model.)
- **Construction:** stepped pyramid silhouette — wide low base
  (0.0–0.6) with foot panels, body (0.6–1.4), top deck (1.4–1.9)
  with a hexagonal *upgrade chamber* (the "purple cube" in the
  current draw — keep the bob anim on the chamber), 4 antenna spikes
  at the corners, glowing rune trim along the body. Front side has
  a coin slot + nameplate "PaP" extruded.
- **Materials (4):** `P_Steel` `{25,25,30}`, `P_Trim` `{50,50,60}`,
  `P_Glow` purple `{80,50,130}`, `P_Accent` lavender `{200,150,255}`.
- **Tri budget:** ~3500–5000.
- **Engine wiring:** `DrawPaP` uses the model for the body; the
  upgrade chamber stays as a separate animated `DrawCube` (or moves
  to a second model `pap_chamber.obj` later — out of scope here).

### Wall-buy panel (`data/models/wallbuy_panel.obj`)

Replaces the coloured mount-plate cube in `DrawWallBuys`. The weapon
model still floats in front; this is just the chalk outline / hooks
behind it.

- **Footprint:** **1.6 W × 1.0 H × 0.10 D** (the existing slab depth).
- **Origin:** centre. Author with the *front face* on **-Y** in
  Blender so it ends up facing -Z in OBJ. Renderer rotates 90° yaw
  for X-facing walls (same as `DrawWallBuys` today).
- **Construction:** plywood backing plate, two metal hooks at the top,
  a chalk outline of a generic gun on the face (extruded geometry —
  white chalk material), a tear-off price tag in the corner.
- **Materials (3):** `WB_Wood` `{140,100,60}`, `WB_Iron` `{40,40,45}`,
  `WB_Chalk` faded white `{220,220,200}`.
- **Tri budget:** ~700–1200.
- **Engine wiring:** `DrawWallBuys` draws the panel behind the
  weapon, tinted to `w->tint` so each weapon type still reads
  colour-coded.

### Power-up drop (`data/models/powerup_drop.obj`)

Replaces `cube+sphere` in `DrawPowerUps`. Single mesh; the letter
inset varies per type — solve with **separate sub-meshes for each
letter** in one OBJ (all 5: A, N, 2x, X, C), and the renderer makes
visible only the slot matching `pu->type`. Simpler alternative: just
one model with a *blank* face, and `render.c` overlays the existing
letter using `DrawText3D` — pick whichever is easier when modelling.

- **Footprint:** **0.6 W × 0.6 H × 0.6 D**.
- **Origin:** centre.
- **Construction:** rounded cube with two horizontal trim bands and a
  glowing top dome; the front face is recessed 0.05 m so the
  per-type letter (extruded text geometry) sits inset; small floor
  ring under the cube reads as "this is dropping/floating".
- **Materials (3):** `PU_Body` neutral grey `{180,180,180}` (tinted
  per-type by the renderer), `PU_Letter` near-black `{20,20,20}`,
  `PU_Dome` white-emissive `{250,250,250}`.
- **Tri budget:** ~700–1100.
- **Engine wiring:** `DrawPowerUps` calls `DrawProp` with
  `tint = POWERUP_COLORS[pu->type]` so each drop reads as its
  category colour. The mesh stays the same shape for all types.

### Other-player avatar (`data/models/player_m.obj`)

Replaces the cube+sphere silhouette in `DrawOtherPlayer`. A coop
player should read as a *living* person at a distance, distinct from
zombies.

- **Footprint:** matches `0.55 W × 1.6 H × 0.55 D` (current cube +
  sphere bounds).
- **Origin:** feet, centred (same as zombie convention).
- **Construction:** stylised marine/soldier silhouette — boots,
  fatigues, vest with pouches, helmet with chin strap. Arms held at
  the sides (no weapon — the held weapon is drawn separately via
  `DrawWeaponDisplay` over the spine). One downed variant is
  expensive; instead, when a player is downed the renderer tilts the
  model 90° forward and lowers it.
- **Materials (5):** `PM_Fatigues` olive `{60, 75, 50}`, `PM_Vest`
  darker `{40, 50, 35}`, `PM_Skin` skin tone `{210, 175, 140}`,
  `PM_Boots` near-black, `PM_Helmet` matt grey `{60, 65, 55}`.
- **Tri budget:** ~2500–4000.
- **Engine wiring:** `DrawOtherPlayer` calls `DrawProp(PROP_PLAYER_M,
  feet, yawDeg, scale, PLAYER_COLORS[idx])`. The tint multiplies the
  fatigues colour so each coop player still reads as their team
  colour.

## Textures (walls + floor)

Walls and floors stay procedural geometry (one `DrawCube` per wall
segment, one quad per floor patch) — they're large, repeating, mostly
flat surfaces, and tiling 2 m OBJ panels across an arena would push
hundreds of identical draw calls and bloat VRAM. So the spec's
"no image maps" rule is **scoped to props only**. Walls and floors
get textures.

Each texture is registered by `TextureId` in `src/assets.h` and
loaded by `Assets_Load` from `data/textures/<name>.png`. Renderer
calls `SetTextureWrap(tex, TEXTURE_WRAP_REPEAT)` and bilinear filter
on load, then `DrawArena` / `DrawInteriorWalls` use a small rlgl
immediate-mode helper to emit textured cube faces with UVs scaled to
the box size so the texture tiles seamlessly along each surface.
Missing texture → flat-colour cube draw (current behaviour).

### Authoring rules

- **Seamless tile, square, power-of-two.** 512×512 or 1024×1024 PNG;
  edges must wrap without a visible seam. Test by tiling a 4×4 copy
  in your editor — any seam shows up.
- **Tile size convention: 2 m per texture repeat.** That's the
  `TILE_SIZE` constant the renderer divides surface dimensions by to
  compute UV repeats. A 12 m wall → texture repeats 6× horizontally;
  the arena floor (~40 × 40 m) repeats 20× per axis. Keep the
  internal detail readable at that scale — don't author for a 2 m
  texture that gets stretched.
- **Match the existing colour palette.** Walls were `{80, 90, 100}`
  grey, exterior arena walls a darker grey, floor `{55, 65, 75}`,
  outer ground `{30, 35, 40}`. Tint your texture so the *average*
  pixel sits near those values — that way levels read the same after
  the swap.
- **Stylised, painterly, not photoreal.** Match the prop aesthetic
  (low-poly + flat colour) — hand-painted brick/wood/concrete with
  visible brush strokes, no PBR maps, no specular highlights. Lit
  entirely by the diffuse tint.
- **No alpha.** Solid RGB only — these are opaque surfaces.

### Texture list

| `TextureId` | File | Replaces | Notes |
|-------------|------|----------|-------|
| `TEX_FLOOR` | `floor_concrete.png` | Arena floor `DrawPlane` | concrete slab w/ scattered cracks; avg colour ≈ `{55,65,75}` |
| `TEX_GROUND` | `ground_dirt.png` | Outer "outside the walls" `DrawPlane` | dirt + tufts of grass; avg colour ≈ `{30,35,40}` |
| `TEX_WALL_EXT` | `wall_brick.png` | Exterior arena walls (`DrawWallXSeg` / `DrawWallZSeg`) | weathered brick with chips/bullet pits; avg colour ≈ `{80,90,100}` (slight warm tint OK) |
| `TEX_WALL_INT` | `wall_plaster.png` | Interior dividing walls (`DrawInteriorWalls`) | cracked plaster with exposed lath here and there; avg colour ≈ `{120, 110, 95}` |
| `TEX_CEILING` | `ceiling_wood.png` | (not used yet — reserved for indoor maps) | dark beamed wood ceiling |

`TEX_CEILING` is registered now so the slot is reserved; the renderer
doesn't currently draw a ceiling on Nacht. Drop the others in
`data/textures/` and they take effect next launch.

### Engine wiring

1. `Assets_Load` walks `TEXTURE_FILES[]`, attempts each prefix
   (`data/textures/`, `../data/textures/`, `./data/textures/`),
   stores into `textures[TextureId]`, sets wrap + filter, and prints
   `texture: loaded …` or `texture: … not found, using flat colour`.
2. `Assets_Unload` calls `UnloadTexture` on every loaded entry.
3. `render.c:DrawTexturedBox(center, size, TextureId, tint, fallback)`
   emits 6 quads with UVs `(size.x/TILE_SIZE, size.y/TILE_SIZE)` etc.
   per face via `rlBegin(RL_QUADS)`; falls back to
   `DrawCubeV(center, size, fallback)` if the texture isn't loaded.
4. `DrawArena` floor → `DrawTexturedQuad(...)` (a flat plane variant
   of the helper); exterior walls →
   `DrawTexturedBox(c, size, TEX_WALL_EXT, WHITE, oldGrey)`.
5. `DrawInteriorWalls` → same call with `TEX_WALL_INT`.

### Acceptance test

1. Drop nothing in `data/textures/` → game still runs, floor/walls
   look exactly as before (flat colours).
2. Drop `floor_concrete.png` alone → just the floor changes; walls
   stay flat.
3. Drop all four → floor tiles concrete, exterior shows brick,
   interior shows plaster. No visible seam between adjacent wall
   segments (they share the texture, not a per-segment instance).
4. No image-related console errors at load.

### Texture checklist

- [x] `floor_concrete.png` (present, 1024²)
- [x] `ground_dirt.png` (present, 1024²)
- [x] `wall_brick.png` (present, 1024²)
- [x] `wall_plaster.png` (present, 1024²)
- [x] `ceiling_wood.png` (present, 1024²) — reserved, not yet drawn

## Shaders (`data/shaders/`)

The 3D pass now runs through one custom shader pair instead of raylib's
default. Sources are GLSL #version 330 (desktop core profile) and live
beside the rest of the data.

| File | Role |
|------|------|
| `world.vs` / `world.fs` | Texture × tint × vertex-colour + linear distance fog. Applied to every loaded Model (props + weapons) via `Assets_ApplyWorldShader` and swapped onto rlgl's default during `Render_World3D` so textured wall / floor draws fog the same way. |
| `sky.vs` / `sky.fs` | Procedural night skybox — horizon-to-zenith gradient, faint horizon glow, hash-based static stars. Rendered as a unit cube centred on the camera with depth writes disabled so it sits behind every world object. No texture asset required. |

Tuning knobs (`src/assets.h`):

- `fogStart` / `fogEnd` — distance in metres where fog begins / fully
  saturates. Defaults `10` / `55`.
- `fogColor` — RGBA; defaults to a near-black blue `{10,12,22,255}` so
  the fog blends with the skybox horizon.

Edit those globals from gameplay code (e.g. `Level_LoadFile`) to shift
the mood per-map. If either shader file is missing the loader prints
`shader: world.{vs,fs} not found, fog disabled` and the renderer falls
back to raylib's default flat shader with no fog and a plain
`ClearBackground` sky.

### Shader checklist

- [x] `world.vs` / `world.fs` — fog + tint
- [x] `sky.vs` / `sky.fs` — procedural night sky
- [ ] `world.fs` v2 — add a single directional sun/moon term for proper
      shading (currently every face gets uniform brightness via the
      MTL `Kd`)
- [ ] Decal shader — alpha-tested quad with depth-bias, for bullet
      impacts and blood splats

## Round-2 checklist

All Round-2 prop OBJs below are authored and present in `data/models/`, and the
`PROP` map line + per-prop collision are wired (`PROP_DEFS[]` in `level.c`).

- [x] `door.obj`
- [x] `door_frame.obj`
- [x] `obstacle_crate.obj`
- [x] `obstacle_barrel.obj`
- [x] `perk_juggernog.obj`
- [x] `perk_speed_cola.obj`
- [x] `perk_double_tap.obj`
- [x] `perk_staminup.obj`
- [x] `pap_machine.obj`
- [x] `wallbuy_panel.obj`
- [x] `powerup_drop.obj`
- [x] `player_m.obj`
- [x] Parse `PROP <name> x z yawDeg [scale]` in `level.c` (`MapProp`)
- [x] `PROP_DEFS[]` table in `level.c` for prop collision boxes

## Round-2 acceptance test

1. `cmake --build build && ./build/shooter` — no `model: … not found`
   spam for any registered prop.
2. Spawn into Nacht — interior should read as a brick room with
   stone-tile floor (if `wall_panel`/`floor_tile` are built);
   otherwise the old flat planes still draw.
3. Doors look like wooden barricades with metal bracing, not just
   brown slabs; the frame stays visible after they open.
4. Each perk machine reads as a CoD-style cabinet with the right
   colour faceplate and a stencilled tag.
5. PaP pulses purple and the chamber animation still works.
6. Power-up drops show the correct letter inset and read as their
   category colour from across the room.
7. Other players (coop) look like soldiers, not cubes.
8. `data/maps/nacht.map` can place a `PROP sandbag_stack -8 12 90`
   and it spawns with collision.
