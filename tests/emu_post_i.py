#!/usr/bin/env python3
"""Differential test of post_i.c (falling rock, engine glow, shrapnel)
against the original, on random records. Calls out to other code (sound,
shakes, the grid, track_cell, ...) are stubbed and logged, so both sides
must make the same calls with the same arguments.

    tests/emu_post_i.py EXE build/emu_i.elf [iterations]
"""
import random
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x20000
CAR_SIZE = 0x1800
CARS = [SCRATCH + i * CAR_SIZE for i in range(6)]
o = SCRATCH + 6 * CAR_SIZE
RACE, BODY, RING, CELL, CELL_INFO, SURFACES, SURFACE, PIECES, FREE, LIVE, VERTS, MODEL, OUT = (
    o + x for x in (0, 0x800, 0xA00, 0x1200, 0x1220, 0x1280, 0x1480, 0x1500, 0x1D00, 0x1D40, 0x1D80, 0x2580, 0x25C0))
OBJS, SETTINGS, SURFLIST, CAMERA, VEC, VEC2, VEC3, PTS, OUT2 = (
    o + x for x in (0x2600, 0x2E00, 0x2F00, 0x2F40, 0x2F80, 0x2F90, 0x2FA0, 0x3000, 0x3100))
RING_RECORDS = 6
STATE_SIZE = OUT2 + 0x40 - SCRATCH
VIEWS, SPOTS = 0x6CF6A0, 0x6CFBE0

GLOBAL_RANGES = [(0x6B9D40, 0x30), (0x6B9838, 0x18), (0x6CCB50, 0x40), (0x6CCC30, 0xA8), (0x4B11D8, 0xC),
                 (0x4B2430, 4), (0x4D074C, 4), (0x4D4B98, 4), (0x6C0A50, 4), (0x4B1C8C, 4), (0x4B1960, 4),
                 (0x6BFAC0, 4), (0x6BFB40, 4), (0x6BFB80, 0x274 * 6), (0x6CF660, 0x10),
                 (VIEWS, 0x540 + 0x38 * 12), (0x4B6278, 0x300), (0x6C0A40, 0x10), (0x4B65F4, 4), (0x6BFACC, 4),
                 (0x6B9CC0, 0x80), (0x5D1FC0, 4), (0x4B2198, 4)]

# address: (argument count, how to log) -- 'v' value, 'p3' pointer to 3 ints, 'p1' pointer to 1 int
STUBS = {
    0x42AA30: "p3 p3 v v v",       # shake
    0x458320: "v",                 # rock lands
    0x419150: "v v v v",
    0x471E60: "v v v v v v v v",   # looping sound
    0x441D00: "v v v v v p1",      # glow light
    0x4612A0: "v",                 # piece off the grid
    0x465890: "v",
    0x460FB0: "v",
    0x419420: "v v v x",           # track_cell
    0x4687F0: "v v v v v x",       # body probe
    0x423F00: "v",                 # field of view (x87)
    0x40AFB0: "v",
    0x4611D0: "v",                 # grid_unlink
}

