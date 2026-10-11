#!/usr/bin/env python3
"""Differential test of the post-physics world updates in post_j.c (weather,
timed explosions, sprite effects, shadows, debris, tyre trails, animated
car textures) against the original, on random state.

Calls the C code shares with the original by address (sound, other groups'
functions) and the track lookups (track_cell, slot_track_sweep, both the
original and the C one) are stubbed with recorders, so the order and
arguments of those calls are compared too.

    tests/emu_post_j.py EXE build/emu_j.elf [iterations]
"""
import random
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x30000
CAR_SIZE = 0x2400
CARS = SCRATCH                      # 6 cars
WHEELS = SCRATCH + 0xE000           # 12 wheel bodies, 0x100 each
EMITTERS = SCRATCH + 0x10000        # 6 emitters, 0xd00 each
EMITTER_BODIES = SCRATCH + 0x15000  # 6 bodies, 0x200 each
SPRITES = SCRATCH + 0x16000         # 32 sprites, 0x100 each
DEBRIS = SCRATCH + 0x18000          # 64 pieces, 0x180 each
SECTIONS = SCRATCH + 0x1E000        # 256 pointers, then 256 entries of 8
TRACK = SCRATCH + 0x1F000
SURFACES = SCRATCH + 0x20000
CELL = SCRATCH + 0x21000
RACE = SCRATCH + 0x22000
OPTIONS = SCRATCH + 0x23000
TEXTURES = SCRATCH + 0x24000
TEX_BASE = SCRATCH + 0x26000
RECORD = SCRATCH + 0x26100
BUF = SCRATCH + 0x27000             # arguments: boxes, vectors, out buffers

SLOTS = 0x6BFB80
REGIONS = [
    (SCRATCH, SCRATCH_SIZE),
    (0x4B2C50, 0x18), (0x4D5898, 0x80), (0x5D6248, 0x4), (0x5D6260, 0xD8), (0x5D6338, 4), (0x5D6360, 0x1C),
    (0x6CCB5C, 4), (0x6CCB60, 8), (0x6CCB84, 4), (0x4BE128, 0xC), (0x4BE1A8, 4), (0x4B11D8, 4),
    (0x6D06C0, 4), (0x6D06E0, 0x100), (0x4D3E00, 0x18), (0x5D9288, 4), (0x5D9290, 4),
    (0x6CCBE0, 0x170), (SLOTS, 0x274 * 6), (0x5F7400, 0x1088 * 6), (0x6CF6E0, 0x150 * 8),
    (0x5FF4A8, 0x3000), (0x6C0A40, 4), (0x6C0A50, 4), (0x6CCE40, 1), (0x6CCE7C, 4),
]

# name: (original, returns?)
TESTS = {
    "weather_level_step": (0x486290, False),
    "car_view": (0x40D090, False),
    "weather_emitter_place": (0x484120, False),
    "weather_indoors": (0x4850F0, True),
    "weather_box": (0x4848C0, False),
    "weather_emitter_indoors": (0x4846C0, False),
    "weather_drops_spawn": (0x4846F0, False),
    "weather_emitter_update": (0x4842E0, False),
    "weather_thunder_clap": (0x484FD0, False),
    "weather_thunder": (0x484E20, False),
    "weather_lightning": (0x484EF0, False),
    "weather_update": (0x484080, False),
    "explosions_update": (0x405140, False),
    "flash_timer_step": (0x42C400, False),
    "sprite_move_smoke": (0x42CD80, False),
    "sprite_move_drag": (0x42CE70, False),
    "sprite_move_rising": (0x42CE50, False),
    "sprite_move_spray": (0x42CF40, False),
    "sprite_move_swirl": (0x42D000, False),
    "sprite_move_falling": (0x42D130, False),
    "sprite_move_basic": (0x42D400, False),
    "sprite_colour": (0x42D300, True),
    "sprite_grow": (0x42D560, False),
    "sprite_free": (0x42AD00, False),
    "sprites_move": (0x42D1E0, False),
    "sprites_update": (0x42B990, False),
    "shadow_probe": (0x46C520, False),
    "shadow_ground": (0x46C450, True),
    "shadow_corner": (0x46C750, True),
    "shadow_corners": (0x46C580, False),
    "shadows_update": (0x46C300, False),
    "debris_update": (0x47C210, False),
    "tyre_trails_update": (0x481F20, False),
    "car_texture_frames": (0x475430, False),
    "car_textures_update": (0x4751B0, False),
}


