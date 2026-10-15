# reroll

**Version 1.0.0.** An incremental reimplementation of **Rollcage** (Psygnosis / Attention to
Detail, 1999, PC Direct3D version) as a native Linux program, in the style of
OpenRCT2: the original `Rollcage.exe` was loaded at its own addresses and its
functions were replaced, one at a time, with C. Now every function the game
runs is C (about 2,500, each checked against the original machine code), and
the game runs without any original code: only the exe's data (tables,
strings, menu layouts) is still used, read once from your copy into
`reroll_data.bin`. It is a 64-bit program on a small Windows layer built on
SDL3, and everything is drawn by a new Vulkan renderer.

**No game files are included.** This repository holds only reverse-engineered
code, notes and tools written for the project. To play you need your own copy
of Rollcage (the PC version): the program reads the game's data from your
installation (and, once, the data inside your `Rollcage.exe`), and plays music
from your CD (or a BIN/CUE image of it).

## What it adds

Everything is meant to play exactly like the original: the physics runs in
the original's 32 ms steps and is checked bit for bit against the original
code. On top of that:

- **Any resolution, ultrawide included**: the 3D view widens (more to the
  sides, the same vertical view), and the HUD stays in the original 4:3 middle.
- **High frame rates**: races are drawn at up to 500 fps (`REROLL_MAX_FPS`,
  0 for no cap), presented in mailbox mode so the display shows the newest
  frame at each refresh (best with variable refresh rate), with cars and
  cameras blended between physics steps. `REROLL_VSYNC=1` locks to one frame
  a refresh instead. (The cap keeps a fast GPU from idling down: at a locked
  240 fps an RTX 4090 dropped to idle clocks and the compositor then missed
  refreshes.)
- **Longer draw distance**: the whole track and its trackside objects, instead of
  the original's distance-limited visibility lists (`REROLL_DRAW_DISTANCE`).
- **Options > Display** (fullscreen or windowed, resolution) and **anisotropic
  filtering** in Options > Graphics.
- Modern gamepads through SDL3 (laid out as an XInput pad, triggers for the
  throttle), with the game's force feedback played as rumble
  (`REROLL_NO_RUMBLE=1` turns it off).
- **Sound on OpenAL Soft**: 3D sound, with HRTF on headphones
  when OpenAL Soft picks it, and the game's EAX 1.0 environmental audio
  (the tunnel reverb) as an EFX reverb, so Options > Sound > environmental
  audio works again (`REROLL_NO_EAX=1` turns the reverb off).
- **Network play over the internet** (TCP/IP only): the game's own network
  code on a new UDP transport instead of DirectPlay; see below.

## Playing

