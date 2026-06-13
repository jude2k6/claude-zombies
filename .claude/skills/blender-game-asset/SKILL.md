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
4. **Anatomically / mechanically correct.** Every joint must bend the way the
   real thing does — **knees flex backward** (calf swings toward the back of
   the thigh, NOT forward), **elbows** the matching way for arms, ankles,
   jaws, and any mechanical hinge in its real direction. A backwards knee is a
   hard defect, not a style choice. **You cannot eyeball the rotation sign — it
   depends on each bone's local axes/roll.** Calibrate it (see "Joint-direction
   calibration" below) before authoring any clip, and confirm the bent joints
   in the rendered frames. This applies to *all* models; if you touch an
   existing rig (even just re-exporting), re-verify its joints too.
5. **Validate in-engine** with `--anim-test` before declaring done.

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

**FIRST — joint-direction calibration (do this before keying any clip).**
A bone's local axes/roll decide which way a positive rotation bends it, and it
is NOT guessable — author a backwards knee and you'll ship one. So calibrate
each flexing joint empirically:
- Clear the pose. Apply a single test rotation (e.g. `shin.L` `+55°` about local
  X), render a **side view**, then the opposite sign (`-55°`), render again.
  Note which way `+Y` (the model's forward) points in your camera.
- Read off the anatomically correct sign: a **knee** is correct when flexing
  swings the foot/calf **backward (toward −Y) and up** (toward the back of the
  thigh) — never forward. Do the same for elbows (forearm folds toward the
  upper arm), ankles, jaw, and any mechanical hinge.
- Author every clip with the calibrated signs, and **look at the bent joints in
  the rendered frames** to confirm. If a collapse/sit/kneel pose was built on a
  wrong-way fold, fixing the joint may change the whole pose — re-tune it (and
  re-check floor contact / min-Z) rather than leaving the joint wrong.
- Calibration is per-rig: a different model can have different bone roll. Don't
  copy signs blind from another asset without a quick re-check.

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

## Techniques that worked here (learned authoring the zombie)
- **Character body via a Skin-modifier stick figure.** A graph of joint
  verts + edges with a `SKIN` modifier (per-vert radius), then apply +
  Subdivision (`SIMPLE`) gives ONE watertight connected island for the whole
  torso/limbs — automatic weights then deform it cleanly and the connectivity
  audit passes for free. Tune radii for proportion (waist taper, shoulder
  bulk); subdiv level 1 reads crisper/blockier, level 2 smoother.
- **Model faces/detail as real geometry, NOT skin blobs.** A skin-modifier
  head comes out a featureless wedge. Build the head and its features (brow,
  cheekbones, dark eye sockets + glowing eyes, dark maw + tooth bars, chin) as
  **separate beveled box objects** that *overlap* the skull, each a single
  island, each rigidly bound to its bone (Armature modifier + one full-weight
  vertex group — `head`, or `jaw` for the lower jaw/teeth so they drop on a
  bite). Separate overlapping parts are fine for the audit (gap 0); never
  `join` them (that makes a multi-island object = FAIL).
- **Facing: characters face +Y in Blender.** `export_yup=True` maps Blender
  +Y → raylib -Z (forward). Authoring -Y gives a backwards model in-game. This
  is the OPPOSITE of the OBJ rule in `ASSETS.md`; see `ANIMATIONS.md` facing
  note. anim-test's fixed camera views the back of a correctly-facing model.
- **Emissive accents** (zombie eyes, raygun coils): set Principled
  `Emission Color` + `Emission Strength` on that part's material.
- **First-person weapons: bake a COMBINED per-weapon rig.** ⚠️ The old
  "shared `arms_vm.glb` + gun bolted to `hand.R` at runtime" approach is
  **superseded** (see `docs/arms-rig-generalisation.md` §0). It can't do moving
  gun parts (racking charging handle, slide blowback, mag swap) or per-gun
  animation, and it caused a long-running hand-placement bug. New guns are ONE
  rigged `.glb` = **arms + hands + gun + mechanism part-bones**, hands posed ON
  the gun in the rig (no runtime seating). The bone-bolt trick still works for
  other "object mounted on a skeleton" cases, just not weapons.

- **Combined per-weapon viewmodel rig — proven recipe (MP5 `smg_vm.glb`,
  2026-06-13).** Author guns 2–5 the same way:
  - **Skeleton (9 bones for the MP5 archetype):** reuse the canonical arm chain
    `root, upperarm.L/R, forearm.L/R, hand.L/R`, plus per-gun mechanism bones as
    children of `root` (MP5: `bolt` = charging handle/reciprocating bolt,
    `magazine`). The engine binds by **bone name**, so keep these names.
  - **Part breakdown:** each mechanism part is its OWN single-island mesh,
    rigidly bound 100% to its bone (so it keys independently). Forearms
    gradient-blend `forearm.*`↔`hand.*` for a clean elbow/wrist; the rest is
    rigid. **Hit the `ASSETS.md` ~3–5k tri budget — do NOT ship a featureless
    block.** The MP5 v1 was rejected for being ~64 verts; v2 that passed was
    **40 single-island objects ≈ 3,984 tris** (receiver, ribbed handguard, A3
    tube stock, charging-handle tube, hooded front + drum rear sights, real
    trigger-guard loop, curved mag, bevelled edges). Build the silhouette from
    many separate overlapping single-island parts, each rigidly bound — that's
    how you get detail AND pass the per-object "1 island" audit.
  - **Facing / scale / ORIGIN (this is the contract the engine draws against —
    get it right or the viewmodel is invisible/mis-framed):** author facing
    **+Y in Blender, export `export_yup=True`** (same as zombie/player; → in
    raylib the barrel is **−Z**, up **+Y**, right **+X**). **Real metric** (MP5
    body ≈0.64 m long). **Put the rig origin at the EYE/camera point** and build
    the whole assembly (arms + gun) extending **forward (+Y) and slightly DOWN**
    from origin. Target model-space AABB roughly `x[-0.13,0.13] y[-0.30,0]
    z[-0.68,0.05]` — i.e. geometry hangs *below and in front of* origin. The
    engine anchors that origin near the camera and applies shared `CRIG_*`
    framing offsets, so DON'T bury the gun at the origin or it clips the lens.
  - **Clips (lowercase = engine API):** `idle`, `fire` (mechanism reciprocates +
    `root` recoil kick), `reload` (mag swap, **no** rack), `reload_empty` (mag
    swap THEN the rack — e.g. `bolt` −12 mm open → −32 mm yank → 0 release),
    `raise`, `lower`, `sprint`, `inspect` (optional). Match durations to the
    `.weapon` gameplay constants (reload_time, fire_cooldown).
  - **Validate:** integrity audit PASS → export to
    `data/weapons/<id>/<id>_vm.glb` → `./build/shooter --anim-test
    data/weapons/<id>/<id>_vm.glb` logs `(N mesh, M mat, 9 bones, 8 clips)` and
    must deform with no "skeleton does not match". The engine auto-discovers the
    file (`Viewmodel_LoadCombinedRigs`) — **no per-gun engine code**, framing is
    the shared `CRIG_*` constants in `src/viewmodel.c`.
  - **Gotchas hit:** (1) `bpy.ops.wm.read_factory_settings` WIPES the live scene
    — export the `.glb` BEFORE any round-trip/reset checks. (2) Don't mix yup
    conventions: combined rigs use `export_yup=True` (the legacy `arms_vm.glb`
    used False). (3) Boolean ops leave odd mesh-data names (`_mag1_data`) — fine,
    the engine binds by node/bone name, not mesh-data name.
