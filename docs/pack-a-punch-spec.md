# Pack-a-Punch — Spec (CoD-style)

Reworks Pack-a-Punch (PaP) from a single 4-second timer into a CoD-style,
multi-phase machine: the player **inserts** their held weapon, the machine
**works** on it, then **presents** the upgraded weapon, which the player must
**manually retrieve**.

## Goals / feel

- You walk up, press Use, and your gun visibly leaves your hands and goes into
  the machine.
- The machine churns for a few seconds (glow + sparks + closed chamber).
- The upgraded gun pops back out and floats in front of the machine, glowing.
- It does **not** auto-return — you press Use again to take it back. Until you
  do, the machine is occupied and nobody else can use it.

## State machine

One machine, one job at a time. Phases (`pap.phase`):

| Phase        | Meaning                                   | Exit                              |
|--------------|-------------------------------------------|-----------------------------------|
| `PAP_IDLE`   | Free, ready to take a weapon              | Owner pays + presses Use → INSERT |
| `PAP_INSERT` | Gun animating from hands into the chamber | `timer<=0` → WORK                 |
| `PAP_WORK`   | Chamber closed, upgrading                 | `timer<=0` → READY                |
| `PAP_READY`  | Upgraded gun presented, awaiting pickup   | Owner presses Use → IDLE          |

Timings (`types.h`):

- `PAP_INSERT_TIME = 0.8s`
- `PAP_WORK_TIME   = 3.5s`
- READY: indefinite (no timer)
- `PAP_COST = 5000` (unchanged)

### Transitions

**IDLE → INSERT** (`Interact_UsePackAPunch`)
Requires: `phase==IDLE`, held slot is `owned && !packed`, `points >= PAP_COST`.
Effect: deduct cost; record `ownerPlayer`, `slotInProgress`, `weaponIdx`;
`phase=INSERT`, `timer=PAP_INSERT_TIME`. The slot is now **locked** (see below).

**INSERT → WORK / WORK → READY** (`Interact_UpdatePaP`, host-authoritative)
Pure countdown on `timer`.

**READY → IDLE** (retrieval, `Interact_Do` on `IK_PAP`)
Only the owner (alive) may retrieve. Effect: mark the slot
`packed = true`, refill ammo/reserve to packed capacities, clear lock, reset
machine to `IDLE`.

### Locking & safety

While `phase != IDLE` the slot identified by `(ownerPlayer, slotInProgress)` is
**locked**:

- The first-person viewmodel for that slot is hidden (the gun is in the
  machine, not the hands).
- `Weapon_Fire` is blocked for that slot.
- The owner may still switch to and use their *other* inventory slot.

Safety: if the owner goes inactive or dies (`!alive`) at any point, the machine
auto-finalizes — the upgrade is applied to the slot (if still valid) and the
machine returns to IDLE — so a weapon can never be permanently stranded.

## Interaction prompts (`IK_PAP`)

| Condition                              | Prompt                              |
|----------------------------------------|-------------------------------------|
| IDLE, can afford, held gun upgradable  | `[F] PACK-A-PUNCH  -  5000`         |
| IDLE, can't afford                     | same, red                           |
| IDLE, held gun already packed          | `Already Pack-a-Punched`            |
| INSERT / WORK                          | `Upgrading...`                      |
| READY, you are owner                   | `[F] TAKE WEAPON`                   |
| READY / busy, not owner                | `In use`                            |

## Rendering (`DrawPaP`)

Machine is drawn procedurally (a dedicated model can replace it later, same as
today's `pap_chamber.obj` note). Built from a dark cabinet with glowing purple
trim, a front **chamber slot** at ~1.2 m, and a sliding **door** panel.

Per-phase visuals:

- **IDLE**: gentle emitter glow, door open, slot dark.
- **INSERT**: the weapon model lerps from a hand-height point in front of the
  machine into the chamber slot, rotating to lie flat; the door slides shut as
  `timer` runs out. First-person viewmodel hidden for the owner.
- **WORK**: door shut, machine pulses brighter, sparks emit from the seam,
  slight shake. Weapon hidden (inside).
- **READY**: door open, the **upgraded** weapon floats out in front of the slot,
  glowing purple, bobbing and slowly spinning, until retrieved.

The presented/animating weapon uses the shared `DrawWeaponDisplay` (life-size).

## Multiplayer

Host is authoritative; `Interact_UpdatePaP` runs only via the host game tick.
Snapshot syncs `phase`, `timer`, `slotInProgress`, `ownerPlayer`, `weaponIdx`
(replacing the old `papActive` byte). Clients render purely from the snapshot;
the per-slot `packed` flag already syncs in the inventory block, so retrieval
results propagate normally.

## Out of scope (future)

- Dedicated animated GLB model for the cabinet + door.
- Per-machine facing/yaw in the `.map` format (currently presents toward +Z).
- "Leave it too long and lose it" rotation behavior from later CoD titles.
