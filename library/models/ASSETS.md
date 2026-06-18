# Asset spec — Blender → raylib

Conventions for hand-authored models the game loads at runtime. Fixes the
aesthetic, scale/axis/origin rules, and the registry/formats so new assets line
up without per-model fudge factors. Anything that needs a **skeleton + animation
clips** is in [`../ANIMATIONS.md`](../ANIMATIONS.md) — start there; this doc is
the static-mesh + conventions reference. (All Round-1/Round-2 props and the 5
textures are authored and wired; this is the spec to follow when adding more.)

## Engine facts that dictate everything

- **Renderer:** raylib 5.5; OBJ via `LoadModel`, rigged glTF via the anim path.
- **Units:** 1 Blender metre = 1 world unit = 1 raylib unit.
- **Axes (OBJ):** raylib is Y-up, right-handed, **-Z is forward**.
- **Origin:** at the model's **feet/grip**, not its centre (so floor/hand
  anchoring is fudge-free).
- **Rigging-first:** build models *for animation from the start* (neutral
  T/A-pose; guns with slide/bolt/charging-handle/mag as separate movable parts;
  edge loops at every joint that bends). The scale/axis/origin/palette rules here
  still apply; rig-first changes topology and pose, not the art direction. Only
  genuinely inert props (crates, barrels, sandbags, boards) stay static OBJ.

## Aesthetic

Blocky but past pure cubes — readable detail at 5–10 m.

- **Stylised low-poly:** ~3000–5000 tris/asset. Bevel visible hard edges
  (1–2 segment chamfer); add secondary shapes, don't leave limbs as single cuboids.
- **Shade Flat globally** — get "rounded" from geometry, not smoothing.
- **Flat colour, no textures (props).** One material per part; RGB via
  `Principled BSDF → Base Color` (the OBJ/MTL exporter emits `Kd`, which raylib
  reads as diffuse). No image maps / PBR. **Walls & floors are the exception —
  they get tiled textures (see Textures below).**
- **4–8 material zones per asset** so detail reads via colour breaks.
- **Palette:** desaturated, muddy — match in-game greens (`{80,140,60}`) and
  muted wood/metal prop tones. Bright tints are reserved for power-ups / FX.
- **Not crossing:** photoreal textures, subsurf, PBR maps. Stay "stylised cubes
  with character."

## Scale & facing conventions

- **Zombie:** 1.7 m tall (`ENEMY_HEIGHT`), inside r=0.6 m (`ENEMY_RADIUS`);
  origin at the feet on the ground plane. (The live enemy is the rigged
  `zombie.glb`, authored **+Y-forward** per the glTF rule in `ANIMATIONS.md`.)
