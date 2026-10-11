#!/usr/bin/env python3
"""Differential test of pre_q.c (0x41ac50: water ripples and the special
track objects, and the effect spawners the blast dispatcher uses) against
the original, on random track worlds.

Functions left original (sound, grid / sprite lists, particles, blasts,
track cells) are stubbed in both runs: each stub logs its arguments and
returns a scripted value, and the call logs are compared too. The stack
is filled with random bytes so uninitialised reads show up.

    tests/emu_pre_q.py EXE build/emu_q.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x100000
# writable areas (compared)
OBJECTS = SCRATCH  # 8 objects, 0x200 each
WATERS = SCRATCH + 0x1000  # 4 records, 0x1c each
WATER_BUFS = SCRATCH + 0x1100  # 4 x (p 0x400, q 0x400)
POLYS = SCRATCH + 0x3100  # 2 lists x 256 polys x 0x84
PIECES = SCRATCH + 0x13900  # 64 x 0x80
FREE, USED = SCRATCH + 0x15900, SCRATCH + 0x15A00  # u16 stacks
SMOKE = SCRATCH + 0x15C00  # 5 x 0x1e2c
SHOCK = SCRATCH + 0x1F400  # 3 x 0xf14
PARTICLES = SCRATCH + 0x22000  # 0x100 x 0xc0
VERTS = SCRATCH + 0x2E000  # 8 sections x 0x800
OUT = SCRATCH + 0x32000  # 0x40
VEC = SCRATCH + 0x32100  # 3 vectors of 0x10
WRITABLE = (SCRATCH, 0x32200)
# read-only areas
MODEL = SCRATCH + 0x40000
SPECIALS = SCRATCH + 0x40100  # pointer array
CAMERA, BODY = SCRATCH + 0x40200, SCRATCH + 0x40300
SECTIONS = SCRATCH + 0x40400  # 8 x 0x40
SECTION_PTRS = SCRATCH + 0x40800  # 256 pointers
PALETTE = SCRATCH + 0x40C00  # 0x400 x 4
CELL = SCRATCH + 0x41C00
CELL_RECS = SCRATCH + 0x42000  # 0x4000 x 6 (random head only)
STRIPS = SCRATCH + 0x5A000  # 0x22000 (random head only)
RACE = SCRATCH + 0x80000

SLOTS = (0x6BFB80, 0x274 * 6)
POINTERS = {0x4B1960: RACE, 0x6CE708: MODEL, 0x6CE738: SPECIALS, 0x5D6378: WATERS, 0x6B983C: PIECES,
            0x6B9840: FREE, 0x6B9844: USED, 0x6CCB54: SMOKE, 0x6CC800: SHOCK, 0x6BFAC0: SECTION_PTRS,
            0x6C1E20: PALETTE, 0x6BFB40: CELL_RECS, 0x6BFACC: STRIPS}
# (address, size): random each case, compared after
GLOBALS = [(0x4B11D8, 8), (0x4B2C64, 4), (0x4BE180, 0x30), (0x5D637C, 4), (0x5D71E0, 0xE08), (0x6B9848, 8),
           (0x6C0A50, 4), (0x6CCA50, 0x2B0), (0x6CE6FC, 4), (0x6CE700, 0x34), (0x6CE734, 4), (0x4AC724, 4),
           (0x6CCE40, 1)]

# address: (name, number of args, args not compared, returns)
STUBS = {
    0x4611D0: ("grid_unlink", 1, (), False),
    0x460FF0: ("sprite_place", 4, (), False),
    0x471C90: ("sound_play", 5, (), True),
    0x407F30: ("surface_near", 2, (), True),
    0x462690: ("spawn_particle", 3, (), False),
    0x42AC30: ("take_particle", 0, (), True),
    0x42D300: ("particle_colour", 2, (), True),
    0x42AA30: ("blast_effects", 5, (), False),
    0x419420: ("cell_at", 4, (3,), True),
}

# branches of the originals counted (original runs only), to show the
# random worlds reach them
PROBES = {
    0x482E6B: "ripple a water", 0x482F5F: "ripple colour",
    0x41B036: "screen 1", 0x41B043: "screen 2", 0x41B117: "screen sound", 0x41B131: "screen 4",
    0x41B163: "screen 5 ends", 0x41B183: "screen 6", 0x41B239: "screen 7 ramp", 0x41B27F: "screen 7 dark",
    0x41B299: "screen wraps", 0x41B2A6: "screen off",
    0x41ADBA: "wreck gone", 0x41ADDC: "wreck done", 0x41AE8D: "wreck blast", 0x41AEB7: "wreck shrink",
    0x43E699: "spray starts", 0x43E6B5: "spray one", 0x43E74A: "spray full", 0x43E71B: "spray fades",
    0x43E6F9: "spray stops", 0x43E7AB: "spray particles",
    0x441203: "row added", 0x441D52: "light extra",
    0x42B420: "smoke big", 0x42B48B: "smoke white", 0x42B4A1: "smoke grey", 0x42C382: "shock started",
    0x4624FA: "piece thrown", 0x46245A: "no piece",
    0x42AF70: "burst aimed", 0x42B052: "burst 0x10", 0x42B0F1: "burst 1", 0x42B1F2: "burst 0x4000",
    0x42B2AC: "burst numbers wrap",
    0x440CB4: "cell single", 0x440D2C: "strip short", 0x440D80: "strip long", 0x440E0C: "level 10",
    0x440E4F: "level 10 clamp", 0x440EEC: "narrow odd", 0x440F13: "widen odd", 0x440F53: "narrow odd low",
    0x440F7A: "widen odd low", 0x440FA0: "clamp a", 0x440FAC: "a below 0",
    0x42C836: "scorch last row", 0x42C8D1: "scorch near", 0x42C908: "scorch darkens",
}

TESTS = {
    "track_animation_update": (0x41AC50, "", False),
    "track_specials_update": (0x41AD50, "", False),
    "water_ripples_update": (0x482DF0, "", False),
    "screen_update": (0x41B000, "O", False),
    "wreck_burn_update": (0x41ADA0, "O", False),
    "spray_update": (0x43E640, "O", False),
    "mesh_rows_touch": (0x4411C0, "RS", False),
    "light_add": (0x441D00, "VViiiX", False),
    "light_flash_small": (0x42B2D0, "Vi", False),
    "light_flash": (0x42B300, "Vi", False),
    "blast_flash_set": (0x42B330, "Vi", False),
    "blast_glow_set": (0x42B370, "Vi", False),
    "smoke_cloud_start": (0x42B3B0, "Vb", False),
    "shockwave_start": (0x42C360, "V", False),
    "blast_point_set": (0x42C3C0, "V", False),
    "piece_take": (0x462450, "", True),
    "pieces_spawn": (0x4624D0, "VVs", False),
    "particles_burst": (0x42AD60, "VVzik", False),
    "gte_square_ir": (0x4282D0, "", False),
    "cell_row_ranges": (0x440C60, "COl", False),
    "scorch_track": (0x42C760, "Vl", False),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return rng.randbytes(n)


def s32(v):
    return struct.pack("<i", ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000)


def make_object(rng):
    o = bytearray(rbytes(rng, 0x200))
    kind = pick(rng, 0x14, 0x1F, 0x20, 0x14 | 0x200, rng.getrandbits(9))
    struct.pack_into("<I", o, 0, (rng.getrandbits(23) << 9) | kind)
    for i in range(3):
        struct.pack_into("<i", o, 0x34 + 4 * i, rng.randrange(-0x4000000, 0x4000000))
    struct.pack_into("<i", o, 0x184, pick(rng, 2, 2, 3, 0, rng.getrandbits(32) - 0x80000000))
    struct.pack_into("<h", o, 0x188, pick(rng, rng.randrange(0, 0x60), 0x5F, 0x60, 0x61, rng.randrange(-0x10, 0x70)))
    # spray: +0x180 state, +0x184 / +0x186 counters (shares +0x184 with the wreck)
    if kind == 0x20 or rng.randrange(3) == 0:
        struct.pack_into("<i", o, 0x180, pick(rng, 0, 1, 2, 3, rng.getrandbits(32) - 0x80000000))
        struct.pack_into("<h", o, 0x184, pick(rng, rng.randrange(-4, 0x80), 0x3F, 0xFF, 1, 0, 2, rng.getrandbits(16) - 0x8000))
        struct.pack_into("<h", o, 0x186, pick(rng, rng.randrange(0, 7), 4, 5, 1, 0, -1, rng.randrange(-0x40, 0x40)))
    return bytes(o)


def dark_seeds():
    """Seeds whose next value has bits 16..24 clear (the screen's dark flicker)."""
    found, s = [], 1
    while len(found) < 8:
        s += 7919
        if not ((s * 16807 & 0x7FFFFFFF) & 0x1FF0000):
            found.append(s)
    return found


DARK = dark_seeds()


def make_world(rng):
    w = {}
    cars = pick(rng, 1, 2, 4, 6, rng.randrange(0, 7))
    race = bytearray(rbytes(rng, 0x600))
    race[0x54D] = cars
    race[0x551] = pick(rng, 0, 1, 2, 3, 8, rng.getrandbits(8))
    w["race"] = bytes(race)
    slots = bytearray(rbytes(rng, SLOTS[1]))
    for i in range(6):
        struct.pack_into("<I", slots, i * 0x274 + 0x28, pick(rng, 0x12345678, CAMERA, CAMERA) if i else
                         pick(rng, CAMERA, CAMERA, CAMERA, 0x12345678 if cars <= 1 else CAMERA))
    w["slots"] = bytes(slots)
    w["camera"] = struct.pack("<I", BODY) + rbytes(rng, 0xFC)
    w["body"] = rbytes(rng, 0x100)
    w["objects"] = b"".join(make_object(rng) for _ in range(8))
    nspec = pick(rng, 0, 1, 2, 3, 6, 8)
    w["specials"] = b"".join(struct.pack("<I", OBJECTS + rng.randrange(8) * 0x200) for _ in range(8))
    # waters
    nw = pick(rng, 0, 1, 2, 3, 4, rng.randrange(-2, 1))
    waters = bytearray()
    for i in range(4):
        rec = bytearray(rbytes(rng, 0x1C))
        struct.pack_into("<H", rec, 2, pick(rng, rng.randrange(0, 0x800), 0x7FF, 0x7FE, 0, 7, 8, rng.getrandbits(16)))
        cols, rows, extra = rng.randrange(0, 7), rng.randrange(0, 7), rng.randrange(0, 4)
        struct.pack_into("<HHH", rec, 4, cols, rows, rng.getrandbits(16))
        struct.pack_into("<H", rec, 0xA, extra)
        struct.pack_into("<II", rec, 0xC, WATER_BUFS + i * 0x800, WATER_BUFS + i * 0x800 + 0x400)
        struct.pack_into("<hh", rec, 0x14, pick(rng, rng.randrange(-0x400, 0x400), rng.getrandbits(16) - 0x8000), rng.getrandbits(16) - 0x8000)
        struct.pack_into("<hh", rec, 0x18, pick(rng, rng.randrange(-0x400, 0x400), rng.getrandbits(16) - 0x8000), rng.getrandbits(16) - 0x8000)
        waters += rec
    w["waters"] = bytes(waters)
    w["water_bufs"] = rbytes(rng, 0x2000)
    w["polys"] = rbytes(rng, 0x10800)
    model = bytearray(rbytes(rng, 0x40))
    struct.pack_into("<II", model, 0x14, POLYS + 128 * 0x84, POLYS + 0x8400 + 128 * 0x84)
    w["model"] = bytes(model)
    # pieces
    w["pieces"] = rbytes(rng, 0x2000)
    w["free"] = b"".join(struct.pack("<H", rng.randrange(64)) for _ in range(0x80))
    w["used"] = rbytes(rng, 0x200)
    w["smoke"] = bytearray(rbytes(rng, 5 * 0x1E2C))
    for i in range(5):
        struct.pack_into("<I", w["smoke"], i * 0x1E2C + 0xC, pick(rng, 0, 0, 1, rng.getrandbits(32)))
    w["smoke"] = bytes(w["smoke"])
    w["shock"] = bytearray(rbytes(rng, 3 * 0xF14))
    for i in range(3):
        struct.pack_into("<I", w["shock"], i * 0xF14 + 0xC, pick(rng, 0, 0, 1, rng.getrandbits(32)))
    w["shock"] = bytes(w["shock"])
    w["particles"] = rbytes(rng, 0xC000)
    # track sections, cells, palette
    sections = bytearray(rbytes(rng, 8 * 0x40))
    for i in range(8):
        struct.pack_into("<i", sections, i * 0x40 + 8, rng.randrange(0, 8))
        struct.pack_into("<i", sections, i * 0x40 + 0xC, pick(rng, rng.randrange(0, 20), 0, 1, 2))
        struct.pack_into("<I", sections, i * 0x40 + 0x20, VERTS + i * 0x800)
    w["sections"] = bytes(sections)
    w["section_ptrs"] = b"".join(struct.pack("<I", SECTIONS + rng.randrange(8) * 0x40) for _ in range(256))
    w["verts"] = rbytes(rng, 0x4000)
    w["palette"] = rbytes(rng, 0x1000)
    w["cell_recs"] = rbytes(rng, 0x600)
    w["strips"] = rbytes(rng, 0x400)
    c0 = rng.getrandbits(32) & pick(rng, 0xFFFFE07F | 0x1F80, 0xFFFFE0FF, 0xFFFFE001)
    if rng.randrange(3):
        c0 &= ~0x1E00  # strip offsets in the random head
    c1 = (rng.randrange(0, 0x100) << 16) | rng.randrange(0, 0x100)
    if rng.randrange(8) == 0:
        c1 = rng.getrandbits(32)
    w["cell"] = struct.pack("<II", c0, c1) + rbytes(rng, 8)
    w["out"] = rbytes(rng, 0x40)
    vecs = b""
    for _ in range(3):
        vecs += b"".join(s32(pick(rng, rng.randrange(-0x4000000, 0x4000000), rng.randrange(-0x10000, 0x10000),
                                  rng.getrandbits(32) - 0x80000000)) for _ in range(4))
    w["vec"] = vecs
    # globals
    g = [bytearray(rbytes(rng, n)) for _, n in GLOBALS]
    struct.pack_into("<I", g[0], 0, pick(rng, rng.randrange(1, 0x7FFFFFFF), rng.randrange(1, 0x7FFFFFFF), rng.choice(DARK)))
    struct.pack_into("<I", g[0], 4, rng.randrange(1, 0x7FFFFFFF))
    struct.pack_into("<i", g[3], 0, nw)
    struct.pack_into("<I", g[4], 0xE04, pick(rng, rng.randrange(0, 0x41), 0, 0x3F, 0x40, rng.getrandbits(32)))  # rows count 0x5d7fe4
    struct.pack_into("<I", g[4], 0x100, pick(rng, rng.randrange(0, 0x11), 0, 0xF, 0x10, rng.getrandbits(32)))  # lights 0x5d72e0
    struct.pack_into("<ii", g[5], 0, rng.randrange(0, 0x40), pick(rng, rng.randrange(-1, 0x40), -1, 0, 63))
    struct.pack_into("<I", g[6], 0, pick(rng, rng.randrange(0, 1000) * 25, rng.getrandbits(32)))
    struct.pack_into("<i", g[8], 0, nspec)
    struct.pack_into("<i", g[9], 0, pick(rng, rng.randrange(-0x10, 0x420), 0x7F, 0x80, 0x400, 0x401, 200, 201, 0x37,
                                          rng.getrandbits(32) - 0x80000000))
    struct.pack_into("<I", g[9], 4, pick(rng, *range(0, 9), rng.getrandbits(32)))
    struct.pack_into("<h", g[9], 0xC, pick(rng, rng.randrange(0, 5), 4, 8, rng.randrange(-2, 9)))
    for i in range(4):
        g[9][0x10 + i] = pick(rng, rng.randrange(0, 8), rng.getrandbits(8))
    struct.pack_into("<I", g[11], 0, pick(rng, 0x100, 0x80, 0x200, rng.randrange(0, 0x300)))
    g[12][0] = pick(rng, 0, 4, rng.getrandbits(8))
    w["globals"] = [bytes(x) for x in g]
    w["script"] = {
        "sound_play": [rng.getrandbits(8) for _ in range(4)],
        "surface_near": [pick(rng, 0, 0, 1, rng.getrandbits(32)) for _ in range(8)],
        "particle_colour": [rng.getrandbits(32) for _ in range(0x200)],
        "cell_at": [CELL],
    }
    w["stack"] = rbytes(rng, E.STACK_SIZE)
    return w


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.particle = 0
        self.counting = False
        self.hits = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        for address in PROBES:
            m.uc.hook_add(UC_HOOK_CODE, self._probe, begin=address, end=address)

    def _probe(self, uc, address, size, _):
        if self.counting:
            self.hits[PROBES[address]] = self.hits.get(PROBES[address], 0) + 1

    def reset(self, script):
        self.log = []
        self.particle = 0
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        name, nargs, skip, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i in skip:
            args[i] = "-"
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if isinstance(a, int) and E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        eax = 0
        if name == "take_particle":
            eax = PARTICLES + (self.particle % 0x100) * 0xC0
            self.particle += 1
        elif returns:
            eax = self._next(name, 0)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, w):
    args = []
    vectors = iter([VEC, VEC + 0x10, VEC + 0x20])
    for k in kinds:
        if k == "O":
            args.append(OBJECTS + rng.randrange(8) * 0x200)
        elif k == "V":
            args.append(next(vectors))
        elif k == "X":
            args.append(pick(rng, 0, next(vectors)))
        elif k == "C":
            args.append(CELL)
        elif k == "O":
            args.append(OUT)
        elif k == "l":
            args.append(pick(rng, 6, 7, 8, 9, 10, 10, rng.randrange(0, 16), rng.getrandbits(32)))
        elif k == "b":
            args.append(pick(rng, 0, 1, 1, rng.getrandbits(32)))
        elif k == "s":
            args.append(pick(rng, rng.randrange(-5, 8), 0, -4, rng.getrandbits(32) - 0x80000000))
        elif k == "z":
            args.append(pick(rng, rng.randrange(0, 0x20), 0, 0xFFFFFFFD, rng.randrange(0, 0x100)))
        elif k == "k":
            args.append(pick(rng, 1, 4, 8, 0x10, 0x4000, 0x10004, rng.getrandbits(16), rng.getrandbits(32)))
        elif k == "R":
            args.append(pick(rng, rng.randrange(0, 0x40) << 11, rng.getrandbits(32)))
        elif k == "S":
            args.append(pick(rng, rng.randrange(0, 0x80), rng.getrandbits(32)))
        else:
            args.append(pick(rng, 0, 1, rng.randrange(0, 0x40), rng.getrandbits(8), rng.getrandbits(32)))
    if kinds == "COl":
        args[1] = OUT
    return args


