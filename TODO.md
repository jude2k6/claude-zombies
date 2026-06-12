# TODO

Live punch list, ordered by impact-per-effort. The architecture for almost
every item below is already in place — these are mostly authoring + small
shader / system additions.

## Architecture cleanup (code audit 2026-06-12)

The module layout itself (one TU per responsibility sharing `types.h`,
host-authoritative sim, model-first render with fallbacks) is sound — keep
it. What follows is the drift + hygiene found auditing the code against
HANDOFF/README, ordered by priority.

### Dead code & waste
- [x] **Remove the dead pistol-glb viewmodel path.** `DrawPistolViewmodelGLB`,
      the `pistolVM`/`pistolVMState`/`pvm*` statics, `Render_{Load,Unload}
      PistolVM` (render.c/render.h), and all main.c call sites deleted (772606f).
      `pistol_vm.glb` kept on disk; `W_PISTOL` uses the gun-only OBJ path.
- [x] **Drop `data/models/weapons/{revolver,sniper}.{obj,mtl}`** — deleted
      (c23bab9); referenced nowhere.

### Warnings & safety nets
- [x] **Enable `-Wall -Wextra` for GCC/Clang** — done (a0468ba); all warning
      backlog fixed (b1c6d7b): misleading-indentation clamps in fx.c/hud.c,
      sign-compare + unused-var in audio.c. `-Wno-unused-parameter` added to
      suppress the intentional ones. Remaining: a few `-Wstringop-truncation`
      warnings from `strncpy` in net.c/level.c/settings.c/main.c — harmless,
      not yet addressed.
  - [ ] **Fix `-Wstringop-truncation` `strncpy` warnings** in
        net.c/level.c/settings.c/main.c.
- [x] **`strdup` under strict C11** — replaced with a local `xstrdup` in
      `weapons.c` (b1c6d7b); strict-C11 safe.
- [x] **Add an ASan build option** — `option(SHOOTER_ASAN OFF)` +
      `-fsanitize=address,undefined` added to CMakeLists.txt (a0468ba).
- [ ] **CI + parser tests** — tracked under Infrastructure below; listed
      here because it's the backstop that keeps all of this honest.

### Oversized translation units (mechanical splits, no behaviour change)
- [x] **`render.c` → `viewmodel.{c,h}`** — carved out (491b5c1, 352 lines):
      `Viewmodel_DrawFirstPerson`, `DrawArmsViewmodel`, `ViewmodelMotion`,
      `GunGrip`/`gunGrip[]`, `Viewmodel_LoadArms`/`Viewmodel_UnloadArms`.
      render.c is now 1116 lines.
- [x] **`main.c` → `devtools.{c,h}`** — carved out (7d56ffb, 363 lines):
      all 5 CLI modes behind `Devtools_HandleCLI(argc, argv, &exitCode)`.
      main.c is now 431 lines — entry + main loop only.
- [ ] **`entities.c` (953 lines) — optional.** It's really 4 systems
      (enemies AI, bullets, throwables, powerups) in one file, but they're
      stable; split only if it starts churning.

### Layering (sim → presentation)
- [ ] **Don't deepen the `entities.c`/`game.c` → `Decals_*`/`Fx_*` calls.**
      The host sim currently writes presentation state directly (blood
      decals, camera shake from inside `Bullets_Update`). `audio.c` already
      models the right pattern (snapshot-delta event detection). A full
      event-queue refactor isn't worth it yet (there's no headless-server
      build to protect), but new sim code should not reach into
      render/hud/assets — and the Mid-term "MP-correct recoil" item is the
      same family of fix.

### Doc drift (HANDOFF/README vs code — align one side)
- [x] HANDOFF "Current state" refreshed: arms-VM pass noted as committed
      (6c794a7), PaP commits (3619151, 39dc856) documented, architecture-
      cleanup pass (772606f–c23bab9) described.
- [x] HANDOFF "Map engine limits": `MAX_INTERIOR_WALLS=16` → **32** (matches
      types.h).
- [x] Scoring aligned: `KILL_POINTS` bumped 50 → 60 in types.h (c23bab9);
      body kill = 60, headshot kill = 100. HANDOFF/README/TODO now match.
- [x] README dev-CLI list: `--screenshot-pap` added.

## Next up (highest impact-per-effort)

