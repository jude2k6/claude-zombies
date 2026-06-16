# Viewmodel Rigs — Decision & Authoring Reference

How the first-person viewmodel holds and animates every gun. The chosen
architecture (combined per-weapon rigs) and the authoring reference that carries
over from the original clean-room analysis. The detailed *rejected* alternative
(one shared arms mesh + left-arm IK) has been dropped from this doc — it couldn't
do mechanical parts or per-gun animations.

---

## 0. DECISION — combined per-weapon viewmodel rigs

A shared arms mesh with the gun bolted on (and IK'd support hand) was considered
and **rejected**, because by design it **cannot**:

- **Animate mechanical gun parts** — a charging handle that racks, slide
  blowback, mag drop/insert, hammer fall, cylinder swing, break-action. A rigid
  mesh on a bone can't articulate internally.
- **Give each gun its own animations** — one shared clip set forces every weapon
  through the same retimed reload/fire. No Olympia break-open, no MP5
  mag-swap-and-rack, no Ray Gun cell swap as distinct motions.

**Chosen: each weapon is ONE rigged glTF** containing arms + gun + the gun's
mechanical part-bones, with its OWN per-gun clip set (idle / raise / lower / fire
/ reload / reload_empty / sprint / inspect). The animator poses the hands AND the
mechanism together in the same file.

Why this is correct:

- **Mechanical parts are free** — the charging handle / slide / mag / hammer are
  bones; the clip animates them and the operating hand together (a sync problem
  that's intractable with separate gun/arm rigs).
- **Per-gun animation identity is free** — every gun owns its clips.
- **It eliminates the hand-placement bug** — hands and gun authored in one rig
  are on the gun by construction. No runtime seating, no `vm_grip_*`, no
  bone-bolt math, no IK. The engine just plays clips on a skinned model.

Accepted cost: **each gun is authored as a full rig + clip set in Blender** — no
"drop in an OBJ + tune four numbers." `arms_vm.glb` becomes the *authoring base*
(arm mesh + canonical `root/upperarm/forearm/hand` skeleton), not a
runtime-shared bolt target.

### Per-weapon mechanism checklist (what each rig must animate)

| weapon | mechanical part-bones | empty-reload mechanism |
|---|---|---|
| M1911 pistol | slide, hammer, magazine | slide locks back → released to chamber |
| MP5 SMG | bolt/charging handle, magazine | mag swap → **yank charging handle back** |
| Olympia shotgun | break hinge, 2 shells | break open → eject → insert 2 → snap closed |
| M14 rifle | op-rod/charging handle, bolt (locks back), magazine | mag swap → op-rod/bolt release |
| Ray Gun | energy cell, coils | pop cell → insert fresh cell → coils spin up |

### Implementation path

1. **Engine:** reuse `anim.c` skinned-glTF playback per weapon (one `AnimState`
   per local viewmodel). `viewmodel.c:DrawCombinedRigViewmodel` auto-loads
   `data/weapons/<id>/<id>_vm.glb` and plays the clip set, framed by the shared
   `CRIG_*` constants. **Done.**
2. **Convention:** lock the combined-rig contract — canonical arm bone names,
   mechanism bone naming per archetype, clip names, −Z muzzle, one shared metric.
3. **Assets:** author the 5 rigs from the `arms_vm.glb` base via the
   `blender-game-asset` skill (rig-first + connectivity audit). **MP5 done
   (`smg_vm.glb`); pistol/shotgun/rifle/raygun remain.** Re-scale pistol/raygun
   to the shared metric at author time.
4. Validate each via `--anim-test` and `--screenshot-viewmodels`; once all 5
   exist, retire the bolt-on fallback (`DrawArmsViewmodel` / `weaponGrip[]` /
   `vm_grip_*` / `arms_vm.glb` / gun-only OBJ path).

---

## 1. Authoring reference (measured from the assets)

**Model-space convention (make it a written rule):** muzzle/forward = **−Z**,
up = **+Y**, right = **+X**, origin near the **receiver**. A gun violating this
is re-exported, not special-cased. Express sockets/offsets in this frame, metres.

**Where the hands go** — measured grip/region positions in raw (pre-`model_scale`)
model units; an authoring reference for posing the hands on each gun:

| gun | id | model_scale | trigger-hand grip | support-hand region | muzzle (min Z) |
|---|---|---|---|---|---|
| M1911 pistol | PISTOL | 35.0 | `grip` (0,−0.063,0.151) | none (1-handed; optional cup) | −0.326 |
| MP5 SMG | SMG | 10.0 | rear-bottom (0,−0.091,0.064) | `polymer` foregrip (0,0.004,0.244) | −0.613 |
| Olympia shotgun | SHOTGUN | 10.0 | rear-bottom (0,−0.052,0.086) | `wood` forend (0,−0.029,−0.10) | −0.733 |
| M14 rifle | RIFLE | 10.0 | rear-bottom (0,−0.052,0.107) | `wood` forestock (~0,0,0.03) | −1.040 |
| Ray Gun | RAYGUN | 11.0 | `grip` (0,−0.090,0.120) | none (energy pistol) | −0.420 |

The trigger hand barely moves between guns; the **support hand** holds all the
variance (pistol/raygun: none; SMG: ~modest forward reach; shotgun/rifle: far
forward on the forend/forestock).

## 2. Open / to fix at author time

- **Gun model-space is not a shared metric.** `model_scale` ranges 10×–35×;
  **`pistol.obj` is authored ~2.5× life size** (0.54 m) and the raygun mildly so.
  Re-scale to a shared metric when authoring the combined rigs so all 5 share one
  scale (also fixes oversized world draws — see `TODO.md`).
- **No `inspect` clip** exists in `arms_vm.glb` (it ships 7 clips), and
  **`idle_pistol` is absent** — `vm_pose PISTOL` is a no-op on the fallback path.
  Author both into the combined rigs if wanted.
- **No per-finger bones** on the legacy arms rig — finger wrap is mesh art. The
  combined-rig template (MP5) adds a curled grooved finger-bank + opposing thumb;
  follow it (`blender-game-asset` skill).
