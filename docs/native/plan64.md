# Towards a 64-bit build

Goal (set by the user 2026-10-11): a native 64-bit build with no need for
the original Rollcage.exe. The game's data files and CD still come from the
user's installation; no game code or data is copied into the repository.

## Where it starts (2026-10-11)

`tools/remaining.py build/functions.txt build/hooked.txt --list` (hooked.txt:
the addresses in reroll.log's `hook` lines) lists the original functions
that can still run: the exe's entry point, every exe function our C
mentions, and every function address stored in the exe's data, followed
through direct calls and code references. Hooked functions count as ported,
except the 19 trampoline wrappers (ours runs, then the original).

    2436 functions; 1340 ported; 19 wrapped
    still reachable: 1050 functions, 266 KB; never reached: 46

Of those, 292 are the Microsoft C runtime (0x496000 and up), entered from
the game through about 65 functions (string, memory, sprintf, rand, ftol,
file and archive reads, malloc). The rest are scene drawing (the packet
builders the Vulkan back end decodes), texture and DirectDraw set-up, the
front end's 3D and text, the movie player, network play and the
start-up/platform code.

## Phase 1: port everything that runs

Same workflow as before (docs/porting.md: groups of functions, emulator
tests against the original, in-game verification, one play session per
batch), with the scope widened: the C runtime, file I/O, allocation, x87
float code, DirectDraw use and drawing are now in scope.

Order, cheapest wins first:

1. **C runtime entry points** (~65, removes ~290): replaced by C with the
   same behaviour where the game can see it: MSVC's `rand` LCG and seed,
   `sprintf` / `%g` / `%e` formatting, `_ftol` truncation, `strncpy`
   padding, the archive reader's file handling. Allocation stays in the
   low-memory arena (game memory must stay below 2 GB).
2. **Platform and DirectX glue**: texture surfaces, the capability probe,
   window and message loop, timers, the movie player (or a native movie
   player), network play stubbed as "no network" in C.
3. **Scene drawing**: the packet builders (models, sprites, text, fades).
   Checked by capturing packets (F9 / rcap) from the original and ours and
   comparing them (tools/rcapcompare.py) as well as in the emulator.
4. **Front end 3D and what's left.** Then the wrapped hooks: the trampoline
   wrappers become plain C.

Floating point: the game runs the x87 at single precision. SSE single
precision gives the same results except for range edge cases; physics is
integer, so floats only reach drawing and set-up.

Measure: `tools/remaining.py` after each batch, plus a run-time check
(Phase 2) of what actually executes.

## Phase 2: no original code

- A coverage switch that traps on the entry of every unported function and
  logs the first hit, played through menus, races, replays and options:
  must log nothing.
- Then stop mapping the exe's code section. Its initialised data (tables,
  strings, menu layouts) is still needed: decided 2026-10-11, a tool
  extracts it once from the user's Rollcage.exe into a data file next to
  the game files; the game loads that file at the same addresses and never
  opens the exe. The repository holds only the extraction tool.
  Done (native/datafile.c): `rollcage --extract-data` writes
  reroll_data.bin (the exe without its code); `REROLL_NO_EXE=1` maps it,
  fills the code range with int3 except our hooks' jumps, and stops at the
  first original code still needed, naming it in native.log. First stop
  (2026-10-12): the C runtime start-up 0x498ec5.
- `REROLL_EXE_COVERAGE=build/remaining.txt` (native/exe_coverage.c) breaks
  on the first run of each listed function and logs it to native.log and
  exe_coverage.txt. Menus and the attract demo ran 571 of 1031 (build/
  exe_ran_demo.txt, 2026-10-11).
- The import table, fake COM objects and Win32 layer stay only as long as
  C still calls them; ported code calls our SDL / Vulkan / file layers
  directly, and native/ shrinks to what is used.

## Phase 3: 64-bit

Game memory keeps its layout: the data image stays at 0x4a0000-0x6d6000
and game allocations stay in the low arena, so structures keep their
32-bit pointer fields and every offset in the C stays valid
("pointer compression", as some emulators and JITs do):

- every pointer read from or written to game memory goes through 32-bit
  accessors (`PTR(x, off)` loads a uint32 and widens it; stores narrow it),
  checked by building with -m64 and warnings for pointer/int casts;
- our own code and data are linked at a low address (non-PIE, small code
  model), so function pointers stored in game memory (callbacks, tables)
  still fit in 32 bits;
- the low memory is reserved at start-up (MAP_FIXED_NOREPLACE below 4 GB);
- libraries (SDL3, Vulkan) and our own state live anywhere: only game
  memory is compressed.

Then 64-bit SDL3 and Vulkan, and no 32-bit libraries are needed.

## After the 64-bit build

- Sound on OpenAL Soft (decided 2026-10-13; waits for 64-bit because Arch
  has no 32-bit OpenAL package). native/qmixer.c's 32 channels become
  OpenAL sources (polar position to 3D, distance model as now; Doppler and
  HRTF come with it). The game's EAX 1.0 use (src/game/orig_po.c:
  environment preset 0..25, reverb volume, per-voice reverb mix, the
  support check that greys out the menu option) goes to an EFX reverb with
  the matching EFX_REVERB_PRESET_* (what OpenAL Soft's own EAX layer maps
  to), so it doesn't depend on that layer being built in. CD music stays
  on SDL.
- Far objects drawn by the renderer (REROLL_FAR_OBJECTS is off).