Download `reroll-1.0.0-linux-x86_64.tar.gz` from the
[releases page](https://github.com/GeoffWilson/reroll/releases), unpack it
and follow its `README.txt`:

    sudo pacman -S sdl3 vulkan-icd-loader openal libdecor   # Arch; plus your GPU's Vulkan driver
    ./setup.sh "/path/to/Rollcage.cue"   # once: copies the game from your CD, image or mounted disc
    ./run.sh

`setup.sh` takes a BIN/CUE image (which also gives the CD music), an ISO
or the mounted disc. The archive holds only the program and these scripts.

## Requirements

- Linux on x86-64 with SDL3, the Vulkan loader, OpenAL Soft (`openal`) and
  libdecor (window title bars on Wayland). Nothing 32-bit.
- To build: clang, `glslc` (shaderc), Python 3.
- The game: an installed copy of Rollcage. The expected executable is
  `Direct3D/Rollcage.exe`, sha256
  `0648503bf95e692859a96efcacc101b33af9312e4e4ba80c6e9d00692c0e5879`
  (image base 0x400000). On the first run its data (everything but its code)
  is copied to `Direct3D/reroll_data.bin` (`rollcage --extract-data` does the
  same); a different executable is refused. After that the exe isn't read.

## Build and run

    make                      # build/rollcage (x86-64)
    cd "<game folder>/Direct3D"
    /path/to/reroll/build/rollcage
    make dist                 # build/reroll-<version>-linux-x86_64.tar.gz (dist/)

`GAME_DIR` (Makefile) defaults to
`~/Games/rollcage/prefix/drive_c/Program Files (x86)/Psygnosis/Rollcage`, an
installation made with Wine; any folder with the installed game works. The CD
image is found through `REROLL_CD_CUE`, else the first `.cue` in
`~/Games/rollcage` or `~/Documents/rollcage`.

The program writes `native.log` (its Windows layer) and `reroll.log` (the
reimplementation) next to the executable, keeps the registry settings in
`reroll_registry.reg` and the display settings in `reroll_display.cfg`.

Useful environment variables: `REROLL_DRAW_DISTANCE` (world units, `0` = the
original's), `REROLL_FAR_OBJECTS=0` (no trackside objects past the original's
range), `REROLL_ANISOTROPY`, `REROLL_MAX_FPS`,
`REROLL_VSYNC`, `REROLL_WINDOW=WxH`, `REROLL_NO_RUMBLE=1`, `REROLL_NO_EAX=1`,
`REROLL_NET_PORT`, `REROLL_DRIVE_C` (the folder that stands for C:). For tests without a window:
`SDL_VIDEO_DRIVER=offscreen` with `REROLL_SHOT_EVERY=n` (saves frames) and
`REROLL_KEYS="ms:Key,..."` (scripted key presses).

## Checking against the original

The physics has to match bit for bit (replays and the handling depend on it):

- `make emu-test`: reimplemented routines are compiled for i386 and run next to
  the original machine code in the Unicorn emulator, on random and edge-case
  inputs (`tests/emu_*.py`; needs the Python `unicorn` module,
  `EMU_PYTHON=/path/to/python` if it isn't system-wide).
- In-game verification (`REROLL_VERIFY=1`, the original and ours run from the
  same state each physics step) needed the i386 build, which was dropped once
  the port was complete; the emulator tests remain.

## Deliberate changes from the original

Besides the additions above:

- **Gamepads** (`input.c`): a `Poll` returning `DI_NOEFFECT` counts as success;
  the original never read such pads.
- **Analogue throttle** (`controls.c`): with the game's analogue option on, the
  throttle comes from the triggers instead of the left stick's Y axis.
- **No CD check** (`st_sa.c`): the original wants the Rollcage CD in a
  CD-ROM drive and otherwise disables League, Arcade and Time Attack; the
  game only reads the installed data, and the music comes from the CD
  image.
- **Network play** (`native/dplayx.c`, `fe_l.c`, `orig_pu.c`): TCP/IP is the
  only connection (no IPX, serial cable or modem), on our own UDP transport;
  a session still open when the front end loads again is closed, so the
  other players hear about it. In a network race the local player's car
  takes player 1's controller set-up (`setup_s.c`; the original used the
  car's slot number, so it was mostly digital, without rumble).
- **Parked messages** (`orig_pq.c`): message letters wholly off the game's screen
  aren't drawn. Practice parks its "pause game to change car" hint past the
  right edge, where the wide picture would show it.
- **Frame rate** (`frame.c`, `interp.c`, `st_sa.c`): physics keeps the original
  32 ms steps on its own clock; per-draw state (HUD flash countdowns, the
  trackside trigger queue, the random generator) still advances once per
  step. In network races the whole lockstep step (controls, physics, the
  wait for the other players) runs every 32 ms, with blended draws between.

## Network play

Multiplayer > Networked. The original used DirectPlay: the joining player
typed the host's address, and there was no server list. That stays the
same, on a small UDP transport of our own (`native/dplayx.c`) that answers
the game's DirectPlay calls:

- **Host**: Start New Game. Other players connect to UDP port **47700**
  (`REROLL_NET_PORT` to change it), so on the internet that port has to be
  forwarded to the host's machine. Only the host needs that: everything
  goes through the host, so the players joining can be behind NAT.
- **Join**: Configure Connection > TCP IP Address: the host's address or
  name, with `:port` when it isn't 47700. Left empty, Join Existing Game
  searches the local network instead. IPv4 only.
- Every message arrives, in order (resent when lost). Each player's
  connection is logged every 30 s (round trip, resends) in `native.log`.
- Everyone needs the same build. As in the original, the race runs in
  lockstep (every game simulates every car from the players' controls),
  and each race ends the session: host and join again for the next one.

## Layout

    native/         the loader (maps reroll_data.bin) and the
                    Windows layer: KERNEL32, USER32 (SDL3 window), DirectDraw /
                    Direct3D stand-ins, DirectInput, QMixer (on OpenAL Soft),
                    DirectPlay (on UDP) and CD audio (SDL)
    src/            hook installer, verifier, log, capture, the Vulkan renderer's
                    game side (vkgame.c, widescreen.c, subunit.c)
    src/game/       the reimplemented game functions
    src/render/     packet decoder and Vulkan back end (shared with the tools)
    tests/          native and emulated tests
    tools/          archive tool (rollcage_img.py), hook bytes (expect.py),
                    frame capture tools (rcapdec, rcaprender, rview), Ghidra
                    scripts (make decompile, calltree, xrefs)
    docs/           status.md (summary), porting.md (workflow), functions.csv
                    (every function and its state), formats.md (data layouts),
                    renderer/ and native/ (design notes and plans)

## Known limitations

- A network race ends the session: everyone hosts and joins again for the
  next race. IPv4 only, and the pause menu in a network race runs at the
  original 31 fps.
- The credits and league-winner movies are untested.
- Only the original PC release's `Rollcage.exe` (the sha256 above) is
  supported.

## How it was made

Each of the original's functions was found with Ghidra, written in C with
the original's calling convention and side effects, hooked in place of the
original (the tables in `src/main.c`) and checked against the original
machine code in the emulator, then in the game. Once every function the
game runs was C, the original code was no longer loaded at all, and the
program became 64-bit. `docs/porting.md` is the full brief for porting a
function, `docs/functions.csv` lists every function and its state, and
`CLAUDE.md` is the guide for working on the code.

## Legal

Rollcage is © Psygnosis / Attention to Detail. This project is not affiliated
with or endorsed by them or the current rights holders. It contains no game
code, data or assets; it needs a legitimately owned copy of the game.
