# Arms-Rig Generalisation — Design Doc

**Status:** Clean-room design. No code or assets were read beyond `data/models/` and
`data/weapons/`. The existing `vm_*` keys in the `.weapon` files were deliberately ignored;
this is a from-scratch proposal for how *one* arms asset holds *all* guns.

**Goal:** One shared first-person arms mesh + skeleton holds and animates every gun across the
standard FPS clip set (idle, raise, lower, fire, reload, reload_empty, sprint, inspect) without
re-rigging or re-animating per weapon. Adding a gun = drop in a mesh + a few numbers.

---

## 0. DECISION (2026-06-13) — this supersedes §2's recommendation

The clean-room recommendation below (static gun OBJ bolted to `hand.R` + one shared arm
clip set + left-arm IK) is **NOT adopted.** It optimises for cheap authoring but, by design,
**cannot do two things the project requires:**

- **Mechanical gun parts** — a charging handle that racks back, slide blowback, magazine
  drop/insert, hammer fall, cylinder swing, break-action. A rigid mesh on a bone can't
  articulate internally.
- **Per-gun animations** — a single shared clip set forces every weapon to use the same
  retimed reload/fire motion. No Olympia break-open, no MP5 mag-swap-and-rack, no Ray Gun
  cell swap as distinct motions.

**Chosen architecture: combined per-weapon viewmodel rigs.** Each weapon is ONE rigged glTF
containing arms + gun + the gun's mechanical part-bones, with its OWN per-gun clip set
(idle / raise / lower / fire / reload / reload_empty / sprint / inspect). The animator poses
the hands AND the mechanism together in the same file.

Why this is the correct call:

- **Mechanical parts are free** — the charging handle / slide / mag / hammer are bones in the
  rig; the relevant clip animates them, and the hand that operates them is choreographed in the
  same clip (so the support hand and the charging handle move together — a sync problem that is
  intractable if the gun and arms are separate rigs).
- **Per-gun animation identity is free** — every gun owns its clips.
- **It eliminates the hand-placement bug entirely** — when hands and gun are authored in one
  rig, the hands are on the gun by construction. No runtime seating, no `vm_grip_*`, no
  bone-bolting math, no IK solver. The engine just plays clips on a skinned model, which
  `anim.c` already supports (this is what `pistol_vm.glb` was before the combined path was
  removed — that removal should be reconsidered).

The cost, accepted deliberately: **each gun must be authored as a full rig + clip set in
Blender** — no "drop in an OBJ + tune four numbers." `arms_vm.glb` becomes the *authoring base*
(arm mesh + the canonical `root/upperarm/forearm/hand` skeleton you start each weapon from),
not a runtime-shared bolt target.

What still carries over from the analysis below: the **model-space convention** (muzzle −Z,
up +Y, origin at receiver — §2.2), the **measured grip/region positions** (§1.2, now an
authoring reference for where to pose the hands), the **scale audit** (pistol OBJ ~2.5×
oversized, raygun mildly so — fix at author time so all rigs share one metric), and the finding
that **no `inspect` clip exists yet** and `idle_pistol` is absent.

### Per-weapon mechanism checklist (what each rig must animate)

| weapon | mechanical part-bones | empty-reload mechanism |
|---|---|---|
| M1911 pistol | slide, hammer, magazine | slide locks back → released to chamber |
| MP5 SMG | bolt/charging handle, magazine | mag swap → **yank charging handle back** |
| Olympia shotgun | break hinge, 2 shells | break open → eject → insert 2 → snap closed |
| M14 rifle | op-rod/charging handle, bolt (locks back), magazine | mag swap → op-rod/bolt release |
| Ray Gun | energy cell, coils | pop cell → insert fresh cell → coils spin up |

### Implementation path (engine + assets)

1. **Engine:** reuse `anim.c` skinned-glTF playback per weapon (one `AnimState` per local
   viewmodel); retire the bolt-on-OBJ + `vm_grip_*` + `weaponGrip[]` path. This is *simpler*
   than the rejected IK approach — no solver to write.
2. **Convention:** lock the combined-rig contract — canonical arm bone names, mechanism bone
   naming per archetype, clip names, −Z muzzle, one shared metric scale.
