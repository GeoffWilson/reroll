#!/usr/bin/env python3
"""Differential test of slot_track_sweep (0x415ea0) and its helpers against
the original, on random synthetic track: a grid of vertices, a list of
nearby polygons (as polygon_contacts makes them) and a moving sphere.

    tests/emu_track.py EXE build/emu_fixed.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH = 0x01000000
BODY, SLOT, OUT, LIST, TILE, TILES, GRID = (SCRATCH + o for o in (0, 0x200, 0x280, 0x300, 0x800, 0x840, 0x1000))
HITS = 0x6CE760
WALL = [(0x6C8004, 4), (0x6C80E0, 6), (0x6C80F0, 12), (0x6C8010, 12)]
GTE = (0x6CCBE0, 0x6CCD00)
W, H = 12, 12  # vertices per row (tile width + 1), rows
STEP = 0x40     # vertex spacing in int16 units


def vertex_addr(col, row):
    return GRID + (row * W + col) * 8


def make_grid(rng):
    heights = [[0] * W for _ in range(H)]
    base = rng.randrange(-0x40, 0x40)
    for r in range(H):
        for c in range(W):
            heights[r][c] = base + rng.randrange(-8, 9) * rng.choice([0, 1, 1, 2])
    flags = [[rng.choice([0, 0, 0, 0x10, 0x20, 0x30]) for _ in range(W)] for _ in range(H)]
    data = bytearray()
    for r in range(H):
        for c in range(W):
            data += struct.pack("<hhhBB", c * STEP, heights[r][c], r * STEP, 0, flags[r][c])
    return data, heights


def corners(kind, col, row):
    """Corner (col, row) positions as slot_track_sweep orders them."""
    v, v1, vw1, vw2 = (col, row), (col + 1, row), (col, row + 1), (col + 1, row + 1)
    return {2: (vw1, v, v1), 3: (v1, vw2, vw1), 4: (vw2, vw1, v), 5: (v, v1, vw2)}.get(kind, (v, v1, vw2))


def pack_normal(n):
    length = sum(x * x for x in n) ** 0.5 or 1
    x, y, z = (int(round(c / length * 500)) & 0x3FF for c in n)
    return x << 20 | y << 10 | z


def plane_for(kind, col, row, heights, rng):
    if rng.randrange(15) == 0:
        return rng.getrandbits(32)
    if kind >= 6:
        n = rng.choice([(1, 0, 0), (-1, 0, 0), (0, 0, 1), (0, 0, -1), (1, 0, 1)])
    else:
        a, b, c = corners(kind, col, row)
        pa, pb, pc = ((p[0] * STEP, heights[p[1]][p[0]], p[1] * STEP) for p in (a, b, c))
        u = [pb[i] - pa[i] for i in range(3)]
        v = [pc[i] - pa[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if rng.randrange(2):
            n = tuple(-x for x in n)
    return pack_normal(n)


def make_case(rng):
    grid, heights = make_grid(rng)
    count = rng.randrange(1, 24)
    centre = (rng.randrange(2, W - 3), rng.randrange(2, H - 3))
    entries = bytearray()
    used = set()
    for _ in range(count):
        kind = rng.choice([0, 1, 2, 3, 4, 5, 0, 1, 6, 7])
        col = min(W - 3, max(1, centre[0] + rng.randrange(-2, 3)))
        row = min(H - 3, max(1, centre[1] + rng.randrange(-2, 3)))
        if (kind, col, row) in used and rng.randrange(3):
            continue
        used.add((kind, col, row))
        corner = rng.randrange(4) if rng.randrange(3) else 0
        key = 0 | (col & 0xFF) << 8 | (row & 0xFFFF) << 16
        plane = plane_for(kind, col, row, heights, rng)
        b0 = kind | (rng.choice([0, 0, 0x10, 0x20, 0xE0]) if rng.randrange(4) == 0 else 0)
        entries += struct.pack("<BBBBIII", b0, corner, rng.getrandbits(8), W - 1, key, plane, vertex_addr(col, row))
    count = len(entries) // 16
    body = bytearray(0x200)
    struct.pack_into("<I", body, 0, rng.choice([3, 3, 4, 7]) | 0x800)
    struct.pack_into("<I", body, 0x154, LIST)
    struct.pack_into("<H", body, 0x15C, count)
    cx, cz = centre
    slot = bytearray(0x48)
    pos = (cx * STEP * 256 + rng.randrange(-0x6000, 0x6000),
           heights[cz][cx] * 256 + rng.randrange(-0x3000, 0x3000),
           cz * STEP * 256 + rng.randrange(-0x6000, 0x6000))
    vel = (rng.randrange(-0x1000, 0x1000), rng.randrange(-0x2000, 0x800), rng.randrange(-0x1000, 0x1000))
    radius = 0 if rng.randrange(4) == 0 else rng.randrange(0x100, 0x1800)
    struct.pack_into("<3i3i", slot, 8, *pos, *vel)
    struct.pack_into("<i", slot, 0x40, radius)
    out = struct.pack("<9i", *[rng.randrange(-0x10000, 0x10000) for _ in range(9)])
    limit = rng.choice([0x10000, 0x8000, rng.randrange(1, 0x10000), rng.randrange(1, 0x10000)])
    if rng.randrange(6) == 0:
        limit |= rng.choice([0x1000000, 0x7F000000])
    hits = bytes(rng.getrandbits(8) for _ in range(count * 0x28))
    return dict(grid=bytes(grid), entries=bytes(entries), count=count, body=body, slot=slot, out=out, limit=limit, hits=hits)


def run(m, target, case):
    m.write(GRID, case["grid"])
    m.write(LIST, case["entries"])
    m.write(BODY, bytes(case["body"]))
    m.write(SLOT, bytes(case["slot"]))
    m.write(OUT, case["out"])
    m.write(HITS, case["hits"])
    m.write(TILE, struct.pack("<4I", 0, 0, 0, H - 1))
    m.write(TILES, struct.pack("<I", TILE))
    m.write(0x6C1E30, struct.pack("<I", TILES - 0x100) )
    m.write(TILES - 0x100, struct.pack("<I", TILES))
    for a, s in WALL:
        m.write(a, b"\0" * s)
    m.write(GTE[0], b"\0" * (GTE[1] - GTE[0]))
    r = m.call(target, [BODY, SLOT, OUT, case["limit"]])
    state = (m.read(OUT, 36) + m.read(LIST, case["count"] * 16) + m.read(HITS, case["count"] * 0x28) +
             b"".join(m.read(a, s) for a, s in WALL) + m.read(GTE[0], GTE[1] - GTE[0]))
    return r, state, struct.unpack("<9i", m.read(OUT, 36))


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 5000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x10000)
    rng = random.Random(3)
    bad = 0
    states = {}
    for i in range(iterations):
        case = make_case(rng)
        (r1, s1, o1), (r2, s2, o2) = run(m, 0x415EA0, case), run(m, m.symbols["slot_track_sweep"], case)
        states[o1[7]] = states.get(o1[7], 0) + 1
        if r1 != r2 or s1 != s2:
            bad += 1
            if bad <= 5:
                print(f"case {i}: original {r1} out {o1}\n         ours     {r2} out {o2}" +
                      ("" if s1[36:] == s2[36:] else " (other memory differs)"))
    print(f"slot_track_sweep: {'ok' if not bad else f'{bad} of {iterations} differ'}; result states {states}")
    names = open("src/coverage.c").read()
    counters = struct.unpack("<64I", m.read(m.symbols["coverage"], 256))
    import re
    listed = re.findall(r'"([a-z0-9 +\-]+)"', names.split("coverage_names")[1])
    print("our coverage:", {n: c for n, c in zip(listed, counters) if c})
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