### Animation & assets (engine pipeline is IN — assets next)
The glTF skeletal-animation pipeline shipped 2026-06-02 (`anim.{c,h}`,
`world_skinned.vs`, `--anim-test`). Specs: `data/ANIMATIONS.md` (clip lists,
rig-first mandate) + the `blender-game-asset` skill (rig-first authoring +
connectivity auditor). Remaining work is authoring + per-entity wiring:
- [x] **Rigged Normal zombie** (`zombie.glb`) — proof-of-pipeline DONE.
      Rig-first humanoid (17-bone skeleton, A-pose, skin-modifier single
      island, 4576 tris, 6 material zones), clips `walk`/`attack_a`/`death`.
      Audited PASS, validated via `--anim-test`. `DrawEnemy` now prefers the
      rigged model (render-local `AnimState[]` per enemy slot, walk loop with
      movement-scaled playback + a melee attack swipe), falling back to the
      OBJ/cube path. See HANDOFF "rigged zombie" gotcha.
- [ ] **Zombie clip set + per-type variants** — `spawn`/`run`/`attack_b`,
      runner `lunge`, crawler `crawl`, boss `steamroll`/`attack_heavy`.
- [~] **Weapon viewmodels** (`<name>_vm.glb`) — `idle`/`fire`/`reload`/
      `reload_empty`/`raise`; blowback on `fire`, charging-handle cock on
      `reload_empty`. Replaces the procedural viewmodel anim in `render.c`.
  - [x] **M1911** (`data/weapons/pistol/pistol_vm.glb`) authored: arms + gun,
        9 bones (root/frame/slide/mag/hammer + both hands/forearms), 20 rigid
        parts, audit PASS. All 8 clips — `idle` 2.4s, `fire` 0.2s (slide
        blowback + recoil + hammer), `reload` 1.34s (mag swap, no rack),
        `reload_empty` 1.62s (slide locked back → release), `raise`/`lower`,
        `sprint`, `inspect`. Validated via `--anim-test`.
  - [x] **Wired (then removed) the combined vm.glb path** — `DrawPistolViewmodelGLB`
        was implemented and verified, then removed (772606f) per the "take the
        arms away" request. `W_PISTOL` now uses the gun-only OBJ path in
        `viewmodel.c:Viewmodel_DrawFirstPerson`.
  - [~] Roll the vm clip set across SMG / shotgun / rifle / raygun. **Engine
        path is built and committed:** a shared rigged `data/models/arms_vm.glb`
        (arms+hands, full clip set) drives all 4 non-pistol guns via
        `viewmodel.c:DrawArmsViewmodel`; the equipped gun OBJ is bolted onto the
        `hand.R` bone each frame (new `Anim_FindBone` / `Anim_BoneMatrix`), so
        only the arms need a skin, not a combined glb per gun. Same clip state
        machine as the pistol VM. **gunGrip[] tuned 2026-06-12 (a8b1cfb)** —
        all 4 guns sit in the hands, verified via `--screenshot-viewmodels`.
        `W_PISTOL` uses the gun-only OBJ path. Still open: authoring per-gun
        vm clips (fire blowback etc. currently come from the shared arms
        clips). See HANDOFF "shared arms viewmodel" gotcha.
