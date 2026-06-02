---
name: blender-game-asset
description: This skill should be used when the user wants to create, model, rig, or animate a 3D game asset in Blender for this project (a zombie/enemy, a weapon/viewmodel, a perk machine, a prop, a character, etc.), or asks to "make a model", "build an asset", "rig this", "author a glb", or export a model for the game. It enforces a strict rigging-first workflow, drives Blender through the blender MCP, and mandates a connectivity/integrity audit so no asset ships with parts that float free of the main body.
---

# Blender game-asset creation (rigging-first)

Authoring pipeline for this project's 3D assets. Assets are built **for
animation from the start** and verified before export, then validated in-engine.

Blender is driven through the `mcp__blender__*` tools (primarily
`execute_blender_code` and `get_scene_info`). The game loads animated assets
as glTF `.glb` via the skeletal pipeline in `src/anim.{c,h}`.

## Read the spec first

Before modelling anything, read the project's authoring specs and follow them —
they own the art direction, scale, axes, palette, and the exact animation
clip list per asset:

- `data/models/ASSETS.md` — static look, scale/axis/origin conventions, palette, tri budget.
- `data/ANIMATIONS.md` — rigging-first mandate, per-asset skeletons + required clip names, gun action/blowback rules.

This skill is the **how**; those docs are the **what**. If they conflict, the docs win.

## Non-negotiables

1. **Rigging-first.** Never model a static mesh and rig it afterward. Plan the
   skeleton, then build geometry to deform.
2. **Everything connects to the body.** No floating/orphan parts. This is
   audited by a script before every export (see "Integrity audit" — it is
   mandatory and must PASS).
3. **Complexity matches the spec.** Game assets, not placeholders — hit the
   tri budget and detail level in `ASSETS.md`. As complex as the asset needs.
4. **Validate in-engine** with `--anim-test` before declaring done.

## Workflow

Do these in order. Each step is small `execute_blender_code` chunks (the MCP
prefers small steps; break model build into logical pieces and check
`get_scene_info` between major stages).

### 1. Plan the rig (before geometry)
- Decide the **skeleton**: bone names + hierarchy, matching `data/ANIMATIONS.md`
  for this asset type (shared skeleton across a family — all zombie types, all
  weapon viewmodels via shared arms). Bone names are the engine API.
- Decide the **part breakdown**: which pieces move as rigid units and become
  separate mesh islands bound to their own bone (gun slide/bolt/charging
  handle/mag; box lid; perk dispenser flap), vs. which areas flex and need
  smooth weights (limbs, jaw).
- Decide the **rest pose**: characters in **T- or A-pose**; weapons with the
  action/charging handle in rest position.

### 2. Model rig-first
- Build in the rest pose, not a display pose.
- Put **edge loops at every joint that bends** (shoulders, elbows, wrists,
  hips, knees, jaw; slide rails, hinge, trigger). A single cuboid limb cannot
  bend — give moving parts their own loops.
- Match scale/axis/origin from `ASSETS.md` (1 Blender m = 1 world unit, -Z
  forward, origin at feet/grip). Set materials via `Principled BSDF → Base
  Color` (flat colour, one material per zone, no textures).
- **Connectivity while modelling:** each logical part is ONE connected mesh.
  Use mesh editing (extrude/bridge/merge-by-distance) rather than dropping
  loose primitives next to each other. After booleans or duplications, merge
  doubles and delete loose geometry. Movable parts are *separate objects*, each
  itself fully connected, positioned **touching/overlapping** the body.

### 3. Rig + skin
- Add the armature with the planned bones.
- Bind: smooth automatic weights (`parent_set(type='ARMATURE_AUTO')`) for
  flexing meshes; rigid binding (each part object 100% weighted to one bone, or
  bone-parented) for the blocky/mechanical parts.
- Every vertex must have a non-zero total weight. Every mesh object must be
  bound to the armature.

### 4. Integrity audit (MANDATORY — must PASS before export)
Run the bundled auditor inside Blender. From the skill directory, read
`scripts/check_asset.py` and send its contents to `execute_blender_code`
(optionally append `check_asset(require_rig=True)` for rigged assets, or
`require_rig=False` for static props). Read the printed report.

The audit FAILS on, and you must FIX, then re-run until it PASSES:
- **Disconnected geometry** — any mesh object made of more than one connected
  island (the classic "part floating free of the body"). Each object = exactly
  one island.
- **Loose verts/edges** — vertices/edges not part of any face.
- **Floating objects** — a separate part whose geometry is spatially isolated
  (a clear gap from every other object); real parts touch or overlap the body.
- **Unweighted vertices** (rigged) — verts with zero total bone weight.
- **Unparented mesh** (rigged) — a mesh object not bound to the armature.

WARN (review, usually fix): non-manifold edges, tri count far from the
`ASSETS.md` budget, ungrouped/again-default material names.

Never export an asset that has not passed this audit.

### 5. Animate (if the asset needs clips)
- Author the exact clip names listed for this asset in `data/ANIMATIONS.md`
  (e.g. zombie `walk`/`attack_a`/`death`; gun `idle`/`fire`/`reload`/
  `reload_empty`/`raise`). Match clip durations to the gameplay constants noted
  there (reload times, fire cooldowns, lunge/fuse timers).
- **No root motion** — animate in place around the origin; code owns world
  translation. Locomotion loops seamlessly; one-shots (reload/fire/death) read
  cleanly at 1× (some replay at 2× for Speed Cola — don't bake easing that
  breaks when scaled).
- Guns: the `fire` clip cycles the action (slide/bolt blowback) on a dedicated
  bone where mechanically real; `reload_empty` ends by racking the charging
  handle / releasing the slide. See the blowback table in `ANIMATIONS.md`.
- Name the actions to match the clip names; set scene frame range per clip.

### 6. Export glTF
```python
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.gltf(
    filepath="/home/jude/c/shooter/data/models/<name>.glb",
    export_format='GLB', export_animations=True, export_skins=True,
    export_yup=True, use_selection=False)
```
Static props can stay OBJ per `ASSETS.md`; anything rigged is `.glb`.

### 7. Validate in-engine
```
cd /home/jude/c/shooter
./build/shooter --anim-test <name>.glb [clip]        # writes anim_test_0..3.png
```
Read the PNGs to confirm it loads, is lit, and **deforms** across the clip
(not just translates). Add `--live` for a spinning interactive viewer. Confirm
the load log shows the expected mesh/material/bone/clip counts and no
"skeleton does not match" warning.

## Notes
- Build models in small `execute_blender_code` chunks; call `get_scene_info`
  to confirm object/material counts between stages.
- raylib's OBJ loader SEGVs on n-gons >20 verts — irrelevant for `.glb`
  (different loader), but if you ever export OBJ, triangulate first.
- The integrity audit is the safeguard against the recurring "generated asset
  has parts not connected to the main body" problem. Treat a non-PASS as a
  hard block.
