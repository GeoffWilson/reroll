# CLAUDE.md

Guidance for working on reroll: a C reimplementation of Rollcage (1999, PC
Direct3D version) that runs as a native x86-64 Linux program
(`build/rollcage`) on SDL3, Vulkan and OpenAL Soft. README.md describes the
project for players; this file is for changing the code.

## Ground rules

- **No game content in the repository or a release.** Only
  reverse-engineered code, notes and our own tools. Never commit or package
  `Rollcage.exe`, `reroll_data.bin`, IDXData, movies, CD images, captures or
  screenshots of the game. The program reads all of these from the player's
  own installation. `build/`, `ghidra/` and `captures/` are ignored.
- **Play exactly like the original.** The physics runs in the original's
  32 ms steps and must stay bit-exact (replays, handling and network
  lockstep depend on it). Reproduce the original's quirks and bugs, marked
  `/* sic */`. Additions (rendering, frame pacing, input, sound, network)
  go around the game's logic, not into it, and deliberate changes are
  listed in README.md under "Deliberate changes from the original".
- **The game's memory keeps its 32-bit layout.** The data image is mapped at
  0x400000 and the game's heap stays below 2 GB (`native/lowmem.c`); our code
  and statics are linked low too (Makefile). Game pointers are 32-bit
  values: go through `GMEM()` / `GPTR()` (`src/game/gamefn.h`,
  `src/game/winapi.h`) and never store a host pointer in game memory unless
  it is below 4 GB. Allocate with the same Windows heap calls the original
  makes.
- **The HUD stays in the 4:3 middle** of the screen; the 3D view widens.

## Layout

- `native/`: the loader (`loader.c` maps `reroll_data.bin`, which is the
  exe's data extracted once by `datafile.c`) and the Windows layer the game
  calls: kernel32, user32 (SDL3 window), ddraw, dinput (SDL gamepads,
  rumble), qmixer (OpenAL Soft, EFX for EAX), dsound (movies), dplayx
  (network play on our UDP transport), winmm (CD audio from a BIN/CUE).
- `src/main.c`: the hook tables that send each original function address to
  our C. Game code still calls functions by their original address, so
  every function needs its entry here.
- `src/game/`: the reimplemented functions, one comment per function
  naming its original address. `physics.h` and `gamefn.h` hold the
  prototypes.
- `src/vkgame.c`, `widescreen.c`, `subunit.c`, `src/render/`: the Vulkan
  renderer (it decodes the packets the game's drawing code builds).
  `src/game/frame.c` and `interp.c`: frame pacing and the blended draws
  between physics steps.
- `docs/`: `status.md` (what's done), `porting.md` (the brief for porting
  functions), `functions.csv` (every original function and its state),
  `formats.md` (data layouts), `renderer/` and `native/` (design notes).
- `dist/`: the release scripts and README packaged by `make dist`.

## Building and testing

    make                  # build/rollcage; must stay warning-free
    make dist             # build/reroll-<VERSION>-linux-x86_64.tar.gz
    make net-test         # network transport: a host and three clients, with packet loss
    make argcheck         # argument counts of our functions against the original's
    make -j8 -k emu-test  # emulator tests (only those whose inputs changed)

- The emulator tests (`tests/emu_*.py`) run our C, compiled for i386,
  next to the original machine code in Unicorn on random and edge-case
  inputs. They need the original `Rollcage.exe` (`EXE` in the Makefile,
  from `GAME_DIR`) and the Python `unicorn` module. Some cases fail the same
  way in both, which counts as a match.
- `make decompile ADDR=0x...` (Ghidra) and `make disasm ADDR=0x... LEN=0x...`
  for reading the original. Trust the disassembly over Ghidra.
- Never run `make clean`: the emulator test stamps and the build are slow to redo.
- `VERSION` in the Makefile is the release version (`rollcage --version`,
  and the first line of `native.log`).

## Running

The program runs from the game's `Direct3D` folder and writes `native.log`
(Windows layer, including `net:` lines), `reroll.log` (game side, including
`frametime:` lines) and the settings files `reroll_registry.reg` and
`reroll_display.cfg` there.

Headless runs for testing:

    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy ALSOFT_DRIVERS=null \
    REROLL_NO_PADS=1 REROLL_KEYS="9000:Return,11000:Return" REROLL_SHOT_EVERY=150 \
    timeout -s KILL 30 build/rollcage

- Keep headless runs silent (both audio variables above).
- Back up `reroll_registry.reg` (and `reroll_display.cfg`) first and restore
  them afterwards: a scripted run changes the player's settings.
- `REROLL_KEYS` takes `ms:Key` presses; confirm screens ignore a key
  pressed too early. Screenshots are written as `shot_*.ppm`
  (`REROLL_SHOT_PREFIX` for a prefix).
- Two copies on one machine can play a network race (`REROLL_NET_PORT`);
  quitting a race (Esc, Up, Return, Down, Return) ends it for everyone.
- Check that nobody is playing (`nvidia-smi`) before a headless run, and
  don't launch the game in loops: one session for a person to play, headless
  runs for self-checks.

## Porting or changing a game function

Follow `docs/porting.md`: same calling convention (`GAMEAPI`, cdecl) and the
same stack arguments as the original (`build/params.txt`), the same memory
writes and calls in the same order, the return value callers read
(`make eax-users`), an emulator test that compares against the original,
then the function's row in `docs/functions.csv`.

## Commits

Small, descriptive commits on master; push to
github.com/GeoffWilson/reroll when the change has been played and
confirmed. Update README.md, `docs/status.md` and `dist/README.txt` when
behaviour players see changes.