3. **Assets:** author the 5 weapon rigs from the `arms_vm.glb` base via the
   `blender-game-asset` skill (rig-first + connectivity audit). Re-scale the pistol/raygun to
   the shared metric at author time.
4. **Prove it on one gun first** (recommend the MP5 — it exercises the charging-handle rack you
   specifically want), validate via `--anim-test` and `--screenshot-viewmodels`, then roll the
   pattern across the rest.

---

## 1. Inventory of facts (measured from the assets)

### 1.1 The shared arms rig — `data/models/arms_vm.glb`

Parsed directly from the GLB (glTF coords: **+Y up, −Z forward, +X right**, metres).

**Skeleton — 7 joints, two symmetric 3-bone chains under one root:**

```
root
├── upperarm.R → forearm.R → hand.R
└── upperarm.L → forearm.L → hand.L
```

Skin `arms_rig`, joint order `[root, upperarm.R, forearm.R, hand.R, upperarm.L, forearm.L, hand.L]`.
Four skinned meshes (`forearm_L/R`, `glove_L/R`) bound to that skeleton. Generator: Blender glTF I/O.

**Rest-pose world positions (metres):**

| joint | world pos (x,y,z) |
|---|---|
| root | (0.00, 0.45, −0.30) |
| upperarm.R | (0.15, 0.44, −0.26) |
| forearm.R | (0.11, 0.22, −0.09) |
| **hand.R** | **(0.02, 0.04, 0.00)** |
| upperarm.L | (−0.19, 0.44, −0.22) |
| forearm.L | (−0.16, 0.24, −0.03) |
| **hand.L** | **(−0.04, 0.04, 0.20)** |

The rest pose is *not* a gun-hold pose (the hands are ~0.24 m apart and hand.L is at +Z, behind
where a gun would be). The hold is produced by the animation clips, not the bind pose.

**Animation clips present (7):**

| clip | length (s) |
|---|---|
| idle | 2.50 |
| fire | 0.33 |
| raise | 0.21 |
| lower | 0.17 |
| reload | 1.42 |
| reload_empty | 1.71 |
| sprint | 1.71 |

Every clip animates all 7 joints (21 channels = 7 joints × T/R/S). **There is no `inspect`
clip** — the prompt's standard set names one, but the asset does not contain it (see §5).

**What the clips do to the hands** (sampled start/mid/end, world space):

- **idle** and **fire**: `hand.R` is pinned at ≈ **(0.02, 0.04, 0.0)** at every keyframe; during
  fire it only dips in Z (≈ −0.056 at the recoil peak) and returns. `hand.L` is pinned at
  ≈ **(−0.04, 0.04, 0.20)**. So the neutral hold is a *fixed two-hand clasp* baked into the clips.
- **reload**: `hand.R` stays put (≈ 0.02, 0.04, 0.0) while `hand.L` swings far away
  (to ≈ 0.056, 0.418, 0.291 at mid) — the classic "support hand fetches a mag" motion.
- **sprint**: *both* hands drop and pull back/inward (hand.R ≈ −0.068, 0.277, 0.172;
  hand.L ≈ −0.145, 0.359, 0.348) — the weapon is lowered across the body.

**The single most important fact for the design:** across idle/fire/reload the **right hand is the
stable anchor** and the **left hand is the mover**. The natural mount point for any gun is `hand.R`;
the support hand is the variable that must adapt per weapon.

### 1.2 The five guns — `data/weapons/<id>/<id>.obj`

All five share a consistent model-space convention, discovered from the geometry:

- **Longest axis is always Z**; the **muzzle is at −Z** (barrel tips are the min-Z verts), the
  **stock/rear is +Z**. **Up is +Y**. Grips sit **below centre (−Y) toward the rear (+Z)**.
- Meshes are authored small and rely on a per-gun `model_scale` (pistol 35×, raygun 11×, the
  long guns 10×). All numbers below are **raw model units**; multiply by `model_scale` for the
  in-hand size. Group names are semantic (`grip`, `trigger`, `mag`, `wood`, `sight`, …) and are
  the best machine-readable hints for where the hands go.