- [x] **Player third-person model** (`data/models/player.glb`) — authored +
      validated + WIRED. Rig-first soldier on the shared 17-bone humanoid family
      (same bone names as `zombie.glb`): skin-modifier stick-figure body in
      A-pose facing +Y (olive fatigues), with helmet/visor/jaw/plate-vest/
      shoulder-straps/collar/belt/pouches/boots/gloves as separate single-island
      parts rigidly bound to one bone each. 1840 tris, 9 material zones, audit
      PASS. All 8 clips — `idle`/`walk`/`run`/`fire`/`reload`/`revive`/`downed`/
      `death` — in-place, no root motion; ground clips lower the `pelvis` (root)
      to the floor. Validated via `--anim-test player.glb`.
  - [x] **Wired into `render.c:DrawOtherPlayer`** — prefers the rigged model
        over `PROP_PLAYER_M`/cubes; render-local `AnimState[NET_MAX_PLAYERS]`
        (not serialized). Clip reconstructed from the synced fields: locomotion
        from a smoothed pos-delta speed (>8.5 m/s → `run`, >0.8 → `walk`, else
        `idle`, playback scaled by speed), `reload` from `inventory.reloadTimer`
        (playback scaled to fit Speed Cola), `downed`/`death` from `downed`/
        `!alive`, `revive` inferred from proximity to a downed teammate with
        `reviveAsTarget>0` (reviverIdx isn't serialized), `fire` from `fireHeld`
        (host-only). Team-colour wash lightened toward white so the soldier
        keeps material detail. Loaded via `Render_LoadPlayerAnim()` in `main.c`.
  - [x] **`--screenshot-coop`** debug mode (mirrors `--screenshot-viewmodels`):
        spawns dummy teammates in locomotion/reload/downed/dead/revive states
        and writes `coop_*.png` so the third-person model can be verified
        without a real multiplayer game.
- [~] **Machine polish** — PaP done 2026-06-04 (authored CoD-style cabinet
      with lit chamber + procedural shutter/sparks, `--screenshot-pap`);
      mystery-box lid + perk dispense still code/shader-faked. Optional,
      low priority.

### Weapons polish (architecture is in)
- [x] **Weapon system fully data-driven (ef1afe2).** No compiled-in stats:
      `.weapon` files are the single source of truth (loud per-slot error if
      one is missing). New keys: `sfx`, `haptic`, `mbox_weight`,
      `vm_grip_pos/rot/scale`. M1911 unified onto the shared arms path;
      hand placement root-caused (grips had been tuned against a mid-raise
      pose — the screenshot devtool captured frame 1 of `raise`; it now
      settles 75 frames first) and re-seated at `vm_grip_pos 0 0 0`.
  - [ ] **In-game feel check of the re-seated grips** — verify in actual
        play (ADS, sprint, reload) and fine-tune per-gun `vm_grip_pos` in
        the `.weapon` files if any gun rides oddly. Box odds change to
        flag on playtest: raygun `mbox_weight 0.5` (~11% vs old 20%).
- [x] **Verify the 5 new viewmodels in first-person** — done via
      `--screenshot-viewmodels` CLI mode. Sizes look right; pistol
      bumped to `model_scale 15`. Tune individual `model_scale` /
      `model_offset` in `data/weapons/<name>/<name>.weapon` if needed.
- [x] **Reload + weapon-swap viewmodel animation** — `render.c`
      now does a parabolic dip + 31° muzzle-down tilt during reload,
      and a 0.22s "raise from below" swap-in animation when the
      displayed weapon changes.
- [ ] **Headshot freeze-frame** + screen flash on kill.

### Zombie polish (AI is in)
- [x] **Runner lunge wind-up tell** — runner now spends 0.2s in a
      visible "winding up" state before the speed burst kicks in:
      red body tint + pulsing glowing-red eye spheres at head height,
      orientation-tracked. The lunge is now anticipated, not "I died
      fast." (Crawler tell is its squished no-head silhouette;
      boss tell is the magenta stripe + 1.7×1.5 scale.)
- [x] **Per-type audio tells** — done (c4170d6): groan / growl / hiss /
      roar per type + melee snarl, positional. See audio section.

### Rendering
- [x] **Particle system** — done (b362902): `src/particles.{c,h}`,
      256-slot pool, muzzle flash + casing eject (Weapon_Fire), blood
      mist on hits, frag explosion burst. HUD-tint muzzle hack removed.
      `--screenshot-particles` dev mode verifies.
- [x] **Post-FX render target** — done (563068c): world pass renders
      into a screen-size RT, `data/shaders/postfx.fs` fullscreen quad
      does thresholded bloom (PaP, raygun, muzzle), vignette, hit-flash
      red overlay, low-HP heartbeat desat/pulse. HUD stays unprocessed;
      missing shader = old direct path. `--screenshot-postfx` verifies.
- [ ] **Frustum culling for props** — bounding-sphere test before each
      `DrawProp`. Matters once more props are in scene.

### Equipment / categories
- [x] **Wire `WeaponCategory`.** HUD now reads `category`:
      `PRIMARY` / `SPECIAL` is shown as a small tag above the held
      weapon name. Foundation for further category-driven UI.
  - [x] **Lethal + tactical slots** with own bindings — G (kbd) /
        DPad-Up (pad) throws a frag; H / DPad-Down throws a stun. New
        `Throwable` entity in `entities.{c,h}` with gravity + XZ wall
        bounce + floor settle; frag detonates on 2 s fuse (260 dmg
        peak, 5 m radius, linear falloff, friendly-fire on local
        player); stun freezes every zombie in 5.5 m for 4.5 s
        (Enemy.stunTimer drives speed×0.2 + no bite). Players start
        with 2 of each, capped at 4.
  - [ ] **Melee as a weapon slot** (bowie knife / bat) — deferred.
        Pulling V out into a 3rd slot is too much churn vs. the rest;
        leaving the button-melee model in place.
  - [x] **HUD grouping** — equipment badges (G / H + counts) continue the
        bottom-left loadout row after the perk badges (redesigned
        2026-06-02); current-weapon line shows a `PRIMARY` / `SPECIAL` tag.

