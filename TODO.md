# TODO

> ▶ **NEXT SESSION — START HERE:** The **combined per-weapon viewmodel rig** is
> built and **proven on the MP5** (`smg_vm.glb` + `DrawCombinedRigViewmodel`).
> Remaining: **author guns 2–5** (`pistol/shotgun/rifle/raygun` `<id>_vm.glb`)
> using the updated recipe in the **`blender-game-asset` skill → "Combined
> per-weapon viewmodel rig"** (includes gripping-hands + higher-poly-arm
> template), then re-run `--screenshot-viewmodels`. Once all 5 exist, **retire
> the bolt-on path** (`armsVM`/`DrawArmsViewmodel`/`weaponGrip[]`/`vm_grip_*`/
> `vmDebugMarkers` + gun-only OBJ fallback). Decision + mechanism checklist:
> `docs/arms-rig-generalisation.md` §0. Status: `HANDOFF.md` top.
>
> ℹ️ Framing: canted CoD-style hold via the shared `CRIG_*` constants in
> `src/viewmodel.c` — scale 1.0, down −0.03, base pitch 0.06, base **roll 0.36
> (cant)**. The diagonal comes from ROLL, not pitch. Shared across all
> combined-rig guns — re-check when guns 2–5 land.

Live punch list, ordered by impact-per-effort.

## Architecture cleanup

### Warnings & safety nets
- [ ] **Fix `-Wstringop-truncation` `strncpy` warnings** in
      net.c/level.c/settings.c/main.c.
- [ ] **CI + parser tests** — backstop that keeps everything honest (see
      Infrastructure).

### Oversized translation units
- [ ] **`entities.c` (953 lines) — optional.** Four systems (enemy AI,
      bullets, throwables, powerups) in one file; stable, split only if
      it starts churning.

### Layering (sim → presentation)
- [ ] **Don't deepen `entities.c`/`game.c` → `Decals_*`/`Fx_*` calls.**
      Host sim currently writes presentation state directly (blood decals,
      camera shake from inside `Bullets_Update`). New sim code should not
      reach into render/hud/assets. Mid-term "MP-correct recoil" is the
      same family of fix.

## Next up (highest impact-per-effort)

### Animation & assets
- [ ] **Zombie clip set + per-type variants** — `spawn`/`run`/`attack_b`,
      runner `lunge`, crawler `crawl`, boss `steamroll`/`attack_heavy`.
- [~] **Weapon viewmodels** (`<name>_vm.glb`) — `idle`/`fire`/`reload`/
      `reload_empty`/`raise`; blowback on `fire`, charging-handle cock on
      `reload_empty`. MP5 done; guns 2–5 remain (see NEXT SESSION block).
  - [ ] **Author `idle_pistol` clip in `arms_vm.glb`.** Wired in code but
        clip absent — silent no-op; pistols use the two-handed foregrip
        idle. Add a one-handed pistol hold (support hand cups the grip).
- [~] **Machine polish** — PaP done; mystery-box lid + perk dispense still
      code/shader-faked. Low priority.

### Weapons polish
- [ ] **Re-export `pistol.obj` at real scale** — authored ~2.5× life size
      (0.54 m); `vm_grip_scale 0.6` hides it in first person but
      wallbuy/mystery-box/PaP world draws show it oversized.
- [ ] **In-game feel check of re-seated grips** — verify in actual play
      (ADS, sprint, reload) and fine-tune per-gun `vm_grip_pos` in
      `.weapon` files. Box odds flag for playtest: raygun `mbox_weight
      0.5` (~11% vs old 20%).
- [ ] **Headshot freeze-frame** + screen flash on kill.

### Rendering
- [ ] **Frustum culling for props** — bounding-sphere test before each
      `DrawProp`. Matters once more props are in scene.

### Equipment
- [ ] **Melee as a weapon slot** (bowie knife / bat) — deferred. V
      button-melee model stays until there's less churn elsewhere.

### Map format
- [ ] **`LIGHTS x y z r g b range`** in `.map` — per-map placed lights;
      pass an array to the lit shader.
- [ ] **`sky_tint` → `sky.fs`** — already parses, just needs the uniform
      hooked up.

### Infrastructure
- [ ] **`tests/map_parser_test.c`** — bad/good fixtures through
      `Level_Validate`; assert error counts + resulting state.
- [ ] **CI** — GitHub Action: `./build/shooter --validate` on every
      `data/maps/*.map`, fail build on any error.
- [ ] **`tests/weapon_parser_test.c`** — `.weapon` files are the ONLY
      source of weapon stats (no compiled fallbacks); malformed file =
      broken weapon. Fixture-based test needed.

## Mid-term

- [ ] **Persistent player stats** — extend `settings.cfg` with total
      kills, headshots, revives, best round, hours played. Show on
      game-over / menu screens.
- [ ] **4th map** — `rooftop.map` using `PROP sandbag_stack` for vertical
      cover. Proves the format on something different from indoor boxes.
- [ ] **Map thumbnails** — `data/maps/<name>.png` next to the name in the
      map picker.
- [ ] **Per-type zombie OBJs** (`zombie_runner.obj`, `_crawler.obj`,
      `_boss.obj`) with stripe baked in. Currently a `DrawCube` overlay.
- [ ] **Server browser / LAN discovery** — current flow is "type the IP".
- [ ] **MP-correct recoil** — recoil writes to `p->pitch`/`p->yaw`
      server-side; fine for solo/host but kicks may flicker for MP
      clients. Fix: client-side prediction — apply matching kick locally
      when fireTimer transitions positive.

## Long-tail

- [ ] **Shadow maps** — FBO + depth-only pass from moon direction.
- [ ] **LOD** — same OBJ at any distance; matters on larger maps.
- [ ] **Replays** — deterministic input log + playback. Needs network
      protocol to also serialise inputs.
- [ ] **`SPAWN_REGION x z sx sz`** — random spawn within a box instead of
      fixed points.
- [ ] **`PROP` rotation around X/Z** — currently only yaw.
- [ ] **Save / load mid-round** — currently restart only.
- [ ] **Crash reporting** — minidump on segfault.
- [ ] **Weapon attachments** (red dot, suppressor) — `.weapon` format is
      ready to grow per-weapon sub-objects.
