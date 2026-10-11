# reroll

An incremental reimplementation of Rollcage (1999, PC, Direct3D build), in the
style of OpenRCT2: the original `Rollcage.exe` keeps running under Wine while
its functions are replaced one at a time with C. When nothing original is
left, the C code can be built natively for Linux (SDL + Vulkan).

Personal project; the game's code and data are not part of this repository.

## How it works

`build/QMIXER.DLL` replaces the game's own `QMIXER.DLL` (the QSound mixer that
ships next to `Rollcage.exe`). All 75 QMixer exports forward to the original,
renamed `qmixer_orig.dll`, so sound is unchanged. When the game loads it,
`DllMain` patches a `jmp` over the start of each reimplemented function. Each
hook first checks the function's original bytes, so an unexpected executable
is left alone.

Target executable: `Direct3D/Rollcage.exe`,
sha256 `0648503bf95e692859a96efcacc101b33af9312e4e4ba80c6e9d00692c0e5879`,
image base 0x400000, no relocations.

## Build, test, install

Needs clang, lld-link and llvm-dlltool (no MinGW or C runtime).

    make            # build/QMIXER.DLL
    make test       # native check of reimplemented code against game data
    make install    # copy into the game folder (keeps the original)
    make uninstall  # restore the original QMIXER.DLL

`GAME_DIR` defaults to `~/Games/rollcage/prefix/drive_c/Program Files (x86)/Psygnosis/Rollcage`.
The game writes `reroll.log` into its `Direct3D` folder (appended to by every
game process; the launch script clears it).

## Checking against the original

Two tools, because the physics has to match bit for bit (replays and the
handling depend on it):

- `make emu-test`: pure routines (so far `src/game/fixed.c`) are compiled for
  i386 and run next to the original machine code in the Unicorn emulator, on
  random and edge-case inputs (`tests/emu_fixed.py`). Needs the Python
  `unicorn` module (`EMU_PYTHON=/path/to/venv/bin/python` if not system-wide).
- `REROLL_VERIFY=1 ~/Games/rollcage/run-rollcage.sh`: every physics step runs
  twice from the same state, first the original (all physics hooks switched
  off), then ours; `reroll.log` lists every byte of game memory that differs,
  annotated (`car2+1a4`, `body7+34`, `event3+48`). The original's result is
  kept, so the race always plays as the original. See `src/verify.c` for what
  is deliberately not compared (sound engine state, the timer thread's
  counters, scratch memory, the stack). `REROLL_VERIFY=survey` logs which
  memory pages the first steps write.

Lessons so far: Ghidra sometimes shows a function as `void` when callers use
its eax (0x429170), and the "GTE" globals are not always packed.

## Deliberate changes from the original

Everything else is meant to behave exactly as the original.

- **Gamepads** (`input.c`): `Poll` returning `DI_NOEFFECT` (USB pads, Wine)
  counts as success; the original never read such pads.
- **Analog throttle** (`controls.c`): with the game's analogue option on, the
  throttle comes from the triggers (DirectInput Z axis) instead of the left
  stick's Y axis, so steering cannot touch it.
- **Frame rate** (`frame.c`, `interp.c`): physics still runs in the original
  32 ms steps (31.25 per second; 16 ms when race byte `+0x58d` is set), but on
  its own clock. Races are drawn once per display frame, capped by the
  environment variable `REROLL_MAX_FPS` (default 120, `0` = VSync only), with
  cars, wheels, cameras and TV cameras blended between the last two steps.
  Per-draw state (HUD flash countdowns, the trackside trigger queue) still
  advances once per step. Menus, network races and the "no wait" setting keep
  the original timing.

## Workflow for each function

1. Find and understand it in Ghidra (`ghidra/Rollcage.gpr`; `make ghidra-names`
   applies the names in `docs/functions.csv`).
2. Write it in `src/game/`, declared in `src/game/game.h`, keeping the original
   calling convention (`GAMEAPI`, cdecl) and side effects, in order. Check the
   callers for use of eax (`make disasm`) even if Ghidra says `void`.
3. Add it to the `hooks` table in `src/main.c` (physics code goes in
   `physics_hooks`, which the verifier switches off) with its first 8 original
   bytes (`tools/expect.py`).
4. Test natively or in the emulator where possible (`tests/`), then in the
   game, with `REROLL_VERIFY=1` for physics.
5. Update its row in `docs/functions.csv`.

Status values in `docs/functions.csv`:

- `inferred`: guessed from a quick look.
- `understood`: behaviour worked out from the disassembly.
- `replaced`: hooked to our C.
- `verified`: replaced and checked against the original (test or in game).

## Layout

    src/            DLL entry, hook installer, verifier, log, freestanding runtime
    src/game/       reimplemented game functions
    def/            export forwarders (qmixer.def) and kernel32 imports
    tests/          native and emulated tests
    tools/          rollcage_img.py (archive tool), expect.py (hook bytes),
                    install scripts, Ghidra scripts (make decompile, calltree,
                    xrefs; make disasm uses llvm-objdump)
    docs/           functions.csv registry

## Known formats

- `IDXData/Rollcage.idx`: 16-byte records `{u32 offset, u32 stored, u32 unpacked,
  s16 margin, s16 link}`. Names come from the executable's table at 0x4ad000
  (`tools/rollcage_img.py list`).
- `IDXData/ROLLCAGE.IMG`: entries back to back, GT20-compressed when stored !=
  unpacked; decoded in place (see `src/game/gt20.c`).
- `.BT6`: 256x256 texture pages, 16-bit ARGB1555. `.BTP`: 8-bit version, not
  decoded yet.
- `.BLP`/`.BL6` (`GFXM`): track geometry, not decoded yet.
