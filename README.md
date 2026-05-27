# Claude Zombies

A 3D zombies shooter written in C with [raylib](https://www.raylib.com/),
[raygui](https://github.com/raysan5/raygui), and [enet](http://enet.bespin.org/).
Round-based survival in the style of *Call of Duty: Zombies* — wall-buys,
perks, Pack-a-Punch, purchasable doors, boardable windows, and up to
**4-player coop** over UDP.

Built end-to-end with [Claude Code](https://claude.com/claude-code).

## Features

- **Round-based survival** — endless waves, scaling zombie HP/speed/count
- **Boarded windows** — zombies spawn outside, break boards to enter,
  hold `E` to repair (and earn points)
- **Purchasable doors** — the south room (with Pack-a-Punch and the M14
  wall-buy) is locked behind a door. Spend points to open it and unlock
  that area's window as well.
- **5 weapons** with wall-buys and ammo refills
  - M1911 (starter pistol)
  - MP5 SMG · Olympia shotgun · M14 rifle · Ray Gun
- **Pack-a-Punch** — spend points to upgrade a weapon (damage x2.5, mag x2,
  new name)
- **4 perks** — Juggernog (more HP), Speed Cola (faster reload),
  Double Tap (more damage, faster fire), Stamin-Up (faster movement)
- **Per-player progression** in coop — own HP, points, inventory, perks
- **Down/revive** — at 0 HP you go down; revive at the next round break.
  Game over when everyone is down at the same time.

## Controls

| Key                  | Action                          |
|----------------------|---------------------------------|
| `W` `A` `S` `D`      | Move                            |
| Mouse                | Look                            |
| `Shift` (hold)       | Sprint                          |
| `C` / `Ctrl` (hold)  | Crouch                          |
| Left click           | Fire                            |
| `V`                  | Melee (knife)                   |
| `R`                  | Reload                          |
| `Q` / `Tab` / `1` `2`| Swap weapon slot                |
| `F`                  | Buy wall weapon / perk / PaP    |
| `E` (hold)           | Repair window board             |
| `Esc`                | Pause                           |
| `F11`                | Toggle fullscreen               |
| `F3`                 | Toggle God Mode (cheat)         |

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

Press `F3` in-game to toggle **God Mode**: invincible + unlimited points.
A yellow "GOD MODE" indicator appears in the top-right corner when active.
Works fully in solo and as the host. In client mode the toggle still
shows, but the server is authoritative on HP and points so the cheat
won't have any real effect.

## Layout

```
CMakeLists.txt        FetchContent for raylib / raygui / enet, per-OS link bits
data/maps/*.map       map definitions (text, see format below)
src/main.c            entry + main loop only
src/{level,player,weapons,perks,entities,interact,game,protocol,render,hud,menu}
                      one translation unit per responsibility
src/types.h           shared structs / enums / constants
src/net.{h,c}         enet wrapper (host/client init, poll, send)
```

## Maps

The level layout lives in `data/maps/default.map`. The format is a tiny
text grammar — one entry per line, `#` comments — so adding a new map is
just copying the file and editing it:

```
SPAWN    x z
OBSTACLE cx cy cz sx sy sz
WALL     cx cy cz sx sy sz
DOOR     cx cy cz sx sy sz cost
WALLBUY  x z nx nz   PISTOL|SMG|SHOTGUN|RIFLE|RAYGUN
WINDOW   x z nx nz   lockedByDoor    (-1 = always open)
PERK     x z         JUG|SPEED|DTAP|STAMIN
PAP      x z
```

The build copies `data/` next to the executable, so the game finds it
when launched from `build/`. Failing that it falls back to a hardcoded
default layout so the binary always runs.

## License

raylib, raygui, and enet are under their own licenses (zlib for raylib/raygui,
MIT-style for enet). This game code is provided as-is for fun — feel free
to do whatever with it.
