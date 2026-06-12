# Handoff — Claude Zombies

For the next Claude agent picking up this project. Read this first, then
`README.md` for player-facing context, then `TODO.md` for what's next.

## What this is

A 3D round-based zombies shooter in C using **raylib 5.5**, **raygui 4.0**,
and **enet 1.3.18** (all via CMake `FetchContent`). Host-authoritative
4-player coop over UDP. Cross-platform: Linux (X11), macOS, Windows
(MSVC + MinGW).

Repo: `git@github.com:jude2k6/claude-zombies.git` · branch: `main`.

## Current state (2026-06-12)

Architecture-cleanup pass (2026-06-12, commits 772606f–c23bab9):

- **Dead pistol-glb path removed** (772606f): `DrawPistolViewmodelGLB`, the
  `pistolVM` statics, and `Render_{Load,Unload}PistolVM` deleted from
  render.c/render.h/main.c. `data/weapons/pistol/pistol_vm.glb` stays on disk
  (authored content); `W_PISTOL` uses the gun-only procedural OBJ path.
- **New TU `src/viewmodel.{c,h}`** (491b5c1, 352 lines): first-person viewmodel
  system carved out of render.c — shared arms VM, `Viewmodel_LoadArms`/
  `Viewmodel_UnloadArms` (renamed from `Render_Load/UnloadArmsVM`),
  `ViewmodelMotion`, `GunGrip`/`gunGrip[]`, `DrawArmsViewmodel`, and
  `Viewmodel_DrawFirstPerson(Camera)` (was `DrawFirstPersonViewmodel`). render.c
  is now 1116 lines.
- **New TU `src/devtools.{c,h}`** (7d56ffb, 363 lines): all five CLI debug modes
  (`--validate`, `--screenshot-viewmodels`, `--screenshot-coop`,
  `--screenshot-pap`, `--anim-test`) moved out of main.c behind
  `Devtools_HandleCLI`. main.c is now 431 lines.
- **Build flags** (a0468ba): `-Wall -Wextra -Wno-unused-parameter` now on for
  GCC/Clang; new `option(SHOOTER_ASAN OFF)` adds `-fsanitize=address,undefined`.
  A few pre-existing `-Wstringop-truncation` warnings from `strncpy` in
  net.c/level.c/settings.c/main.c remain harmless and aren't yet addressed.
- **All warnings fixed** (b1c6d7b): fx.c/hud.c indentation clamps, audio.c
  sign-compare + unused var, weapons.c uses a local `xstrdup` (strict-C11 safe).
- **Scoring aligned** (c23bab9): `KILL_POINTS` 50 → 60 in types.h; body kill =
  60, headshot kill = 100 (existing +40 bonus). The documented 10/60/100/130 CoD
  scoring is now true in code.
- **Legacy assets deleted** (c23bab9): `data/models/weapons/{revolver,sniper}.
  {obj,mtl}` — referenced nowhere.

Pack-a-Punch pass (2026-06-04, commits 3619151 and 39dc856):

- **CoD-style PaP machine** (`data/models/pap_machine.obj`): full-sized cabinet
  authored in Blender — lit chamber baked in, procedural shutter animation and
  spark/particle effects from `render.c`, `--screenshot-pap` dev mode added.
- **Insert-weapon workflow** (3619151): player walks up and inserts the weapon;
  machine "works" it (PaP upgrade applied), then player manually retrieves from
  the chamber. Replaces the old instant-purchase interaction.

Prior pass (2026-06-03) — committed as 6c794a7 and 6aa759d:

**Movement/viewmodel pass** — `W_PISTOL` takes the **gun-only floating OBJ
path** in `Viewmodel_DrawFirstPerson` (the arms path is gated `wi != W_PISTOL`).
`ViewmodelMotion(me, fwd, right, up, &pos, &tilt)` adds procedural **walk-bob**
(sinusoidal, amplitude scales with the new `Player.moveBlend`) and a **sprint
sink + muzzle-down tilt + high-freq shake** (scales with `sprintBlend`, settled
by ADS); applied to both the gun-only path (pos+tilt) and the shared arms path
(pos only, on top of its sprint clip). `Player.moveBlend` (new, local-only,
eased like `sprintBlend`, set in `Player_ApplyLocalMove`) also **opens the
crosshair** (`hud.c` bloom) and **degrades hip-fire accuracy**
(`weapons.c:Weapon_Fire` adds `moveBlend*1.2 + sprintBlend*3.5` deg of spread,
×0.25 in ADS). MP caveat: moveBlend/sprintBlend are local-only. The pistol
viewmodel is framed entirely from `pistol.weapon` (`model_scale 35`,
`model_yaw 28`, `model_offset 0.05 0.09 0.0`).

**raylib OBJ loader fixed at the root (2026-06-03).** Adding that `model_offset`
line used to crash the OBJ loader at boot. The cause was NOT model_offset — it
was two latent bugs in raylib 5.5's `LoadOBJ`, and parsing one extra `.weapon`
line just shifted the heap enough to detonate them. Both are now patched durably
via **`cmake/patch_raylib_obj.cmake`**, wired as a `PATCH_COMMAND` on the raylib
`FetchContent_Declare` (idempotent; cross-platform; runs on fresh fetches). The
two bugs (found with an ASan build):
  1. **tinyobj face-buffer overflow (the actual crash).** `parseLine` reads an
     `f` line's verts into a fixed `f[16]` stack array with no bounds check,
     *before* triangulating. `pistol.obj` (20-vert faces) and `shotgun.obj`
     (24-vert cap n-gons) overflow it. Fix: raise `TINYOBJ_MAX_FACES_PER_F_LINE`
     16 → 256. This means **the "always export triangulated" rule is no longer a
     crash-safety requirement** (still fine to triangulate, but un-triangulated
     n-gons up to ~85 verts now load + triangulate cleanly).
  2. **UV-less texcoord/normal read.** `LoadOBJ` reads
     `objAttributes.texcoords[vt_idx*2]` with no `vt` data (vt_idx = 0x80000000
     → wild index). All our OBJs are UV-less. Fix: guard the read, zero-fill
     when absent. So **don't add UVs to "fix" anything, and ASan can now be
     enabled** (this was the blocker noted in the old gotcha).
If you bump the raylib `GIT_TAG`, re-check the patch still matches (the script
fails loudly with a clear message if `LoadOBJ`/tinyobj sources moved).

Prior sub-pass (2026-06-03) — now committed (6c794a7):

- **Shared first-person ARMS viewmodel for the other 4 guns (`data/models/arms_vm.glb`).**
  One rigged pair of arms+hands on the viewmodel skeleton with the full clip set
  (`idle`/`fire`/`reload`/`reload_empty`/`raise`/`lower`/`sprint`); the equipped
  gun is a **separate model bolted onto the `hand.R` bone each frame**, so a
  future character skin only retextures this *one* arms model instead of baking
  a combined arms+gun glb per weapon. `viewmodel.c:DrawArmsViewmodel(camera, wi)`
  drives it: same player-state→clip state machine as the pistol VM (raise on
  swap, fire on shot, reload/reload_empty by mag-empty latch, sprint, idle),
  posed via `Anim_Pose`, then the arms are drawn with a camera-space root matrix
  (model `X→right, Y→camFwd, Z→camUp`) and the gun is drawn at
  `root * handBone * gunLocal` using the **new `Anim_FindBone` + `Anim_BoneMatrix`**
  helpers (anim.{c,h}) — so recoil/reload hand motion carries the gun for free.
  Arms use `worldSkinnedShader`, the gun keeps its OBJ `worldShader`; both under
  the flat-lighting override. Wired in `Viewmodel_DrawFirstPerson`: `W_PISTOL`
  takes the gun-only OBJ path; the other four take this arms path
  when `armsVM.loaded`, else fall through to the legacy gun-only floating OBJ
  viewmodel. Loaded/unloaded via `Viewmodel_LoadArms()`/`Viewmodel_UnloadArms()`
  in `main.c` (boot + `--screenshot-viewmodels`). **The per-gun grips are NOT
  tuned yet** — the `gunGrip[W_COUNT]` table at the top of `viewmodel.c` is all
  `{pos {0,0,0}, rotDeg {0,0,0}, scale 1.0}` placeholders. Each gun needs its
  `pos`/`rotDeg`/`scale` dialed in via `--screenshot-viewmodels` so the muzzle
  and grip sit right in the hand. See the arms-viewmodel gotcha below.