| gun | id | model_scale | raw dims (x,y,z) | longest (scaled, ≈m) | has explicit `grip` group? |
|---|---|---|---|---|---|
| M1911 pistol | PISTOL | 35.0 | 0.062 × 0.325 × 0.542 | 19.0 (huge — see note) | **yes** |
| MP5 SMG | SMG | 10.0 | 0.143 × 0.329 × 1.055 | 10.6 | no (infer) |
| Olympia shotgun | SHOTGUN | 10.0 | 0.075 × 0.194 × 1.123 | 11.2 | no (infer) |
| M14 rifle | RIFLE | 10.0 | 0.092 × 0.211 × 1.549 | 15.5 | no (infer) |
| Ray Gun | RAYGUN | 11.0 | 0.100 × 0.273 × 0.618 | 6.8 | **yes** |

> Note: the pistol's 35× scale makes its *scaled* length ~19 "units" which is clearly not metres —
> the gun scales are tuned for on-screen viewmodel size, not world-true dimensions. This is an
> important wrinkle: **gun model-space is not a shared metric space.** See §2.4 and §5.

**Grip / functional regions (raw model units, group bbox centres):**

| gun | trigger-hand grip (rear-bottom) | support-hand region (fore) | muzzle tip (min Z) |
|---|---|---|---|
| PISTOL | `grip` group ctr (0, −0.063, 0.151), bbox y∈[−0.148,0.022] | none (one-handed); optional cup at grip | z = −0.326 |
| SMG | rear-bottom cluster (0, −0.091, 0.064); `mag` ctr (0,−0.109,0.016) | `polymer` foregrip ctr (0, 0.004, 0.244)→front-bottom (0,0.02,−0.39) | z = −0.613 |
| SHOTGUN | rear-bottom (0, −0.052, 0.086); `wooddark` (stock) ctr (0,0.01,0.281) | `wood` forend ctr (0,−0.029,−0.10), bbox z∈[−0.55,0.38] | z = −0.733 |
| RIFLE | rear-bottom (0, −0.052, 0.107); `mag` ctr (0,−0.035,−0.075) | `wood` forestock ctr (0,0,0.03)→front-bottom (0,0,−0.745) | z = −1.040 |
| RAYGUN | `grip` group ctr (0, −0.090, 0.120), bbox y∈[−0.155,−0.025] | none (energy pistol) | z = −0.420 |

**Archetype spread, normalised to scaled units (×model_scale), the thing one clip set must absorb:**

- **PISTOL** — trigger grip ≈ (0, −2.2, +5.3) scaled; one hand; barrel short. Support hand, if used,
  *cups under* the firing hand (no forward reach).
- **RAYGUN** — like the pistol: single `grip` group at (0, −0.99, +1.32) scaled, no forend.
- **SMG** — two-hand, but compact: trigger grip near z≈+0.6, foregrip near z≈+2.4 (raw 0.244)
  → support hand reaches ~1.8 scaled units forward of the trigger hand.
- **SHOTGUN** — two-hand, long forend (`wood` spans z∈[−0.55,0.38]); support hand rides the slide /
  forend well forward (~−0.10 raw, ~−1.0 scaled forward of trigger).
- **RIFLE** — the extreme case: 1.55-unit-long body, support hand on the forestock at z≈+0.03 raw
  but the trigger grip at z≈+0.11; the *physical* spread of the two hands is the widest of the set,
  and the muzzle is far out at z=−1.04.

So the support-hand forward reach the rig must cover ranges from **0 (pistol/raygun)** to roughly
**0.25–0.3 raw model units ≈ several scaled units (rifle/shotgun)**. The trigger hand barely moves
between guns; the support hand is where all the variance lives — matching exactly what the clips do.

---

## 2. Generalisation strategy

### 2.1 The core decision: anchor the gun to `hand.R`, drive `hand.L` by IK. (Hybrid.)

There are three honest options:

1. **Pose-only (parent gun to hand.R, both hands baked by clips).** Cheapest at runtime, zero IK.
   But it *cannot* satisfy "one clip set for all guns": the baked hand.L position (−0.04, 0.04, 0.20)
   is correct for exactly one grip spread. A pistol has no forend there; a rifle's forend is far
   forward of it. You would need per-gun clips — which the brief forbids.