def load(m, w):
    m.write(SCRATCH, bytes(SCRATCH_SIZE))
    m.write(OBJECTS, w["objects"])
    m.write(WATERS, w["waters"])
    m.write(WATER_BUFS, w["water_bufs"])
    m.write(POLYS, w["polys"])
    m.write(PIECES, w["pieces"])
    m.write(FREE, w["free"])
    m.write(USED, w["used"])
    m.write(SMOKE, w["smoke"])
    m.write(SHOCK, w["shock"])
    m.write(PARTICLES, w["particles"])
    m.write(VERTS, w["verts"])
    m.write(OUT, w["out"])
    m.write(VEC, w["vec"])
    m.write(MODEL, w["model"])
    m.write(SPECIALS, w["specials"])
    m.write(CAMERA, w["camera"])
    m.write(BODY, w["body"])
    m.write(SECTIONS, w["sections"])
    m.write(SECTION_PTRS, w["section_ptrs"])
    m.write(PALETTE, w["palette"])
    m.write(CELL, w["cell"])
    m.write(CELL_RECS, w["cell_recs"])
    m.write(STRIPS, w["strips"])
    m.write(RACE, w["race"])
    m.write(SLOTS[0], w["slots"])
    for (a, n), v in zip(GLOBALS, w["globals"]):
        m.write(a, v)
    for a, p in POINTERS.items():
        m.write(a, struct.pack("<I", p))
    m.write(E.STACK, w["stack"])