Prior pass (2026-06-02) — committed to `main`, not pushed:

- **Player third-person model authored AND wired (`data/models/player.glb`).**
  Rig-first soldier on the shared 17-bone humanoid family (same bone names as
  `zombie.glb` — `pelvis/spine/chest/neck/head`, `upperarm/forearm/hand.{L,R}`,
  `thigh/shin/foot.{L,R}`): a skin-modifier stick-figure body (single island,
  auto-weighted) in A-pose facing +Y, plus helmet/visor/jaw/plate-vest/shoulder-
  straps/collar/belt/pouches/boots/gloves as separate single-island boxes each
  rigidly bound to one bone. 1840 tris, 9 material zones, connectivity audit
  PASS. 8 in-place clips (no root motion): `idle`/`walk`/`run`/`fire`/`reload`/
  `revive`/`downed`/`death` — the ground clips drop the `pelvis` (root bone) to
  the floor (verified min-Z ≈ 0). `render.c:DrawOtherPlayer` now prefers it over
  `PROP_PLAYER_M`/cubes: render-local `AnimState[NET_MAX_PLAYERS]` (NOT
  serialized — same precedent as zombie/viewmodel anim), clip reconstructed from
  the synced player fields (smoothed pos-delta speed → walk/run/idle, `reload`
  from `inventory.reloadTimer`, `downed`/`death` from `downed`/`!alive`, `revive`
  inferred from proximity to a downed teammate with `reviveAsTarget>0`, `fire`
  from host-only `fireHeld`). Team colour is a *lightened* wash (toward white) so
  the soldier keeps its material detail. Loaded by `Render_LoadPlayerAnim()` in
  `main.c` boot (after `Assets_Load`). Verify via the new
  **`./build/shooter --screenshot-coop`** mode (writes `coop_*.png` of dummy
  teammates in every state — no real MP needed). See the player-model gotcha.
- **M1911 viewmodel wired into first person — shows in-game** (later removed).
  The combined `DrawPistolViewmodelGLB` path was added here, then dropped in
  2026-06-03 (the "take the arms away" request). `W_PISTOL` now uses the
  gun-only floating OBJ path in `Viewmodel_DrawFirstPerson`. **Gotcha from
  original authoring:** the model's forearms were long and had to be shortened
  to stubs — long forearms reach back to/behind the camera and the perspective
  divide wraps them to the top of the screen. **Added `Anim_Pose`** (anim.{c,h})
  = the posing half of `Anim_Draw` without the draw, so the viewmodel can draw
  with its own transform.
- **M1911 viewmodel authored: `data/weapons/pistol/pistol_vm.glb`** (rigged,
  animated). Arms + gun, 9 bones (`root`/`frame`/`slide`/`mag`/
  `hammer` + `hand.{L,R}`/`forearm.{L,R}`), 20 rigid box parts each bound 100%
  to one bone (slide/mag/hammer are separate movable islands per the rig-first
  mandate), audit PASS, ~880 tris. All 8 common viewmodel clips: `idle`,
  `fire` (slide reciprocates + recoil kick on `root` + hammer fall, ~0.2s),
  `reload` (left hand drops, mag ejects via the `mag` bone, new mag in — no
  slide rack), `reload_empty` (slide held back the whole clip, then slide
  release slams it forward), `raise`/`lower`, `sprint`, `inspect`. Authored
  +Y-forward (muzzle +Y, sights +Z up, grip at origin) per the glTF facing
  rule. Validated via `--anim-test`. The combined glb viewmodel path was later
  removed (2026-06-03); `W_PISTOL` now uses the gun-only OBJ path in
  `viewmodel.c:Viewmodel_DrawFirstPerson`. `pistol_vm.glb` is kept on disk as
  authored content. See the arms-viewmodel gotcha below.
- **First rigged glTF asset shipped: `data/models/zombie.glb`** — the
  proof-of-pipeline for the skeletal-animation system. Authored rig-first via
  the `blender-game-asset` skill: 17-bone humanoid skeleton (pelvis/spine/
  chest/neck/head, upperarm/forearm/hand ×2, thigh/shin/foot ×2), A-pose,
  built from a skin-modifier stick figure so the whole body is ONE connected
  island (integrity audit PASS), 4576 tris, 6 flat-colour material zones
  (skin/shirt/pants/boots/blood/bone). Clips `walk` (loop, 0.85s, alternating
  legs + knee bend + arm counter-swing), `attack_a` (0.61s overhead swipe),
  `death` (1.34s stagger→collapse). Exported `export_yup=True` so it faces -Z
  in raylib at yaw 0. Verified with `--anim-test` (deforms, lit, no skeleton
  mismatch). **Wired onto `Enemy`:** `render.c:DrawEnemy` now prefers the
  rigged model over `PROP_ZOMBIE`/cubes; per-enemy `AnimState` is a
  render-local parallel array (`zombieAnimState[MAX_ENEMIES]`, indexed by
  `e - enemies`) — NOT serialized (same precedent as the local viewmodel
  anim), so no protocol bump. Walk plays with playback rate scaled by
  `e->speed`; swaps to a one-shot `attack_a` when within melee range of the
  target. Load/unload via `Render_LoadZombieAnim()` (after `Assets_Load`, so
  `worldSkinnedShader` exists) / `Render_UnloadZombieAnim()`. See the rigged
  zombie gotcha below.
- **Door fix** — door header/lintel walls were blocking the doorway in XZ
  (collision ignores Y). Added `interiorWallNoClip[]`; lintels render +
  block bullets but not walking. See the gotcha below.
- **Weapon sizing/colour fix** — world weapon draws are now life-size
  (decoupled from the viewmodel scale knob); viewmodel drawn under flat
  lighting so it stops colour-swinging as you turn. See the scale gotcha.
- **CoD balance pass** — zombie HP curve, 50 contact dmg, HP regen, CoD
  scoring, retuned weapon stats (M14 is now semi-auto), PaP reserve ×2.
  See the balance gotcha for the formulas.
- **HUD redesign** — circular perk/equipment badges with vector glyphs,
  unified styling (shadowed text, gold/dim palette), rounded HP/stamina
  bars, cleaner round/ammo/points. All in `hud.c`.
- **Skeletal animation pipeline** — `anim.{c,h}` + `world_skinned.vs` +
  `--anim-test`. glTF GPU skinning, ready for animated assets. See gotcha.
- **Docs** — `data/ANIMATIONS.md` (rig-first animation spec, gun
  blowback + empty reloads), `ASSETS.md` rig-first note, this file, README,
  TODO. New `.claude/skills/blender-game-asset` skill (rig-first authoring +
  connectivity auditor).
- **Policy** — always commit to `main`, never push (see below). Root-level
  `*.png` screenshots are gitignored.

### Prior session (2026-06-01)

