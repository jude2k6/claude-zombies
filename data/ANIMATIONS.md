# Animation design — weapons & entities

Companion to `data/models/ASSETS.md` (which covers the static low-poly look,
scale, axes, palette). That doc still applies to silhouette and materials.
**This doc covers everything that needs a skeleton + animation clips** for the
glTF/GPU-skinning pipeline (`src/anim.{c,h}`, `data/shaders/world_skinned.vs`).

The point of the list below is so each rig is authored **once** with a
consistent set of named clips, and the engine just looks them up by name.

---

## How the pipeline consumes these (read before authoring)

- **Format: glTF `.glb`.** OBJ cannot hold a skeleton. Animated assets are
  `.glb`; static props stay OBJ.
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

> Gameplay timings (current `.weapon` files): reload — pistol **1.3s**,
> smg **1.8s**, shotgun **2.0s**, rifle **1.5s**, raygun **2.6s**. Fire
> cooldown — pistol 0.18, smg 0.067 (auto), shotgun 0.45, rifle 0.13,
> raygun 0.22. Weapon-swap raise ≈ **0.22s**. Speed Cola → reloads at 2×.

### Common viewmodel clips (all guns)
| clip            | once/loop | ~dur | notes |
|-----------------|-----------|------|-------|
| `idle`          | loop      | 2–4s | subtle breathing sway; the default pose |
| `fire`          | once      | ≤ fireCooldown | must finish within the cooldown; auto guns loop it |
| `reload`        | once      | = reloadTime | from a partially-full mag |
| `reload_empty`  | once      | ≥ reloadTime | empty-mag variant (slide/bolt release); optional but classic |
| `raise`         | once      | 0.22s | swap-in / round-start; replaces the procedural raise |
| `lower`         | once      | 0.18s | swap-out |
| `sprint`        | loop      | 1–2s | gun lowered/tilted while sprinting |
| `inspect`       | once      | 2–4s | flavor; optional |

### Per-weapon specifics
| weapon | model id | reload style | fire notes | special |
|--------|----------|--------------|------------|---------|
| **M1911** pistol | `PISTOL` | mag out / mag in / slide if empty | semi; light slide kick | `Mustang` (PaP) is cosmetic — same rig |
| **MP5** SMG | `SMG` | curved mag swap, charging handle | **auto** → `fire` loops cleanly, no per-shot settle | — |
| **Olympia** shotgun | `SHOTGUN` | **break-action**: `reload` = hinge open → eject 2 shells → insert 2 → snap closed. Single shared reload (always reloads both) | semi, heavy break-barrel kick | only 2 shells; no partial reload needed |
| **M14** rifle | `RIFLE` | en-bloc/mag swap; `reload_empty` does bolt release | **semi** (now, not burst); crisp single kick | — |
| **Ray Gun** | `RAYGUN` | **energy cell** swap, not a mag — `reload` = pop spent cell, slot fresh one, coils spin up | semi; muzzle coils glow on `fire` (drive emissive via material/tint, geometry can pulse) | wonder weapon — give `idle` a faint coil idle motion |

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
5. Player third-person model (co-op visibility).
6. Machine polish (PaP, box, perks) as time allows.
