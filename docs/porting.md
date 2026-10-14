# Porting a group of functions

How a worker turns a list of original Rollcage.exe functions into verified C.
This is the whole brief: read it, then read the files it points to; the
project's history is not needed.

## The project in brief

reroll replaces Rollcage.exe's code function by function. `build/rollcage`
(the native loader, `native/`) maps the original executable and patches a
`jmp` over the start of each original function it replaces (the hook table in
`src/main.c`), so the game runs our C instead. The C must be bit-exact: same memory writes, same calls
with the same arguments in the same order, same return value.

- Original: `$(EXE)` = `~/Games/rollcage/prefix/drive_c/Program Files (x86)/Psygnosis/Rollcage/Direct3D/Rollcage.exe`
- Disassembly: `build/rollcage.asm` (llvm-objdump; it desyncs in a few places, so check raw bytes with `make disasm`)
- Function list with sizes: `build/functions.txt`; argument sizes: `build/params.txt`
- What's done: `docs/functions.csv`; data layouts found so far: `docs/formats.md`
- The Vulkan renderer decodes the packets the game's drawing code builds;
  drawing code is ported like any other (same packets, same memory).

## Your inputs

- `build/groups/<g>_owned.txt`: the functions you own. Port only these.
  Anything else you call (another group's, already hooked, or out of scope)
  is called by address so the existing hooks route it:
  `((void(GAMEAPI *)(uint8_t *))0x4612a0)(body)`, or a `#define` of that.
- A short note from the lead with the area and anything special.

## Scope

Everything that still runs is now in scope (docs/native/plan64.md, phase
1): the C runtime, file I/O, allocation, x87 float code, DirectDraw use and
drawing. Your group's note says what it covers. Functions outside your
list are called by address so the existing hooks route them. Keep the
calls the original makes to Windows functions, by name through
src/game/winapi.h (`ReadFile(GMEM(h), ...)`), and to COM objects through
src/game/com.h (`IDirectDrawSurface_Lock(surface, ...)`): natively they call
native/ directly, and in the emulator they go through the import slot or the
object's vtable so the test can log and compare them. Never read an import
slot or a vtable yourself. Never hook 0x471c90 or
0x471da0 (the verifier needs them). List anything you skip in your notes
with the reason.

Allocation must stay in the game's low memory (below 2 GB): use the same
Windows heap / virtual memory calls the original makes. A family of
functions that share state (an allocator and its free, a FILE table and
its stream functions, rand and srand) must be replaced together, including
any internal entry the remaining original code reaches directly; say in
your notes which internal entries you covered.

## Per function

1. `make decompile ADDR=0x...` and `make disasm ADDR=0x... LEN=0x...`. Trust
   the disassembly over Ghidra where they disagree (Ghidra drops arguments,
   misreads stack slots, and its signatures are not saved).
2. Arguments: `grep '^00<addr>' build/params.txt` gives the stack bytes the
   function reads and any register arguments (ecx/edx = fastcall/thiscall,
   but MSVC's `push ecx` to reserve a slot shows up as ecx too: check). Our C
   must declare the same stack arguments. **When you call an original
   function, pass every argument its code reads**: MSVC often pushes a
   function's later arguments before an unrelated call, so they look like
   that call's arguments. Count the pushes and the `add esp` after each call.
3. Return value: `make eax-users ADDR=0x...` lists callers reading eax after
   the call (byte/word loads into al/ax are false positives). Void only if
   no caller reads it; otherwise return what the original leaves in eax.
4. Hook safety: no branch may land in the first 8 bytes of a hooked function
   (grep `build/rollcage.asm` for jumps to those addresses). Expected bytes
   for the hook line: `tools/expect.py "$EXE" 0x...`.
5. Write the C in `src/game/<prefix>_<g>.c`, in the style of the existing
   files (e.g. `src/game/fe_z.c`, `src/game/post_g.c`): `GAMEAPI` (cdecl),
   `U32/I32/VEC` from `body.h` and file-local `U8/U16/S16/PTR` macros like
   the ones at the top of `fe_z.c`, one comment per function naming its
   address and what it does. The build uses `-fwrapv
   -fno-strict-aliasing`; shift negative values through `uint32_t`; reproduce
   original quirks and bugs, marked `/* sic */`. Leftover-stack reads in the
   original can't be reproduced: write a defined value and note it.

## Testing in the emulator

Write `tests/emu_<prefix>_<g>.py` on `tests/emu_fixed.py`'s `Machine`
(examples: `tests/emu_fe_z.py`, `tests/emu_post_g.py`). For each hooked
function, run the original address and our C symbol on the same random,
realistic state and compare:

- the return value (if not void) and all memory the function can touch;
- a log of every call out of the function: stub each outside callee with a
  logging hook that records **as many arguments as that callee reads**
  (from `build/params.txt`, not from Ghidra's signature) and returns scripted
  values; compare what stack-pointer arguments point to, not the pointers.

Cases where the original faults count as a match if ours faults the same way;
skip them if ours legitimately differs (say why).

Build your private ELF with `tools/emu_elf.sh <g> src/game/<prefix>_<g>.c`
(gives `build/emu_<g>.elf`) and run
`build/venv/bin/python -I tests/emu_<prefix>_<g>.py "$EXE" build/emu_<g>.elf 500`.

Budget, to keep batches quick:

- 500 iterations while developing; one final run at 1000. Don't rerun the
  whole file after a small fix: run just the affected function (pass its
  name after the iteration count, if your test supports it).
- Plant 2-3 deliberate bugs once, to show the test can fail, then remove them.
- Measure branch coverage only for large or branch-heavy functions.
- Don't edit shared files (`Makefile`, `src/main.c`, `physics.h`,
  `verify.c`, `tests/emu_fixed.py`, other groups' files); say in your report
  if one needs changing. Don't install or launch the game.
- Check the build of your file: `make build/native/src/game/<prefix>_<g>.o`,
  warning-free.

## In-game verification

With `REROLL_VERIFY=1` the game runs the original, snapshots memory (the data
section 0x4a7000-0x6d3000 and the game heap), runs ours from the same state,
and logs differences to `reroll.log`. A `verify_call2` wrapper only suits
functions whose whole call tree touches just that memory: no file I/O,
allocation, DirectX, network, clock reads, or sound other than 0x471c90 and
0x471da0 (recorded and replayed). Sample 1 for rare events (a menu choice, a
race start), 31 for per-frame functions. If a function is only unsafe in some
states, the wrapper can skip verification then (see `race_progress_entry` in
`src/main.c`). Otherwise it's fuzz-only: say so in the notes.

## Deliverables (in `build/groups/`)

- `<g>_hooks.txt`: hook table lines, then a line `--- verify ---`, then the
  verify wrappers. Format: see `build/groups/z_hooks.txt`. A verified
  function's hook line points at its `*_entry` wrapper.
- `<g>_protos.txt`: prototypes for `physics.h`, each with `/* 0xADDR */`.
- `<g>_notes.txt`: one line per function (address, name, what it does, test
  status), then what was skipped and why.
- `<g>_formats.txt`: any data layouts found (records, tables, file formats),
  for `docs/formats.md`.
- A short final report: done/skipped, fuzz result, risks.

## The lead's side (after workers finish)

1. `tools/integrate.py <prefix> <groups...>` (merges hooks, wrappers,
   prototypes and Makefile sources; refuses duplicate hook addresses).
2. `make` (warning-free), `make argcheck` (argument audit, 5 s),
   `make -j8 -k emu-test` (reruns only tests whose inputs changed;
   `make emu-test-all` for everything).
3. One game session (`build/rollcage`) with `REROLL_VERIFY=1` for the user,
   read `reroll.log`.
4. Append the groups to `docs/functions.csv` and `docs/formats.md`.