This session committed the big asset/rendering/map pass that had been
sitting uncommitted, plus added: differentiated zombie AI, proactive
obstacle avoidance, fire modes + recoil + damage dropoff for weapons,
and a per-folder data-driven weapon system (`.weapon` files). All 5
weapons were re-authored from scratch in Blender (replacing the
Quaternius set). Build clean, smoke-tested.

### Today's session (2026-06-01, later pass)

- **Dynamic crosshair** — `hud.c` got a COD-style 4-tick reticle
  that blooms with weapon spread (`spreadDeg`), sprintBlend, and a
  per-shot kick (scaled by `spreadDeg` + `recoilPitch`). Smoothed
  with a framerate-independent exp lerp at ~18 Hz; kick decays at
  36 px/s. Collapses to a dot in ADS. State lives in
  `hudCrossGap`/`hudCrossKick`/`hudCrossLastShots` statics.
- **Equipment system** — `Throwable` entity (`entities.{c,h}` +
  `MAX_THROWABLES=16` + serialized in snapshot) with gravity, XZ
  wall/obstacle bounce via `Level_ResolveXZ` + per-axis velocity
  damp, floor bounce + settle. Two kinds:
  - `TH_FRAG`: 2 s fuse → 260 dmg peak, 5 m radius, linear falloff.
    Damages the local player at 0.40× (friendly fire by proximity),
    kicks camera + rumble.
  - `TH_STUN`: 2 s fuse → 5.5 m radius, sets `Enemy.stunTimer`
    = 4.5 s. While > 0, speed × 0.20 + bite suppressed; cyan body
    tint in `DrawEnemy`.
- **New bindings** — `BA_THROW_LETHAL` (kbd G, pad DPad-Up) and
  `BA_THROW_TACTICAL` (kbd H, pad DPad-Down). Persisted to
  `settings.cfg`, listed in `Menu_DrawBindings`, reset by RESET
  DEFAULTS button.
- **Player loadout** — `Player.lethals` / `Player.tacticals` int
  counts; `STARTING_LETHALS`/`STARTING_TACTICALS` = 2, `MAX_*` = 4.
  `Player_GiveStartingPistol` seeds the counts. Serialized in
  `SerPlayer` as uint8_t (clamped).
- **HUD equipment block** — `DrawEquipmentIcons` in `hud.c` renders
  a pair of badges bottom-right of the screen near the perks,
  showing G/H key hints, count, and a category-coloured icon
  (olive disc for frag, cyan band for stun). Greyed when empty.
- **HUD weapon category tag** — `WeaponCategory` is now read at
  the HUD: a small `PRIMARY` / `SPECIAL` (etc.) tag sits above
  the current weapon name in the bottom-right block.
- **Splash damage on bullet impact** — `WeaponDef.splashRadius` +
  `splashDamage`; `.weapon` file directive `splash R D`. After a
  bullet's direct hit, `Bullets_Update` iterates all live enemies,
  computes a linear-falloff damage at hit point, awards points to
  the shooter, drops blood decals. Direct-hit enemy is skipped to
  avoid double-counting; headshot ×2 does NOT apply to splash.
  Raygun gets 3 m / 70 dmg by default in `data/weapons/raygun/raygun.weapon`.
- **Protocol bump** — `NET_PROTO_VERSION` = **8**. Added
  `SerThrowable` (pos / vel / fuse / kind / owner), serialized
  list in the snapshot; added `SerPlayer.lethals/tacticals` and
  `SerEnemy.stunTimer`; added `ACT_THROW_LETHAL` / `ACT_THROW_TACTICAL`
  action codes for clients.
- **Melee-as-slot deferred** — TODO #6 mentions a bowie/bat slot to
  replace V-button melee. Implementing that fully would mean a 3rd
  inventory slot + swap UI + wallbuy plumbing. Left in place for a
  follow-up pass; V-button melee is unchanged and still good.

### Today's session (2026-06-01, evening pass)

- **Zombie type AI** (`src/entities.c`):
  - **Runner**: 0.55s lunge bursts at ~1.9× speed with ~3s cooldown, only
    when 1.2–9 m away *and* `Level_PathClearXZ` to target. Retargets
    ~2× faster than normal. Smaller bite (0.75×) but shorter touch CD.
  - **Crawler**: serpentine weave (lateral sine around `dirGoal`, max
    ~25° deflection). Longest retarget commit. Bites *hardest* (1.4×).
  - **Boss**: no weave, slight slow (0.95×), 3× contact damage, +30%
    bite cooldown, extra camera punch on hit.
  - **Normal**: unchanged shuffle.
  - Per-type contact damage + touch cooldown lives in the ZS_INSIDE
    branch of `Enemies_Update`. New field `Enemy.specialTimer` holds
    runner lunge cooldown/active state.
- **Proactive obstacle avoidance** — new `Level_PathClearXZ(from, dir,
  radius, dist)` in `level.{c,h}`. ZS_INSIDE movement now does a
  ~0.4 s lookahead in `moveDir`. If blocked, fans out angles
  (30°/60°/90°/125°, both sides, biased toward last successful side)
  and picks the smallest deflection that's clear. Commits to the
  chosen direction for 0.3 s via `escapeDir`/`escapeTimer` to avoid
  jitter at obstacle edges. Old reactive "haven't moved → sidestep"
  remains as fallback for wedged-in-corner cases.
- **Weapon fire modes** (`src/types.h`, `weapons.c`, `game.c`) — new
  `FireMode` enum: `FM_SEMI` / `FM_BURST` / `FM_AUTO`. Edge-detected
  press via `Player.prevFireHeld` (semi-auto previously auto-spammed
  on hold). Burst progression via `WeaponSlot.burstRemaining` +
  `burstTimer` — burst completes even if trigger released.
  `fireCooldown` is now the *post-burst* cooldown; `burstInterval`
  gates within the burst.
  Assignment: pistol/shotgun/raygun = semi, SMG = auto, rifle =
  3-round burst (0.07s interval).
- **Per-weapon recoil** — new `recoilPitch` + `recoilYaw` on
  `WeaponDef`. `Weapon_Fire` kicks `p->pitch` (up) and ±`yaw` after
  the shot fires (so first round still lands where aimed). ADS halves.
  Pitch clamped to existing ±1.55 rad. Solo/host correct; MP clients
  may flicker since local input writes pitch each frame — flag for
  later if MP play happens.
- **Damage dropoff** — new `dropoffStart` / `dropoffEnd` /
  `dropoffMinMul` on `WeaponDef`. `Bullet` now carries `origin` +
  `weaponIdx`. At hit, distance from origin scales damage linearly
  between start/end. Headshot ×2 applies post-dropoff. Pistol/SMG
  moderate, shotgun steep (25% at 18m), rifle near-flat, raygun flat.
