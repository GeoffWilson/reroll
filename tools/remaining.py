#!/usr/bin/env python3
"""Which original functions can still run, i.e. what is left to port.

    tools/remaining.py build/functions.txt build/hooked.txt [--list]

functions.txt: make function-sizes; hooked.txt: the addresses hooked at
start-up (from reroll.log). A hooked function's original body never runs,
except those wrapped through a trampoline (the original still runs inside
our wrapper), which count as not ported. Roots: the exe's entry point,
every exe function address our C mentions, and function addresses stored in
the exe's data (callbacks, tables). From there, direct calls and function
addresses used as immediates inside unported functions are followed."""
import bisect, collections, csv, glob, os, re, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.expanduser('~/Games/rollcage/prefix/drive_c/Program Files (x86)/Psygnosis/Rollcage/Direct3D/Rollcage.exe')


def main():
    funcs = {}
    for line in open(sys.argv[1]):
        p = line.split()
        if len(p) < 3:
            continue
        a, size, name = p[:3]
        funcs[int(a, 16)] = (int(size), name)
    starts = sorted(funcs)
    hooked = {int(x, 16) for x in open(sys.argv[2]).read().split()}
    # trampoline-wrapped hooks: the original still runs
    wrapped = set()
    src = {}
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.c'), recursive=True):
        src[path] = open(path).read()
    for text in src.values():
        for m in re.finditer(r'hook_install_trampoline\(&(\w+)', text):
            d = re.search(r'struct hook ' + m.group(1) + r'\s*=\s*\{\s*"[^"]*",\s*(0x[0-9a-f]+)', text)
            if d:
                wrapped.add(int(d.group(1), 16))
    ported = (hooked - wrapped) & set(funcs)
    names = {}
    with open(os.path.join(ROOT, 'docs', 'functions.csv')) as f:
        for row in csv.reader(f):
            try:
                names[int(row[0], 16)] = row[1]
            except (ValueError, IndexError):
                pass

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and a < starts[i] + max(funcs[starts[i]][0], 1):
            return starts[i]
        return None

    # edges from the disassembly
    edges = collections.defaultdict(set)
    cur = None
    for line in open(os.path.join(ROOT, 'build', 'rollcage.asm')):
        m = re.match(r'\s+([0-9a-f]+):\s+\S', line)
        if not m:
            continue
        a = int(m.group(1), 16)
        if a in funcs:
            cur = a
        o = owner(a)
        if o is None:
            continue
        for t in re.findall(r'0x([0-9a-f]{6})\b', line.split('<')[0]):
            t = int(t, 16)
            if t in funcs and t != o:
                edges[o].add(t)
    # roots
    roots = set()
    data = open(EXE, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    entry = 0x400000 + struct.unpack_from('<I', data, pe + 0x28)[0]
    roots.add(owner(entry) or entry)
    nsec = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    for i in range(nsec):
        o = pe + 24 + opt + 40 * i
        name = data[o:o + 8].rstrip(b'\0')
        vs, va, rs, ra, flags = struct.unpack_from('<IIII', data, o + 8) + (struct.unpack_from('<I', data, o + 36)[0],)
        if flags & 0x20000000:  # code
            continue
        for k in range(0, min(rs, vs) - 3, 4):
            v = struct.unpack_from('<I', data, ra + k)[0]
            if v in funcs:
                roots.add(v)
    c_refs = set()
    for text in src.values():
        for t in re.findall(r'0x(4[0-9a-f]{5})\b', text):
            t = int(t, 16)
            if t in funcs:
                c_refs.add(t)
    roots |= c_refs
    # walk through unported functions
    seen, todo = set(), [r for r in roots if r not in ported]
    while todo:
        f = todo.pop()
        if f in seen or f in ported:
            continue
        seen.add(f)
        todo.extend(edges[f])
    size = sum(funcs[f][0] for f in seen)
    dead = set(funcs) - ported - seen
    print(f'{len(funcs)} functions; {len(ported)} ported; {len(wrapped & set(funcs))} wrapped (original still runs)')
    print(f'still reachable: {len(seen)} functions, {size} bytes; never reached: {len(dead)} '
          f'({sum(funcs[f][0] for f in dead)} bytes)')
    print(f'  directly called from our C: {len(c_refs & seen)}')
    if '--list' in sys.argv:
        for f in sorted(seen):
            print(f'{f:08x} {funcs[f][0]:6d} {names.get(f, funcs[f][1])}{" (wrapped)" if f in wrapped else ""}'
                  f'{" C" if f in c_refs else ""}')


main()