def u32(v):
    return v & 0xFFFFFFFF


def rbytes(rng, n):
    return rng.randbytes(n)


class Stubs:
    """Recording stand-ins for calls out of the code under test."""

    def __init__(self, m):
        self.m = m
        self.log = []
        self.sweep_rng = random.Random()
        self.cell = CELL

    def install(self, address, handler, nargs):
        def hook(uc, _address, _size, _data):
            esp = uc.reg_read(UC_X86_REG_ESP)
            words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
            eax = handler(words[1:])
            uc.reg_write(UC_X86_REG_EAX, u32(eax or 0))
            uc.reg_write(UC_X86_REG_ESP, esp + 4)
            uc.reg_write(UC_X86_REG_EIP, words[0])
        self.m.uc.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)

    def read_ints(self, address, n):
        return struct.unpack("<%di" % n, self.m.read(address, 4 * n))

    def setup(self):
        m = self.m
        self.install(0x471C90, lambda a: self.log.append(("sound", *a)), 5)
        self.install(0x4857F0, lambda a: self.log.append(("flakes", a[0])), 1)
        self.install(0x42AA30, lambda a: self.log.append(
            ("effect", self.read_ints(a[0], 3), self.read_ints(a[1], 3), *a[2:])), 5)
        self.install(0x4052B0, lambda a: self.log.append(("scenery", a[0])), 1)
        self.install(0x4612A0, lambda a: self.log.append(("grid_remove", a[0])), 1)

        def cell(a):
            self.log.append(("track_cell", *a[:3]))
            return self.cell
        for address in (0x419420, m.symbols["track_cell"]):
            self.install(address, cell, 4)

        def sweep(a):
            body, slot, out, limit = a
            # only the fields shadow_probe sets (the rest is stack garbage)
            record = self.m.read(slot, 0x48)
            self.log.append(("sweep", body, record[8:0x20], record[0x40:0x46], limit))
            r = self.sweep_rng
            hit = [r.randrange(-0x8000, 0x8000) for _ in range(3)] + \
                  [r.randrange(-0x10000, 0x10001) for _ in range(3)] + \
                  [r.getrandbits(32) - (1 << 31), r.choice([0, 0, 1, 2, 0x80000]), r.getrandbits(16), 0]
            self.m.write(out, struct.pack("<10I", *(u32(v) for v in hit)))
            return r.choice([0, 1, r.randrange(0, 0x10001), 0x1010000, 0xFFFFFFFF])
        for address in (0x415EA0, m.symbols["slot_track_sweep"]):
            self.install(address, sweep, 4)


def make_car(rng, i):
    car = bytearray(rbytes(rng, CAR_SIZE))
    for o in range(0xC4, 0xD6, 2):
        struct.pack_into("<h", car, o, rng.randrange(-0x1000, 0x1001))
    for o in (0x34, 0x38, 0x3C):
        struct.pack_into("<i", car, o, rng.randrange(-0x4000000, 0x4000000))
    for o in (0x14BC, 0x14C0, 0x14C4):
        struct.pack_into("<i", car, o, rng.randrange(-0x40000, 0x40000))
    for w in range(2):
        struct.pack_into("<I", car, 0x518 + w * 0xC4, WHEELS + (i * 2 + w) * 0x100)
        trail = 0x14CC + w * 0x6D4
        struct.pack_into("<ii", car, trail, rng.choice([rng.randrange(0, 12), rng.randrange(-2, 14)]),
                         rng.randrange(0, 14))
    struct.pack_into("<I", car, 0x6AC, rng.choice([0xFFFFFFFF, RECORD, RECORD + 1]))
    struct.pack_into("<i", car, 0x654, rng.choice([rng.randrange(0x300000, 0x2000000), rng.getrandbits(32) - (1 << 31)]))
    car[0x63F] = rng.choice([0, 1, 2])
    struct.pack_into("<i", car, 0x11C, rng.choice([rng.randrange(0, 256), -1, rng.getrandbits(32) - (1 << 31)]))
    return bytes(car)