2. **Full IK (gun placed in the world/at the camera, both hands IK'd to grip sockets).** Maximally
   flexible, but throws away the hand-authored character of the clips (the recoil dip, the reload
   choreography, the sprint sway) and makes every pose a solver problem. Overkill and fragile.
3. **Hybrid (recommended): gun rigidly mounted to `hand.R`; clips drive the skeleton as authored;
   a two-bone IK pass on the *left* arm retargets `hand.L` onto a per-gun support socket.**

**Pick option 3.** It matches the asset's own design intent (right hand = anchor, left hand =
mover) and isolates per-gun variance to a single IK target plus one mount transform. The clips stay
universal: they provide *timing and style* (the recoil dip, the mag-fetch arc, the sprint lower),
while the IK provides *placement* (where this particular gun's forend actually is).

Concretely, per frame:

1. Evaluate the active clip on the 7-joint skeleton (universal, gun-agnostic).
2. Compute `hand.R` world transform from the pose.
3. Place the gun: `gun_world = hand.R_world · grip_mount⁻¹` (see §2.3) so the gun's **trigger
   socket** lands in the right hand.
4. Solve **two-bone IK on the left arm** (upperarm.L, forearm.L) so `hand.L` reaches the gun's
   **support socket** (transformed into world by step 3). Blend the IK in by a per-gun, per-clip
   weight (see §2.5) so that during reload/sprint — when the clip deliberately pulls the support
   hand *off* the gun — IK is disabled and the authored animation wins.

This gives: universal clips, one mount transform + one support socket + a few weights per gun.

### 2.2 Bone, axis, and unit conventions (the contract)

The arms and guns must agree on these or attachment is ambiguous:

- **Arms skeleton names are canonical and fixed:** `root`, `upperarm.[LR]`, `forearm.[LR]`,
  `hand.[LR]`. Any future arms variant must keep these names so the attach/IK code is rig-agnostic.
- **Handedness:** trigger hand = `hand.R`, support hand = `hand.L`, always. (Left-handed players,
  if ever needed, are a mirror at the rig-instance level, not a per-gun concern.)
- **Coordinate frame:** glTF/engine convention **+X right, +Y up, −Z forward**, metres, as the GLB
  already uses. All sockets and offsets are expressed in this frame.
- **Gun model-space convention (already true of all five meshes, make it a written rule):**
  **muzzle/forward = −Z, up = +Y, right = +X, origin near the receiver.** A gun that violates this
  must be re-exported, not special-cased.
- **Scale:** gun OBJ units are *not* metres; each gun carries `model_scale`. The mount math must
  apply `model_scale` to the gun before seating, and **all per-gun socket offsets below are
  expressed in raw (pre-scale) model units** so the artist can read them straight off the mesh.

### 2.3 The socket convention (how a gun declares where the hands go)

A gun declares, in its own model space (raw units), four things:

- **`grip_socket`** — transform (translation + rotation) of the trigger-hand grip. The shared arms'
  `hand.R` bone is seated *here*. Translation ≈ the `grip`/rear-bottom cluster centre; rotation
  orients the palm around the grip's rake.
- **`support_socket`** — transform of the support-hand hold (forend/slide/cup). `hand.L` is IK'd
  here. For one-handed guns this is omitted (or flagged `none`).
- **`muzzle`** — point + forward axis (the −Z tip), for muzzle flash / tracer origin and to define
  the gun's aim line independent of mount jitter.
- **`scale`** — `model_scale` (already in the roster).

These are *gun-authored frames*, not arm-authored. The shared arms never change; each gun says "put
the right hand here, the left hand there." The mount is then:

```
gun_world      = hand.R_world · inverse(grip_socket_local · scale)
support_world  = gun_world · support_socket_local · scale     // IK target for hand.L
muzzle_world   = gun_world · muzzle_local · scale
```

### 2.4 Minimal per-gun metadata — suggested schema

Add a small block to each `.weapon` (or a sidecar `<id>.grip.json`). Translations in **raw model
units**, rotations as Euler degrees or quaternion, in gun model-space:

```
# --- arms attachment (clean-room scheme) ---
grip_socket_pos      0.0  -0.063  0.151      # trigger-hand seat (hand.R)
grip_socket_rot      0    0       0          # palm rake about the grip
support_socket_pos   0.0   0.004  0.244      # support-hand seat (hand.L IK target); omit if 1-handed
support_socket_rot   0    0       0
support_hand         two                     # one | two   (two => IK the left arm onto support_socket)
muzzle_pos           0.0   0.05  -0.613      # -Z tip; flash/tracer origin + aim axis
# scale comes from existing model_scale
```

Defaults: if `grip_socket_*` is absent, fall back to the rear-bottom cluster centroid auto-derived
from the mesh (the values in §1.2 were computed exactly this way — a sane auto-default). If
`support_hand` is `one`, no left-arm IK is run and the clips' authored hand.L pose is used as-is
(which reads as the off-hand resting/bracing).

### 2.5 Keeping one clip set valid across wildly different guns

Three mechanisms, layered:

1. **Mount absorbs the trigger-hand difference for free.** Because the gun is seated *to* hand.R via
   its own `grip_socket`, the firing hand is always correct regardless of gun size — no clip change.
2. **Left-arm two-bone IK absorbs the support-hand spread.** The clip's authored hand.L is treated
   as a *fallback / blend source*, not gospel. When `support_hand == two`, IK retargets hand.L from
   the authored pose to the gun's `support_socket`. Pistol → IK target ≈ the firing hand (off-hand
   cups under); rifle → IK target far forward on the forestock. Same clip, different solved pose.
