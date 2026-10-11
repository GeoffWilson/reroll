#!/usr/bin/env python3
"""Merge finished worker groups into the build.

    tools/integrate.py PREFIX GROUP...      e.g. tools/integrate.py fe l m n

For each group g it takes build/groups/<g>_hooks.txt (hook table lines, then
'--- verify ---' and the verify wrappers) and <g>_protos.txt, and adds:
the wrappers before main.c's physics hook table, the hook lines to the
replacement table, the prototypes to physics.h, and src/game/PREFIX_<g>.c to
the Makefile's SRCS and EMU_SRCS. tests/emu_PREFIX_<g>.py is picked up by
make emu-test on its own. Refuses duplicate hook addresses.
"""
import re
import sys

prefix, groups = sys.argv[1], sys.argv[2:]
main = open("src/main.c").read()
phys = open("src/game/physics.h").read()
mk = open("Makefile").read()
taken = set(re.findall(r'^    \{"[^"]+", (0x[0-9a-f]+),', main, re.M))
for g in groups:
    source = f"src/game/{prefix}_{g}.c"
    if source in mk:
        print(g, "already integrated")
        continue
    hooks, _, verify = open(f"build/groups/{g}_hooks.txt").read().partition("--- verify ---")
    addresses = re.findall(r'^    \{"[^"]+", (0x[0-9a-f]+),', hooks, re.M)
    clash = taken.intersection(addresses) | {a for a in addresses if addresses.count(a) > 1}
    if clash:
        sys.exit(f"{g}: hook addresses already taken: {' '.join(sorted(clash))}")
    taken.update(addresses)
    if verify.strip():
        main = main.replace("/* Reimplemented physics:", verify.strip() + "\n\n/* Reimplemented physics:", 1)
    main = main.replace('    {"controls_analogue", 0x428fe0,', hooks.strip("\n") + '\n    {"controls_analogue", 0x428fe0,', 1)
    protos = open(f"build/groups/{g}_protos.txt").read().strip()
    phys = phys.replace("void GAMEAPI ai_pace(uint8_t *car);", "void GAMEAPI ai_pace(uint8_t *car);\n" + protos, 1)
    mk = mk.replace("src/game/aiplan.c", f"src/game/aiplan.c {source}")
    print(g, "integrated:", len(addresses), "hooks")
open("src/main.c", "w").write(main)
open("src/game/physics.h", "w").write(phys)
open("Makefile", "w").write(mk)