def make_emitter(rng, i):
    e = bytearray(rbytes(rng, 0xD00))
    centre = [rng.randrange(-0x2000, 0x2000) for _ in range(3)]
    for k in range(3):
        struct.pack_into("<h", e, k * 2, centre[k] - rng.randrange(0x200, 0x500))
        struct.pack_into("<h", e, 8 + k * 2, centre[k] + rng.randrange(0x200, 0x500))
    struct.pack_into("<I", e, 0x10, EMITTER_BODIES + i * 0x200)
    for d in range(400):
        o = 0x14 + d * 8
        struct.pack_into("<hhhh", e, o, *[centre[k] + rng.randrange(-0x500, 0x500) for k in range(3)],
                         rng.choice([0, 1, 1, 1]))
    struct.pack_into("<I", e, 0xC94, EMITTERS + i * 0xD00 + 0x14 + rng.randrange(0, 400) * 8)
    struct.pack_into("<hhh", e, 0xC98, rng.randrange(-0x20, 0x20), rng.randrange(-0x40, 0x40),
                     rng.randrange(-0x20, 0x20))
    e[0xCA0] = rng.choice([0, 0, 1])
    e[0xCA1] = rng.choice([0x7F, rng.randrange(0, 16), rng.getrandbits(8)])
    e[0xCA2] = rng.choice([rng.randrange(0, 8), rng.getrandbits(8)])
    return bytes(e), centre


def make_body(rng, centre_from):
    b = bytearray(rbytes(rng, 0x200))
    cx, cy, cz = centre_from
    spread = rng.choice([0x1000000, 0x4000000, 0x400000])
    for k, c in enumerate((cx, cy, cz)):
        struct.pack_into("<i", b, 0x34 + k * 4, (c << 16) + rng.randrange(-spread, spread))
    for o in range(0xC4, 0xD6, 2):
        struct.pack_into("<h", b, o, rng.randrange(-0x1000, 0x1001))
    struct.pack_into("<i", b, 0x11C, rng.choice([rng.randrange(0, 256), -1, rng.randrange(0, 256)]))
    return bytes(b)


def make_sprite(rng):
    s = bytearray(rbytes(rng, 0x100))
    struct.pack_into("<I", s, 0, rng.choice([8, 0x17, 0x1B, 0x1C, 0x1E, 0x22, rng.randrange(0, 0x30), rng.getrandbits(32)]))
    s[0x94] = rng.choice([0, 1, 1, 1])
    struct.pack_into("<h", s, 0x96, rng.choice([1, rng.randrange(-4, 0x50), rng.randrange(0, 0x200),
                                                rng.getrandbits(16) - 0x8000]))
    struct.pack_into("<H", s, 0xA2, rng.choice([0x10, 1, 0x4000, 4, 8, rng.getrandbits(16), rng.randrange(0, 20)]))
    for o in (0xA8, 0xAC):
        struct.pack_into("<I", s, o, rng.choice([0, SPRITES + rng.randrange(0, 32) * 0x100]))
    return bytes(s)