3. **Per-clip IK weight curve** so authored choreography wins when it should. The IK weight is high
   in idle/fire/raise/lower (hand belongs on the gun) and ramps to **0** during the parts of
   reload/reload_empty where the clip deliberately yanks hand.L away to fetch a magazine, and during
   sprint where both hands lower off-weapon. Practically: a single scalar `ikL_weight(clip, t)`,
   authored once on the shared rig (e.g. as a curve baked alongside each clip), reused by every gun.

Optional polish, not required for v1:

- **Additive aim/recoil layer:** instead of baking recoil into `fire`, an additive on hand.R lets
  per-gun recoil scale (the roster already has `recoil_pitch/yaw`) drive viewmodel kick without new
  clips. Keeps fire timing shared, kick magnitude per-gun.
- **Pose-offset per gun:** a static additive offset on the whole rig (small T/R) to fine-tune a
  given gun's screen framing without touching clips — equivalent to the roster's `model_offset`.

---

## 3. Per-archetype handling

**Pistol (M1911) — one-handed, optional support cup.**
`grip_socket` = `grip` group centre (0, −0.063, 0.151). `support_hand = one` for a true one-hand
hold, or `two` with `support_socket` placed just under/forward of the grip (≈ 0, −0.10, 0.12) for a
two-hand "cup". With `one`, hand.L keeps its authored idle pose (reads as off-hand bracing). No
forend, short barrel; the mount handles it entirely.

**Ray Gun — energy pistol, treat exactly like the pistol.** Explicit `grip` group at
(0, −0.090, 0.120). One-handed. Wider/heavier-looking body but the grip seat is the same kind of
single rear-bottom socket. `support_hand = one`. The only special-case is cosmetic (glow/glass
groups) — irrelevant to the rig.

**SMG (MP5) — compact two-hander.** `grip_socket` ≈ rear-bottom (0, −0.091, 0.064);
`support_socket` on the `polymer` foregrip (0, 0.004, 0.244). Modest forward reach (~0.18 raw),
well within left-arm IK range. Standard two-bone IK, full weight in idle/fire.

**Rifle (M14) — the stress test, long two-hander.** Body 1.55 units long, muzzle at z=−1.04.
`grip_socket` ≈ rear-bottom (0, −0.052, 0.107); `support_socket` on the `wood` forestock
(0, 0.0, 0.03) — but note the forestock spans z∈[−0.575, 0.496], so the artist should pick a
comfortable point (≈ z = −0.10…+0.05 raw). This is where left-arm IK earns its keep: the authored
hand.L (−0.04, 0.04, 0.20) is nowhere near the rifle forestock, and IK pulls it there. Check IK
reach: if the solved support pose hyperextends the left arm, nudge the whole-rig pose-offset back
(rifle held slightly further from camera) rather than re-animating.

