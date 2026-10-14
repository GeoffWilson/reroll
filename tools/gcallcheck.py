#!/usr/bin/env python3
"""Checks every GCALL(address, name) in src/: the address must be one a hook
sends to our C (else the call reaches original code, which isn't there
without the exe), and name's prototype should belong to that address
(src/game/gamefn.h's comment).

    tools/gcallcheck.py
"""
import glob
import re
import sys

hooked = set()
for path in glob.glob("src/*.c") + glob.glob("src/game/*.c"):
    for m in re.finditer(r'"[^"\n]+", (0x[0-9a-f]+), \w+,', open(path).read()):
        hooked.add(int(m.group(1), 16))
proto = {}
for m in re.finditer(r"\b(\w+)\([^;]*\); /\* (0x[0-9a-f]+) \*/", open("src/game/gamefn.h").read()):
    proto.setdefault(m.group(1), set()).add(int(m.group(2), 16))
bad = 0
for path in sorted(glob.glob("src/*.c") + glob.glob("src/game/*.c") + glob.glob("src/game/*.h")):
    for n, line in enumerate(open(path), 1):
        for m in re.finditer(r"GCALL\((0x[0-9a-f]+),\s*(\w+)\)", line):
            addr, name = int(m.group(1), 16), m.group(2)
            if addr not in hooked:
                print("%s:%d: 0x%x is not hooked (%s): the original would run" % (path, n, addr, name))
                bad += 1
print("%d problems" % bad)
sys.exit(1 if bad else 0)
