#!/usr/bin/env python3
"""Differential test of setup_s.c (race setup 0x46a060 and the integer
parts of the track loader's tree: track objects, lights, sprites, lit
polygons, texture pages, TV camera rails) against the original, on random
track, model, object and race state.

Functions outside the group that touch the grid, the renderer or the
allocator (and, for race_setup, every setup step it calls) are stubbed in
both runs: each stub logs its arguments and returns a scripted value, and
the call logs are compared as well as memory. The object pool helpers
(0x460af0, 0x460b20, 0x460b40, 0x460dc0, 0x460e40, 0x460f50) and the maths
(0x480780, 0x480b10, 0x480bc0, 0x407e90, 0x480c70, 0x480cd0) run as the
original in both.

    tests/emu_setup_s.py EXE build/emu_s.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

S, S_SIZE = 0x01000000, 0x40000
TRACK_INFO = S + 0x0000
MODEL_PTRS = S + 0x0200  # 256 pointers
MODELS = S + 0x1000  # 8 models of 0x400
MODEL_SIZE = 0x400
RECORDS = S + 0x3000  # 16 records of 0x40 (0x38 apart for the lit scan)
OBJ = S + 0x3400  # an object for the functions that take one
LIGHT_TYPES = S + 0x3800  # 256 of 8
TEXTURES = S + 0x4000  # 0x200 of 0x10
LIT_FACES = S + 0x6000
OBJ_LIST = S + 0x6800
TRACK_MODELS = S + 0x6C00
TRACK = S + 0x7000
TRACK_MODEL_PTRS = S + 0x7100
TRACK_INDEX = S + 0x7500
LIST58 = S + 0x7800
TABLE5C = S + 0x8000
CONFIG = S + 0x9000
COLOURS = S + 0x9100
TEXRECS = S + 0xA000
POOL = S + 0x10000
ALLOCS = S + 0x30000
RACE, RACE_SIZE = 0x02000000, 0xC0000

POINTERS = {0x4B1960: RACE, 0x4B2198: CONFIG, 0x6C1EB0: TRACK_INFO, 0x6C1E14: TEXTURES, 0x6C1E30: TRACK,
            0x6BFAC8: TRACK_MODELS, 0x6CE738: OBJ_LIST, 0x6BFB58: LIST58, 0x6BFB5C: TABLE5C, 0x6B9B90: POOL}
WINDOWS = [(0x4B1000, 0x6000), (0x4BA700, 0x900), (0x4BCD78, 4), (0x4C2400, 0x100), (0x4D0800, 0x5800),
           (0x5D6000, 0x1400), (0x5F7200, 0x100), (0x6B9B00, 0x200), (0x6BF800, 0x1800), (0x6C1E00, 0x100),
           (0x6CCD00, 0x300), (0x6CE600, 0x200), (0x6CFB00, 0x1500)]
COMPARE_SCRATCH = [(S, 0xA100), (POOL, 0x8000)]
RACE_DWORDS = [RACE + 0x1FB20 + i * 0x1E884 for i in range(6)]
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, returns, special)
STUBS = {
    0x460FB0: ("grid_link", 1, False, None),
    0x4611D0: ("grid_unlink", 1, False, None),
    0x460FF0: ("link_at", 4, False, None),
    0x465740: ("body_prepare", 1, False, None),
    0x442C90: ("light_place", 3, False, "pos"),
    0x41E710: ("texture_upload", 6, True, None),
    0x41DE10: ("colour_set", 3, False, None),
    0x4445E0: ("game_alloc", 1, True, None),
}
SETUP_CALLEES = {
    0x46A040: 0, 0x41B370: 0, 0x41AA40: 0, 0x405050: 0, 0x478340: 0, 0x418FA0: 0, 0x442660: 0, 0x465550: 1,
    0x43FA60: 2, 0x444080: 0, 0x41AA60: 0, 0x435110: 0, 0x47C330: 0, 0x441B90: 0, 0x479AF0: 0, 0x46C210: 0,
    0x441D70: 0, 0x46BA00: 0, 0x4377C0: 0, 0x47D980: 0, 0x486780: 0, 0x465270: 0, 0x463860: 1, 0x463990: 0,
    0x467120: 0, 0x4583E0: 0, 0x43A8D0: 0, 0x479720: 0, 0x46D2B0: 0, 0x457F80: 0, 0x47BD70: 0, 0x433D30: 0,
    0x462360: 0, 0x4040F0: 0, 0x434380: 0, 0x42A610: 0, 0x46EAB0: 0, 0x41AB80: 0, 0x423F00: 1, 0x47B140: 0,
    0x40E670: 2, 0x406BE0: 3, 0x490390: 2, 0x401470: 1, 0x449CA0: 1, 0x449CD0: 1, 0x428CC0: 2, 0x489FA0: 1,
    0x449D00: 5, 0x40F1E0: 1, 0x475090: 0, 0x481E50: 0, 0x483D90: 0, 0x4730B0: 0, 0x41B460: 1, 0x463930: 1,
    0x429220: 1, 0x4351A0: 0, 0x46A940: 0, 0x4430F0: 0, 0x485070: 0, 0x4291F0: 1,
}
for _a, _n in SETUP_CALLEES.items():
    STUBS.setdefault(_a, (f"setup_{_a:x}", _n, True, None))
OBJECT_STUBS = {0x460FB0, 0x4611D0, 0x460FF0, 0x465740}

# name: (address, argument kinds, returns, active stubs)
TESTS = {
    "prim_flag2_set": (0x4270E0, "pn", False, set()),
    "quat_from_angles": (0x480A80, "aaaq", False, set()),
    "body_init_from_template": (0x465640, "OT", False, OBJECT_STUBS),
    "object_create": (0x460BA0, "mkzs", True, OBJECT_STUBS),
    "track_object_make": (0x401300, "Rk", True, OBJECT_STUBS),
    "track_object_make_static": (0x41AC60, "R", True, OBJECT_STUBS),
    "track_object_make_k5": (0x45BF10, "R", True, OBJECT_STUBS),
    "track_marker_make": (0x43E5C0, "R", True, OBJECT_STUBS),
    "track_object_spawn": (0x41AA70, "R", True, OBJECT_STUBS),
    "sprite_object_create": (0x47DCC0, "xxxtbb", True, OBJECT_STUBS),
    "sprite_spawn_marker": (0x47DBC0, "xxxtbb", False, OBJECT_STUBS),
    "light_sprite_spawn": (0x47DC30, "xxxtbbbbc", True, OBJECT_STUBS),
    "object_light_spawn": (0x4426E0, "OiC", False, OBJECT_STUBS),
    "object_lights_setup": (0x442680, "ORC", False, OBJECT_STUBS),
    "object_triggers_register": (0x4039D0, "Rn", False, set()),
    "track_static_add": (0x403A20, "n", False, set()),
    "track_blast_register": (0x405070, "OR", False, set()),
    "lit_face_add": (0x4783D0, "nn", False, set()),
    "object_lit_faces_register": (0x478370, "O", False, set()),
    "faces_count_lit": (0x478E70, "LMC", False, set()),
    "model_count_lit": (0x478EC0, "RMC", False, set()),
    "track_models_mark_lit": (0x478F80, "cR", True, set()),
    "track_tables_alloc": (0x4044C0, "ss", False, {0x4445E0}),
    "object_pool_reset": (0x460AA0, "s", False, {0x4445E0}),
    "texture_bytes": (0x41F1B0, "i", True, set()),
    "track_textures_load": (0x428130, "cffXpPF", True, {0x41E710}),
    "ambient_colour_set": (0x428350, "nnn", False, {0x41DE10}),
    "track_tv_spots": (0x40B5B0, "", False, set()),
    "race_setup": (0x46A060, "", True, set(SETUP_CALLEES)),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def make_model(rng, base):
    m = bytearray(rng.getrandbits(8) for _ in range(MODEL_SIZE))

    def level(off, parts_at, kmax):
        k = rng.randrange(0, kmax + 1)
        struct.pack_into("<II", m, off + 0x10, parts_at, parts_at + k * 0x28)
        struct.pack_into("<H", m, off + 6, rng.randrange(0, 9))
        m[off + 8] = pick(rng, 0, 0, 1, 2, 4, rng.getrandbits(8))
        struct.pack_into("<HH", m, off + 0x1C, 0, 0)

    level(0, 0x60, 3)
    level(0x200, 0x20, 2)
    level(0x300, 0x20, 2)
    struct.pack_into("<H", m, 0x1C, pick(rng, 0, 0x200, 0x200))
    struct.pack_into("<H", m, 0x1E, pick(rng, 0, 0x300, 0x300, 0x200))
    m[0x27] = rng.randrange(0, 7)
    struct.pack_into("<H", m, 0x34, 0x100)
    struct.pack_into("<H", m, 0x4C, 0x180)
    for i in range(5):
        m[0x180 + i * 8 + 7] = rng.randrange(0, 256)
    return bytes(m)


def make_record(rng):
    r = bytearray(rng.getrandbits(8) for _ in range(0x40))
    r[1] = pick(rng, 4, 4, 0, rng.getrandbits(8))
    r[3] = pick(rng, rng.randrange(0, 10), rng.randrange(0, 10), rng.getrandbits(8))
    r[0x1C] = pick(rng, 0, rng.getrandbits(8))
    r[0x1D] = pick(rng, 0, rng.getrandbits(8))
    r[0x1F] = pick(rng, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, rng.getrandbits(8))
    for o in (0x28, 0x2A, 0x2C):
        struct.pack_into("<H", r, o, rng.randrange(0, 0x200))
    for i in range(5):
        w = pick(rng, 0x2000 | rng.randrange(0, 0x1000), rng.getrandbits(16), 0)
        struct.pack_into("<H", r, 0x2E + i * 2, w)
    return bytes(r)


def make_state(rng):
    st = {}
    info = bytearray(rng.getrandbits(8) for _ in range(0x200))
    struct.pack_into("<I", info, 0x18, MODEL_PTRS)
    struct.pack_into("<I", info, 0xD0, LIGHT_TYPES)
    struct.pack_into("<I", info, 0xEC, LIT_FACES)
    struct.pack_into("<H", info, 0x2E, rng.randrange(0, 0x40))
    st["info"] = bytes(info)
    st["models"] = b"".join(make_model(rng, MODELS + i * MODEL_SIZE) for i in range(8))
    st["model_ptrs"] = b"".join(struct.pack("<I", pick(rng, MODELS + rng.randrange(8) * MODEL_SIZE,
                                                          MODELS + rng.randrange(8) * MODEL_SIZE, 0))
                                for _ in range(256))
    st["records"] = b"".join(make_record(rng) for _ in range(16))
    obj = bytearray(rng.getrandbits(8) for _ in range(0x400))
    struct.pack_into("<I", obj, 0x10, MODELS + rng.randrange(8) * MODEL_SIZE)
    struct.pack_into("<I", obj, 0x194, COLOURS)
    obj[0x198] = pick(rng, 0, 1, rng.getrandbits(8))
    struct.pack_into("<i", obj, 0x3F0, rng.randrange(0, 0x200))  # the light counter ("C")
    st["obj"] = bytes(obj)
    types = bytearray(rng.getrandbits(8) for _ in range(0x800))
    for i in range(256):
        struct.pack_into("<H", types, i * 8 + 6, rng.randrange(0, 0x200))
        types[i * 8 + 4] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    st["types"] = bytes(types)
    st["textures"] = bytes(rng.getrandbits(8) for _ in range(0x2000))
    st["lit"] = bytes(rng.getrandbits(8) for _ in range(0x500))
    st["obj_list"] = bytes(rng.getrandbits(8) for _ in range(0x400))
    tm = bytearray(0x100)
    struct.pack_into("<I", tm, 0x18, MODEL_PTRS)
    st["track_models"] = bytes(tm)
    track = bytearray(0x100)
    struct.pack_into("<II", track, 0x18, TRACK_MODEL_PTRS, 0)
    struct.pack_into("<I", track, 0x58, TRACK_INDEX)
    st["track"] = bytes(track)
    st["track_ptrs"] = bytes(rng.getrandbits(8) for _ in range(0x400))
    st["track_index"] = b"".join(struct.pack("<h", rng.randrange(0, 0x100)) for _ in range(0x100))
    st["list58"] = b"".join(struct.pack("<H", rng.randrange(0, 0x2000)) for _ in range(0x400))
    st["table5c"] = bytes(pick(rng, 0xFF, 0xFF, rng.getrandbits(8)) for _ in range(0x1000))
    config = bytearray(rng.getrandbits(8) for _ in range(0x100))
    config[0x2B] = pick(rng, 0, 1)
    st["config"] = bytes(config)
    colours = bytearray()
    for _ in range(0x40):
        colours += struct.pack("<I", pick(rng, 0x4000000 | rng.getrandbits(24), r32(rng)))
    st["colours"] = bytes(colours)
    texrecs = bytearray(rng.getrandbits(8) for _ in range(0x100))
    st["texrecs"] = bytes(texrecs)
    race = bytearray(rng.getrandbits(8) for _ in range(0xC00))
    race[0x54D] = pick(rng, rng.randrange(1, 7), rng.randrange(1, 7), 0)
    race[0x552] = rng.randrange(0, 8)
    race[0x551] = pick(rng, 4, 0, rng.getrandbits(8))
    race[0x579] = pick(rng, rng.randrange(0, 0x14), rng.randrange(0, 0x14), 0xC, rng.getrandbits(8))
    race[0x58C] = pick(rng, 0, 0, rng.getrandbits(8))
    race[0x555] = pick(rng, 0, 1)
    st["race"] = bytes(race)
    st["race_dwords"] = [r32(rng) for _ in RACE_DWORDS]
    # exe data fields the functions read
    g = {}
    g[0x4D087C] = struct.pack("<I", rng.randrange(0, 0x1000))
    g[0x4D1084] = struct.pack("<I", pick(rng, 0, POOL - 0x40 + rng.randrange(0, 0x80)))
    g[0x4D1088] = struct.pack("<I", pick(rng, rng.randrange(0, 0x200), 0x1FF, 0x200))
    g[0x6CE6FC] = struct.pack("<I", rng.randrange(0, 0x40))
    g[0x5F72A4] = struct.pack("<I", rng.randrange(0, 0x30))
    g[0x4D5800] = struct.pack("<i", pick(rng, rng.randrange(0, 0x28), 0x27, 0x28, rng.randrange(-5, 0x40)))
    g[0x4B65F4] = struct.pack("<i", pick(rng, rng.randrange(0, 0x28), 0x26, 0x28, 0, -1))
    g[0x6D06C0] = struct.pack("<I", rng.randrange(0, 0x30))
    g[0x4B5FA0] = struct.pack("<I", rng.randrange(0, 0x20))
    g[0x4B5F98] = struct.pack("<I", rng.randrange(0, 0x40))
    g[0x4BCD78] = struct.pack("<I", pick(rng, 0, 1, r32(rng)))
    g[0x4D4654] = struct.pack("<I", pick(rng, 0, 1))
    g[0x4D3DB0] = struct.pack("<I", r32(rng))
    g[0x4B1964] = struct.pack("<H", rng.getrandbits(16))
    # tables the functions append to: random, so every field written shows
    for a, n in ((0x6D06E0, 0x400), (0x4B5E58, 0x100), (0x6D0A80, 0x300), (0x5D6400, 0x1E0), (0x5D6E00, 0x800),
                 (0x4D0880, 0x800)):
        g[a] = bytes(rng.getrandbits(8) for _ in range(n))
    slots = bytearray(rng.getrandbits(8) for _ in range(0x274 * 6))
    g[0x6BFB80] = bytes(slots)
    lines = bytearray(rng.getrandbits(8) for _ in range(0x38 * 0x28))
    for i in range(0x28):
        struct.pack_into("<i", lines, i * 0x38, pick(rng, 1, 1, 0, 2, r32(rng) - 0x80000000))
        for k in range(1, 7):
            struct.pack_into("<i", lines, i * 0x38 + k * 4, rng.randrange(-0x20000000, 0x20000000))
    g[0x6CFBE0] = bytes(lines)
    slots_tex = bytearray(rng.getrandbits(8) for _ in range(0x900))
    for i in range(0x40):
        struct.pack_into("<I", slots_tex, i * 0x24, pick(rng, 0, 1, r32(rng)))
        struct.pack_into("<i", slots_tex, i * 0x24 + 0x14, pick(rng, rng.randrange(0, 0x10), r32(rng) - 0x80000000))
    g[0x4BA700] = bytes(slots_tex)
    st["globals"] = g
    st["script"] = {
        "texture_upload": [pick(rng, 1, 1, 1, 0, 5) for _ in range(12)],
        "game_alloc": [ALLOCS + rng.randrange(0, 0x100) * 4 for _ in range(4)],
        "setup_465550": [r32(rng)],
        "setup_43fa60": [pick(rng, 1, 1, 1, 0)],
        "setup_449ca0": [r32(rng) for _ in range(6)],
        "setup_449cd0": [pick(rng, 0, 1, r32(rng)) for _ in range(6)],
        "setup_428cc0": [r32(rng) for _ in range(6)],
        "setup_489fa0": [r32(rng) for _ in range(6)],
        "setup_449d00": [r32(rng) for _ in range(6)],
        "light_place": [[rng.randrange(-0x8000, 0x8000) for _ in range(3)] for _ in range(6)],
    }
    st["change"] = None
    if rng.randrange(4) == 0:
        st["change"] = (f"setup_{rng.choice(list(SETUP_CALLEES)):x}", rng.randrange(0, 7))
    return st


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.active = set()
        self.change = None
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active, change):
        self.log = []
        self.script = {k: [list(x) if isinstance(x, list) else x for x in v] for k, v in script.items()}
        self.active = active
        self.change = change

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address not in self.active:
            return
        name, nargs, returns, special = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        if special == "pos":
            pos = self._next(name, [0, 0, 0])
            uc.mem_write(args[2], struct.pack("<3h", *pos))
            args[2] = "local"
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if isinstance(a, int) and E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        if self.change and self.change[0] == name:
            uc.mem_write(RACE + 0x54D, bytes([self.change[1]]))
            self.change = None
        eax = self._next(name, 0) if returns else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args = []
    for k in kinds:
        if k == "p":
            args.append(RECORDS + rng.randrange(0, 0x3F0))
        elif k == "n":
            args.append(pick(rng, 0, 1, r32(rng)))
        elif k == "a":
            args.append(pick(rng, 0, 0, rng.randrange(-0x8000, 0x8000), r32(rng)))
        elif k == "q":
            args.append(OBJ + 0x58)
        elif k == "O":
            args.append(OBJ)
        elif k == "T":
            args.append(pick(rng, 0x4A7048, 0x4A7DA0, 0x4B0C78, RECORDS))
        elif k == "m":
            args.append(pick(rng, MODELS + rng.randrange(8) * MODEL_SIZE, 0))
        elif k == "k":
            args.append(pick(rng, 0x19, 0x1F, 0x14, 0x23, rng.getrandbits(12)))
        elif k == "z":
            args.append(pick(rng, 0x18C, 0x180, 0x188, rng.randrange(0, 0x400)))
        elif k == "s":
            args.append(pick(rng, 0, 0, 1, rng.randrange(0, 0x40)))
        elif k == "R":
            args.append(RECORDS + rng.randrange(16) * 0x40)
        elif k == "x":
            args.append(pick(rng, rng.randrange(-0x8000, 0x8000), r32(rng)))
        elif k == "t":
            args.append(rng.randrange(0, 0x200))
        elif k == "b":
            args.append(pick(rng, rng.getrandbits(8), r32(rng)))
        elif k == "c":
            args.append(pick(rng, rng.randrange(0, 0x40), rng.randrange(0, 17), 0, -1 & 0xFFFFFFFF))
        elif k == "C":
            args.append(OBJ + 0x3F0)  # a counter word at the end of the object
        elif k == "i":
            args.append(pick(rng, rng.randrange(0, 5), rng.randrange(0, 5), rng.randrange(0, 0x10), r32(rng)))
        elif k == "L":
            args.append(LIST58 + rng.randrange(0, 0x200) * 2)
        elif k == "M":
            args.append(MODELS + rng.randrange(8) * MODEL_SIZE + pick(rng, 0, 0, 0x200, 0x300))
        elif k == "f":
            args.append(pick(rng, rng.randrange(-2, 10), r32(rng)))
        elif k == "X":
            args.append(TEXRECS)
        elif k == "P":
            args.append(r32(rng))
        elif k == "F":
            args.append(pick(rng, 0, 1, 2, 3, r32(rng)))
        else:
            args.append(r32(rng))
    return args


def setup_memory(m, st, pristine):
    m.write(S, bytes(S_SIZE))
    m.write(TRACK_INFO, st["info"])
    m.write(MODEL_PTRS, st["model_ptrs"])
    m.write(MODELS, st["models"])
    m.write(RECORDS, st["records"])
    m.write(OBJ, st["obj"])
    m.write(LIGHT_TYPES, st["types"])
    m.write(TEXTURES, st["textures"])
    m.write(LIT_FACES, st["lit"])
    m.write(OBJ_LIST, st["obj_list"])
    m.write(TRACK_MODELS, st["track_models"])
    m.write(TRACK, st["track"])
    m.write(TRACK_MODEL_PTRS, st["track_ptrs"])
    m.write(TRACK_INDEX, st["track_index"])
    m.write(LIST58, st["list58"])
    m.write(TABLE5C, st["table5c"])
    m.write(CONFIG, st["config"])
    m.write(COLOURS, st["colours"])
    m.write(TEXRECS, st["texrecs"])
    m.write(RACE, st["race"])
    for a, v in zip(RACE_DWORDS, st["race_dwords"]):
        m.write(a, struct.pack("<I", v))
    for (a, n), data in zip(WINDOWS, pristine):
        m.write(a, data)
    for a, p in POINTERS.items():
        m.write(a, struct.pack("<I", p))
    for a, v in st["globals"].items():
        m.write(a, v)
    m.write(E.STACK, STACK_FILL)


def snapshot(m):
    data = b"".join(m.read(a, n) for a, n in COMPARE_SCRATCH)
    data += b"".join(m.read(a, n) for a, n in WINDOWS)
    data += m.read(RACE, 0xC00) + b"".join(m.read(a, 4) for a in RACE_DWORDS)
    return data


def run_case(m, stubs, pristine, name, address, kinds, returns, active, rng):
    st = make_state(rng)
    args = make_args(rng, kinds)
    results = []
    for target in (address, m.symbols[name]):
        setup_memory(m, st, pristine)
        stubs.reset(st["script"], active, st["change"])
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        results.append((r, snapshot(m), stubs.log))
    if results[0][0][0] == "error" and results[1][0][0] == "error":
        return "error", None
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), struct.unpack_from("<i", a, i)[0], struct.unpack_from("<i", b, i)[0])
                                for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return "bad", f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return "ok", None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, S_SIZE)
    m.uc.mem_map(RACE, RACE_SIZE)
    pristine = [m.read(a, n) for a, n in WINDOWS]
    stubs = Stubs(m)
    rng = random.Random(23)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, returns, active) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        for _ in range(iterations):
            status, msg = run_case(m, stubs, pristine, name, address, kinds, returns, active, rng)
            if status == "error":
                errors += 1
            elif msg:
                if bad < 3:
                    print(msg)
                bad += 1
        extra = f" ({errors} runs hit an emulator error in both)" if errors else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{extra}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
