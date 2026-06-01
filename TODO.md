# TODO

Live punch list, ordered by impact-per-effort. The architecture for almost
every item below is already in place — these are mostly authoring + small
shader / system additions.

## Next up (highest impact-per-effort)

### Weapons polish (architecture is in)
- [ ] **Verify the 5 new viewmodels in first-person.** This session
      replaced all Quaternius models with new ones authored via Blender
      MCP, but only the SMG + rifle were rebuilt with proper magazine /
      receiver connectivity. Pistol / shotgun / raygun may still have
      visible seams. `model_scale` was bumped to 10–12 across the board
      (my models are in metric metres; Quaternius were in inches).
      Tune `model_scale` / `model_offset` in `data/weapons/<name>/<name>.weapon`
      until each gun looks right at the viewmodel anchor.
- [ ] **Reload + weapon-swap viewmodel animation** — currently instant
      snap. Even a 200 ms slide/rotate would be a feel upgrade.
- [ ] **Headshot freeze-frame** + screen flash on kill.

### Zombie polish (AI is in)
- [ ] **Per-type tells (visual + audio).** Differentiated AI is
      meaningful but unreadable right now:
  - Crawler has no stripe currently (runner = yellow, boss = magenta).
  - Runner lunge has no wind-up — needs a brief growl + red eye-glow
    so the burst is readable, not just "I died fast."
  - Distinct groan per type via raylib's positional audio attenuation
    (covered below in audio).

### Rendering
- [ ] **Particle system** — pooled additive-blend particles for muzzle
      flash (replaces the HUD-tint hack), casing eject, blood mist on
      zombie hits. ~256-particle pool, simple vel + gravity + lifetime.
- [ ] **Post-FX render target** — wrap `Render_World3D` in a
      `RenderTexture2D`; final fullscreen quad does bloom on bright
      pixels (PaP, raygun, muzzle), vignette, hit-flash red overlay,
      low-HP heartbeat pulse. Unlocks every subsequent "feel" upgrade.
- [ ] **Frustum culling for props** — bounding-sphere test before each
      `DrawProp`. Matters once more props are in scene.

### Equipment / categories
- [ ] **Wire `WeaponCategory`.** The enum exists (PRIMARY / SPECIAL /
      MELEE / LETHAL / TACTICAL) and weapons are tagged, but nothing
      reads `category` yet. Foundation for:
  - [ ] **Lethal + tactical slots** with own input bindings (G frag,
        LB stun). Needs a `Throwable` entity type (projectile + gravity
        + bounce + timed detonation + AoE damage / stun effect).
  - [ ] **Melee as a weapon slot** (bowie knife / bat) replacing the
        "knife is a button" model.
  - [ ] **HUD ordering** — group inventory display by category.

### Audio (still the thinnest area)
- [ ] **Per-map music** — `ATMOSPHERE { music name }` already parses;
      load `data/audio/<name>.ogg` on map load, loop quietly.
- [ ] **3D positional zombie groans** via raylib's audio attenuation.
      Layer in per-type variants (runner growl, crawler hiss, boss roar).
- [ ] **Footstep + reload SFX** — game is jarringly quiet outside of
      gunfire.
- [ ] **Bleedout vignette + heartbeat audio** when downed.

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
- [ ] `pap_chamber.obj` (chamber animates separately — deferred)

### Infrastructure
- [ ] **`tests/map_parser_test.c`** with bad/good fixtures fed through
      `Level_Validate`. Asserts error counts + resulting state.
- [ ] **CI**: GitHub Action that runs `./build/shooter --validate` on
      every `data/maps/*.map` and fails the build on any error.
- [ ] **`tests/weapon_parser_test.c`** — `.weapon` files now have
      enough machinery to deserve a fixture-based test too.

## Mid-term

- [ ] **Persistent player stats** — extend `settings.cfg` with total
      kills, headshots, revives, best round, hours played. Show on the
      game-over / menu screens.
- [ ] **4th map** — `rooftop.map` using `PROP sandbag_stack` for vertical
      cover. Proves the new format on something different from indoor
      boxes.
- [ ] **Map thumbnails** — `data/maps/<name>.png` rendered next to the
      name in the map picker.
- [ ] **Mystery box slow-roll** — currently cycles instantly; should
      lerp through `MBOX_ROLLING` over the 4 s timer.
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
