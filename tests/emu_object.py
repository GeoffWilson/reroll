#!/usr/bin/env python3
"""Differential test of object_polygon_sweep (0x414fe0) against the original,
on random box-shaped track objects (as object_contacts sees them) and a
sphere moving near them, in the object's frame.

    tests/emu_object.py EXE build/emu_fixed.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH = 0x01000000
OBJECT, MODEL, PLANES, VERTS, SLOT, OUT, POLY = (SCRATCH + o for o in (0, 0x400, 0x800, 0xA00, 0xC00, 0xC80, 0xD00))
GTE = (0x6CCBE0, 0x6CCD00)
LZ = (0x6CCCDC, 8)

# a box's corners and faces (corner order around each face) and edges
CORNERS = [(x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
FACES = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]


def pack(n, scale=500):
    length = sum(c * c for c in n) ** 0.5 or 1
    x, y, z = (int(round(c / length * scale)) & 0x3FF for c in n)
    return x << 20 | y << 10 | z


def make_case(rng):
    size = [rng.randrange(4, 0x30) for _ in range(3)]
    verts = [tuple(c * s for c, s in zip(corner, size)) for corner in CORNERS]
    edges = {}
    edge_list = []

    def edge_id(a, b):
        key = (min(a, b), max(a, b))
        if key not in edges:
            edges[key] = len(edge_list)
            d = [verts[b][i] - verts[a][i] for i in range(3)]
            edge_list.append(struct.pack("<IBBBB", pack(d) if rng.randrange(10) else rng.getrandbits(32),
                                         a + 1, b + 1, 0, 0))
        return edges[key]

    planes = bytearray()
    faces = bytearray()
    for f, corners in enumerate(FACES):
        a, b, c, d = corners
        u = [verts[b][i] - verts[a][i] for i in range(3)]
        v = [verts[d][i] - verts[a][i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        if rng.randrange(2):
            n = tuple(-x for x in n)
        triangle = rng.randrange(5) == 0
        plane = pack(n) if rng.randrange(12) else rng.getrandbits(32)
        planes += struct.pack("<BBBBI", a + 1, d + 1, b + 1, 0 if triangle else c + 1, plane)
        ids = [edge_id(corners[i], corners[(i + 1) % 4]) for i in range(4)]
        if rng.randrange(8) == 0:
            rng.shuffle(ids)
        faces += struct.pack("<4B4BB", *ids, 0, 0, 0, 0, f)
    vert_data = b"".join(struct.pack("<hhhh", *v, 0) for v in verts)

    model = bytearray(0x400)
    struct.pack_into("<HH", model, 0x38, 0x100, 0x200)
    struct.pack_into("<I", model, 0x40, VERTS + 8)
    model[0x100:0x100 + len(faces)] = faces
    for i, e in enumerate(edge_list):
        model[0x200 + i * 8:0x208 + i * 8] = e

    obj = bytearray(0x200)
    pos = [rng.randrange(-0x40, 0x40) * 0x10000 + rng.getrandbits(16) for _ in range(3)]
    struct.pack_into("<I", obj, 0x10, MODEL)
    struct.pack_into("<I", obj, 0x2C, PLANES)
    struct.pack_into("<3i", obj, 0x34, *pos)

    kind = rng.choice(["point", "edge", "faces", "faces", "faces"])
    first = rng.randrange(6)
    if kind == "point":
        info = 0x10000 | first
    elif kind == "edge":
        info = 0x20000 | first
    else:
        count = rng.randrange(1, 7 - first) if rng.randrange(4) else 6 - first
        info = count << 16 | first
    if rng.randrange(20) == 0:
        info = rng.getrandbits(8) | rng.randrange(0, 0x20) << 16
    corner = verts[rng.randrange(8)]
    poly = struct.pack("<hhhhhhIII", *corner, 0, 0, 0, rng.getrandbits(32), info, 0)

    centre = [p >> 8 for p in struct.unpack("<3i", obj[0x34:0x40])]
    scale = max(size) * 0x200
    slot = bytearray(0x48)
    spos = [centre[i] + rng.randrange(-scale, scale) for i in range(3)]
    vel = [rng.randrange(-0x2000, 0x2000) for _ in range(3)]
    radius = 0 if rng.randrange(6) == 0 else rng.randrange(0x100, 0x2000)
    struct.pack_into("<3i3i", slot, 8, *spos, *vel)
    struct.pack_into("<i", slot, 0x40, radius)
    dt = rng.choice([0x10000, 0x10000, rng.randrange(1, 0x10000)])
    out = struct.pack("<9i", *[rng.randrange(-0x10000, 0x10000) for _ in range(9)])
    return dict(obj=bytes(obj), model=bytes(model), planes=bytes(planes), verts=bytes(8) + vert_data,
                slot=bytes(slot), out=out, poly=poly, dt=dt)


def run(m, target, case):
    m.write(OBJECT, case["obj"])
    m.write(MODEL, case["model"])
    m.write(PLANES, case["planes"])
    m.write(VERTS, case["verts"])
    m.write(SLOT, case["slot"])
    m.write(OUT, case["out"])
    m.write(POLY, case["poly"])
    m.write(GTE[0], b"\0" * (GTE[1] - GTE[0]))
    m.write(LZ[0], b"\0" * LZ[1])
    r = m.call(target, [0, OBJECT, POLY, SLOT, OUT, case["dt"]])
    state = m.read(OUT, 36) + m.read(SLOT, 0x48) + m.read(GTE[0], GTE[1] - GTE[0]) + m.read(*LZ)
    return r, state, struct.unpack("<9i", m.read(OUT, 36))


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 5000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x10000)
    rng = random.Random(5)
    bad = 0
    states = {}
    for i in range(iterations):
        case = make_case(rng)
        (r1, s1, o1), (r2, s2, o2) = run(m, 0x414FE0, case), run(m, m.symbols["object_polygon_sweep"], case)
        states[o1[7]] = states.get(o1[7], 0) + 1
        if r1 != r2 or s1 != s2:
            bad += 1
            if bad <= 5:
                print(f"case {i}: original {r1} out {o1}\n         ours     {r2} out {o2}" +
                      ("" if s1[36:] == s2[36:] else " (other memory differs)"))
    print(f"object_polygon_sweep: {'ok' if not bad else f'{bad} of {iterations} differ'}; result states "
          f"{dict(sorted(states.items()))}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
