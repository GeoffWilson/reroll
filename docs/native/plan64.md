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
- `REROLL_EXE_COVERAGE` (native/exe_coverage.c, removed 2026-10-14 once
  no-exe mode's traps did its job) logged the first run of each unported
  function. Menus and the attract demo ran 571 of 1031 (2026-10-11).
- The import table, fake COM objects and Win32 layer stay only as long as
  C still calls them; ported code calls our SDL / Vulkan / file layers
  directly, and native/ shrinks to what is used.

## Status (2026-10-13)

Phase 1 is done: `tools/remaining.py` finds no original function the game
can reach (its one listing, 0x4979b7, is a lone `ret` stored as _FPmtinit /
_FPmtterm, which crt_fpmt_patch points at C). About 2,500 hooks: the
Ghidra-listed functions in groups, the 19 trampoline wrappers as plain C
bodies (orig_wa/wb/wc), and about 140 entry points Ghidra never made
functions (callbacks reached only through immediates or data tables:
orig_xa/xb/xc/xd, found by scanning for code addresses used as values).
Every one is fuzzed against the original in the emulator.

Phase 2, first step done: no original code is the default. The loader maps
reroll_data.bin (made from Rollcage.exe on the first run, after checking it
is the expected version); `REROLL_NO_EXE=0` or `REROLL_VERIFY` map the
original instead. Menus, the attract demo and races run that way.

Phase 2 done (2026-10-14): the game's C calls Windows functions by name
(src/game/winapi.h, generated from native/'s definitions) and COM methods
through typed wrappers (src/game/com.h: the method at its vtable index,
read at pointer width). Natively the import table is not used: in no-exe
mode every slot is a trap naming its import, and menus and races run
without one firing. The emulator build still goes through the slots, so
the tests are unchanged. Without the exe nothing uses fs:, so there is no
thread block either. Found on the way: QSWaveMixStopProfile takes the
statistics buffer and size (the game reads five counters back), and
RegisterClassExA's result is used whole.

Phase 3 status (2026-10-15): `make native64` built build/rollcage64, which
runs the menus and races headless. Later the same day the i386 native build
was dropped: `make` builds build/rollcage for x86-64 and nothing 32-bit is
needed. The i386 code paths in src/ stay, as the emulator tests compile game
files for i386 (`make emu-test`); in-game verification (REROLL_VERIFY,
verify.c) went with the i386 build. How it was done:

- every call to a game function by address is typed: GCALL(0x47aa00,
  car_boost)(slot), with the prototypes in src/game/gamefn.h (generated by
  tools/gamefn.py from the hook tables and address comments);
  tools/gcallcheck.py checks that every such address is hooked;
- every pointer stored in game memory is read and written as 32 bits (GP(),
  G32() = (uint32_t)(uintptr_t)p); native/ fills Windows structures in their
  32-bit layout and writes 4-byte out-parameters (winapi.h / com.h say
  uint32_t *);
- asm that only exists for the original's register conventions or calls
  original addresses is i386-only, with a C path beside it; the asm entries
  have same-name C functions on x86-64 (or main.c maps the hook to the C
  body); x87 helpers with memory operands work in both;
- variadic functions that walk 32-bit stack words pack their va_list on
  x86-64 (msvc_sprintf, debug_log_printf, string_printf...);
- verification (verify.c) is i386-only: there is no original to check
  against;
- our code is linked at 0x8000000 and the game's arena (0x20000000-
  0x7f000000) is a no-file section of the image, so the C library's heap
  (randomly placed in the first GB above the image) can't land in it; the
  game thread's stack and every thread that runs game code are in the arena.

## Phase 3: 64-bit (the plan)

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

Known work, from the Phase 2 conversion:

- out-parameters that receive our objects or memory (`void **` in com.h /
  winapi.h: CreateSurface, CreateDevice, QueryInterface, HeapAlloc's
  callers storing results, DirectSound Lock) are 32-bit slots in game
  memory: native/ must write uint32_t there, and the headers say so;
- methods native/ answers with a generic stub take DWORDs where the real
  argument is a pointer (SetClipper, Viewport Clear's rects,
  GetSessionDesc, SendChatMessage...): type them when they matter;
- callbacks passed by original address (EnumDisplayModes 0x487190,
  DirectDrawEnumerate 0x487a80, DirectInput EnumDevices 0x488e80 and
  EnumObjects 0x4890b0, D3D EnumDevices 0x41d280, EnumTextureFormats
  0x41d790, DirectPlay EnumSessions 0x48bb30, lobby EnumAddress 0x48ac40,
  dialog procs 0x437171 and 0x4375c7) become their C symbols;
- function pointers the C runtime keeps in game memory from
  GetProcAddress (MessageBoxA, GetActiveWindow, GetLastActivePopup at
  0x4db50c..14; IsProcessorFeaturePresent) and the timer callback at
  0x4d48c8;
- the naked jmp thunks (orig_pv's PV_IMPORT_JMP, crt_ca's
  DirectSoundCreate, crt_cc's RaiseException call) and the other asm
  entries become C.

Then 64-bit SDL3 and Vulkan, and no 32-bit libraries are needed.

## After the 64-bit build

- Sound on OpenAL Soft: done 2026-10-15 (native/qmixer.c, replacing the
  SDL mixer). The 32 channels
  are OpenAL sources placed from the polar position (listener-relative;
  range attenuation, elevation and Doppler by the channel's QMixer features
  1 / 2 / 4, as QMixer did: this game enables 0x9, so neither range nor
  Doppler is used and the game's own volumes do the distance). Each
  channel's DirectSound buffer is a COM object whose IKsPropertySet takes
  the EAX 1.0 listener and buffer sets: the environment loads its EAX 1
  preset over EFX_REVERB_PRESET_* (OpenAL Soft's own EAX 1 mapping: volume
  = late reverb gain, damping = decay HF ratio), the reverb mix is the
  channel's send gain. The game sets reverb mix 0.6 on car sounds in tunnel
  sections (track section flag 2) and environment 21 (sewer pipe) or 1
  (padded cell) by its loud-channel check. With more than 16 property sets
  the sound menu's environmental audio is no longer greyed; the reverb is
  heard while that setting (0x4b1c88) is on. REROLL_NO_EAX=1 removes the
  property sets, REROLL_EAX_LOG=1 logs the reverb mix sets, REROLL_EAX_TEST=1 sends every
  sound fully into the sewer-pipe reverb (to hear that it works). Headless tests:
  ALSOFT_CONF with `drivers=wave` writes the output to a file.
- Far objects drawn by the renderer (REROLL_FAR_OBJECTS is off).
- Gamepad hot-plug (asked for 2026-10-14): native/dinput.c only finds pads
  at start-up, so a pad switched on later is never seen. Watch SDL's
  gamepad added/removed events and attach the pad to the game's joystick
  device (and its force-feedback effects) while it runs.