### Audio (full pass done 2026-06-12, c4170d6)
- [x] **Per-map music** — done: streams `data/audio/<name>.ogg` from the
      map's `ATMOSPHERE { music name }` at low volume; silent skip if the
      file is absent. No .ogg ships yet — authoring the loops is the
      remaining (content, not code) work.
- [x] **3D positional zombie groans** — done: per-type vocals (groan /
      growl / hiss / roar + melee snarl), squared rolloff to 20 m,
      camera-relative pan, max 2 new voices per tick.
- [x] **Footstep + reload SFX** — done: 3 footstep variants with
      sprint/walk/crouch cadence; 2-stage reload (mag-out, mag-in at 55%
      of reload time).
- [x] **Bleedout vignette + heartbeat audio** when downed — heartbeat
      done c4170d6 (lub-dub ramps 1.30 s → 0.55 s, other SFX ducked);
      vignette/desat pulse done 563068c via the post-FX `lowHp` uniform
      (pins at 1.0 while downed).

### Map format / engine integration
- [ ] **`LIGHTS x y z r g b range`** in `.map` — per-map placed lights;
      pass an array of N to the lit shader.
- [ ] **`sky_tint` → `sky.fs`** — already parses, just needs the uniform
      hooked up.

### Assets (specs already in `data/models/ASSETS.md`)
- [ ] **Textures (5)** — `floor_concrete.png`, `ground_dirt.png`,
      `wall_brick.png`, `wall_plaster.png`, `ceiling_wood.png` (last is
      reserved, no current map draws a ceiling). 512² seamless PNGs;
      engine, mipmaps, wrap mode, tile UVs all already wired.