- **Per-folder weapons** — `WEAPONS[]` no longer `const`. Compiled
  defaults serve as fallbacks; `Weapons_Load()` scans `data/weapons/`
  recursively (raylib's `LoadDirectoryFilesEx`), parses each
  `<name>.weapon` (line-based, `#` comments, mirrors `.map` style),
  populates the slot via the `id` field (PISTOL/SMG/SHOTGUN/RIFLE/
  RAYGUN), and loads the model relative to the `.weapon` file's
  directory. Storage for `weaponModels[]` / `weaponModelLoaded[]` /
  `weaponTune[]` moved from `assets.{c,h}` to `weapons.{c,h}`.
  `Assets_Load` no longer touches weapons; `Assets_ApplyWorldShader`
  still iterates `weaponModels[]` (includes `weapons.h`).
  Boot order in `main.c`: `Weapons_Load()` → `Assets_Load()` →
  `Assets_ApplyWorldShader()`.
- **`.weapon` file format** — see any of `data/weapons/*/<name>.weapon`.
  Every `WeaponDef` field exposable plus `model`, `model_scale`,
  `model_yaw`, `model_offset`. Defaults preserved for missing keys.
- **`WeaponCategory` enum** — `WC_PRIMARY` / `WC_SPECIAL` / `WC_MELEE`
  / `WC_LETHAL` / `WC_TACTICAL`. Currently 4 guns are PRIMARY, raygun
  is SPECIAL. Nothing reads `category` yet — it's foundation for the
  next pass (dedicated grenade/tactical slots, melee weapon slots).
- **5 new weapon models authored in Blender** under
  `data/weapons/<name>/`:
  - M1911 pistol — slide + frame + grip + sights + ejection port
  - MP5 SMG — receiver + magwell + curved mag + collapsible stock
  - Olympia shotgun — over/under double-barrel + wood stock & forend
  - M14 rifle — long barrel + wood furniture + 20-round box mag
  - Ray Gun — green metal body + glowing coils + brass + power cell
  All multi-material, Principled-BSDF `Base Color`, exported with
  `forward_axis='Z'` + `export_triangulated_mesh=True`. Long axis
  authored along `-Y` in Blender → `-Z` in OBJ → `model_yaw=0`.
- **Discovered & worked around raylib OBJ bug** — `LoadOBJ` SEGVs on
  n-gons with **>20 vertices**. Cylinder caps with `vertices ≥ 24`
  export as a single n-gon face and trigger a heap overflow in
  tinyobj's face buffer. **Fix**: always export with
  `export_triangulated_mesh=True`. Different from the
  no-UV-texcoord-overflow bug already documented — that one corrupts
  texcoords silently; this one crashes.
- **Weapon scale baseline** — Quaternius models are authored at ~10
  units long (1 unit ≈ 1 inch). My models are at ~1 unit ≈ 1 metre,
  so `model_scale` had to be bumped by ~10× across the board
  (pistol 12, smg/shotgun/rifle 10, raygun 11). When adding more
  weapons, target ~0.5–1.5 m in Blender and use `model_scale` in the
  10–15 range.

Earlier-in-day work (also still uncommitted before this commit):

- **Bullets** — muzzle-origin spawn (was crosshair), per-weapon
  `bulletSpeed` / `bulletLife` / `tracerWidth` in `WEAPONS[]`, swept-slab
  vs. AABB + swept-cylinder vs. enemy in `Bullets_Update`, billboard
  tracers with speed-derived colour.
- **Decals** — new `src/decals.{h,c}`, 96-slot ring buffer, `DECAL_BLOOD`
  on enemy hits + `DECAL_IMPACT` on static geometry. `rlDisableDepthMask`
  while drawing.
- **Lit shader** — `world.{vs,fs}` v2 with directional light + ambient
  via screen-space derived flat normals (`dFdx`/`dFdy` on
  `fragWorldPos`), so it works for both authored OBJs and rlgl
  immediate-mode quads without per-vertex normals. `sunDir` / `sunColor`
  / `ambientColor` globals in `assets.{h,c}`.
- **All Round-2 prop OBJs authored in Blender** — `door.obj`,
  `door_frame.obj`, `obstacle_crate.obj`, `obstacle_barrel.obj`, 4 perk
  machines, `pap_machine.obj`, `wallbuy_panel.obj`, `powerup_drop.obj`,
  `player_m.obj`. All re-exported with base at Y=0 (floor convention).
- **Perks re-shaped as proper vending machines** — was 1.04 × 0.84 ×
  2.44 m (too tall, too thin); now 1.26 × 0.96 × 2.34 m with plinth,
  cabinet, glowing top sign, recessed display, dispense tray + lip,
  coin slot, side stripes. Per-perk colour theme baked in.
- **Door geometry fit** — header wall segment above the door cutout so
  the opening is 2.5 m tall (matching `DOOR_HEIGHT`), door + frame
  share scale (`openingW / FRAME_OBJ_WIDTH`) so the door sits inside
  the frame jamb. Plank-barricade-across-closed-door removed.
- **Map door widths narrowed** from 4–6 m to 2 m across all three maps
  (so the 1.8 m frame OBJ doesn't horizontally stretch).
- **Zombie HP recolouring removed** — model's authored colours now
  show regardless of damage; type stripes (runner yellow, boss magenta)
  unchanged.

Headline changes since the previous handoff:

- **Asset pipeline** — new `data/models/zombie.obj`, `weapons/raygun.obj`,
  `mystery_box.obj`, `board.obj`, `sandbag_stack.obj` (all bevelled,
  multi-material, MTL `Kd` via Principled BSDF base colour). Old
  "exploded zombie" geometry was caused by a `primitive_cube_add(size=1)
  * scale*0.5` half-size bug; fixed.
- **Prop / texture / shader registry** — `assets.{h,c}` now exposes
  `PropId` + `propModels[]` + `propModelLoaded[]`, `TextureId` +
  `textures[]`, and `worldShader` / `skyShader`. Single `Assets_Load`
  walks `PROP_FILES[]` / `TEXTURE_FILES[]` / `data/shaders/*.{vs,fs}`,
  reports each as `model: loaded …` / `texture: loaded …` / `shader:
  loaded …` or its "not found, using fallback" counterpart. Renderer
  is model-first everywhere; missing assets fall back to the previous
  cube / sphere primitive draws (Round-2 OBJs and all 5 textures are
  still unauthored, so a fresh checkout still renders cubes — but each
  asset drops in on next launch).
- **`render.c`** — `DrawTexturedBox` / `DrawTexturedFloor` use rlgl
  immediate mode with UVs scaled by `size / TILE_SIZE` for seamless
  tiling; `DrawProp` / `DrawPropEx` is the model-first helper used by
  every site. `Render_World3D` opens with `DrawSkybox` + `BeginWorldShader`,
  closes with `EndWorldShader` (which **must** pass
  `rlGetShaderLocsDefault()`, not `NULL` — `NULL` was the menu-SEGV cause).
- **`data/shaders/world.{vs,fs}`** — texture × tint × linear distance
  fog. Uniforms `fogColor`, `fogStart`, `fogEnd`. Replaces raylib's
  default shader on every loaded `Model.materials[].shader` (via
  `Assets_ApplyWorldShader`) and on `rlgl`'s default during the world
  pass.
- **`data/shaders/sky.{vs,fs}`** — procedural night sky: horizon→zenith
  gradient, faint warm horizon glow, hash-based stable star field. No
  texture asset required.
- **New map format** in `data/maps/*.map`. Compact `WALL x1 z1 x2 z2
  [DOOR center width cost [AS name]]`, symbolic `+x/-x/+z/-z`
  directions, `WINDOW … LOCKED_BY <door_name>`, `OBSTACLE x z sx sz
  [h]` (no more y/sy boilerplate), `ROOM <name> … END` blocks,
  `ATMOSPHERE { fog R G B start end; sky_tint R G B; music name } END`,
  `PROP <name> x z [yaw d] [scale s]`. All three shipped maps already
  converted.
- **`./build/shooter --validate path/to/map.map`** — parses without
  opening a window, prints line-numbered errors, exits 0 or 1. Hook it
  into an editor save action.
- **`MapProp` + per-prop collision** — `PROP` lines look up a name in
  `PROP_DEFS[]` (in `level.c`) for the PropId + collision half-extent.
  `Level_ResolveXZ`, `UnstickXZ`, and `Level_PointBlocked` all include
  `mapProps[].collider`.
- **Caps bumped**: `MAX_OBSTACLES 12→24`, `MAX_INTERIOR_WALLS 8→16`,
  `MAX_DOORS 4→8`, new `MAX_MAP_PROPS=32`.
- **Atmosphere globals** — `fogStart` / `fogEnd` / `fogColor` are
  global in `assets.{h,c}`; `ClearLevel` resets them to defaults so
  the ATMOSPHERE block of each map starts from baseline.

`git status` will show edits to: `src/{assets,decals,entities,game,level,
render,types,main,menu,hud,player,weapons}.{c,h}` (note `decals.{c,h}`
are new), `CMakeLists.txt`, `data/maps/*.map`, and new files under
`data/models/` + `data/shaders/`. `TODO.md` and `HANDOFF.md` are also
updated.

`NET_PROTO_VERSION` is **8** (`src/net.h`). Bump it whenever you change
anything in `SerPlayer` / `SerEnemy` / `SerThrowable` / `PktSnapshotHeader`.

## Build / run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shooter
./build/shooter --validate data/maps/nacht.map  # editor save hook
```

First Linux build needs X11 dev headers — see `README.md`. The CMake
target has a `POST_BUILD` step that copies `data/` next to the
executable. **It only fires when the `shooter` target rebuilds** — if
you only changed a `.map`, a texture, a shader, or a model with no code
changes, the copy doesn't run; manually `cp -r data/ build/` or touch a
source.

`settings.cfg` lives next to the binary (i.e. inside `build/` when run
from the build dir). On first run with no file, defaults are written.

## Architecture

`src/main.c` is just the entry point + game loop (~430 lines). Everything else
is one translation unit per responsibility, sharing `types.h`:

| File              | Owns                                                   |
|-------------------|--------------------------------------------------------|
| `types.h`         | All shared structs/enums/constants (incl. `MapProp`, bumped `MAX_*`) |
| `assets.{c,h}`    | `propModels[]`, `textures[]`, `worldShader`, `skyShader`, fog globals, `TILE_SIZE`. (Weapon storage moved out — see `weapons.{c,h}`.) |
| `level.{c,h}`     | Map tokeniser + parser, `Level_Validate`, `obstacles/walls/doors/windows/mapProps`, `Level_Reset`, `UnstickXZ`, `Level_PathClearXZ` (AI lookahead), `PROP_DEFS[]` |
| `player.{c,h}`    | `players[]`, look/move, sprint/crouch, stamina, sprintBlend, push-out |
| `weapons.{c,h}`   | `WEAPONS[]` (mutable, populated by `Weapons_Load` from `.weapon` files), `weaponModels[]`, `weaponTune[]`, fire/reload/melee, fire-mode + recoil + dropoff |
| `entities.{c,h}`  | `enemies[]`, `bullets[]`, `powerUps[]`, per-type zombie AI (lunge/weave/steamroll), proactive obstacle probing, damage→downed→dead transition |
| `interact.{c,h}`  | Wall-buys, perks, PaP, doors, repairs, revive, Mystery Box |
| `perks.{c,h}`     | Perk effects (jug/speed/dtap/stamin)                   |
| `game.{c,h}`      | `Game_Tick` — orchestrates a server tick, round breaks, respawn-on-round-start |
| `protocol.{c,h}`  | Serialization (`SerPlayer`, `SerEnemy`, `PktSnapshotHeader`) |
| `net.{c,h}`       | enet wrapper, `Net_GetLocalIPs` for the host's join prompt |
| `render.{c,h}`    | 3D world draw (model-first with cube fallbacks), textured walls/floor via rlgl, fog/sky/lit shader swap, tracer billboards |
| `viewmodel.{c,h}` | First-person viewmodel system: shared arms VM (`arms_vm.glb`) + bone-bolted gun, `GunGrip`/`gunGrip[]` table, procedural walk-bob/sprint motion, `Viewmodel_LoadArms`/`Viewmodel_UnloadArms`, `Viewmodel_DrawFirstPerson(Camera)` |
| `devtools.{c,h}`  | The 5 CLI debug/validation modes (`--validate`, `--screenshot-viewmodels`, `--screenshot-coop`, `--screenshot-pap`, `--anim-test`) behind `Devtools_HandleCLI(argc, argv, &exitCode)` |
| `decals.{c,h}`    | 96-slot ring buffer for blood + impact decals, drawn from `Render_World3D` |
| `anim.{c,h}`      | Skeletal animation pipeline. `AnimModel` (shared skinned glTF + clips) + per-instance `AnimState`; load/find-clip/update/draw via raylib 5.5 GPU skinning. `Anim_Pose` (pose without draw, for custom transforms), `Anim_FindBone`/`Anim_BoneMatrix` (bolt a separate object — e.g. a gun — onto a skeleton bone) |
| `hud.{c,h}`       | 2D HUD overlay, round splash, downed/spectate overlays |
| `menu.{c,h}`      | Menu screens incl. game-over stats table, controller bindings UI, map picker |
| `pad.{c,h}`       | Raw gamepad axis/button readers                        |
| `settings.{c,h}`  | `bindButton[]` runtime bindings, `Bind_Pressed/Down/PollAny`, `Settings_Load/Save` |
| `audio.{c,h}`     | Procedural SFX, snapshot-delta event detection         |
| `fx.{c,h}`        | Camera shake (no real particle system yet)             |

Data layout under `data/`:

```
data/
├── audio/                       # (nothing here yet; ATMOSPHERE music: parses but isn't loaded)
├── maps/*.map                   # new format; --validate to check
├── models/                      # props/decor (OBJ) + rigged characters (glTF)
│   ├── ASSETS.md                # spec for every prop + texture + shader
│   ├── *.obj/*.mtl              # static props (mystery box, doors, perks, …)
│   ├── zombie.glb               # rigged enemy (walk/attack_a/death)
│   ├── player.glb               # rigged co-op third-person soldier
│   └── arms_vm.glb              # shared first-person arms (4 non-pistol guns)
├── weapons/                     # NEW — per-folder weapon defs + assets
│   ├── pistol/{pistol.weapon,pistol.obj,pistol.mtl,pistol_vm.glb}
│   ├── smg/...
│   ├── shotgun/...
│   ├── rifle/...
│   └── raygun/...
├── shaders/{world,sky}.{vs,fs}
└── textures/*.png               # currently empty; specs in ASSETS.md
```

## Conventions & gotchas

- **Host-authoritative.** Clients send `PktInput`; host runs sim and
  broadcasts `PktSnapshot` at 20 Hz. Local-only fields (stamina,
  sprintBlend, godMode, noclipMode) live on/around `Player` but aren't
  serialized.
- **Bump `NET_PROTO_VERSION`** in `net.h` any time you change a
  serialized struct in `protocol.{c,h}`. The handshake rejects
  mismatched versions.
- **Map file path resolution** tries several relative locations and
  falls back to hardcoded geometry. Don't assume CWD. Same pattern for
  models (`data/models/`, `../data/models/`, `./data/models/`),
  textures, and shaders.
- **rlgl shader swap** — `EndWorldShader` **must** pass
  `rlGetShaderLocsDefault()` as the second arg to `rlSetShader`, not
  `NULL`. Passing `NULL` leaves `currentShaderLocs` dangling and
  `EndDrawing`'s render-batch flush SEGVs (it dereferences
  `currentShaderLocs[RL_SHADER_LOC_MATRIX_MVP]`). This was the
  menu-launch crash on 2026-06-01.
- **Blender OBJ axis quirks** (documented in ASSETS.md but easy to
  re-hit):
  - Author with Blender Z up, model facing **-Y**. Export with
    `forward_axis='Z'` (NOT `'NEGATIVE_Z'`, despite what the name
    suggests) so Blender -Y maps to OBJ -Z. Blender +X gets mirrored
    to OBJ -X; bake asymmetric details accordingly.
  - `primitive_cube_add(size=1)` already gives a unit cube. Setting
    `o.scale = (sx, sy, sz)` gives final size `sx × sy × sz` — do
    **NOT** multiply by 0.5. (That was the "exploded zombie" bug.)
  - For MTL `Kd` to carry, the material must `use_nodes = True` and
    the colour set on `Principled BSDF → Base Color`. Plain
    `material.diffuse_color` is viewport-only.
- **`-Wall -Wextra` are ON** (GCC/Clang, CMakeLists.txt). `SHOOTER_ASAN=ON`
  adds `-fsanitize=address,undefined` (the raylib OBJ patch removed the old
  blocker). Remaining: a handful of `-Wstringop-truncation` warnings from
  `strncpy` in net.c/level.c/settings.c/main.c — harmless, not yet addressed;
  everything else is clean.
- **raylib 5.5 OBJ loader — both known bugs are now PATCHED** (2026-06-03)
  via `cmake/patch_raylib_obj.cmake`, applied as a `PATCH_COMMAND` on the
  raylib `FetchContent_Declare`. Historic context (the patch makes both moot):
  - *No-UV read:* `LoadOBJ` read `texcoords[vt_idx*2]` for OBJs with no `vt`
    (all of ours) → wild OOB read. Now guarded + zero-filled. **You may add
    sanitizers now** (the old "ASan can't be enabled" blocker is gone), and
    don't bother adding UVs to "fix" anything.
  - *N-gon overflow (was the crash for `model_offset`):* tinyobj's `parseLine`
    read an `f` line into a fixed `f[16]` before triangulating; our cap n-gons
    (pistol 20-vert, shotgun 24-vert) overflowed it. Limit raised to 256.
    `export_triangulated_mesh=True` is still good hygiene but **no longer
    required for crash-safety** — un-triangulated n-gons load fine.
  - If you bump raylib's `GIT_TAG`, re-verify the patch matches (the script
    aborts loudly with a clear message if the upstream source moved).
- **Skeletal animation pipeline (2026-06-02).** Engine support for animated
  models is in; only authored animated assets are missing. Key facts:
  - Animations need **glTF (`.glb`)**, NOT OBJ — OBJ can't carry a skeleton.
    raylib 5.5 GPU skinning is used (`UpdateModelAnimationBones` writes only
    per-instance bone matrices; `DrawMesh` uploads them to the `boneMatrices`
    uniform — cheap enough to draw all 48 zombies at different poses).
  - `anim.{c,h}`: `Anim_Load/Unload/ApplyShader/FindClip/Play/Update/Draw`.
    `AnimModel` is the shared asset (model + clips); `AnimState` is per
    instance (clip/time/loop/speed). glTF clips are baked at ~60 fps
    (`ANIM_FPS = 1000/17`), so playback is driven in seconds.
  - Animated models must use **`worldSkinnedShader`** (`world_skinned.vs` +
    the shared `world.fs`) via `Anim_ApplyShader`, so they get the same fog +
    lighting. `BeginWorldShader` pushes the fog/sun uniforms to it each frame.
  - Validate any `.glb` with **`./build/shooter --anim-test <file.glb> [clip]`**
    (writes `anim_test_0..3.png` across the clip) or add `--live` for an
    interactive spinning viewer. `data/models/anim_test.glb` is a 2-bone
    bend-test fixture proving the path.
  - **Next:** author the real assets (rigged zombie / viewmodel / perk
    machine as `.glb`) and wire `AnimState` onto the entities. Engine work
    per entity is just: load AnimModel, `Anim_ApplyShader`, store an
    AnimState, `Anim_Update` each tick, `Anim_Draw` instead of `DrawProp`.
- **Rigged zombie (`zombie.glb`, 2026-06-02).** First animated asset; the
  template for every future rigged model.
  - **Body = one skin-modifier island; head = separate modeled parts.** The
    torso/limbs are a skin-modifier stick figure (single watertight island,
    auto-weighted, deforms cleanly). The **head is modeled geometry** — a
    skull box plus a heavy brow, two cheekbones, dark eye sockets with glowing
    pale-green eyes, a dark maw with upper/lower tooth bars, and a chin — each
    a separate beveled box, all *overlapping* the skull (so the audit's
    floating-part check passes) and each rigidly bound to the `head` bone
    (`teethL`/`chin` to `jaw`) via an Armature modifier + a single
    full-weight vertex group. Pure skin-modifier heads come out as featureless
    blobby wedges (the first two attempts) — model the face as real geometry.
    Each separate part must stay ONE island (don't `join` the boxes — that
    makes multi-island objects = audit FAIL) and must touch/overlap a
    neighbour.
  - **Facing — author +Y in Blender.** glTF `export_yup=True` maps Blender
    **+Y → raylib -Z** (the forward/look direction the enemy yaw expects).
    The *first* export was authored facing -Y → came out +Z = **backwards**
    in-game; the rebuild faces +Y so it now turns to face the player at the
    yaw `DrawEnemy` computes. This is the glTF rule and is the OPPOSITE of the
    OBJ `forward_axis='Z'` convention in ASSETS.md — don't cross them.
  - **AnimState is render-local, not on `Enemy`.** `render.c` keeps
    `zombieAnimState[MAX_ENEMIES]` indexed by `e - enemies` and ticks it with
    `GetFrameTime()` in `DrawEnemy`. Intentional: it's purely visual, so it
    stays out of `SerEnemy` (no protocol bump). If you ever need teammates to
    see *synchronised* attack/death frames, that's when it moves into the
    serialized state.
  - **Uniform scale only** (`Anim_Draw` takes one float). Boss uses 1.7×,
    crawler is kept small (0.7×) but can't be Y-squished like the old cube
    path — the crawler/boss really want their own meshes per `ANIMATIONS.md`.
    The yellow runner stripe + magenta boss stripe + runner red-eye tell are
    still drawn as overlays on top of the rigged model.
  - **No death/attack from the sim yet.** Enemies just flip `alive=false` on
    kill, so the `death` clip never plays in-game and `attack_a` is triggered
    purely by render-side proximity. Wiring a real death window (keep the
    corpse ~1.3s while the clip plays) + a sim attack flag is the natural
    follow-up; the clips already exist.
- **Knee direction (2026-06-02).** Both `player.glb` and `zombie.glb` originally
  shipped with **backwards knees** — the `shin` bone was flexed with a POSITIVE
  local-X rotation, which swings the foot forward (knee juts back). Correct is
  NEGATIVE shin-X (foot tucks backward+up, toward the back of the thigh).
  Verify empirically per rig (rotate `shin.L ±55°`, render a side view) — the
  sign depends on bone roll. Fixed in both: player walk/run/death/downed/revive
  re-keyed with `-shin`; the zombie `walk`/`death` shin keys were sign-flipped
  (quaternion→euler→negate X→quaternion) and the `death` clip had to be fully
  **re-authored** because its old "collapse" was built on the wrong-way knee
  fold (flipping alone made the corpse stand up). The `blender-game-asset` skill
  now has an "anatomically/mechanically correct" non-negotiable + a
  joint-direction calibration step. (Also caught a stray ~2 m `Icosphere` in the
  Blender scene during the zombie re-export — NOT part of `zombie.glb`; delete
  any such artifact before exporting since `use_selection=False` bakes the whole
  scene.)
- **Rigged player third-person model (`player.glb`, 2026-06-02).** Built with
  the same skin-figure-body + rigid-detail-parts technique as the zombie, on the
  shared 17-bone humanoid skeleton (so the family's bone names/hierarchy match).
  Wiring notes for `render.c:DrawOtherPlayer`:
  - **Only OTHER players use it** — you never see your own body (that's the
    viewmodel), so the draw loop skips `localPlayerIdx`. The `AnimState[]` is
    render-local (not in `SerPlayer`); no protocol bump.
  - **Clip is reconstructed from synced fields**, because the things you'd want
    aren't all serialized. Locomotion uses a *smoothed* horizontal speed from
    the per-frame `pos` delta (snapshots are 20 Hz but we render at 60 fps, so a
    raw delta spikes on snapshot frames — an EMA of `dist/dt`, clamped, recovers
    the true average). `reload` reads `inventory[currentSlot].reloadTimer` (it
    IS serialized). `revive` is INFERRED: `reviverIdx` isn't serialized, so it
    fires when a downed teammate with `reviveAsTarget>0` is within ~2.2 m. `fire`
    keys off `fireHeld`, which is host-only (absent from the snapshot) — so it
    just never plays on clients. Harmless, but if you want clients to see
    teammates fire, add a fire flag to `SerPlayer` (protocol bump).
  - **Ground clips own the lay-down.** `downed`/`death` lower the `pelvis` (root)
    to the floor in the animation itself, so the draw passes `feet.y=0` with NO
    tilt hack (unlike the old OBJ path's Y-squash). Facing is `-p->yaw*RAD2DEG`,
    same as the OBJ path (the glb is +Y-in-Blender → -Z-forward in raylib).
  - **Team colour is a *lightened* wash** (`128 + c/2` toward white), not the raw
    team colour — a full-saturation multiply would flatten the soldier's
    materials. Dead/downed keep the existing grey/red `c`.
  - **Verify without MP:** `./build/shooter --screenshot-coop` spawns dummy
    teammates in locomotion/reload/downed/dead/revive states → `coop_*.png`.
- **Shared arms viewmodel + bone-bolted gun (`arms_vm.glb`, 2026-06-03).** The
  non-pistol guns share ONE rigged arms model; the gun is a separate object
  attached to the `hand.R` bone each frame instead of being baked into a combined
  arms+gun glb. So skins/new guns are cheap (retexture the arms once, drop in any
  gun OBJ). Implementation notes for `viewmodel.c:DrawArmsViewmodel`:
  - **The attach math.** `Anim_BoneMatrix(am, st, boneIdx)` returns the bone's
    model-space transform at the current frame; the gun's world matrix is
    `gunLocal * bone * root` (raylib `MatrixMultiply` applies the LEFT operand
    first, so the chain reads inner→outer). `root` is the same camera-space matrix
    the arms are drawn with. `gunLocal` first rotates the gun OBJ +90° about X
    (the OBJ's `-Z` muzzle / `+Y` up → `+Y` muzzle / `+Z` up to match the
    arms-forward frame), then applies the per-gun `gunGrip[wi]` `scale`/`rotDeg`/
    `pos` nudge in hand-local metres.
  - **Pistol stays on the OBJ path.** The M1911 was originally authored as a
    combined `pistol_vm.glb` (arms+gun in one rig), but the combined glb path
    (`DrawPistolViewmodelGLB`) was removed — `W_PISTOL` uses the gun-only
    floating OBJ path in `Viewmodel_DrawFirstPerson`. `pistol_vm.glb` is kept
    on disk (authored content) but is currently unused. Don't route the pistol
    through the arms path; it has no separate gun OBJ to bolt.
  - **Two shaders, two flat-light overrides.** Arms are skinned
    (`worldSkinnedShader`); the gun is a rigid OBJ (`worldShader`). Each gets its
    own flat sun/ambient override (restored after) so neither colour-swings —
    note the gun's override is darker (it's gunmetal, not skin).
  - **Grips are unfinished.** `gunGrip[]` (in `viewmodel.c`) ships as all-zero
    placeholders, so right now the gun renders at the raw hand-bone origin with
    only the base +90° orientation — it will look misplaced until each entry is
    tuned. Tune with `--screenshot-viewmodels` (it renders every weapon's
    viewmodel); adjust `pos`/`rotDeg`/`scale` per gun until the grip sits in the
    hand and the muzzle points forward. The arms model itself is authored at real
    metric; one `VM_SCALE` (0.62) sizes the whole assembly.
  - **Falls back gracefully.** If `arms_vm.glb` is missing or has no `hand.R`
    bone, `Viewmodel_DrawFirstPerson` drops through to the legacy procedural
    gun-only OBJ viewmodel for those 4 guns — nothing crashes.
- **CoD-style balance pass (2026-06-02).** Numbers are tuned to classic
  Treyarch zombies, so don't "simplify" them back:
  - **Zombie HP** (`entities.c::Enemies_RoundHP`): `150 + 100*(r-1)` through
    round 9 (R1=150 … R9=950), then `*1.1` compounding each round from 10.
  - **Player damage** (`types.h::ENEMY_DAMAGE=50`): ~2 hits down at 100 HP,
    ~5 with Juggernog (250). Per-type mult in `entities.c`: runner ×0.8,
    crawler ×1.0, boss ×2.0.
  - **HP regen** (new): `REGEN_DELAY` 4 s damage-free, then `REGEN_RATE`
    110 HP/s back to max. Driven by `Player.regenTimer` (local, not
    serialized — reset at every damage site; regen runs host-side in
    `Game_Tick`). Downed players don't regen.
  - **Points** (`entities.c` bullet hit): 10 / hitmarker, 60 body kill,
    100 headshot kill, 130 melee. Don't re-add a non-kill headshot bonus.
  - **Weapon stats** live in the `.weapon` files (the compiled `WEAPONS[]`
    defaults are only fallbacks — edit BOTH). Base damages: M1911 40,
    MP5 35, Olympia 60/pellet, M14 100 (now SEMI, not burst), Ray Gun 1000
    + 500 splash. **Pack-a-Punch** = damage ×2.5, mag ×2, reserve ×2
    (`weapons.c::Weapon_Eff*`).
- **Two independent weapon scales (2026-06-02).** The 5 weapon OBJs are
  authored at true real-world size (pistol 0.54 m … rifle 1.55 m long).
  `weaponTune.scale` (`model_scale` in `.weapon`) is **only** the
  first-person viewmodel framing knob (`render.c` viewmodel base ×
  `weaponTune.scale`, base currently 0.05). World draws — wall-buy, Mystery
  Box, PaP floater — go through `DrawWeaponDisplay`, which now draws at
  literal world scale (1.0 = life-size) and ignores `weaponTune.scale`.
  Don't reintroduce the `displayScale * weaponTune.scale` multiply: that was
  making world guns ~10× too big while the viewmodel stayed small. The
  viewmodel is also drawn under flattened lighting (low sun / high ambient,
  restored after) so its facets don't swing dark↔bright as the camera turns.
- **Weapon model sizing.** The Quaternius weapon OBJs we used to ship
  are authored at ~10 units long (1 unit ≈ 1 inch). Anything authored
  in metric metres (typical Blender default) needs `model_scale` in
  the 10–15 range to display at the same on-screen size — that's why
  the new weapons all have `model_scale ≈ 10–12` in their `.weapon`
  files.
- **Map engine limits** (current): `MAX_INTERIOR_WALLS=32`,
  `MAX_DOORS=8`, `MAX_WINDOWS=4`, `MAX_OBSTACLES=24`, `MAX_MAP_PROPS=32`.
  Windows must be on arena perimeter walls (engine renders gaps in
  those for windows).
- **Spawns must not overlap an OBSTACLE or PROP's XZ projection** —
  collision is XZ-only, ignores box Y extent. `UnstickXZ` will rescue
  the player but the spawn flash looks bad. Authoring discipline: keep
  spawns clear.
- **Door lintel walls are no-clip for movement** (2026-06-02 fix). Each
  parsed `DOOR` emits a header/lintel wall above the opening into
  `interiorWalls[]`. Because movement collision is XZ-only, that lintel's
  footprint would wall off the doorway whether the door is open or shut.
  `interiorWallNoClip[]` (parallel to `interiorWalls[]`, set on the two
  lintel emit sites) is skipped by the three XZ collision paths in
  `level.c` but still rendered + bullet-collided (the bullet test is
  Y-aware). Don't add new full-height geometry above a walkable gap
  without flagging it no-clip.
- **Settings persistence**: `Settings_Save` is called from a few paths
  — on graceful exit, on closing the Settings screen, and after every
  binding change. SIGTERM/SIGKILL won't save.
- **rand()** — `interact.c` uses it for Mystery Box rolls; needs
  `#include <stdlib.h>`.
- **rlgl** — if you use `rlPushMatrix`/`rlTranslatef`/`rlSetShader`,
  include `rlgl.h` explicitly. raylib doesn't pull it in.
- **Spectator mode** — when `!me->alive`, `main.c` runs a 3rd-person
  spectate cam over a live teammate's shoulder. F / A / LMB / Jump
  bind cycles to the next teammate. Auto-respawns at next round start.
- **Noclip = out-of-body fly cam** (F4 / R3). The body is left behind
  FROZEN at the spot/facing where you toggled noclip on — `main.c` no
  longer writes the fly camera's yaw/pitch back onto the player, and never
  moved `me->pos`. While noclipping, the local player's own third-person
  body IS drawn (the `DrawOtherPlayer` loop skips `localPlayerIdx` only
  when `!noclipMode`) and the first-person viewmodel is suppressed
  (`Viewmodel_DrawFirstPerson` bails on `noclipMode`) so the arms+gun don't
  trail the detached camera. Toggling noclip off snaps the camera back to
  the body. Verify with `--screenshot-coop` (the `coop_noclip` shot).
- **Round 1 skip** — was a real bug, fixed by initializing
  `roundNum=0; gamePhase=GS_ROUND_BREAK` so `Game_Tick` rolls it into
  `Game_StartRound(1)`. Don't regress.
- **Commit policy** (updated 2026-06-02): always commit completed work
  to `main`; never `git push` — the user pushes themselves. Don't ask
  before committing. Terse subject + bulleted body.

## Controller bindings model

`BindAction` enum in `settings.h` has **16** entries; UI is
`Menu_DrawBindings`. See `settings.{h,c}` for the live mapping. Default
mapping mirrors the old hardcoded one (RT fire, LT ADS, Y reload, X
interact, RB melee, LB swap, A jump, B crouch, L3 sprint, Start pause,
Back hold scoreboard, R3 noclip) plus DPad-Up throw lethal, DPad-Down
throw tactical. Keyboard equivalents (hardcoded alongside binds in
`main.c`): G lethal, H tactical, F3 god, F4 noclip.

## Working style the user has shown

- Likes terse commits with a concise subject + bulleted body.
- Bundles multiple features in a single request ("fix it all", "add
  1 2 3 4 12 9"). Implement all in one batch; one commit per natural
  unit is fine.
- Will reject feel-changes if they go too far. First Nacht map was
  rejected with "no make it like the cod zombies map" — got specific
  layout iteration the second time. Similar pattern with the zombie:
  exploded → connected but undetailed → connected and detailed →
  uncoloured → properly coloured.
- Skips planning docs — when asked to spec, asks for a punch list
  instead.
- Replies with "fix it all" / "do those three" / "yes" — commits to
  full scopes when given options.
- Doesn't proofread; "explod" = exploded, "raygun" lower-case, "to do"
  = TODO. Just infer.
- Asks "what else needs to be added?" mid-stream — wants a survey,
  not a sales pitch. Be honest about gaps; rank by impact.

## Suggested next things

See `TODO.md` for the full live list. Impact-ordered short version:

1. **Author the first rigged glTF asset** — the engine animation
   pipeline is in (`anim.{c,h}`, `--anim-test`); only assets are
   missing. Per `data/ANIMATIONS.md`, start with one Normal zombie
   (`walk`/`attack_a`/`death`) built rig-first via the
   `blender-game-asset` skill, then wire an `AnimState` onto `Enemy` and
   swap its `DrawProp` for `Anim_Draw`. This is the proof-of-pipeline.
2. **Roll animation out** — fill zombie clips + per-type overrides. The
   weapon viewmodels and the player third-person model are now wired; the
   open item there is **tuning the per-gun `gunGrip[]` table** in
   `viewmodel.c` (shared arms + bone-bolted gun for the 4 non-pistol guns —
   currently placeholder zeros) and authoring the rest of the zombie/per-type
   clips. Clip lists in `data/ANIMATIONS.md`.
3. **Per-type zombie audio tells** — distinct groan/hiss/roar via
   raylib positional audio; the visual tells (runner red-eye wind-up,
   boss stripe) already exist.
4. **Particle system** — pooled additive-blend for muzzle flash,
   casing eject, blood mist. Replaces the HUD-tint muzzle hack.
5. **Post-FX render target** — bloom + vignette + hit-flash red +
   low-HP heartbeat. Unlocks every subsequent "feel" upgrade.
6. **Per-map music** — `ATMOSPHERE { music name }` already parses; load
   `data/audio/<name>.ogg` on map load.
7. **`LIGHTS x y z r g b range`** in `.map` + `sky_tint` plumbing.
8. **Textures (5)** — `floor_concrete.png`, `ground_dirt.png`,
   `wall_brick.png`, `wall_plaster.png`, `ceiling_wood.png`.
9. **Frustum culling for props** — bounding-sphere test before each
   `DrawProp`. Matters once more props ship.
10. **`tests/map_parser_test.c`** + CI step running `--validate` on
    every shipped map.

Tune-on-playtest flags from the 2026-06-02 balance pass: boss melee
(100 dmg = 1-shot a no-Jug player from full), HP regen rate/delay
(110/s after 4 s), PaP damage ×2.5, and the new weapon viewmodel base
scale (0.05) — all single-number tweaks if they feel off.

## If something breaks

1. `cmake --build build -j 2>&1 | tail -40` — most issues are
   missing-include or implicit-decl warnings.
2. `./build/shooter --validate data/maps/<name>.map` — if a map won't
   load, this prints line-numbered errors and exits 1.
3. Run `./build/shooter` and check stdout for the
   `model: loaded …` / `texture: …` / `shader: …` /
   `map: loaded '<name>' from … (W walls, D doors, …)` block — confirms
   the asset pipeline + map parser are seeing what you expect.
4. For multiplayer issues, check `NET_PROTO_VERSION` matches between
   host and client builds.
5. For "stuck in geometry" reports, look for SPAWN points overlapping
   OBSTACLE / PROP XZ projections in the relevant `.map`. `UnstickXZ`
   will rescue but it's a smell.
6. **For a menu-launch SEGV**: check `EndWorldShader` in `render.c` —
   `rlSetShader(rlGetShaderIdDefault(), rlGetShaderLocsDefault())`,
   not `NULL`.
7. If bindings UI doesn't capture: confirm `Pad_Connected()`. Without
   a pad, the screen shows the "(no controller detected)" hint.