**Shotgun (Olympia) — break/pump action, hand LEAVES the forend.** `grip_socket` ≈ rear-bottom
(0, −0.052, 0.086) (wrist of the stock, `wooddark` stock at z≈+0.281); `support_socket` on the
`wood` forend (0, −0.029, −0.10). The special case is the **reload**: the support hand must come off
the forend to break the action / load shells, then return. This is handled by the **per-clip IK
weight** (§2.5): during reload/reload_empty the left-arm IK weight drops to 0 so the authored
mag-fetch motion plays, then ramps back to 1 to re-seat the hand on the forend. No bespoke shotgun
clip needed — it rides the shared reload with IK disabled in the middle. (If the shared reload's arc
reads wrong for a break action, that's a clip-authoring nicety, not a rig-architecture problem.)

---

## 4. "Adding a 6th gun" checklist

1. Author/obtain the mesh as `data/weapons/<id>/<id>.obj` (+`.mtl`) following the model-space rule:
   **muzzle −Z, up +Y, right +X, origin at the receiver.** Use semantic group names
   (`grip`, `mag`, `wood`/forend, `sight`) — they double as auto-default hints.
2. Add the roster identity to `<id>.weapon` (id, name, category, `model`, `model_scale`) as today.
3. Pick the grip socket: eyeball the `grip`/rear-bottom cluster, set `grip_socket_pos/rot`.
   (If omitted, the auto-default from the mesh's rear-bottom centroid is usually close.)
4. If two-handed, set `support_socket_pos/rot` on the forend/slide and `support_hand two`;
   otherwise `support_hand one`.
5. Set `muzzle_pos` to the −Z tip.
6. Launch the viewmodel, eyeball idle + reload + sprint. Tune only: grip socket, support socket, and
   the whole-gun pose-offset/scale. **No new clips, no re-rig.**
7. (Optional) tune recoil via the existing roster `recoil_*` if using the additive recoil layer.

That is the entire per-gun cost: a mesh + ~5 lines of metadata + a screenshot tuning pass.

---

## 5. Risks / open questions / what could not be determined

- **No `inspect` clip exists in the asset.** The brief's standard set lists inspect; the GLB has
  idle/fire/raise/lower/reload/reload_empty/sprint only. Either inspect must be authored (one shared
  clip, gun-agnostic, IK-driven on hand.L like the others) or it's out of scope. Flagged for the
  owner. Everything else in the set is present.
- **Gun model-space is not a shared metric space.** `model_scale` ranges 10×–35× and the pistol's
  scaled length (~19) is clearly tuned for screen size, not world-true metres. The mount math must
  consistently apply `model_scale`; socket offsets are kept in raw units to stay readable, but two
  guns at different scales will *feel* different in hand-spread terms. Worth a one-time pass to
  confirm each gun's scaled grip lands in the left-arm IK reach envelope.
- **IK reach envelope is unmeasured.** I have rest-pose and clip-pose hand positions but did not
  derive max left-arm extension (upperarm.L→forearm.L→hand.L bone lengths give ~0.28+0.33 from the
  node translations, but joint limits/comfortable cone are unknown). The rifle is the gun most
  likely to hyperextend the support arm; verify in-engine.
- **Grip *rotation* is unmeasured.** I measured socket *positions* confidently from group bboxes;
  the palm rake (rotation) of each grip is an artist eyeball value. The scheme carries
  `grip_socket_rot`/`support_socket_rot` for it; expect a tuning pass per gun.
- **hand.R is assumed rigid to the gun.** The clips show hand.R essentially static in idle/fire (it
  only dips for recoil). If any future clip moves hand.R relative to the gun on purpose (e.g. a
  charging-handle pull with the firing hand), the "gun rigidly parented to hand.R" assumption breaks
  and that action needs either a third socket or a brief gun-detach. None of the current five need
  this. The shotgun's break-action and the bolt guns' charging handle are handled on the *support*
  side, which the IK-weight scheme already covers.
- **No per-finger bones.** The rig is forearm/hand only — finger wrap around triggers/grips is mesh
  art (the `glove_L/R` meshes), not riggable. Grip realism is therefore limited by the static glove
  pose; acceptable for an FPS viewmodel, noted for expectation-setting.
- **`anim_test.glb` and `player.glb`** were not used; they are out of the arms-rig scope and may or
  may not share the skeleton.