- [x] ~~`pap_chamber.obj`~~ — superseded: the 2026-06-04 `pap_machine.obj`
      bakes the chamber into the cabinet; the shutter stays procedural by
      design (aligned to the model's mouth at y≈1.99).

### Infrastructure
- [ ] **`tests/map_parser_test.c`** with bad/good fixtures fed through
      `Level_Validate`. Asserts error counts + resulting state.
- [ ] **CI**: GitHub Action that runs `./build/shooter --validate` on
      every `data/maps/*.map` and fails the build on any error.
- [ ] **`tests/weapon_parser_test.c`** — `.weapon` files now have
      enough machinery to deserve a fixture-based test too. Higher value
      since ef1afe2: the files are the ONLY source of weapon stats (no
      compiled fallbacks), so a malformed file = a broken weapon.

## Mid-term

- [ ] **Persistent player stats** — extend `settings.cfg` with total
      kills, headshots, revives, best round, hours played. Show on the
      game-over / menu screens.
- [ ] **4th map** — `rooftop.map` using `PROP sandbag_stack` for vertical
      cover. Proves the new format on something different from indoor
      boxes.
- [ ] **Map thumbnails** — `data/maps/<name>.png` rendered next to the
      name in the map picker.
- [x] **Mystery box slow-roll** — `Interact_UpdateMBox` now uses a
      decelerating flip rate (f(u) = 12u - 1.375u²) so the box spins
      fast early and slows to ~1 Hz as time runs out, then locks to
      the final weapon for the last 0.5 s so the box visibly "lands".
- [ ] **Per-type zombie OBJs** (`zombie_runner.obj`, `_crawler.obj`,
      `_boss.obj`) with stripe baked into geometry. Currently a
      `DrawCube` overlay on top of the shared `zombie.obj`.
- [ ] **Server browser / LAN discovery** — current flow is "type the IP".
- [ ] **MP-correct recoil.** Currently the per-weapon recoil writes
      directly to `p->pitch`/`p->yaw` server-side. In solo/host this is
      fine. For MP clients, local input rewrites pitch each frame so the
      kick may flicker. Fix is client-side prediction: when fireTimer
      transitions positive, apply matching kick locally too.

## Long-tail

- [ ] **Shadow maps** — FBO + depth-only pass from moon direction. Big
      visual win, biggest scope.
- [ ] **LOD** — same OBJ at any distance; matters on larger maps.
- [ ] **Replays** — deterministic input log + playback. Needs the
      network protocol to also serialise inputs.
- [ ] **`SPAWN_REGION x z sx sz`** — random spawn within a box instead
      of fixed points.
- [ ] **`PROP` rotation around X/Z** — currently only yaw.
- [ ] **Save / load mid-round** — currently restart only.
- [ ] **Crash reporting** — minidump on segfault.
- [ ] **Weapon attachments** (red dot, suppressor) — `.weapon` file format
      is now ready to grow per-weapon sub-objects.

## Done this session (2026-06-02)

- [x] **glTF skeletal animation pipeline** — `anim.{c,h}` (AnimModel +
      AnimState, GPU skinning via `UpdateModelAnimationBones`),
      `world_skinned.vs` (skinning VS sharing `world.fs`), `--anim-test`
      validator. Verified end-to-end on a 2-bone test rig.
- [x] **CoD balance pass** — zombie HP curve (150+100/round → +10%/round
      from 10), 50 contact damage, HP regeneration (110/s after 4 s),
      CoD scoring (10/60/100/130), retuned weapon stats (M14 now semi-auto),
      PaP reserve ×2. See HANDOFF balance gotcha.
- [x] **HUD redesign** — circular perk/equipment badges with vector glyphs,
      unified shadowed-text + gold/dim palette, rounded HP/stamina bars,
      cleaner round/ammo/points blocks.
- [x] **Door lintel fix** — `interiorWallNoClip[]` so door header walls
      render + block bullets but don't wall off the (open) doorway.
- [x] **Weapon sizing/colour fix** — world weapon draws life-size
      (decoupled from the viewmodel scale knob); viewmodel under flat
      lighting so it stops colour-swinging while turning.
- [x] **Docs** — `data/ANIMATIONS.md` (rig-first animation spec),
      `ASSETS.md` rig-first note, README/HANDOFF/TODO refresh, new
      `blender-game-asset` skill with a connectivity auditor.

## Done this session (later 2026-06-01)

- [x] **Dynamic COD-style crosshair.** Four ticks that bloom with
      weapon spread, sprintBlend, and a per-shot kick (scaled by
      `spreadDeg` + `recoilPitch`); collapses to a centre dot while
      ADS. Smoothed with a framerate-independent exp lerp at ~18 Hz;
      kick decays at 36 px/s. Outlined for legibility against bright
      surfaces. Lives in `hud.c::HudUpdateFeedback` +
      `HudDrawCrosshair`.
- [x] **Lethal + tactical equipment.** Frag (G / DPad-Up) and stun
      (H / DPad-Down) throwables with full Throwable entity in
      `entities.{c,h}` — gravity, XZ wall bounce, floor settle, fuse,
      AoE damage / AoE stun. Stun applies `Enemy.stunTimer` →
      speed×0.2 + bite suppressed; cyan body tint while stunned.
      Players start with 2 of each, capped at 4. `BA_THROW_LETHAL` /
      `BA_THROW_TACTICAL` bind actions added, default DPad Up/Down.
      `ACT_THROW_LETHAL` / `_TACTICAL` packets for MP. Serialized in
      snapshot (SerThrowable + SerPlayer.lethals/tacticals +
      SerEnemy.stunTimer). NET_PROTO_VERSION bumped to 8.
- [x] **Splash damage on bullet impact.** New `splashRadius` +
      `splashDamage` on `WeaponDef`; `splash R D` directive in
      `.weapon` files. Damage falls off linearly to 0 at radius;
      never applied to the directly-hit enemy. Raygun set to
      3.0 m / 70 dmg.
- [x] **HUD grouping.** Bottom-right gains an equipment block
      (lethal + tactical badges with G/H key hints + counts) next to
      the perks. Current weapon now shows a small `PRIMARY` /
      `SPECIAL` category tag above the name.

## Done this session (2026-06-01 evening)

- [x] **Per-type zombie AI** — runner lunge (0.55 s bursts at ~1.9×
      speed with ~3 s cooldown, gated on `Level_PathClearXZ`), crawler
      serpentine weave, boss steamroll with 3× contact damage,
      per-type retarget cadence + bite damage/cooldown.
- [x] **Proactive obstacle avoidance** — `Level_PathClearXZ(from, dir,
      radius, dist)` helper + ~0.4 s lookahead fan-probe (30°/60°/90°/
      125° both sides) in `Enemies_Update` ZS_INSIDE branch. Commits
      the chosen escape direction for 0.3 s to avoid jitter.
- [x] **Weapon fire modes** — `FireMode` enum (SEMI / BURST / AUTO),
      edge-detected press via `Player.prevFireHeld`, burst progression
      state machine in `Game_Tick`. Pistol/shotgun/raygun = semi,
      SMG = auto, rifle = 3-round burst.
- [x] **Per-weapon recoil** — `recoilPitch` + `recoilYaw` on WeaponDef,
      applied to `p->pitch`/`p->yaw` after the shot fires so the first
      round lands where aimed. ADS halves.
- [x] **Damage dropoff per weapon** — `dropoffStart`/`End`/`MinMul`;
      `Bullet` now carries `origin` + `weaponIdx`; distance scales
      damage at hit. Shotgun steep, rifle near-flat, raygun flat.
- [x] **Per-folder weapon definitions** — `WEAPONS[]` no longer const;
      `Weapons_Load()` recursively scans `data/weapons/*.weapon` via
      raylib's `LoadDirectoryFilesEx`, parses the line-based format,
      populates the slot via the `id` field, loads the model relative
      to the file's directory. Storage for `weaponModels[]` /
      `weaponTune[]` moved from `assets.{c,h}` to `weapons.{c,h}`.
- [x] **`WeaponCategory` enum** added (PRIMARY / SPECIAL / MELEE /
      LETHAL / TACTICAL). Currently 4 guns = PRIMARY, raygun = SPECIAL.
- [x] **5 new weapon models** authored in Blender — M1911, MP5,
      Olympia, M14, Ray Gun. Multi-material, Principled-BSDF, exported
      with `forward_axis='Z'` + `export_triangulated_mesh=True`.
- [x] **Discovered raylib OBJ ngon-overflow bug** — `LoadOBJ` SEGVs on
      n-gons > 20 vertices. Documented in HANDOFF + fixed by always
      exporting triangulated.

## Done in earlier passes

- [x] **Bullets overhauled** — muzzle-origin spawn, per-weapon
      `bulletSpeed` / `bulletLife` / `tracerWidth`, swept-slab vs.
      AABB + swept-cylinder vs. enemy, billboard tracers with
      speed-derived colour, decals on hit.
- [x] **Lit world shader** — `world.{vs,fs}` v2 with directional sun +
      flat normals derived in the fragment shader.
- [x] **Decal system** — 96-slot ring buffer in `src/decals.{h,c}`,
      blood on enemy hits + impact pock on geometry.
- [x] **All Round-2 prop models authored** — `door.obj`, `door_frame.obj`,
      `obstacle_crate.obj`, `obstacle_barrel.obj`, 4 perk machines,
      `pap_machine.obj`, `wallbuy_panel.obj`, `powerup_drop.obj`,
      `player_m.obj`.
- [x] **Door geometry fit** — header above 2.5 m cutout, frame + door
      share scale, plank barricade removed.
- [x] **Zombie HP recolouring removed** — authored colours show
      regardless of damage.
- [x] **Connected, properly-coloured zombie + raygun OBJs** (fixed
      "exploded" geometry; MTL Kd via Principled BSDF).
- [x] **Texture registry** (`TextureId`, `textures[]`) + rlgl
      immediate-mode `DrawTexturedBox` / `DrawTexturedFloor` with
      mipmaps + seamless tiling.
- [x] **`data/shaders/world.{vs,fs}`** — linear distance fog.
- [x] **`data/shaders/sky.{vs,fs}`** — procedural night skybox.
- [x] **Map format rewrite** — compact `WALL … DOOR … AS name`,
      `WINDOW … LOCKED_BY name`, `ATMOSPHERE` block, `ROOM` blocks,
      `OBSTACLE x z sx sz [h]`, `PROP` lines.
- [x] **`./build/shooter --validate <map>`** — line-numbered errors,
      exit code 0 / 1.
- [x] **All three shipped maps** (default, nacht, factory) converted to
      the new format.
- [x] **`Render_World3D` fog/sky shader swap** (and the
      `rlSetShader(..., rlGetShaderLocsDefault())` fix that resolved
      the menu-launch SEGV).
