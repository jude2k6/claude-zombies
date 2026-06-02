# Claude Zombies

A 3D zombies shooter written in C with [raylib](https://www.raylib.com/),
[raygui](https://github.com/raysan5/raygui), and [enet](http://enet.bespin.org/).
Round-based survival in the style of *Call of Duty: Zombies* — wall-buys,
perks, Pack-a-Punch, purchasable doors, boardable windows, and up to
**4-player coop** over UDP.

Built end-to-end with [Claude Code](https://claude.com/claude-code).

## Features

- **Round-based survival** — endless waves with the classic *CoD: Zombies*
  health curve (150 + 100/round through round 9, then +10% compounding)
- **Health regeneration** — take a hit, break contact for a few seconds and
  you heal back to full, just like CoD (downed players don't regen)
- **CoD scoring** — 10 per hit, 60 body kill, 100 headshot kill, 130 melee
- **Boarded windows** — zombies spawn outside, break boards to enter,
  hold `E` to repair (and earn points)
- **Purchasable doors** — the south room (with Pack-a-Punch and the M14
  wall-buy) is locked behind a door. Spend points to open it and unlock
  that area's window as well.
- **5 weapons** with wall-buys and ammo refills, each with its own fire
  behaviour, recoil and damage drop-off
  - M1911 (starter pistol, semi) · MP5 SMG (auto) · Olympia shotgun (semi)
  - M14 rifle (semi) · Ray Gun (wonder weapon, splash damage)
- **Lethal + tactical equipment** — `G` throws a frag, `H` throws a stun
  grenade; start with 2 of each, capped at 4
- **Pack-a-Punch** — spend points to upgrade a weapon (damage ×2.5, mag ×2,
  reserve ×2, new name)
- **Mystery Box** — $950 to roll a random weapon (decelerating slow-roll);
  the owner has a few seconds to take it
- **Power-ups** — Max Ammo, Nuke, Double Points, Insta-Kill, Carpenter drop
  from kills
- **4 perks** — Juggernog (more HP), Speed Cola (faster reload),
  Double Tap (more damage, faster fire), Stamin-Up (faster movement)
- **Per-player progression** in coop — own HP, points, inventory, perks
- **Down/revive** — at 0 HP you go down; a teammate can hold `E` over you
  for ~4s to bring you back, or you'll auto-revive at the next round break.
  Game over when everyone is down at the same time.
- **Differentiated zombie AI** — Runners lunge in bursts (with a wind-up
  tell) from round 4, Crawlers weave and bite hard from round 7, Bosses
  steamroll with heavy hits every 5 rounds; all do proactive obstacle
  avoidance
- **Door pathfinding** — zombies route via the doorway when chasing a
  player through an opened door
- **Dynamic crosshair** that blooms with spread and collapses in ADS;
  low-HP vignette, hit markers, directional damage indicators
- **Stamina** — sprinting drains a small bar that regens while walking

## Controls

| Key                  | Action                          |
|----------------------|---------------------------------|
| `W` `A` `S` `D`      | Move                            |
| Mouse                | Look                            |
| Left click           | Fire                            |
| Right click (hold)   | Aim down sights                 |
| `Shift` (hold)       | Sprint                          |
| `C` / `Ctrl` (hold)  | Crouch / `Space` jump           |
| `R`                  | Reload                          |
| `Q`                  | Swap weapon slot                |
| `V`                  | Melee (knife)                   |
| `G` / `H`            | Throw lethal (frag) / tactical (stun) |
| `F`                  | Buy wall weapon / perk / PaP / box / open door |
| `E` (hold)           | Repair window board / revive teammate |
| `Tab` (hold)         | Scoreboard                      |
| `Esc`                | Pause                           |
| `F11`                | Toggle fullscreen               |
| `F3` / `F4`          | Toggle God Mode / Noclip (cheats) |

Gamepad is fully supported with rebindable controls (Settings → Controls).

When downed, your camera becomes free-fly: `W`/`A`/`S`/`D` + mouse, `Space` to ascend, `Shift` to descend. You revive automatically at the next round break.

## Build

CMake fetches all dependencies (raylib, raygui, enet) at configure time.

### Linux

```bash
# X11 dev headers (Ubuntu/Debian)
sudo apt install -y libxrandr-dev libxinerama-dev libxcursor-dev \
                    libxi-dev libxkbcommon-dev libwayland-dev libgl1-mesa-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shooter
```

### macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shooter
```

### Windows

**Visual Studio:**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
.\build\Release\shooter.exe
```

**MinGW-w64:**

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shooter.exe
```

Release builds with MinGW are statically linked, so the resulting `.exe`
is portable. Visual Studio builds need the matching MSVC runtime.

## Multiplayer

Default port: **UDP 7777**. Host-authoritative; clients send input and
render server snapshots at 20 Hz.

1. Player A picks **HOST GAME** from the main menu, then **START GAME**
   when everyone has joined.
2. Players B/C/D pick **JOIN GAME** and enter the host's IP.
   - LAN: the host's local IP (`ip addr`, `ipconfig`, etc).
   - Internet: the host must port-forward UDP `7777`.
   - Same machine: `127.0.0.1`.

Caveats: this is minimal coop netcode — no remote-player interpolation
or lag compensation. Mid-game joiners spawn at the next round break with
the starter loadout. Host disconnect kicks everyone to the menu.

## Cheats

Press `F3` in-game to toggle **God Mode** (invincible + unlimited points)
and `F4` for **Noclip** (fly through geometry). Indicators appear in the
top-right when active. Both work fully in solo and as the host; in client
mode the toggle shows but the server is authoritative on HP/points/position,
so the cheats won't have a real effect.

## Layout

```
CMakeLists.txt        FetchContent for raylib / raygui / enet, per-OS link bits
src/main.c            entry + main loop only
src/types.h           shared structs / enums / constants
src/{level,player,weapons,perks,entities,interact,game}  simulation
src/{render,hud,menu,assets,decals,anim,fx}              presentation
src/{net,protocol}    networking (enet wrapper + snapshot serialization)
src/{audio,pad,settings}                                 sfx, gamepad, bindings
data/maps/*.map       map definitions (compact text grammar)
data/models/          props + rigged glTF models (+ ASSETS.md spec)
data/weapons/<name>/  per-weapon .weapon defs + models
data/shaders/         world (lit + fog), world_skinned (GPU skinning), sky
```

A deeper map for contributors lives in `HANDOFF.md` (architecture table,
conventions, gotchas) and `TODO.md` (live punch list).

## Maps

Maps live in `data/maps/*.map` (`default`, `nacht`, `factory`). The format
is a compact text grammar — one entry per line, `#` comments — so adding a
map is just copying a file and editing it:

```
SPAWN    x z
WALL     x1 z1 x2 z2 [DOOR center width cost [AS name]]
WINDOW   x z <+x|-x|+z|-z> [LOCKED_BY door_name]
OBSTACLE x z sx sz [h]
PROP     <name> x z [yaw d] [scale s]
WALLBUY  x z <+x|-x|+z|-z> PISTOL|SMG|SHOTGUN|RIFLE|RAYGUN
PERK     x z JUG|SPEED|DTAP|STAMIN
PAP      x z
MBOX     x z
ROOM <name> ... END
ATMOSPHERE { fog R G B start end; sky_tint R G B; music name } END
```

Validate a map without launching the game (line-numbered errors, exit 0/1):

```bash
./build/shooter --validate data/maps/nacht.map
```

The build copies `data/` next to the executable, so the game finds it when
launched from `build/`. Failing that it falls back to a hardcoded default
layout so the binary always runs.

### Assets

3D models and animations have their own specs:

- `data/models/ASSETS.md` — static low-poly mesh conventions (scale, axes,
  palette, tri budget).
- `data/ANIMATIONS.md` — the rigging-first mandate and per-weapon/entity
  animation clip lists for the glTF skeletal-animation pipeline.

Other dev CLI modes: `--screenshot-viewmodels` (renders each weapon's
viewmodel to a PNG) and `--anim-test <file.glb> [clip]` (renders a skinned
glTF model across an animation clip to verify it).

## License

raylib, raygui, and enet are under their own licenses (zlib for raylib/raygui,
MIT-style for enet). This game code is provided as-is for fun — feel free
to do whatever with it.