- **Guns (world draws — wall-buy / Mystery Box / PaP):** OBJ long axis 9–12
  Blender units along **+Z**, up **+Y**, origin at the grip. `model_scale` in the
  `.weapon` file tunes on-screen size. (`pistol.obj` is authored +X-long — the
  outlier; don't repeat that.) **First-person viewmodels are now combined
  per-weapon rigs** (`data/weapons/<id>/<id>_vm.glb`), not gun-only OBJs — see
  `ANIMATIONS.md` and `docs/arms-rig-generalisation.md` §0.

## Blender OBJ export + axis gotchas

`File → Export → Wavefront (.obj)`: Forward **-Z**, Up **Y**, Apply Modifiers on,
Export Materials on (writes `Kd`), Normals on, UVs off, Triangulate on, Selection
Only on. Save `.obj` + `.mtl` side-by-side (raylib reads `mtllib`; don't rename one).

Hard-won gotchas — re-hitting these wastes hours:

- **Round-2 props were authored Blender Z-up, -Y forward, exported `Forward: Z`**
  (verified empirically), giving feet at `Y=0`, forward `-Z`. This **mirrors X**
  (Blender +X → raylib -X) — author asymmetric detail on the side you want, then
  mirror.
- **`primitive_cube_add(size=1)` is already a unit cube** — scale by
  `(sx,sy,sz)` with **no 0.5 multiplier** (the original "exploded/half-size" bug).
- **Materials must use `Principled BSDF → Base Color`** — plain
  `material.diffuse_color` does NOT reach the MTL `Kd`.
- **All parts must overlap** so the joined mesh has no cracks (nothing welds
  unless geometry shares space). For rigged models, each separate part stays ONE
  island bound to one bone (don't `join` — multi-island = audit FAIL).
- Loader probes `data/X`, `../data/X`, `./data/X`, so the binary runs from repo
  root or `build/`.

## Prop registry

Every prop is a `PropId` in `src/game/assets.h`; the loader walks `PROP_FILES[]`,
probes the prefixes, sets `propModelLoaded[id]`. Render sites use the model if
loaded, else a fallback cube draw — so a missing OBJ never breaks the build.

| `PropId` | File | Used by |
|----------|------|---------|
| `PROP_ZOMBIE` | `zombie.obj` | `DrawEnemy` fallback (live enemy uses `zombie.glb`) |
| `PROP_MYSTERY_BOX` | `mystery_box.obj` | `DrawMysteryBox` |
| `PROP_BOARD` | `board.obj` | `DrawWindows` |
| `PROP_SANDBAG_STACK` | `sandbag_stack.obj` | `PROP` map line |
| `PROP_DOOR` / `PROP_DOOR_FRAME` | `door.obj` / `door_frame.obj` | `DrawDoors` |
| `PROP_OBSTACLE_CRATE` / `PROP_OBSTACLE_BARREL` | `obstacle_crate.obj` / `obstacle_barrel.obj` | `DrawArena` obstacles |
| `PROP_PERK_{JUG,SPEED,DTAP,STAMIN}` | `perk_*.obj` | `DrawPerkMachines` |
| `PROP_PAP` | `pap_machine.obj` | `DrawPaP` |
| `PROP_WALLBUY_PANEL` | `wallbuy_panel.obj` | `DrawWallBuys` |
| `PROP_POWERUP_DROP` | `powerup_drop.obj` | `DrawPowerUps` |
| `PROP_PLAYER_M` | `player_m.obj` | `DrawOtherPlayer` fallback (live uses `player.glb`) |

Footprint/material details for each live in the built `.obj`/`.mtl`; match an
existing sibling when authoring a variant (e.g. per-type zombie OBJs). Conventions:
crate/obstacle origin = centre; cabinets/PaP/perks origin = base centre at Y=0;
characters/wallbuy = as noted above.

### Prop map syntax

```
PROP <name> <x> <z> <yawDeg> [scale]
```
`<name>` = a prop basename (no `.obj`). `level.c` records a `MapProp`; collision
comes from `PROP_DEFS[]` (per-prop `Box`). Drawn via `DrawProp`.

## Textures (walls + floor)

Walls/floors stay procedural geometry with **tiled textures** (the "no image
maps" rule is props-only). Registered by `TextureId` in `assets.h`, loaded from
`data/textures/<name>.png` with `TEXTURE_WRAP_REPEAT` + mipmaps; `DrawTexturedBox`/
`DrawTexturedFloor` emit UVs scaled by `size / TILE_SIZE`. Missing texture →
flat-colour fallback.

Authoring: **seamless, square, power-of-two** (1024² ships). **`TILE_SIZE` is the
metres-per-repeat** the renderer divides by. Match the palette so the average
pixel sits near the old flat colour. Stylised/painterly, no alpha, no PBR.

| `TextureId` | File | avg colour |
|-------------|------|-----------|
| `TEX_FLOOR` | `floor_concrete.png` | `{55,65,75}` |
| `TEX_GROUND` | `ground_dirt.png` | `{30,35,40}` |
| `TEX_WALL_EXT` | `wall_brick.png` | `{80,90,100}` |
| `TEX_WALL_INT` | `wall_plaster.png` | `{120,110,95}` |
| `TEX_CEILING` | `ceiling_wood.png` | reserved (no map draws a ceiling yet) |

## Shaders (`data/shaders/`)

GLSL `#version 330`. (Engine-side ownership of the shader handles/uniforms is in
`src/engine/eng_render.c`; see `docs/engine-usage.md`.)

| File | Role |
|------|------|
| `world.{vs,fs}` | texture × tint × vertex-colour + directional sun + linear distance fog; applied to every model + rlgl world draws |
| `world_skinned.vs` | GPU-skinning variant sharing `world.fs` (rigged glTF) |
| `sky.{vs,fs}` | procedural night sky (gradient + horizon glow + hash stars); no texture |
| `postfx.fs` | fullscreen bloom + vignette + hit-flash + low-HP pulse |

Fog tuning (game-side globals, pushed via `Eng_RenderSetLighting`): `fogStart`/
`fogEnd` (default 10/55), `fogColor` (near-black blue). Shift per-map from
`Level_*`. Missing shader → raylib default flat shader, no fog.

Open shader work (see `TODO.md`): per-map `LIGHTS`, `sky_tint` uniform hookup.