# name: (original, argument kinds)
TESTS = {
    "hsv_to_rgb": (0x467CF0, "hsv"),
    "gte_depth_cue": (0x427590, ""),
    "piece_move": (0x4629B0, "P"),
    "piece_free": (0x462490, "ii"),
    "pieces_update": (0x462840, ""),
    "glow_levels": (0x47BAE0, "CG"),
    "glow_vertices": (0x47BBD0, "Gll"),
    "car_glow": (0x47B9A0, "C"),
    "cars_glow": (0x47B8F0, ""),
    "rock_drop": (0x458180, "A"),
    "rock_update": (0x458010, ""),
    "view_count": (0x40D040, ""),
    "direction_fit": (0x407E90, "V"),
    "cell_has_surface": (0x407F30, "uV"),
    "body_side_axis": (0x4081F0, "oCb"),
    "car_is_upside_down": (0x408240, "C"),
    "surface_at": (0x40A6E0, "V"),
    "camera_spot_nearest": (0x40A570, "Vkor"),
    "camera_probe": (0x40A8F0, "CC"),
    "camera_look_or_keep": (0x40A940, "VWon"),
    "camera_choose_shot": (0x40A9B0, "Vn"),
    "camera_zoom_for": (0x40ADD0, "VWdL"),
    "camera_speed_zoom": (0x40B000, "SV"),
    "point_line_distance": (0x40B180, "VWXo"),
    "nearest_car": (0x40B4F0, "Vo"),
    "camera_find_trackside": (0x40B310, "oOn"),
    "factorials_init": (0x40C940, ""),
    "fix_pow": (0x40C970, "Bp"),
    "bezier_point": (0x40C9E0, "QNoT"),
    "points_scale_for_mode": (0x40CAD0, "Qc"),
    "camera_check_events": (0x407FA0, "n"),
}
RETURNS = {"view_count", "direction_fit", "cell_has_surface", "car_is_upside_down", "surface_at", "camera_probe",
           "camera_zoom_for", "point_line_distance", "camera_find_trackside", "fix_pow"}


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.off_track = 0
        self.sound = 0
        self.probe = 0
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self.hit, begin=address, end=address)

    def u32(self, address):
        return struct.unpack("<I", self.m.read(address, 4))[0]

    def hit(self, uc, address, size, _):
        esp = uc.reg_read(UC_X86_REG_ESP)
        args = []
        for i, kind in enumerate(STUBS[address].split()):
            a = self.u32(esp + 4 + i * 4)
            if kind == "p3":
                args.append(struct.unpack("<3i", self.m.read(a, 12)))
            elif kind == "p1":
                args.append(self.u32(a))
            elif kind == "v":
                args.append(a)
            else:
                args.append(None)
        self.log.append((address, tuple(args)))
        eax = 0
        if address == 0x419420:
            if self.off_track is not None:
                self.m.write(self.u32(esp + 16), struct.pack("<i", self.off_track))
            eax = CELL
        elif address == 0x471E60:
            eax = self.sound
        elif address == 0x4687F0:
            eax = self.probe
        uc.reg_write(UC_X86_REG_EAX, eax)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)


def small_or(rng, values, bits=32):
    return rng.choice(values) if rng.randrange(4) else rng.getrandbits(bits)


def seed_for(value):
    """The seed whose next value (seed * 16807 mod 2^31) is `value`."""
    return value * pow(16807, -1, 1 << 31) & 0x7FFFFFFF


def near_point(rng):
    return [rng.choice([0, 0x1000000, -0x1000000]) + rng.randrange(-0x3000000, 0x3000000) for _ in range(3)]


