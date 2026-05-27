# TODO

## Done since last update

- [x] **Doors stayed open across new games** — added `Level_Reset()` (closes
      doors, refills boards, clears PaP) called from
      `Menu_StartSoloGame` / `Menu_StartHostedGame`.
- [x] **Map data lives in a file** — `data/maps/default.map`, parsed by
      `Level_LoadFromFile`. Adding a new map is just dropping in another
      `.map` file and the parser does the rest. Hardcoded fallback kept so
      the binary still runs if the data dir is missing.

## Bugs

- [x] **Repair barricades is broken** — fixed: `Interact_UpdateRepairs` now
      only zeroes windows nobody is repairing this tick, so progress
      accumulates across frames.
- [x] **Can't buy a 3rd weapon when both slots are full** — the underlying
      buy logic was already replacing the current slot; added a clearer
      prompt: "BUY *X* (replaces *Y*)" so the user can swap to the slot they
      want to overwrite before pressing F.
- [x] **Something blocks the doorway** — the obstacle at `(0, -18)` size
      `(8, 3, 2)` was sitting directly in the door corridor. Moved it to
      `(-10, -16)` and shrunk it.

## Features

- [x] **Zombie ↔ player collision** — local player gets pushed out of any
      inside-state zombie it overlaps; zombie gets nudged back slightly.
- [x] **Melee attack** — `V` swings a knife. 1.8m range, ~70° forward arc,
      150 damage, 0.6s cooldown. Networked via `ACT_MELEE`.
- [x] **Sprint** — hold `Shift` for ~×1.6 movement speed.
- [x] **Crouch** — hold `C` or `Ctrl` for ~×0.55 speed and lower eye
      height (~0.6m).
- [ ] **Prone** — *deferred.*
- [x] **Headshot damage** — bullets hitting the head sphere deal ×2 damage
      and award bonus +30 hit / +50 kill points.
- [x] **Spectator mode when downed** — free-fly camera (WASD + mouse,
      Space/Shift for vertical). Reviving at round break is unchanged.
- [x] **Power-up drops** — ~6% chance per kill. Five types: Max Ammo,
      Nuke, Double Points (20s), Insta-Kill (20s), Carpenter. Sync over
      snapshot. Pick up by walking over the floating cube.
