#!/usr/bin/env python3
"""Differential test of post_f.c (attached points, object grid, slot pool,
fallen bodies and lap counting after the world step) against the
original, on a random synthetic world: a track grid with cells and
layers, height-grid tiles with planes, checkpoints, racing lines, a grid
link pool, collision slots, bodies and car slots.

    tests/emu_post_f.py EXE build/emu_f.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x1C000
O = {name: SCRATCH + off for name, off in dict(
    TRACK=0x0, RACE=0x200, ROWS=0x1000, CELLS=0x2000, LAYERS=0x2200, PATCHES=0x2400, MODEL_PTRS=0x1B000,
    MODELS=0x2600, VERTS=0x2800, INFO=0x4800, EDGES=0x5800, NORMALS=0x5C00, PLANES=0x6000, CHECKPOINTS=0x6400,
    CP_TABLE=0x6600, LINES=0x6700, NODES=0x6800, POOL=0x7000, SLOTS=0x9000, FREE=0x9A00, USED=0x9A40,
    WHERE=0x9A80, BODY_LIST=0x9C00, GROUP=0x9D00, GROUP_ENTRIES=0x9D10, BODIES=0xA000, CONTACTS=0x19000,
    PARTICLES=0x19400, CAMERAS=0x1A000, SOUND=0x1A400, INFOS=0x1A800, POINTS_MODEL=0x1AA00, MARKERS=0x1AC00,
    MARKER_POINTS=0x1B400, OBJ_CONTACTS=0x1AE00, FLAG=0x1AF00).items()}
BODY_SIZE, NBODIES = 0x2800, 6
NSLOTS, NPOOL, NPARTICLES = 32, 64, 8
SOUND_STUB = 0x471C90

COMPARE = [(SCRATCH, SCRATCH_SIZE), (0x4D1000, 0x3000), (0x6BFB80, 0x274 * 6), (0x6B9B80, 0x20), (0x6CE73C, 0x24),
           (0x6CC800, 0x600), (0x4BE128, 0xC), (0x4B11D8, 8), (0x6C1B88, 0x400), (0x6B8D00, 0x40), (0x5D6E00, 0x40),
           (0x600640, 0x40), (0x6B9D48, 4), (0x5FF4A0, 0x30 * 0x40), (0x6BFAC0, 0x40)]

# scratch and other state the code under test may leave behind: reset for every run
RESET = [(0x4D1000, 0x3000), (0x6CC800, 0x620), (0x6C1B88, 0x400), (0x600640, 0x40), (0x5FF4A0, 0xC00),
         (0x6C8000, 0x100), (0x6CE760, 0x100), (0x5D9B20, 0x10), (0x6B9D48, 4), (0x6B8D00, 0x40)]
COMPARE += [(0x6C8000, 0x100), (0x6CE760, 0x100)]

GLOBALS = {0x6C1E30: O["TRACK"], 0x4B1960: O["RACE"], 0x6BFAD4: O["ROWS"], 0x6BFAF8: O["LAYERS"],
           0x6BFAC0: O["MODEL_PTRS"], 0x6BFADC: O["PLANES"], 0x6B9B90: O["POOL"], 0x6CE740: O["SLOTS"],
           0x6CE73C: O["FREE"], 0x6CE744: O["USED"], 0x6CE750: O["WHERE"], 0x6B9B80: O["BODY_LIST"],
           0x6BFB30: O["CHECKPOINTS"], 0x6C1EB0: O["MARKERS"]}


def sound_stub(args=range(5), where=0):
    """sound_play: fold the five arguments into a checksum, count calls,
    return 0 (also used for other presentation code, at another `where`)."""
    s, c = O["SOUND"] + where, O["SOUND"] + where + 4
    code = b""
    for i in args:
        code += bytes([0x8B, 0x44, 0x24, 4 + 4 * i])                # mov eax, [esp + 4 + 4i]
        code += b"\x33\x05" + struct.pack("<I", s)                   # xor eax, [sum]
        code += b"\x69\xC0" + struct.pack("<I", 0x01000193)          # imul eax, eax, prime
        code += b"\xA3" + struct.pack("<I", s)                       # mov [sum], eax
    code += b"\xFF\x05" + struct.pack("<I", c) + b"\x31\xC0\xC3"    # inc [count]; xor eax, eax; ret
    return code


def pack_normal(n):
    length = sum(x * x for x in n) ** 0.5 or 1
    x, y, z = (int(round(c / length * 500)) & 0x3FF for c in n)
    return x << 20 | y << 10 | z


class World:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {}

    def put(self, address, fmt, *values):
        self.mem[address] = struct.pack(fmt, *values)

    def raw(self, address, data):
        self.mem[address] = bytes(data)


def pool_record(i):
    return O["POOL"] + (i - 1) * 0x80


def layer(i):
    return O["LAYERS"] + i * 0xC


def body_addr(i):
    return O["BODIES"] + i * BODY_SIZE


def valid_key(w, rng):
    t = rng.randrange(4)
    width, height = w.tiles[t]
    return t | rng.randrange(width) << 8 | rng.randrange(height - 1) << 16


def make_world(rng):
    w = World(rng)
    r = rng.randrange
    big = bytearray(rng.randbytes(SCRATCH_SIZE))
    w.base = big

    def put(address, fmt, *values):
        struct.pack_into(fmt, big, address - SCRATCH, *values)

    def u8(address, v):
        big[address - SCRATCH] = v & 0xFF

    w.put_ = put
    # track cells: every row spans all columns (some rows have none -> off track)
    for row in range(256):
        a = O["ROWS"] + row * 0x10
        put(a, "<6H", 0xFF00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF)
        put(a + 0xC, "<I", 0 if r(12) == 0 else O["CELLS"])
    for col in range(256):
        put(O["CELLS"] + col * 2, "<H", r(16) * 0xC)
    for i in range(16):
        put(layer(i), "<I", 1)
        put(layer(i) + 4, "<I", r(16) << 16 | rng.getrandbits(16))
        put(layer(i) + 8, "<HH", r(NPOOL + 1), r(NPOOL + 1))
    # tiles (models): width W (cells per row), H vertex rows
    w.tiles = []
    for t in range(4):
        width, height = r(3, 9), r(3, 10)
        m = O["MODELS"] + t * 0x60
        for k in range(t, 256, 4):
            put(O["MODEL_PTRS"] + k * 4, "<I", m)
        verts, info, edges = O["VERTS"] + t * 0x800, O["INFO"] + t * 0x400, O["EDGES"] + t * 0x100
        put(m + 4, "<I", rng.choice([0, 0, 1]))
        put(m + 8, "<ii", width, height - 1)
        put(m + 0x14, "<h", r(-0x100, 0x100))
        put(m + 0x20, "<I", verts)
        put(m + 0x38, "<I", edges)
        put(m + 0x50, "<I", info)
        base_y = r(-0x80, 0x80)
        for row in range(height):
            for col in range(width + 1):
                put(verts + (row * (width + 1) + col) * 8, "<hhhBB", col * 0x40 + r(-4, 5) - 0x100,
                    base_y + r(-0x20, 0x21), row * 0x40 + r(-4, 5) - 0x100, rng.choice([0, 0, 2, 6, 2]),
                    rng.getrandbits(8))
        for i in range(0x80):
            put(info + i * 8 + 2, "<H", r(255))
        for row in range(height):
            put(edges + row * 4, "<HH", rng.choice([0xFFFF, r(256)]), rng.choice([0xFFFF, r(256)]))
        w.tiles.append((width, height))
    put(O["TRACK"], "<I", O["MODEL_PTRS"])
    put(O["TRACK"] + 0x1C, "<I", O["NORMALS"])
    for i in range(256):
        put(O["NORMALS"] + i * 4, "<I", rng.getrandbits(32))
        put(O["PLANES"] + i * 4, "<I", pack_normal((r(-9, 10), r(-9, 10), r(-9, 10))) if r(10) else
            rng.getrandbits(32))
    # patches (track + 0x80)
    put(O["TRACK"] + 0x80, "<I", O["PATCHES"])
    for i in range(20):
        t = r(4)
        width, height = w.tiles[t]
        row0, col0 = r(height - 1), r(width)
        rows = r(0, min(4, height - 1 - row0) + 1) if r(8) == 0 else r(1, min(4, height - 1 - row0) + 1)
        cols = r(0, width - col0 + 1) if r(8) == 0 else r(1, width - col0 + 1)
        put(O["PATCHES"] + i * 6, "<HHH", col0 << 10 | row0, t | r(3) << 9 | rng.getrandbits(1) << 7,
            r(0x30) | rows << 8 | cols << 12)
    # checkpoints
    ncp = r(2, 9)
    put(O["TRACK"] + 0x44, "<B", ncp)
    put(O["TRACK"] + 0x70, "<I", O["CHECKPOINTS"])
    put(O["TRACK"] + 0x90, "<I", O["CP_TABLE"])
    for i in range(16):
        a = O["CHECKPOINTS"] + i * 0x14
        normal = [r(-64, 65) for _ in range(3)]
        put(a, "<BBH3hxx3hBB", r(8), r(ncp), r(0x10000), r(-0x100, 0x100), r(-0x80, 0x80), r(-0x100, 0x100),
            *normal, rng.choice([0, 1]), 0)
    for i in range(8):
        put(O["CP_TABLE"] + i * 4, "<4B", *[rng.choice([r(16), r(16), 0xFF]) for _ in range(4)])
    # racing lines
    nlines = r(2, 8)
    put(O["TRACK"] + 0x46, "<B", nlines)
    put(O["TRACK"] + 0x60, "<I", O["LINES"])
    w.nodes = []
    for i in range(8):
        count = r(2, 16)
        put(O["LINES"] + i * 0xC, "<HBxBxxxI", count, rng.choice([0, 1]), rng.choice([0, 1]), O["NODES"] + i * 0x100)
        for j in range(16):
            put(O["NODES"] + i * 0x100 + j * 0x10, "<3h", r(-0x100, 0x100), r(-0x80, 0x80), r(-0x100, 0x100))
            w.nodes.append(O["NODES"] + i * 0x100 + j * 0x10)
    # race
    race = O["RACE"]
    ncars = r(1, 7)
    put(race + 0x54D, "<B", ncars)
    put(race + 0x551, "<B", rng.choice([0, 0, 1, 3, 4, 8, 2]))
    put(race + 0x579, "<B", rng.choice([0, 0, 3, 0xB, 0xC]))
    put(race + 0x57C, "<I", rng.choice([0, 4, 5, 6]))
    put(race + 0x586, "<B", r(1, 6))
    put(race + 0x58C, "<B", rng.choice([0, 0, 1, 2]))
    put(race + 0xA86, "<B", 0)
    w.ncars = ncars
    # grid pool records: random links into the pool, cells in the layer table
    for i in range(1, NPOOL + 1):
        a = pool_record(i)
        put(a + 4, "<HH", r(NPOOL + 1), r(NPOOL + 1))
        put(a + 0xC, "<I", layer(r(16)) + rng.choice([0, 0, 1]))
    # collision slots and the free / used pool
    perm = list(range(NSLOTS))
    rng.shuffle(perm)
    used = r(4, 20)
    for k, s in enumerate(perm[:used]):
        u8(O["USED"] + k, s)
        u8(O["WHERE"] + s, k)
    for k, s in enumerate(perm[used:]):
        u8(O["FREE"] + k, s)
    w.used_slots = perm[:used]
    w.glob = dict(GLOBALS)
    w.glob[0x6CE748] = used - 1
    w.glob[0x6CE754] = used
    w.glob[0x6CE758] = NSLOTS - used - 1
    for s in range(NSLOTS):
        a = O["SLOTS"] + s * 0x48
        put(a + 8, "<6i", *[r(-0x10000, 0x10000) for _ in range(3)], *[r(-0x400, 0x400) for _ in range(3)])
        put(a + 0x40, "<i", r(0x100, 0x2000))
    # bodies
    w.bodies = []
    for b in range(NBODIES):
        a = body_addr(b)
        kind = rng.choice([3, 3, 3, 5, 7, 0x15, 0x21, 4, 0xC])
        put(a, "<I", kind | rng.choice([0, 0x2000, 0x8000]))
        put(a + 4, "<HH", r(NPOOL + 1), r(NPOOL + 1))
        put(a + 0xC, "<I", layer(r(16)) + rng.choice([0, 1]))
        put(a + 0x10, "<I", O["POINTS_MODEL"])
        node = rng.choice(w.nodes)
        put(a + 0x34, "<3i", *[(c << 16) + r(-0x200000, 0x200000) for c in
                                struct.unpack_from("<3h", big, node - SCRATCH)])
        q = [r(-0x10000, 0x10000) for _ in range(4)]
        put(a + 0x58, "<4i", *q)
        put(a + 0x68, "<3i", *[r(-0x8000, 0x8000) for _ in range(3)])
        put(a + 0xC4, "<9h", *[r(-0x1000, 0x1001) for _ in range(9)])
        n = r(0, 3)
        put(a + 0x10C, "<b", n)
        put(a + 0x110, "<I", rng.getrandbits(32))
        put(a + 0x118, "<I", rng.choice([0, r(0x10000)]))
        put(a + 0x11C, "<I", rng.choice([0xFFFFFFFF, valid_key(w, rng)]))
        put(a + 0x120, "<i", rng.choice([0x7FFFFFFF, r(0x100000)]))
        put(a + 0x128, "<I", rng.choice([0x20, 0x20, 0, 1]))
        for k in range(n + 1):
            u8(a + 0x140 + k, w.used_slots[r(len(w.used_slots))])
        put(a + 0x154, "<I", O["CONTACTS"])
        put(a + 0x158, "<I", O["OBJ_CONTACTS"])
        put(a + 0x15C, "<HH", r(4), 0)
        put(a + 0x164, "<I", O["CAMERAS"] + b * 0x80 if kind == 4 else O["INFOS"] + b * 0x20
            if kind in (7, 0x15, 0x21) else rng.choice([O["INFOS"] + b * 0x20, O["CAMERAS"] + b * 0x80]))
        for k in range(5):
            put(a + 0x16A + k * 2, "<H", rng.choice([0, 0x1000 | r(64), 0x2000 | r(8), 0x4000 | r(16)]))
        for k in range(4):
            u8(a + 0x324 + k * 0xC4, rng.getrandbits(8) | rng.choice([0, 1]))
        car = b % 6
        put(a + 0x64E, "<H", car)
        put(a + 0x658, "<h", rng.choice([0, 1, 2, 3, 5, r(-5, 300)]))
        u8(a + 0x661, rng.choice([0, 1]))
        put(a + 0x6AC, "<I", O["FLAG"])
        u8(a + 0x70C, rng.choice([8, 10]))
        w.bodies.append(a)
    put(O["FLAG"], "<I", 0)
    # info records (pickups / dropped items), cameras
    for b in range(NBODIES):
        put(O["INFOS"] + b * 0x20, "<IiBBxx", body_addr(b), r(0, 20), r(256), r(0x40))
    # body list and group
    order = list(range(NBODIES))
    rng.shuffle(order)
    count = r(1, NBODIES + 1)
    listed = [body_addr(i) for i in order[:count]]
    for k, a in enumerate(listed):
        put(O["BODY_LIST"] + k * 4, "<I", a)
    for k in range(count, 16):
        put(O["BODY_LIST"] + k * 4, "<I", 0)
    w.glob[0x6B9B84] = count | rng.getrandbits(16) << 16
    w.listed = listed
    group = listed[:]
    rng.shuffle(group)
    for a in listed:
        put(a + 0x130, "<I", rng.choice([0, O["GROUP"]]))
    for i in range(NBODIES):
        if body_addr(i) not in listed:
            put(body_addr(i) + 0x130, "<I", 0)
    put(O["GROUP"], "<IH", O["GROUP_ENTRIES"], len(group))
    for k, a in enumerate(group):
        put(O["GROUP_ENTRIES"] + k * 4, "<I", a)
    # points model: +0x4c offset to 5 point offsets
    put(O["POINTS_MODEL"] + 0x4C, "<H", 0x60)
    for k in range(5):
        put(O["POINTS_MODEL"] + 0x60 + k * 8, "<4h", *[r(-0x200, 0x200) for _ in range(3)], 0)
    put(O["MARKERS"] + 0x10C, "<I", O["MARKER_POINTS"])
    # particles: some free (chain via +0xac), the rest in use (oldest .. newest)
    parts = [O["PARTICLES"] + i * 0xB4 for i in range(NPARTICLES)]
    rng.shuffle(parts)
    nfree = r(0, 3)
    free, inuse = parts[:nfree], parts[nfree:]
    for i, p in enumerate(free):
        put(p + 0xAC, "<I", free[i + 1] if i + 1 < len(free) else 0)
    for i, p in enumerate(inuse):
        put(p + 0xA8, "<I", inuse[i + 1] if i + 1 < len(inuse) else 0)
        put(p + 0xAC, "<I", inuse[i - 1] if i else 0)
        put(p + 0xB0, "<I", rng.choice([0, 0, 1]))
    for p in parts:
        u8(p + 0x94, rng.choice([0, 1]))
        put(p + 4, "<HH", r(NPOOL + 1), r(NPOOL + 1))
        put(p + 0xC, "<I", layer(r(16)) + rng.choice([0, 1]))
    w.glob[0x4BE128] = free[0] if free else 0
    w.glob[0x4BE130] = inuse[0]
    w.glob[0x4BE12C] = inuse[-1]
    # car slots
    slots = bytearray(rng.randbytes(0x274 * 6))
    for i in range(6):
        o = i * 0x274
        struct.pack_into("<I", slots, o, body_addr(i))
        struct.pack_into("<I", slots, o + 0x28, rng.choice([O["CAMERAS"] + i * 0x80, 0x12345678]))
        slots[o + 0x2C] = r(0, 6)
        slots[o + 0x2E] = slots[o + 0x2C] + rng.choice([0, 0, 1])
        slots[o + 0x2F] = r(256)
        slots[o + 0x30] = r(ncp)
        slots[o + 0x31] = r(ncp)
        struct.pack_into("<i", slots, o + 0x34, r(0x10000))
        struct.pack_into("<I", slots, o + 0x64, O["LINES"])
        slots[o + 0x258] = rng.choice([0, 0x5A, 0x5B, r(256)])
        slots[o + 0x259] = rng.choice([0, 1])
        slots[o + 0x268] = 0
    w.slots = slots
    # contact list entries already there (body +0x154)
    for k in range(16):
        t = r(4)
        width, height = w.tiles[t]
        row, col = r(height - 1), r(width)
        verts = O["VERTS"] + t * 0x800 + (row * (width + 1) + col) * 8
        put(O["CONTACTS"] + k * 0x10, "<BBBBIII", r(8), r(2), width, 0, t | col << 8 | row << 16,
            rng.getrandbits(32), verts)
    w.glob[0x6B984C] = 0xFFFFFFFF
    w.glob[0x6BFB40] = O["NORMALS"]  # per layer extras (0x440c60)
    for i in range(NBODIES):
        put(O["CAMERAS"] + i * 0x80, "<I", O["CAMERAS"] + i * 0x80 + 0x40)
    w.glob[0x4D11F0] = rng.choice([0, rng.choice(w.nodes)])
    w.glob[0x6CCB7C] = r(0x18)
    w.glob[0x4B11D8] = rng.getrandbits(31)
    w.glob[0x4B1C8C] = r(0x40)
    w.glob[0x4D11EC] = r(0, 4)
    for k in range(4):
        w.glob[0x6B8D00 + k * 4] = body_addr(r(NBODIES))
    for k in range(8):
        w.glob[0x5D6E00 + k * 4] = pool_record(r(1, NPOOL + 1))
    return w


def load(m, w):
    # stale stack (read uninitialised by the track sweep, see sweep_stale)
    m.write(E.STACK + E.STACK_SIZE - 0x8000, bytes(0x8000))
    if "sweep_stale" in m.symbols:
        m.write(m.symbols["sweep_stale"], bytes(4))
    m.write(SCRATCH, bytes(w.base))
    m.write(0x6BFB80, bytes(w.slots))
    for a, v in w.glob.items():
        m.write(a, struct.pack("<I", v & 0xFFFFFFFF))
    for a, n in RESET:
        m.write(a, bytes(n))
    for a, v in w.glob.items():
        m.write(a, struct.pack("<I", v & 0xFFFFFFFF))


def snapshot(m):
    return b"".join(m.read(a, n) for a, n in COMPARE)


def where(offset):
    for a, n in COMPARE:
        if offset < n:
            return f"{a + offset:#x}"
        offset -= n
    return "?"


# name: (original, argument maker, returns?)
def args_for(name, w, rng):
    r = rng.randrange
    b = rng.choice(w.bodies)
    if name in ("body_retire", "pickup_fallen", "dropped_fallen"):
        b = rng.choice(w.listed)
    if name in ("grid_record",):
        return [rng.choice([0, r(1, NPOOL + 1)]) | rng.getrandbits(16) << 16]
    if name == "grid_index":
        return [rng.choice([0, pool_record(r(1, NPOOL + 1))])]
    if name.startswith("object_grid_link2_at"):
        node = rng.choice(w.nodes)
        return [pool_record(r(1, NPOOL + 1))] + [r(-0x2000000, 0x2000000) for _ in range(3)]
    if name.startswith("object_grid"):
        return [rng.choice([pool_record(r(1, NPOOL + 1)), b])]
    if name in ("body_points_update", "body_retire", "pickup_fallen", "respawn_blocked_b"):
        return [b]
    if name == "body_list_remove":
        return [rng.choice(w.listed)]
    if name == "slot_give":
        return [rng.choice(w.used_slots)]
    if name == "particle_fade":
        return [O["PARTICLES"] + r(NPARTICLES) * 0xB4, r(-10, 60), r(2)]
    if name in ("spark_spawn", "spark_spawn_plain"):
        at, d = O["SOUND"] + 0x40, O["SOUND"] + 0x50
        w.base[at - SCRATCH:at - SCRATCH + 24] = struct.pack("<6i", *[r(-0x1000000, 0x1000000) for _ in range(3)],
                                                             *[r(-0x400, 0x400) for _ in range(3)])
        return [at, at + 12, rng.choice([0, 1, 5])] + ([r(2)] if name == "spark_spawn" else [])
    if name == "explosion_effects":
        at = O["SOUND"] + 0x40
        w.base[at - SCRATCH:at - SCRATCH + 24] = struct.pack("<6i", *[r(-0x1000000, 0x1000000) for _ in range(3)],
                                                             *[r(-0x400, 0x400) for _ in range(3)])
        what = rng.choice([0, 0x10000, 0x20, 0x80, 0x100, 0x40, 0x200, 0x400, 0x800, 0x20000, 0x40000, 0x80000,
                           1, 4, 8, 0x10, 0x1000, rng.getrandbits(20)])
        return [at, at + 12, rng.choice([0, 8, r(100)]), what, rng.choice([0, 0xB, 0x100])]
    if name == "dropped_fallen":
        return [O["INFOS"] + w.bodies.index(b) * 0x20]
    if name == "quat_facing":
        q, d = O["SOUND"] + 0x40, O["SOUND"] + 0x60
        w.base[d - SCRATCH:d - SCRATCH + 12] = struct.pack("<3i", *[r(-0x40000, 0x40000) for _ in range(3)])
        return [q, d]
    if name in ("car_camera_cut",):
        return [b]
    if name == "car_respawn_sparks":
        return [b, rng.choice([0, 1, 2])]
    if name == "patch_gather":
        slot = O["SLOTS"] + r(NSLOTS) * 0x48
        pt = O["SOUND"] + 0x40
        w.base[pt - SCRATCH:pt - SCRATCH + 6] = struct.pack("<3h", r(-0x140, 0x140), r(-0xa0, 0xa0),
                                                            r(-0x140, 0x140))
        return [slot, O["PATCHES"] + r(20) * 6, b, rng.choice([0, r(0x10000)]), pt,
                rng.choice([0, r(0x400), r(0x40000)])]
    if name == "respawn_blocked":
        n = struct.unpack_from("<b", w.base, b + 0x10C - SCRATCH)[0]
        return [b, rng.choice(w.bodies), rng.choice([1, 1, 2] + ([0] if n >= 1 else []))]
    if name == "respawn_try":
        pos, q, key = O["SOUND"] + 0x40, O["SOUND"] + 0x50, O["SOUND"] + 0x60
        node = rng.choice(w.nodes)
        x, y, z = struct.unpack_from("<3h", w.base, node - SCRATCH)
        w.base[pos - SCRATCH:pos - SCRATCH + 12] = struct.pack("<3i", x << 16, y << 16, z << 16)
        w.base[q - SCRATCH:q - SCRATCH + 16] = struct.pack("<4i", *[r(-0x10000, 0x10000) for _ in range(4)])
        n = struct.unpack_from("<b", w.base, b + 0x10C - SCRATCH)[0]
        return [b, pos, rng.choice([q, q, 0]), b + 0xC4, rng.choice([1, 1, 2] + ([0] if n >= 1 else [])), key]
    if name == "respawn_place":
        pos, q = O["SOUND"] + 0x40, O["SOUND"] + 0x50
        w.base[pos - SCRATCH:pos - SCRATCH + 28] = struct.pack("<7i", *[r(-0x2000000, 0x2000000) for _ in range(7)])
        return [b, pos, q, b + 0xC4, O["LINES"] + r(8) * 0xC, r(16), rng.choice([0xFFFFFFFF, valid_key(w, rng)])]
    if name in ("slot_lap_line", "slot_lap_done", "slot_checkpoints_reset"):
        return [0x6BFB80 + r(w.ncars) * 0x274]
    if name == "slot_checkpoints":
        return [0x6BFB80 + r(w.ncars) * 0x274, rng.choice([0, 0, 1])]
    return []


TESTS = {
    "grid_record": (0x4613E0, True),
    "grid_index": (0x4613C0, True),
    "object_grid_unlink": (0x4611D0, False),
    "object_grid_unlink2": (0x4612A0, False),
    "object_grid_insert": (0x461170, False),
    "object_grid_insert2": (0x461240, False),
    "object_grid_link": (0x460FB0, False),
    "object_grid_link2_at": (0x460FF0, False),
    "body_points_update": (0x442940, False),
    "bodies_points_update": (0x442840, False),
    "slot_take": (0x4190A0, True),
    "slot_give": (0x4190F0, False),
    "body_list_remove": (0x45C040, False),
    "body_retire": (0x419300, False),
    "particle_take": (0x42AC30, True),
    "particle_fade": (0x42CD40, False),
    "spark_spawn": (0x42B700, False),
    "spark_spawn_plain": (0x42B8B0, False),
    "explosion_effects": (0x42AA30, False),
    "pickup_fallen": (0x458320, False),
    "dropped_fallen": (0x46C160, False),
    "cars_check_fallen": (0x469810, False),
    "quat_facing": (0x40EE60, False),
    "car_camera_cut": (0x406E00, False),
    "car_respawn_sparks": (0x468F80, False),
    "patch_gather": (0x469040, False),
    "respawn_blocked": (0x468D80, True),
    "respawn_try": (0x4687F0, True),
    "respawn_place": (0x468BB0, False),
    "slot_lap_line": (0x43E000, False),
    "slot_lap_done": (0x43D460, False),
    "slot_checkpoints": (0x43D8E0, True),
    "slot_checkpoints_reset": (0x43DE60, False),
    "fallen_bodies_update": (0x468450, False),
}


def run_case(m, name, address, returns, rng, stats):
    w = make_world(rng)
    args = args_for(name, w, rng)
    results = []
    for target in (address, m.symbols[name]):
        load(m, w)
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        results.append((r, snapshot(m)))
    stats[results[0][0][0]] = stats.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        return f"{name}{[hex(x) for x in args]}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at {where(first)})")
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    only = sys.argv[4:] or None
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x20000)
    m.write(SOUND_STUB, sound_stub())
    m.write(0x42C760, sound_stub([1], 8))  # scorch marks (arg 0 is a stack pointer) (presentation; needs track decal data)
    rng = random.Random(6)
    failed = 0
    for name, (address, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad, stats = 0, {}
        for _ in range(iterations):
            msg = run_case(m, name, address, returns, rng, stats)
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {stats}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