def make_state(rng):
    w = []  # (address, bytes)
    count = rng.choice([rng.randrange(0, 7), 6, 4])
    race = bytearray(rbytes(rng, 0xB00))
    race[0x54D] = count
    race[0x553] = rng.choice([0, 1, 1, 2, rng.getrandbits(8)])
    w.append((RACE, bytes(race)))
    w.append((0x4B1960, struct.pack("<I", RACE)))
    options = bytearray(rbytes(rng, 0x40))
    options[0x30] = rng.choice([0, 1, 1])
    w.append((OPTIONS, bytes(options)))
    w.append((0x4B2198, struct.pack("<I", OPTIONS)))
    for i in range(6):
        w.append((CARS + i * CAR_SIZE, make_car(rng, i)))
    slots = bytearray(rbytes(rng, 0x274 * 6))
    for i in range(6):
        struct.pack_into("<I", slots, i * 0x274, CARS + i * CAR_SIZE)
    w.append((SLOTS, bytes(slots)))
    w.append((WHEELS, rbytes(rng, 0x1200)))
    for i in range(6):
        emitter, centre = make_emitter(rng, i)
        w.append((EMITTERS + i * 0xD00, emitter))
        w.append((EMITTER_BODIES + i * 0x200, make_body(rng, centre)))
    table = [EMITTERS + i * 0xD00 for i in range(6)] + [0xFFFFFFFF]
    stop = rng.choice([6, rng.randrange(0, 7)])
    table[stop] = 0xFFFFFFFF
    if rng.randrange(10) == 0:
        table[0] = 0
    w.append((0x5D6360, struct.pack("<7I", *table)))
    # track sections and surfaces
    w.append((SECTIONS, struct.pack("<256I", *[SECTIONS + 0x400 + i * 8 for i in range(256)])))
    w.append((SECTIONS + 0x400, rbytes(rng, 0x800)))
    w.append((TRACK, struct.pack("<I", SECTIONS)))
    w.append((0x6C1E30, struct.pack("<I", TRACK)))
    w.append((SURFACES, rbytes(rng, 0x600)))
    w.append((0x6BFB40, struct.pack("<I", SURFACES)))
    cell = bytearray(rbytes(rng, 0x10))
    struct.pack_into("<I", cell, 4, rng.randrange(0, 0x100) << 16 | rng.getrandbits(16) | rng.getrandbits(2) << 30)
    w.append((CELL, bytes(cell)))
    # cameras
    w.append((0x6CF6E0, rbytes(rng, 0x150 * 8)))
    w.append((0x5D6248, struct.pack("<i", rng.randrange(1, 9))))
    w.append((0x6CCE40, bytes([rng.getrandbits(8)])))
    w.append((0x6C0A40, struct.pack("<I", rng.randrange(0, 6))))
    w.append((0x6C0A50, struct.pack("<I", rng.getrandbits(32))))
    w.append((0x6CCE7C, struct.pack("<I", rng.choice([0, 1]))))
    # weather globals
    seed = rng.randrange(1, 0x7FFFFFFF)
    if rng.randrange(3) == 0:
        # one whose next value is small: thunder and lightning
        seed = rng.randrange(1, rng.choice([0x80, 0x400]) << 15) * pow(16807, -1, 1 << 31) % (1 << 31)
    w.append((0x4B2C50, struct.pack("<6I", seed, rng.randrange(0, 20), rng.randrange(0, 20),
                                    rng.getrandbits(32), rng.randrange(0, 20), rng.choice([0, 1]))))
    w.append((0x4D58E8, struct.pack("<2i", rng.getrandbits(31), rng.choice([rng.randrange(-2, 0x22), 0x1e, 0]))))
    w.append((0x4D58F0, struct.pack("<7I", rng.choice([0, 1]), rng.choice([0, 2]), rng.choice([0, 2]), 0,
                                    rng.choice([0, 0, 1, 5]), rng.choice([0, 0, 1, 3]),
                                    rng.choice([0, 0, rng.randrange(0, 0x15)]))))
    w.append((0x5D6338, struct.pack("<i", rng.choice([0, 1, 2]))))
    # explosions
    w.append((0x6D06C0, struct.pack("<I", rng.randrange(0, 9))))
    ex = bytearray(rbytes(rng, 0x100))
    for i in range(16):
        ex[i * 0x10 + 6] = rng.choice([0, 0, 0xFF])
        ex[i * 0x10 + 7] = rng.choice([0, 0, 1, 5])
        ex[i * 0x10 + 9] = rng.choice([0, 1])
    w.append((0x6D06E0, bytes(ex)))
    # sprites
    for i in range(32):
        w.append((SPRITES + i * 0x100, make_sprite(rng)))
    first, last = sorted(rng.sample(range(33), 2))
    w.append((0x6CCB5C, struct.pack("<I", SPRITES + first * 0x100)))
    w.append((0x6CCB84, struct.pack("<I", SPRITES + last * 0x100)))
    w.append((0x4BE128, struct.pack("<3I", *[rng.choice([0, SPRITES + rng.randrange(32) * 0x100]) for _ in range(3)])))
    w.append((0x4BE1A8, struct.pack("<i", rng.choice([0, rng.randrange(0, 0x16), rng.getrandbits(32) - (1 << 31)]))))
    w.append((0x4B11D8, struct.pack("<I", rng.randrange(1, 0x7FFFFFFF))))
    # debris
    debris = bytearray(rbytes(rng, 64 * 0x180))
    for i in range(64):
        debris[i * 0x180 + 0x2C] = rng.choice([0, 0, 1, rng.getrandbits(8)])
        struct.pack_into("<i", debris, i * 0x180 + 0x28, rng.choice([rng.randrange(-4, 0x40), rng.getrandbits(32) - (1 << 31)]))
    w.append((DEBRIS, bytes(debris)))
    w.append((0x5D9290, struct.pack("<I", DEBRIS)))
    # animated textures
    w.append((TEXTURES, rbytes(rng, 0x2000)))
    w.append((0x6C1E14, struct.pack("<I", TEXTURES)))
    tb = bytearray(rbytes(rng, 0x40))
    struct.pack_into("<H", tb, 0x2E, rng.randrange(0, 0x100))
    w.append((TEX_BASE, bytes(tb)))
    w.append((0x6C1EB0, struct.pack("<I", TEX_BASE)))
    w.append((RECORD, bytes([0, rng.choice([0, 1])])))
    frames = bytearray(rbytes(rng, 0x1088 * 6))
    for i in range(6):
        struct.pack_into("<i", frames, i * 0x1088 + 0x1080, rng.choice([rng.randrange(0, 9), rng.randrange(0, 12)]))
    w.append((0x5F7400, bytes(frames)))
    w.append((0x6CCBE0, rbytes(rng, 0x170)))
    w.append((0x5FF4A8, rbytes(rng, 0x3000)))
    return w


