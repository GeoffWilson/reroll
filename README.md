# reroll

An incremental reimplementation of **Rollcage** (Psygnosis / Attention to
Detail, 1999, PC Direct3D version) as a native Linux program, in the style of
OpenRCT2: the original `Rollcage.exe` was loaded at its own addresses and its
functions were replaced, one at a time, with C. Now every function the game
runs is C (about 2,500, each checked against the original machine code), and
the game runs without any original code: only the exe's data (tables,
strings, menu layouts) is still used, read once from your copy into
`reroll_data.bin`. It runs on a small Windows layer built on SDL3, and
everything is drawn by a new Vulkan renderer. Next: a 64-bit build
(`docs/native/plan64.md`).

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
- **High frame rates**: races are drawn up to 120 fps (`REROLL_MAX_FPS`), with
  cars and cameras blended between physics steps.
- **Longer draw distance**: the whole track and its trackside objects, instead of
  the original's distance-limited visibility lists (`REROLL_DRAW_DISTANCE`).
- **Options > Display** (fullscreen or windowed, resolution) and **anisotropic
  filtering** in Options > Graphics.
- Modern gamepads through SDL3 (laid out as an XInput pad, triggers for the
  throttle), with the game's force feedback played as rumble
  (`REROLL_NO_RUMBLE=1` turns it off).

Not done yet: movies (skipped), network play.

## Requirements

- Linux on x86-64 with 32-bit libraries: the program is still i386 (the game's
  memory layout and 32-bit pointers; the 64-bit build is the next step). On Arch: `lib32-sdl3`, `lib32-vulkan-icd-loader`
  (and your GPU's 32-bit Vulkan driver), `lib32-libdecor` for window title bars
  on Wayland.
- To build: clang, `glslc` (shaderc), Python 3.
- The game: an installed copy of Rollcage. The expected executable is
  `Direct3D/Rollcage.exe`, sha256
  `0648503bf95e692859a96efcacc101b33af9312e4e4ba80c6e9d00692c0e5879`
  (image base 0x400000). On the first run its data (everything but its code)
  is copied to `Direct3D/reroll_data.bin` (`rollcage --extract-data` does the
  same); a different executable is refused. After that the exe isn't read.

## Build and run

    make                      # build/rollcage
    cd "<game folder>/Direct3D"
    /path/to/reroll/build/rollcage

`GAME_DIR` (Makefile) defaults to
`~/Games/rollcage/prefix/drive_c/Program Files (x86)/Psygnosis/Rollcage`, an
installation made with Wine; any folder with the installed game works. The CD
image is found through `REROLL_CD_CUE`, else the first `.cue` in
`~/Games/rollcage` or `~/Documents/rollcage`.

The program writes `native.log` (its Windows layer) and `reroll.log` (the
reimplementation) next to the executable, keeps the registry settings in
`reroll_registry.reg` and the display settings in `reroll_display.cfg`.

Useful environment variables: `REROLL_DRAW_DISTANCE` (world units, `0` = the
original's), `REROLL_FAR_OBJECTS=1` (trackside objects past the original's
range; off, being reworked), `REROLL_ANISOTROPY`, `REROLL_MAX_FPS`,
`REROLL_VSYNC`, `REROLL_WINDOW=WxH`. For tests without a window:
`SDL_VIDEO_DRIVER=offscreen` with `REROLL_SHOT_EVERY=n` (saves frames) and
`REROLL_KEYS="ms:Key,..."` (scripted key presses). `REROLL_NO_EXE=0` maps the
original exe's code again instead of `reroll_data.bin` (for comparing with the
original; `REROLL_VERIFY=1`, which runs the originals beside ours, does this
itself).

## Checking against the original

The physics has to match bit for bit (replays and the handling depend on it):

- `make emu-test`: reimplemented routines are compiled for i386 and run next to
  the original machine code in the Unicorn emulator, on random and edge-case
  inputs (`tests/emu_*.py`; needs the Python `unicorn` module,
  `EMU_PYTHON=/path/to/python` if it isn't system-wide).
- `REROLL_VERIFY=1`: in the game, every physics step runs twice from the same
  state, first the original, then ours; `reroll.log` lists every byte of game
  memory that differs, annotated (`car2+1a4`, `body7+34`). The original's result
  is kept, so the race always plays as the original. It costs about 10 ms a
  frame.

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
- **No network play yet** (`fe_n.c`): Multiplayer's "Networked" is greyed out.
  The network game is ported, but there is no transport under it yet
  (`native/dplayx.c` reports no network).
- **Parked messages** (`orig_pq.c`): message letters wholly off the game's screen
  aren't drawn. Practice parks its "pause game to change car" hint past the
  right edge, where the wide picture would show it.
- **Frame rate** (`frame.c`, `interp.c`): physics keeps the original 32 ms steps
  on its own clock; per-draw state (HUD flash countdowns, the trackside trigger
  queue) still advances once per step.

## Layout

    native/         the loader (maps reroll_data.bin) and the
                    Windows layer: KERNEL32, USER32 (SDL3 window), DirectDraw /
                    Direct3D stand-ins, DirectInput, QMixer and CD audio
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

## Workflow for each function

1. Find and understand it (`make decompile ADDR=...` with Ghidra,
   `make disasm ADDR=... LEN=...`).
2. Write it in `src/game/`, keeping the original calling convention
   (`GAMEAPI`, cdecl) and side effects, in order. Check the callers for use of
   eax even if Ghidra says `void`.
3. Add it to the `hooks` table in `src/main.c` (physics in `physics_hooks`,
   which the verifier switches off) with its first 8 original bytes
   (`tools/expect.py`).
4. Test in the emulator (`tests/`), then in the game with `REROLL_VERIFY=1`.
5. Update its row in `docs/functions.csv`.

See `docs/porting.md` for the full brief.

## Legal

Rollcage is © Psygnosis / Attention to Detail. This project is not affiliated
with or endorsed by them or the current rights holders. It contains no game
code, data or assets; it needs a legitimately owned copy of the game.