def make_state(rng, name=""):
    mem = bytearray(rng.getrandbits(8) for _ in range(STATE_SIZE))
    count = rng.randrange(1 if name == "camera_choose_shot" else 0, 7)
    for car in CARS:
        c = car - SCRATCH
        g = c + 0x1108
        struct.pack_into("<I", mem, g + 4, MODEL)
        mem[g + 0x2DC] = rng.choice([0, 1, 2, 3, 4, rng.getrandbits(8)])
        mem[g + 0x2F5] = rng.choice([0, 1, 0x23, 0x24, rng.getrandbits(8)])
        mem[g + 0x2DE] = rng.choice([0, 1, 7, 8, 10, rng.getrandbits(8)])
        struct.pack_into("<h", mem, g + 0x2E8, rng.choice([0, 4, 0x80, 0xFF, rng.randrange(-0x8000, 0x8000)]))
        struct.pack_into("<h", mem, g + 0x2EA, rng.choice([0, 4, 0x7F, 0x80, 0xFF, rng.randrange(-0x8000, 0x8000)]))
        struct.pack_into("<h", mem, g + 0x2EC, rng.choice([0, 60, 359, 360, rng.randrange(0, 361)]))
        struct.pack_into("<h", mem, g + 0x2EE, rng.choice([0, 120, 360, rng.randrange(0, 361)]))
        struct.pack_into("<h", mem, c + 0x690, rng.choice([0, 0, 1]))
        struct.pack_into("<h", mem, c + 0x692, rng.choice([0, 0xE, 0xF, rng.randrange(-0x8000, 0x8000)]))
        for off in (0x5C8, 0x504):
            struct.pack_into("<i", mem, c + off, rng.choice([0, rng.randrange(0, 0x400000), rng.getrandbits(31)]))
        struct.pack_into("<3i", mem, c + 0x34, *near_point(rng))
        v = rng.choice([0, 0x10000, 0x30000, 0x80000, 0x100000])
        struct.pack_into("<3i", mem, c + 0x68, *[rng.randrange(-v, v + 1) for _ in range(3)])
        mem[c + 0x70C] = rng.choice([8, 10, 10])
        mem[c + 0x63F] = rng.choice([0, 1, 1, 2])
        struct.pack_into("<h", mem, c + 0xCC, rng.choice([0, 0x1000, -0x1000, rng.randrange(-0x8000, 0x8000)]))
    struct.pack_into("<I", mem, MODEL - SCRATCH + 0x24, VERTS)
    r = RACE - SCRATCH
    mem[r + 0x54D] = count
    mem[r + 0x554] = rng.choice([0, 1, 1, 1])
    mem[r + 0x552] = rng.choice([0, 0, 1, 2, 3, 4, rng.getrandbits(8)])
    mem[r + 0x58C] = rng.choice([0, 1, 2])
    mem[r + 0x579] = rng.choice([0, 1, 5, 16, 17, rng.getrandbits(8)])
    # director: objects, settings, surface lists, camera, points
    for i in range(4):
        b = OBJS - SCRATCH + i * 0x200
        struct.pack_into("<I", mem, b + 0x184, rng.choice([0, 1, 2]))
        struct.pack_into("<I", mem, b + 0x188, rng.getrandbits(32) & ~(0 if rng.randrange(4) == 0 else 1))
        struct.pack_into("<I", mem, b + 0x194, rng.getrandbits(32) & ~(0 if rng.randrange(4) == 0 else 1))
        if rng.randrange(3) == 0:
            struct.pack_into("<I", mem, b + 0x188, 0)
    mem[SETTINGS - SCRATCH + 0x95] = rng.choice([0, 1])
    entries = rng.randrange(1, 9)
    for j in range(entries):
        struct.pack_into("<HH", mem, SURFLIST - SCRATCH + j * 4, rng.getrandbits(11),
                         rng.choice([rng.randrange(0, 3), rng.randrange(0, 3), rng.getrandbits(7)]) << 1 | (j == entries - 1) |
                         rng.getrandbits(8) << 8)
    struct.pack_into("<HH", mem, CELL - SCRATCH + 4, rng.randrange(0, entries) * 2, rng.randrange(0, 8))
    struct.pack_into("<II", mem, CAMERA - SCRATCH, OBJS, BODY)
    for vec in (VEC, VEC2, VEC3):
        if rng.randrange(3):
            struct.pack_into("<3i", mem, vec - SCRATCH, *near_point(rng))
        elif rng.randrange(2):
            struct.pack_into("<3i", mem, vec - SCRATCH, *[rng.randrange(-0x400, 0x400) for _ in range(3)])
    struct.pack_into("<I", mem, BODY - SCRATCH + 0x34, rng.getrandbits(32))
    for k in range(8):  # surfaces of the cells, mostly ones the lists name
        mem[CELL_INFO - SCRATCH + 2 + k * 6] = rng.choice([rng.randrange(0, 3), rng.randrange(0, 3), rng.getrandbits(8)])
    if rng.randrange(2):  # a point on the line between VEC2 and VEC3
        a, b = struct.unpack_from("<3i", mem, VEC2 - SCRATCH), struct.unpack_from("<3i", mem, VEC3 - SCRATCH)
        t = rng.choice([rng.randrange(0, 0x4000), rng.randrange(-0x1000, 0x22000)])
        point = [(x + ((y - x) * t >> 16) + rng.randrange(-0x40000, 0x40000)) for x, y in zip(a, b)]
        struct.pack_into("<3i", mem, VEC - SCRATCH, *[((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000 for v in point])
    struct.pack_into("<I", mem, r + 0x57C, rng.choice([4, 4, 0, 6, rng.getrandbits(32)]))
    # rock body, track lookups
    for i in range(128):
        struct.pack_into("<I", mem, SURFACES - SCRATCH + i * 4, SURFACE + (i % 8) * 8)
    # shrapnel
    live = rng.randrange(0, 17)
    pieces = list(range(16))
    rng.shuffle(pieces)
    for i in range(16):
        struct.pack_into("<H", mem, LIVE - SCRATCH + i * 2, pieces[i])
        p = PIECES - SCRATCH + i * 0x80
        struct.pack_into("<H", mem, p + 8, rng.choice([0, 1, 2, 3, 4, 5, 0xB, 0xC, rng.getrandbits(16)]))
        mem[p + 0x6C] = rng.choice([0, 1, rng.getrandbits(8)])
    globals_ = {
        0x6B9D40: bytes(rng.getrandbits(8) for _ in range(0x30)),  # first, so the values below land on it
        0x6B9D44: rng.choice([0, 0, 0, 1, 0xFFFFFFFF]) if rng.randrange(8) else 0,
        0x6B9D48: rng.choice([0, 100, 1000, rng.getrandbits(32)]),
        0x6B9D50: BODY, 0x6B9D54: rng.choice([0, 0, 1, 5, 200, 201, rng.getrandbits(32)]),
        0x6B9D58: rng.getrandbits(32),
        0x6B9D5C: BODY, 0x6B9D60: rng.choice([0, 1]),
        0x6B983C: PIECES, 0x6B9840: FREE, 0x6B9844: LIVE, 0x6B9848: live,
        0x6B984C: rng.randrange(-1, 15 - live) if live < 16 else -1,
        0x6CCB5C: RING, 0x6CCB84: RING + RING_RECORDS * 0x100,
        0x6CCB88: RING + rng.randrange(0, RING_RECORDS + 1) * 0x100,
        0x4B11DC: seed_for(rng.randrange(0, 820 << 15)) if rng.randrange(3) == 0 else rng.getrandbits(31),
        0x4B2430: rng.getrandbits(31), 0x4B11E0: rng.getrandbits(32),
        0x4D074C: rng.choice([0, 1]), 0x4D4B98: rng.choice([0, 0, 1]),
        0x6C0A50: rng.choice([0, 1000, 2000, rng.getrandbits(32)]),
        0x4B1C8C: rng.choice([0, 1, 0x80, 0x100, rng.getrandbits(32)]),
        0x4B1960: RACE, 0x6BFAC0: SURFACES, 0x6BFB40: CELL_INFO,
        0x4B11D8: rng.getrandbits(31), 0x4B2198: SETTINGS, 0x6BFACC: SURFLIST,
        0x4B65F4: rng.choice([0, 1, 2, 5, 12, 12, rng.randrange(0, 13)]),
        0x5D1FC0: OBJS + 0x600,
    }
    for i in range(24):
        globals_[0x6B9CE0 + i * 4] = OBJS + (i % 4) * 0x200
    for i in range(6):
        globals_[0x6B9CC0 + i * 4] = OBJS + rng.randrange(0, 4) * 0x200
    for i in range(4):
        globals_[0x6C0A40 + i * 4] = rng.randrange(0, max(count, 1))
    factorial = [1]
    for i in range(1, 13):
        factorial.append(factorial[-1] * i)
    for i, f in enumerate(factorial):
        globals_[0x4B6388 + i * 4] = f if rng.randrange(20) else rng.getrandbits(32)
    ticks = globals_[0x6C0A50]
    views = bytearray(rng.getrandbits(8) for _ in range(0x540))
    for i in range(4):
        v = i * 0x150
        struct.pack_into("<I", views, v, rng.choice([0, 1, 2, 3, 4, 5, 10, 11, 14, 15, 16, rng.randrange(0, 17)]))
        mode = rng.choice([0, 0, 0, 1, 2, 3, 4])
        struct.pack_into("<i", views, v + 0x3C, mode)
        struct.pack_into("<h", views, v + 0x38, 0 if mode == 4 else rng.randrange(0, 6))
        struct.pack_into("<h", views, v + 0x32, rng.choice([0, 0x3B, 0x3C, 100, rng.randrange(-0x8000, 0x8000)]))
        struct.pack_into("<h", views, v + 0x3A, rng.choice([-1, 0, 0, 1, rng.randrange(-0x8000, 0x8000)]))
        for off in (0x118, 0x11C):
            struct.pack_into("<I", views, v + off, ticks - rng.choice([0, 500, 700, 3000, rng.getrandbits(16)]) &
                             0xFFFFFFFF)
        struct.pack_into("<i", views, v + 0x144, rng.choice([-1, -1, rng.randrange(0, 6)]))
    spots = bytearray(rng.getrandbits(8) for _ in range(0x38 * 12))
    for i in range(12):
        struct.pack_into("<i3i3i", spots, i * 0x38, rng.choice([0, 1, 1, 2]), *near_point(rng), *near_point(rng))
    if name == "camera_find_trackside" and rng.randrange(3) and count:
        # a fast car just past the start of a kind 1 spot's line
        for i in range(4):
            struct.pack_into("<I", views, i * 0x150, rng.choice([4, 4, 1, 15]))
            struct.pack_into("<h", views, i * 0x150 + 0x32, 100)
            struct.pack_into("<h", views, i * 0x150 + 0x3A, rng.choice([0, 0, 1]))
            struct.pack_into("<I", views, i * 0x150 + 0x11C, ticks - 1000 & 0xFFFFFFFF)
        car = rng.randrange(0, count)
        c = CARS[car] - SCRATCH
        spot = rng.randrange(0, 4)
        start = near_point(rng)
        direction = [rng.randrange(-0x4000000, 0x4000000) for _ in range(3)]
        t = rng.randrange(0x400, 0x4800)
        struct.pack_into("<i3i3i", spots, spot * 0x38, 1, *start, *[a + d for a, d in zip(start, direction)])
        struct.pack_into("<3i", mem, c + 0x34, *[a + (d * t >> 16) + rng.randrange(-0x80000, 0x80000)
                                                 for a, d in zip(start, direction)])
        struct.pack_into("<3i", mem, c + 0x68, *[rng.choice([0x80000, -0x80000, 0x100000]) for _ in range(3)])
        globals_[0x4B65F4] = max(globals_[0x4B65F4], spot + 1)
    globals_[VIEWS] = bytes(views)
    globals_[SPOTS] = bytes(spots)
    gte = bytes(rng.getrandbits(8) for _ in range(0xA8))
    slots = bytearray(rng.getrandbits(8) for _ in range(0x274 * 6))
    for i in range(6):
        struct.pack_into("<I", slots, i * 0x274, CARS[i])
        struct.pack_into("<I", slots, i * 0x274 + 0x28, CAMERA)
        slots[i * 0x274 + 0x2D] = rng.choice([0, 1, 5, 6, 7, rng.getrandbits(8)])
    stub = (rng.choice([0, 0, 0, 1, None]), rng.getrandbits(8), rng.choice([0, 1]))
    return bytes(mem), globals_, gte, bytes(slots), stub


def make_args(rng, kinds):
    args = []
    for k in kinds:
        if k == "h":
            args.append(small_or(rng, [0, 59, 60, 61, 119, 180, 300, 359, 360, rng.randrange(0, 361)]))
        elif k == "s":
            args.append(small_or(rng, [0, 0, 1, 0x200, 0x3FF, 0x400, rng.randrange(0, 0x401)]))
        elif k == "v":
            args.append(small_or(rng, [0, 0x3FF, rng.randrange(0, 0x400)]))
        elif k == "C":
            args.append(rng.choice(CARS))
        elif k == "G":
            args.append(rng.choice(CARS) + 0x1108)
        elif k == "P":
            args.append(PIECES + rng.randrange(0, 16) * 0x80)
        elif k == "i":
            args.append(rng.randrange(0, 16))
        elif k == "l":
            args.append(small_or(rng, [0, 0x7F00, 0x10000, rng.randrange(-0x20000, 0x20000), rng.randrange(-20, 20)]))
        elif k == "A":
            args.append(rng.choice(CARS) + 0x34)
        elif k in "VWX":
            args.append({"V": VEC, "W": VEC2, "X": VEC3}[k])
        elif k == "o":
            args.append(OUT2)
        elif k == "O":
            args.append(OUT2 + 0x20)
        elif k == "n":
            args.append(rng.randrange(0, 4))
        elif k == "k":
            args.append(rng.choice([-1, -1, 0, 1, 2, rng.getrandbits(32)]))
        elif k == "r":
            args.append(rng.choice([0, 0x100, 0x400, rng.getrandbits(16), rng.getrandbits(32)]))
        elif k == "u":
            args.append(rng.choice([rng.randrange(0, 3), rng.randrange(0, 8), rng.getrandbits(7), rng.getrandbits(32)]))
        elif k == "b":
            args.append(rng.choice([0, 1, rng.getrandbits(32)]))
        elif k == "S":
            args.append(0x6BFB80 + rng.randrange(0, 6) * 0x274)
        elif k == "d":
            args.append(rng.choice([1, 2, 3, 0x10, 0x100, rng.getrandbits(32) | 1]))
        elif k == "L":
            args.append(rng.choice([0, 1, 4, 0x10, rng.getrandbits(32)]))
        elif k == "N":
            args.append(rng.choice([0, 1, 2, 3, 4, 13, rng.randrange(-2, 14)]))
        elif k == "T":
            args.append(rng.choice([0, 0x8000, 0x10000, rng.randrange(0, 0x10001), rng.getrandbits(32)]))
        elif k == "B":
            args.append(rng.choice([0x10000, 0x8000, rng.randrange(0, 0x20000), rng.getrandbits(32)]))
        elif k == "p":
            args.append(rng.choice([0, 1, 2, 3, 5, 12, 31, rng.getrandbits(32)]))
        elif k == "Q":
            args.append(PTS)
        elif k == "c":
            args.append(rng.choice([0, 1, 2, 13, 20, rng.randrange(-2, 30)]))
    if kinds == "hsv":
        args += [OUT, OUT + 4, OUT + 8]
    return args


def run_case(m, stubs, name, address, kinds, rng):
    mem, globals_, gte, slots, stub = make_state(rng, name)
    args = make_args(rng, kinds)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, mem)
        m.write(0x6CCC30, gte)
        m.write(0x6BFB80, slots)
        for a, v in globals_.items():
            m.write(a, v if isinstance(v, bytes) else struct.pack("<I", v & 0xFFFFFFFF))
        stubs.log = []
        stubs.off_track, stubs.sound, stubs.probe = stub
        r = m.call(target, args)
        if r[0] == "ok" and name not in RETURNS:
            r = ("ok",)
        state = m.read(SCRATCH, STATE_SIZE) + b"".join(m.read(a, n) for a, n in GLOBAL_RANGES)
        results.append((r, state, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            where = f" (memory differs at scratch+{first:#x})" if first < STATE_SIZE else \
                f" (globals differ at byte {first - STATE_SIZE:#x})"
        logs = "" if results[0][2] == results[1][2] else f"\n  calls {results[0][2][:6]}\n   ours {results[1][2][:6]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}{where}{logs}"
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(9)
    failed = 0
    for name, (address, kinds) in TESTS.items():
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, rng)
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
