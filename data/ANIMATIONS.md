# Animation design — weapons & entities

Companion to `data/models/ASSETS.md` (which covers the static low-poly look,
scale, axes, palette). That doc still applies to silhouette and materials.
**This doc covers everything that needs a skeleton + animation clips** for the
glTF/GPU-skinning pipeline (`src/anim.{c,h}`, `data/shaders/world_skinned.vs`).

The point of the list below is so each rig is authored **once** with a
consistent set of named clips, and the engine just looks them up by name.

---

## Rigging-first: rebuild every model for animation

**The whole asset set is being re-authored rig-first.** Do NOT model a static
mesh and try to rig it afterward — that is what forced the zombie rebuild
(feet-together, arms-down, no joint geometry = nothing deforms cleanly). Every
new model is built *from the start* to deform:

- **Build in a neutral rig pose, not a display pose.** Characters in
  **T- or A-pose** (limbs out, away from the body); weapons with the
  **action/slide/charging handle modelled as a separate, movable piece** in
  its rest position.
- **Put geometry where it bends.** Add edge loops at every joint that flexes —
  shoulders, elbows, wrists, hips, knees, jaw on characters; the slide rails,
  hinge, trigger, charging handle, magazine on guns. A single cuboid limb
  cannot bend; give each moving part its own loops.
