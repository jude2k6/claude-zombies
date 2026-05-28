# TODO

## Done in this pass

- [x] **Player stats + game-over screen** — per-player kills, headshots,
      melee kills, shots fired/hit (accuracy %), revives. Synced in the
      snapshot (added to `SerPlayer`). Shown in a table on the game-over
      screen.
- [x] **Stamina** — local `Player.stamina` (0..100). Drains while
      sprinting + moving, regens while walking. Sprint disables when
      empty. Small sliver shown under the HP bar.
- [x] **More zombie types** — `ZombieType` added: Runner (round 4+, x1.6
      speed, x0.55 HP, yellow stripe), Crawler (round 7+, half height,
      no head — can't be headshotted), Boss (round multiples of 5,
      x1.5 size, x6 HP, magenta stripe). Picked at spawn time; rendered
      with size/color cues; synced via `SerEnemy.type`.
- [x] **Door pathfinding** — when a door sits between a chasing zombie
      and its target and the door is open, the zombie routes via the
      doorway position first.
- [x] **Revive teammates** — hold `E` over a downed player for 4s. New
      `IK_REVIVE` interactable; progress synced as `reviveAsTarget`.
      Granting the revive credits the reviver's stats.
- [x] **Mystery Box** — `MBOX x z` in map files. `F` to roll ($950,
      4s spin animation), `F` again by the same player within 8s to take
      the rolled weapon into the current slot. State synced via new
      `mbox*` fields in the snapshot header.

## Bumped

- Network protocol: v3 → v4 (snapshot layout changed for stats, revive,
  zombie type, and Mystery Box).

## Future / not started

Still on the menu from the earlier ranked list:

- Audio (procedural SFX)
- Map selector + a second map (`MBOX` already supported, just need UI)
- Mule Kick (3rd weapon slot)
- Pack-a-Punch Tier 2
- Floating damage numbers
- Settings persistence
- Prone
