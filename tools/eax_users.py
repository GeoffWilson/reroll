#!/usr/bin/env python3
"""List call sites that read eax after calling the given functions, i.e.
places that rely on a return value Ghidra may not show.

    make eax-users ADDR="0x461310 0x4621f0"
"""
import re
import sys



def main():
    targets = {int(a, 0) for a in sys.argv[2:]}
    ins = []
    for line in open(sys.argv[1]):
        m = re.match(r"\s+([0-9a-f]+):\s+(.*)", line)
        if m:
            ins.append((int(m.group(1), 16), m.group(2).strip()))
    for i, (address, text) in enumerate(ins):
        m = re.match(r"call\s+0x([0-9a-f]+)", text)
        if not m or int(m.group(1), 16) not in targets:
            continue
        for _, nxt in ins[i + 1:i + 8]:
            if re.match(r"add\s+esp", nxt):
                continue
            if re.match(r"(mov|movsx|movzx|lea|pop)\s+eax,", nxt) or nxt.startswith(("xor\teax, eax", "call", "ret", "j")):
                break
            if re.search(r"\b(eax|ax|al|ah)\b", nxt):
                print(f"{int(m.group(1), 16):#x} called at {address:#x}: {nxt}")
                break


if __name__ == "__main__":
    main()