- **Plan the skeleton before the mesh is final.** Decide bone names + hierarchy
  up front (they're the engine API — see naming below) and model so each bone
  has clean geometry to drive. Shared skeleton across a family (all zombie
  types; all weapon viewmodels via shared arms).
- **Separate the moving parts.** Anything that translates/rotates as a unit
  (gun slide, bolt, charging handle, magazine, hinge, box lid, perk dispenser
  flap) is its own mesh island bound rigidly to its own bone — so it can be
  keyed independently. Rigid-part skinning is fine and on-style for the blocky
  look; smooth weight-painting only where things actually flex (limbs, jaw).
- **Origin + scale per `ASSETS.md`** still apply (feet/grip origin, 1 m =
  1 unit), but for **facing use the glTF rule below** (author +Y in Blender),
  not ASSETS.md's OBJ -Z-in-Blender rule;
  and the static look/palette rules there still hold — rig-first changes
  *topology and pose*, not the art direction.
- **Exception:** genuinely inert props (crates, barrels, sandbags, boards) and
  the FX/code-driven items in the last table can stay static OBJ. Everything
  that could plausibly move is built rig-first.

---

## How the pipeline consumes these (read before authoring)

- **Format: glTF `.glb`.** OBJ cannot hold a skeleton. Animated assets are
  `.glb`; static props stay OBJ.
- **Facing — author +Y in Blender (NOT the OBJ rule).** Export with
  `export_yup=True`, which maps Blender **+Y → raylib -Z** = the forward/look
  direction enemies and players are turned to. So characters must face **+Y in
  Blender** (toes/jaw/eyes pointing +Y). This is the OPPOSITE of the OBJ
  convention in `ASSETS.md` ("face -Z in Blender") — do **not** carry the OBJ
  axis rule over to `.glb`. Authoring -Y was the cause of the first
  backwards-zombie; verify with `--anim-test` and, if it still looks reversed
  in-game, the fix is the model's facing, not the draw yaw.
- **One shared skeleton per family.** All variants of a thing (zombie types;
  player skins; both view + world weapon models that must match) should use
  the **same bone names + hierarchy**, so one set of clips drives every mesh
  bound to it. The mesh/texture varies per variant; the animation does not.
- **Clip names are the API.** The engine fetches clips with
  `Anim_FindClip(model, "name")`. Use the **exact lowercase names** in the
  tables below. Unknown/extra clips are harmless (ignored).
- **No root motion.** Code owns world translation (walk speed, knockback,
  bob, recoil kick). Animate **in place**, around the model origin. A walk
  cycle moves the *legs*, not the root.
- **Frame rate ~60.** raylib bakes glTF clips at ~58.8 fps
  (`ANIM_FPS = 1000/17`). Author at 24/30/60 in Blender — duration in
  *seconds* is what matters; playback is driven by time, not frame index.
- **Loop vs one-shot** is set per-play in code (`Anim_Play(..., loop, speed)`),
  but author the clip so it *reads* right for its intent (locomotion loops
  seamlessly; reload/fire/death play once).
- **Speed-scalable.** Some clips are replayed at non-1× speed (Speed Cola
  halves reloads → reload clips play at 2×; Double Tap raises fire rate).
  Author them clean at 1× and don't bake in easing that breaks when scaled.
- **Timing must match gameplay constants.** Durations below are pulled from
  the live code (`WeaponDef.reloadTime`, `fireCooldown`, fuse/lunge timers).
  If a clip is longer than its gameplay window it gets cut off; match them.

### Naming convention
`idle`, `walk`, `run`, `attack`, `death`, `spawn`, `fire`, `reload`,
`reload_empty`, `raise`, `lower`, `sprint`, `inspect`, `open`, `close`,
`dispense`, `use`. Suffix variants with `_a`/`_b` for random alternation
(e.g. `attack_a`, `attack_b`). Per-type overrides get a clear name
(`lunge`, `crawl`).

---

## Weapons

Each gun has up to **two** rigs:

- **Viewmodel** (`<name>_vm.glb`) — first person, **arms + gun**, the rich clip
  set. Only the *local* player's held weapon drives it. This is where the
  reload/fire detail lives.
- **World model** (`<name>_world.glb`) — third person + pickups/wall-buy/box
  display. Just the gun (no arms), minimal clips. Optional at first; the
  static OBJ can keep serving world display until teammates-see-reloads
  matters.

Shared viewmodel arm skeleton across all 5 guns is ideal (one set of arms,
swap the gun mesh + gun-specific bones), so hand poses and the `raise/lower`
clips are authored once.

> **Implemented (2026-06-03): shared arms + bone-bolted gun.** The 4 non-pistol
> guns now use ONE rigged arms model, `data/models/arms_vm.glb` (arms+hands, the
> full clip set above, no gun baked in). The equipped gun is a *separate* model
> attached to the **`hand.R`** bone each frame in `render.c:DrawArmsViewmodel`
> (via `Anim_BoneMatrix(am, st, handR)` — the bone's model-space transform —
> composed as `gunLocal * bone * root`). So authoring a new gun viewmodel = just
> the gun mesh (any of the existing world OBJs works); the arms, hand pose, and
> recoil/reload motion are inherited. **The arms model must include a `hand.R`
> bone** for the gun to ride. A per-gun grip nudge (now the `vm_grip_pos/rot/
> scale` keys of each `.weapon` file, read into `weaponGrip[]`; the code moved
> from `render.c` to `viewmodel.c:DrawArmsViewmodel`) seats each gun — re-check
> with `--screenshot-viewmodels`. **Since ef1afe2 the M1911 also rides the
> shared arms path** (the `wi != W_PISTOL` gate is gone); `pistol_vm.glb`
> remains on disk as authored-but-unused content. New guns follow the
> shared-arms model, not a combined glb.
>
> ⚠️ **2026-06-13: the hands still don't sit on the guns.** A Blender pass
> (commit 5cf3a4e) welded the arm meshes (were disconnected vertex islands)
> and re-exported with `export_yup=False` so the forearms point forward —
> arms now render fully. BUT the yup flip rotated the arms frame (`hand.R`
> Y/Z swapped), invalidating the gun-seating calibration: the base
> `MatrixRotateX(PI*0.5)` in `viewmodel.c:DrawArmsViewmodel` and every
> `.weapon` `vm_grip_*` value were tuned for the OLD frame. NEXT step is to
> re-derive those against the new frame (re-tune `vm_grip_rot/pos/scale` and
> likely the base rotation) with `--screenshot-viewmodels`. See HANDOFF "IN
> PROGRESS". Note the asset ships **7 clips** (`fire idle lower raise reload
> reload_empty sprint`) — the `idle_pistol` and `inspect` clips referenced
> elsewhere in this doc are NOT in `arms_vm.glb` yet, so `vm_pose PISTOL` is a
> no-op until authored.

> Gameplay timings (current `.weapon` files): reload — pistol **1.3s**,
> smg **1.8s**, shotgun **2.0s**, rifle **1.5s**, raygun **2.6s**. Fire
> cooldown — pistol 0.18, smg 0.067 (auto), shotgun 0.45, rifle 0.13,
> raygun 0.22. Weapon-swap raise ≈ **0.22s**. Speed Cola → reloads at 2×.

### Common viewmodel clips (all guns)
| clip            | once/loop | ~dur | notes |
|-----------------|-----------|------|-------|
| `idle`          | loop      | 2–4s | subtle breathing sway; the default pose |
| `fire`          | once/loop | ≤ fireCooldown | **includes the action cycling** (blowback — see below). Must finish within the cooldown; auto guns loop it |
| `reload`        | once      | = reloadTime | tactical reload — round still in chamber, so **no** charging-handle/slide rack |
| `reload_empty`  | once      | ≥ reloadTime | **mandatory**, not optional. Fired-dry reload: the bolt/slide is locked back or must be cycled, so this ends with the **charging handle pulled back / slide released** to chamber the first round. Longer than `reload`. |
| `raise`         | once      | 0.22s | swap-in / round-start; replaces the procedural raise |
| `lower`         | once      | 0.18s | swap-out |
| `sprint`        | loop      | 1–2s | gun lowered/tilted while sprinting |
| `inspect`       | once      | 2–4s | flavor; optional |

> **Two reloads per gun are required**, not one. The engine picks `reload`
> when the mag still had rounds, and `reload_empty` (with the cocking action)
> when it was fired completely dry — exactly like CoD. Author both.

### Blowback / action cycling
Where it's mechanically real, the moving action must **reciprocate on every
shot** — the `fire` clip cycles the slide/bolt back and forward (eject + chamber).
This is keyed on a **separate `slide`/`bolt` bone**, so it reads even when the
fire clip is short or looping. Drive it from the action, not a static jolt.

| gun | action | per-shot blowback in `fire`? |
|-----|--------|------------------------------|
| **M1911** | pistol slide | **yes** — slide kicks fully back and forward each shot; locks back on last round (feeds into `reload_empty`) |
| **MP5** | reciprocating bolt (closed) | **yes** — bolt/charging handle blurs back-and-forth while the auto `fire` loops |
| **M14** | semi-auto bolt | **yes** — bolt carrier cycles each shot |
| **Olympia** | break-action, no auto-cycle | **no** — nothing reciprocates; only the break-open happens during reload. Keep recoil to muzzle climb + barrel shudder |
| **Ray Gun** | energy, no mechanical action | **no** — no blowback; sell the shot with coil pulse / recoil kick instead |

### Per-weapon specifics
| weapon | model id | charging handle / action | empty reload (`reload_empty`) | special |
|--------|----------|--------------------------|-------------------------------|---------|
| **M1911** pistol | `PISTOL` | slide, locks back when empty | mag out → mag in → **slide release** drops the slide to chamber | `Mustang` (PaP) cosmetic, same rig |
| **MP5** SMG | `SMG` | left-side charging handle | curved mag swap → **yank charging handle back** and release | — |
| **Olympia** shotgun | `SHOTGUN` | break-action (no charging handle) | `reload`/`reload_empty` are the same: hinge open → eject 2 shells → insert 2 → snap closed | only 2 shells; no partial reload |
| **M14** rifle | `RIFLE` | op-rod / charging handle, bolt locks back empty | mag swap → **pull op-rod handle / hit bolt release** to chamber | **semi** now (not burst) |
| **Ray Gun** | `RAYGUN` | energy cell, no charging handle | pop spent cell → slot fresh cell → coils spin up (no racking) | wonder weapon; faint coil idle motion |

### World/third-person weapon clips (optional, later)
| clip | once/loop | notes |
|------|-----------|-------|
| `fire` | once | muzzle/slide for teammates to read |
| `reload` | once | so teammates *see* a reload |
| `idle` | loop | held pose; also used for wall-buy/box display |
| `spin` | loop | mystery-box / pickup presentation (or keep code-driven spin) |

---

## Enemies (zombies)

**One shared zombie skeleton** for all four types (humanoid: spine, head,
2× arm, 2× leg, optional jaw). Author the clip set once on it; types differ
by **mesh swap + a few override clips + code speed scaling**, not by
re-rigging. (`Crawler` is the exception — see below.)

> Timings: runner lunge = **0.20s wind-up** then **0.55s** burst; melee
> touch cooldown **0.7s**; bleed-out **30s**; per-type speed scales in code
> (runner ×1.6, crawler ×0.55, boss ×0.85).

### Shared clips (every zombie)
| clip       | once/loop | ~dur | notes |
|------------|-----------|------|-------|
| `spawn`    | once      | 1–2s | climb-through-window / claw-up; plays on appear |
| `walk`     | loop      | 1–1.5s | the shamble; **base locomotion, code scales speed** |
| `run`      | loop      | 0.6–0.9s | faster gait used at higher speed / runner |
| `attack_a` | once      | ≤0.7s | swipe; must read within touch cooldown |
| `attack_b` | once      | ≤0.7s | second swipe variant (alternate for variety) |
| `death`    | once      | 1–1.5s | ragdoll-ish collapse (no physics; baked) |
| `death_head` | once    | 1–1.5s | headshot variant (snap back then drop) — optional |
| `hit`      | once      | 0.2s  | flinch on non-killing hit — optional |

### Per-type overrides / extras
| type | uses | extra clips | notes |
|------|------|-------------|-------|
| **Normal** | shared set | — | baseline shuffle |
| **Runner** | shared + | `lunge_windup` (0.20s, telegraph — pairs with the red-eye tell), `lunge` (0.55s explosive reach) | currently a tint+eye-sphere tell; replace/augment with these |
| **Crawler** | **own variant** | `crawl` (loop), `crawl_attack` | no head, drags along the ground — likely a *separate* skeleton/mesh since the pose is fully different |
| **Boss** | shared + | `steamroll` (heavy walk loop), `attack_heavy` (big overhead) | scaled-up mesh; slower, weightier timing |

---

## Player (third-person world model)

`player_m` → `player.glb`. Only seen by **teammates** in co-op (you never see
your own body — that's the viewmodel). Shared humanoid skeleton; ideally the
same arm bones as the weapon viewmodels so held-weapon poses line up.

| clip | once/loop | notes |
|------|-----------|-------|
| `idle` | loop | weapon-ready stance |
| `walk` | loop | code scales by move speed |
| `run` | loop | sprint gait (Stamin-Up affects speed, not the clip) |
| `fire` | once | upper-body additive ideally; simplest = full-body twitch |
| `reload` | once | mirror of the held weapon's reload (rough is fine) |
| `revive` | loop | kneeling/reaching while reviving a downed teammate (4s `REVIVE_TIME`) |
| `downed` | loop | crawling/propped-up bleed-out pose (30s window) |
| `death` | once | full death when bleed-out expires |

---

## Interactables / machines

These are mostly **single instances** with simple, often looping motion.
Many are currently faked in code (PaP chamber bob, box spin). Skeletal
versions are optional polish; listed so the rig is planned if/when made.

### Perk machines (Juggernog, Speed Cola, Double Tap, Stamin-Up)
Shared cabinet skeleton; each perk is a **mesh/texture variant**.
| clip | once/loop | notes |
|------|-----------|-------|
| `idle` | loop | sign flicker / subtle bob; the always-on state |
| `dispense` | once | buy → bottle/can drops into the tray; plays on purchase |

### Pack-a-Punch (`pap.glb`)
> `PAP_DURATION = 4.0s` — the `work` clip must fit this window.
| clip | once/loop | notes |
|------|-----------|-------|
| `idle` | loop | chamber bob (replaces the code-driven `pap.bob`) |
| `insert` | once | weapon slides into the chamber |
| `work` | loop | 4s upgrade churn (lights, shake) |
| `eject` | once | upgraded weapon presented |

### Mystery Box (`mystery_box.glb`)
> `MBOX_ROLL_TIME = 4.0s` roll, `MBOX_WAIT_TIME = 8.0s` grab window.
| clip | once/loop | notes |
|------|-----------|-------|
| `open` | once | lid swings up |
| `roll` | loop | weapon spins/cycles inside during the 4s slow-roll |
| `present` | once | chosen weapon rises and holds for the grab window |
| `close` | once | lid shuts (also the teddy-bear fly-away, if you want it) |

### Doors (`door.glb`)
| clip | once/loop | notes |
|------|-----------|-------|
| `open` | once | swing/slide; plays on purchase (collision already clears) |

---

## Things that probably DON'T need skeletal animation

Keep these code-/shader-driven; rigging them is wasted effort.

| thing | why | how it moves |
|-------|-----|--------------|
| **Throwables** (frag, stun) | tiny, fast, physics-owned | code: gravity + tumble spin; 2s fuse → particle FX |
| **Power-ups** (Max Ammo, Nuke, Double Points, Insta-Kill, Carpenter) | floating icons | code: bob + spin + glow pulse (already) |
| **Window boards** | break/repair is discrete | swap board count + a particle burst |
| **Static props** (crate, barrel, sandbag, board) | inert | none — stay OBJ |
| **Tracers / muzzle flash / blood** | FX, not meshes | particle system (planned) |

---

## Suggested authoring order (prove pipeline → highest impact)

1. **One zombie** (Normal) with `walk` + `attack_a` + `death` — first real
   asset through the pipeline; wire `AnimState` onto `Enemy` and swap the
   `DrawProp` call for `Anim_Draw`. Validate with `--anim-test zombie.glb`.
2. Fill out zombie clips (`spawn`, `run`, `attack_b`) + per-type overrides.
3. **M1911 viewmodel** with `idle`/`fire`/`reload`/`raise` — replaces the
   procedural viewmodel anim; proves the weapon view path + reload sync.
4. Roll the viewmodel clip set across the other 4 guns (shared arm rig).
   *Engine path done (`arms_vm.glb` + bone-bolted gun, 2026-06-03). 🚧
   2026-06-13: hands still don't sit on the guns — asset-side fix in progress
   (bad skin weights / idle pose in `arms_vm.glb`), plus `idle_pistol` clip
   still to author. See HANDOFF "IN PROGRESS".*
5. Player third-person model (co-op visibility). *Done (`player.glb`, wired).*
6. Machine polish (PaP, box, perks) as time allows.