def run_case(m, stubs, name, address, kinds, returns, rng):
    w = make_world(rng)
    args = make_args(rng, kinds, w)
    if name == "particles_burst" and args[4] & 0xFFFF != 8:
        args[2] &= 0xFF  # a huge size is 2^31 particles
    if name == "mesh_rows_touch" and rng.randrange(2):  # the same as the last entry
        g = bytearray(w["globals"][4])
        n = struct.unpack_from("<I", g, 0xE04)[0]
        if 0 < n <= 0x40:
            struct.pack_into("<I", g, (n - 1) * 4, (0x40000000 | (args[1] & 0x7F)) | ((args[0] & 0x3FFFFF) << 7) & 0xFFFFFFFF)
            w["globals"][4] = bytes(g)
    results = []
    for target in (address, m.symbols[name]):
        load(m, w)
        stubs.reset(w["script"])
        stubs.counting = target == address
        r = m.call(target, args)
        stubs.counting = False
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(*WRITABLE) + m.read(*SLOTS) + b"".join(m.read(a, n) for a, n in GLOBALS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  args:", [hex(x) for x in args], "cell:", w["cell"][:8].hex())
            print("  differ:", [(hex(i), struct.unpack_from("<I", a, i)[0], struct.unpack_from("<I", b, i)[0]) for i in diff[:8]])
        logs = "" if results[0][2] == results[1][2] else f"\n  calls: {results[0][2][:6]}\n  ours:  {results[1][2][:6]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(11)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    for name, (address, kinds, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        errors = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    if "-c" in sys.argv:
        print("coverage:", {name: stubs.hits.get(name, 0) for name in PROBES.values()})
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