def box(rng, small=True):
    lo = [rng.randrange(-0x4000, 0x4000) for _ in range(3)]
    if small or rng.randrange(3):
        hi = [v + rng.choice([rng.randrange(0, 0x800), rng.randrange(-0x10, 0x1000), rng.randrange(0, 0x40)]) for v in lo]
    else:
        hi = [rng.randrange(-0x8000, 0x8000) for _ in range(3)]
    return struct.pack("<3h", *lo), struct.pack("<3h", *[((v + 0x8000) & 0xFFFF) - 0x8000 for v in hi])


def args_for(name, rng, extra):
    """Arguments, plus extra (address, bytes) writes."""
    car = CARS + rng.randrange(6) * CAR_SIZE
    sprite = SPRITES + rng.randrange(32) * 0x100
    life = rng.choice([1, rng.randrange(-4, 0x80), rng.getrandbits(16) - 0x8000, rng.getrandbits(32)])
    emitter = EMITTERS + rng.randrange(6) * 0xD00
    if name == "car_view":
        return [SLOTS + rng.randrange(6) * 0x274, BUF]
    if name == "weather_emitter_place":
        return [emitter, rng.choice([0, 1]), rng.randrange(0, 9)]
    if name == "weather_indoors":
        return [rng.choice([0, EMITTER_BODIES + rng.randrange(6) * 0x200])]
    if name == "weather_box":
        return [BUF, BUF + 8, emitter]
    if name == "weather_emitter_indoors":
        return [rng.choice([0, 1]), emitter]
    if name == "weather_drops_spawn":
        lo, hi = box(rng, small=False)
        extra += [(BUF, lo), (BUF + 8, hi)]
        return [BUF, BUF + 8, emitter]
    if name in ("weather_emitter_update",):
        return [emitter]
    if name == "weather_lightning":
        return [rng.choice([0, 1])]
    if name in ("sprite_move_smoke", "sprite_move_rising", "sprite_move_spray"):
        return [sprite, life]
    if name == "sprite_move_drag":
        return [sprite, life, rng.choice([0, 1])]
    if name in ("sprite_move_swirl", "sprite_move_falling", "sprite_free"):
        return [sprite]
    if name == "sprite_move_basic":
        return [sprite, rng.choice([0x10, 1, 0x4000, 4, 8, rng.randrange(0, 20)])]
    if name == "sprite_colour":
        return [rng.choice([0x10, 1, 0x4000, 4, 8, 9, rng.randrange(0, 20), rng.getrandbits(16)]),
                rng.choice([rng.randrange(0, 0x40), rng.randrange(0, 0x100), rng.getrandbits(16)])]
    if name == "sprite_grow":
        return [sprite, rng.choice([1, 4, 0, rng.randrange(0, 10)]), life]
    if name == "shadow_probe":
        return [car, BUF]
    if name == "shadow_ground":
        hit = [rng.randrange(-0x8000, 0x8000) for _ in range(6)] + [rng.choice([0, 1, 2]), 0, 0, 0]
        extra.append((BUF + 0x40, struct.pack("<10i", *hit)))
        return [car, BUF, BUF + 0x20, rng.choice([0, 1, -1, rng.randrange(0, 0x20000)]), BUF + 0x40]
    if name == "shadow_corner":
        vecs = [rng.randrange(-0x380000, 0x380000) for _ in range(6)]
        n = [rng.randrange(-0x10000, 0x10001) for _ in range(3)]
        extra.append((BUF, struct.pack("<6i", *vecs)))
        extra.append((BUF + 0x40, struct.pack("<3i", *n)))
        return [car, BUF, BUF + 12, BUF + 0x20, BUF + 0x40, rng.randrange(-0x400000, 0x400000), rng.getrandbits(8)]
    if name == "shadow_corners":
        n = [rng.randrange(-0x10000, 0x10001) for _ in range(3)]
        if rng.randrange(3):
            n[1] = rng.choice([1, -1]) * rng.randrange(0x1000, 0x10001)
        off = [rng.randrange(-0x200000, 0x200000) for _ in range(3)]
        extra.append((BUF, struct.pack("<3i", *n)))
        extra.append((BUF + 0x20, struct.pack("<3i", *off)))
        return [car, BUF, BUF + 0x20]
    if name == "car_texture_frames":
        return [rng.randrange(6)]
    return []


def run_case(m, stubs, name, address, returns, rng):
    state = make_state(rng)
    extra = []
    args = args_for(name, rng, extra)
    seed = rng.getrandbits(32)
    results = []
    for target in (address, m.symbols[name]):
        for a, b in state + extra:
            m.write(a, b)
        stubs.log = []
        stubs.sweep_rng.seed(seed)
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = b"".join(m.read(a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            off = first
            for base, n in REGIONS:
                if off < n:
                    where = f" (memory differs at {base + off:#x})"
                    break
                off -= n
        logs = "" if results[0][2] == results[1][2] else f" calls differ: {results[0][2][:4]} / {results[1][2][:4]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}{where}{logs}"
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    only = sys.argv[4:]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    stubs.setup()
    rng = random.Random(10)
    failed = 0
    for name, (address, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, returns, rng)
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
