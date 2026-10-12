#!/usr/bin/env python3
"""Differential test of orig_pk.c (group PK: the incoming-missile level,
lighting, the model's GTE set-up and depth, the model packet builders and
the kind-5 track object, 0x458760-0x45bbf0) against the original, on
random state.

Logging stubs (as many arguments as the callee reads; pointer arguments by
the content they point to) with scripted return values: the light colour
matrix load 0x428940 (its 20 bytes), the palette 0x486350, quat_facing
0x40ee60, body_sparks 0x45bd30, contact_particle 0x45be10, explosion_effects
0x42aa30, and the group's own functions where the tested one calls them by
address: 0x45a2a0 / 0x45b400 / 0x45a430 for 0x45a260, 0x45ab20 for 0x45a430.
Pure code runs for real in both: the GTE emulation 0x427fc0 / 0x426620 /
0x4282d0, the transform / projection pair through 0x6ccd78 / 0x6ccd80 (the
x87 pair 0x426a20 / 0x426b00), the light colour through 0x6cce3c (0x427400),
the facing value 0x4278f0, the normalise 0x4290b0 and the group's 0x45a2f0,
0x459e80, 0x45ba30, 0x45b390 / 0x45b9f0 (original code in both runs). The
x87 control word is 0x27f, 0x37f or 0x07f; memory, the call log, ebx / esi /
edi / ebp and the x87 stack top / tag word are compared.

    tests/emu_orig_pk.py EXE build/emu_pk.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x30000
ARG = SCR + 0x0000  # argument blocks (0x400)
BODY = SCR + 0x1000  # 0x400
MODEL = SCR + 0x2000  # base model, LODs at +0x100 / +0x200, run lists at +0x80
NORMALS = SCR + 0x3000
VERTS = SCR + 0x4000
FACES = SCR + 0x5000  # 0x3000
PRIMS = SCR + 0x8000  # the body's own packets (0x8000)
SHARED = SCR + 0x10000  # 0x6c1e18 .. (0x8000)
TEXIDS = SCR + 0x18000  # 0x400
TEXT = SCR + 0x18400  # 0x1000
ANIM = SCR + 0x19400  # 0x1800
FRAMES = SCR + 0x1AC00  # 0x400
UVSETS = SCR + 0x1B000  # 0x400
COLS = SCR + 0x1B400  # 0x400
PAL = SCR + 0x1B800  # 0x400
HEADS = SCR + 0x1C000  # 0x2000
LIGHTS = SCR + 0x1E000  # 0x100
STRENGTH = SCR + 0x1F000  # 0x1000
MAPS = SCR + 0x20000  # 0x400 map pointers, the map at +0x400, cells at +0x800
WEAP = SCR + 0x22000  # 24 x 0x200
OPTIONS = SCR + 0x25000

REGIONS = [
    (0x4B11D8, 8),  # seeds
    (0x4B2198, 4),
    (0x4BDF00, 4),
    (0x4D4B9C, 4),
    (0x5D8000, 0x10000),  # light records
    (0x6B9CE0, 0x60),  # weapon objects
    (0x6BFAC0, 4),
    (0x6BFB44, 0x1C),
    (0x6C1E14, 0x14),
    (0x6CCB00, 0x3A0),  # GTE emulation state
    (0x6CD404, 4),
    (0x6CD510, 0x800),  # ordering tables
    (0x6CDD80, 0x800),
    (0x6CE5D0, 0x20),  # camera matrix
    (0x6CE60C, 4),
]

# argument kinds: v value, m<n> n bytes at a pointer
STUBS = {
    0x428940: ("light_matrix_load", ["m20"]),
    0x486350: ("palette_get", []),
    0x40EE60: ("quat_facing", ["v", "m12"]),
    0x45BD30: ("body_sparks", ["v", "v"]),
    0x45BE10: ("contact_particle", ["v", "v", "v"]),
    0x42AA30: ("explosion_effects", ["m12", "m12", "v", "v", "v"]),
    0x45A2A0: ("model_view_depth", ["v"]),
    0x45B400: ("model_lights_set", ["v", "v"]),
    0x45A430: ("model_faces_draw", ["v", "v"]),
    0x45AB20: ("model_faces_draw_smooth", ["v", "v"]),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def pick(rng, *choices):
    return rng.choice(choices)


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B2198, "<I", OPTIONS)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        if SCR <= address < SCR + SCR_SIZE:
            struct.pack_into(fmt, self.scr, address - SCR, value)
            return
        a = self.find(address)
        struct.pack_into(fmt, self.mem[a], address - a, value)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.active = set()
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.active = active

    def _hook(self, uc, address, size, _):
        if address not in self.active:
            return
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            else:
                logged.append(rd(self.m, a, int(kind[1:])))
        self.log.append((name, tuple(logged)))
        values = self.script.get(name)
        uc.reg_write(UC_X86_REG_EAX, (values.pop(0) if values else 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def s16(rng):
    return pick(rng, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), 0, 0x1000)


def gte_matrix_float(st, rng):
    for i, a in enumerate((0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40,
                           0x6CCD44)):
        st.put(a, "<f", (1.0 if i in (0, 4, 8) else 0.0) + rng.uniform(-0.4, 0.4))
    st.put(0x6CCD0C, "<f", rng.uniform(-800, 800))
    st.put(0x6CCD10, "<f", rng.uniform(-800, 800))
    st.put(0x6CCD14, "<f", pick(rng, rng.uniform(1500, 12000), rng.uniform(-2000, 3000), rng.uniform(12000, 90000)))


def projection(st, rng):
    """The x87 transform / projection pair, its constants, the light colour."""
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD80, "<I", 0x426B00)
    st.put(0x6CCE3C, "<I", 0x427400)
    st.put(0x6CCD74, "<I", ARG + 0x200)
    st.put(0x6CCD00, "<f", rng.uniform(100, 600))  # H
    st.put(0x6CCD04, "<f", pick(rng, rng.uniform(1, 200), rng.uniform(1000, 3000)))  # near
    st.put(0x6CCD08, "<f", pick(rng, rng.uniform(4000, 30000), 1e6))  # far
    st.put(0x6CCD5C, "<f", rng.uniform(-400, 400))
    st.put(0x6CCD60, "<f", rng.uniform(-400, 400))
    st.put(0x6CCD64, "<f", rng.uniform(-1000, 0))
    st.put(0x6CCD68, "<f", rng.uniform(0, 1000))
    st.put(0x6CCD6C, "<f", rng.uniform(-1000, 0))
    st.put(0x6CCD70, "<f", rng.uniform(0, 1000))
    for a in range(0x6CCDD0, 0x6CCE2C, 4):  # light direction and colour matrices
        st.put(a, "<f", rng.uniform(-1.5, 1.5))
    for a in (0x6CCE30, 0x6CCE34, 0x6CCE38):
        st.put(a, "<f", rng.uniform(0, 4096))


def plan_runs(rng, max_faces):
    """Run counts (k * 8 + low bits; 4 ends) and how many faces each takes."""
    runs, total = [], 0
    nruns = rng.randrange(1, 5)
    while True:
        last = len(runs) + 1 >= nruns
        k = pick(rng, rng.randrange(0, 12), rng.randrange(0, 4))
        low = pick(rng, 0, 0, 2, 1, 3) | (4 if last else 0)
        c = (k * 8 + low) & 0xFFFF
        it, x = 0, c
        while True:
            it += 1
            x -= 8
            if x <= 0:
                break
        if total + it > max_faces:
            c, it, x = 4, 1, -4  # one face, then stop
        runs.append((c, it))
        total += it
        if x & 4:
            return runs, total


def g_model(st, rng, smooth):
    projection(st, rng)
    gte_matrix_float(st, rng)
    nv = pick(rng, rng.randrange(0, 12), rng.randrange(0, 60), rng.randrange(0, 256))
    flags = pick(rng, 0, 0x800, rng.getrandbits(32))
    shared = not (flags & 0x800)
    st.put(BODY + 0, "<I", flags)
    st.put(BODY + 8, "<H", pick(rng, 0, 0, 0xFFFE, rng.getrandbits(16)))
    st.put(BODY + 0x10, "<I", MODEL)
    st.put(BODY + 0x24, "<I", VERTS)
    for off in (0x10D, 0x162, 0x163):
        st.put(BODY + off, "<B", nv)
    # the model and its levels of detail
    st.put(MODEL + 0x1C, "<H", 0x100)
    st.put(MODEL + 0x1E, "<H", pick(rng, 0, 0x200))
    for lod in (MODEL, MODEL + 0x100, MODEL + 0x200):
        st.put(lod + 0x10, "<I", 0x80)
        st.put(lod + 0x48, "<I", NORMALS if smooth else 0)
    if not smooth and rng.randrange(10) == 0:
        st.put(MODEL + 0x48, "<I", NORMALS)  # goes to 0x45ab20
    if smooth and rng.randrange(20) == 0:
        st.put(MODEL + 0x48, "<I", 0)  # nothing to draw
    # vertices and normals
    for i in range(nv + 1):
        st.put(VERTS + 8 * i, "<h", rng.randrange(-1500, 1500))
        st.put(VERTS + 8 * i + 2, "<h", rng.randrange(-1500, 1500))
        st.put(VERTS + 8 * i + 4, "<h", rng.randrange(-1500, 1500))
        for k in range(3):
            st.put(NORMALS + 8 * i + 2 * k, "<h", pick(rng, rng.randrange(-0x1000, 0x1001), s16(rng)))
    # faces and runs
    runs, total = plan_runs(rng, 0x180)
    for i in range(total):
        if rng.randrange(12) == 0:
            w = 0
        else:
            top = min(nv + 2, 0x100)  # 1-based; index 0 is outside the original's buffer
            idx = [rng.randrange(1, top) for _ in range(3)]
            d = pick(rng, 0, rng.randrange(1, top))
            w = idx[0] | idx[1] << 8 | idx[2] << 16 | d << 24
        st.put(FACES + 8 * i, "<I", w)
    for off in (0x28, 0x2C, 0x30):
        st.put(BODY + off, "<I", FACES)
    for off in (0x14, 0x18, 0x1C, 0x20, 0x14C, 0x150):
        st.put(BODY + off, "<I", PRIMS + 0x84 * rng.randrange(0, 4))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CD4A0 + 0x10, rng.getrandbits(32)))
    if shared:
        for lod in (MODEL, MODEL + 0x100, MODEL + 0x200):
            for k, (c, _) in enumerate(runs):
                st.put(lod + 0x80 + 2 * k, "<H", c)
    else:
        for off in (0x14, 0x18, 0x1C, 0x20, 0x14C, 0x150):
            start = struct.unpack_from("<I", st.scr, BODY + off - SCR)[0]
            face = 0
            for c, it in runs:
                st.put(start + 0x84 * face + 0x82, "<H", c)
                face += it
    # face colours (flat, shared): runs of (palette index, length)
    for i in range(3):
        st.put(BODY + 0x180 + 4 * i, "<I", COLS + 2 * rng.randrange(0, 8))
    for i in range(0x200):
        st.put(COLS + 2 * i, "<H", rng.randrange(0, 0x100) << 8 | pick(rng, 0, 1, 2, rng.randrange(0, 8)))
    # textures
    st.put(0x6BFB58, "<I", TEXIDS)
    st.put(0x6BFB5C, "<I", TEXT)
    st.put(0x6BFB44, "<I", ANIM)
    st.put(0x6BFB4C, "<I", FRAMES)
    st.put(0x6C1E14, "<I", UVSETS)
    for i in range(3):
        st.put(BODY + 0x1A0 + 2 * i, "<H", rng.randrange(0, 0x40))
    for i in range(0x200):
        st.put(TEXIDS + 2 * i, "<H", (rng.randrange(0, 0x100) * 0x10) * 4 | rng.randrange(4))
    for i in range(0x100):
        e = TEXT + 0x10 * i
        if rng.randrange(2):
            st.put(e + 2, "<H", 0xFFFF)
        else:
            st.put(e + 2, "<H", pick(rng, 0, rng.randrange(0, 8), rng.randrange(0, 0x100)) << 8 |
                   rng.randrange(0, 0x100))
    for i in range(0x100):
        st.put(ANIM + 0x18 * i + 6, "<H", rng.randrange(0, 0x40))
    for i in range(0x40):
        st.put(FRAMES + 6 * i, "<h", rng.randrange(0, 0x40))
        st.put(FRAMES + 6 * i + 4, "<h", pick(rng, 0, 1, rng.getrandbits(16)))
    # the shared packet area
    st.put(0x6C1E18, "<I", SHARED)
    st.put(0x6C1E24, "<I", pick(rng, SHARED + 0x8000, SHARED + 0x84 * rng.randrange(0, 20)))
    # ordering tables
    for base in (0x6CD510, 0x6CDD80):
        for j in range(0x200):
            st.put(base + 4 * j, "<I", (HEADS + 4 * rng.randrange(0, 0x800)) | pick(rng, 0, 0x80000000))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1))
    st.put(0x6CCE94, "<I", pick(rng, 0, 0, 1))
    st.put(0x6CD404, "<I", pick(rng, 0x100, rng.randrange(0x40, 0x200)))
    st.put(OPTIONS + 0x31, "<B", pick(rng, 0, 1, 2))
    st.script["palette_get"] = [PAL]
    depth = pick(rng, rng.randrange(-100, 8000), rng.randrange(0, 40000), rng.getrandbits(32))
    return [BODY, depth]


def g_flat(st, rng, m):
    return g_model(st, rng, False)


def g_smooth(st, rng, m):
    return g_model(st, rng, True)


def g_missile(st, rng, m):
    st.put(ARG, "<I", BODY)
    none = rng.randrange(5) == 0
    for i in range(3):
        st.put(BODY + 0x34 + 4 * i, "<i", rng.getrandbits(32))
    for i in range(24):
        w = WEAP + 0x200 * i
        st.put(0x6B9CE0 + 4 * i, "<I", w)
        st.put(w + 0x188, "<I", pick(rng, 0, 1, 0x40, 0x4, rng.getrandbits(32)))
        st.put(w + 0x190, "<I", 0 if none else pick(rng, BODY, BODY, 0, rng.getrandbits(32)))
        for k in range(3):
            v = struct.unpack_from("<i", st.scr, BODY + 0x34 + 4 * k - SCR)[0]
            st.put(w + 0x34 + 4 * k, "<i", pick(rng, v + rng.randrange(-1 << 28, 1 << 28), rng.getrandbits(32),
                                                 v + rng.randrange(-1 << 22, 1 << 22)))
    return [ARG]


def g_ids(st, rng, m):
    def one():
        return pick(rng, 0, rng.randrange(1, 0x400))
    return [one() | one() << 10 | one() << 20 | rng.getrandbits(2) << 30, ARG]


def light_record(st, rng, a):
    st.put(a + 0xC, "<I", pick(rng, rng.getrandbits(32), rng.getrandbits(24), rng.getrandbits(14) << 10))


def g_lights_set(st, rng, m):
    for i in range(3):
        a = pick(rng, 0, 0x5D8000 + 0x40 * rng.randrange(0x400))
        st.put(ARG + 4 * i, "<I", a)
        if a:
            light_record(st, rng, a)
    return [ARG]


def g_camera(st, rng, m):
    for i in range(9):
        st.put(0x6CE5D0 + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001)))
        st.put(BODY + 0xC4 + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001), s16(rng)))
    for i in range(3):
        st.put(0x6CE5E4 + 4 * i, "<i", pick(rng, rng.randrange(-5000, 5000), rng.getrandbits(32)))
        st.put(BODY + 0x34 + 4 * i, "<i", pick(rng, rng.randrange(-5000, 5000) << 16, rng.getrandbits(32)))
    return [BODY]


def g_depth(st, rng, m):
    args = g_camera(st, rng, m)
    projection(st, rng)
    st.put(BODY + 0x10, "<I", MODEL)
    for i in range(3):
        st.put(MODEL + 0x28 + 2 * i, "<h", rng.randrange(-500, 500))
    return args


def g_draw_lit(st, rng, m):
    st.script["model_view_depth"] = [pick(rng, -1000, -1001, rng.randrange(-2000, 2000), rng.getrandbits(32))]
    return [BODY, rng.getrandbits(32)]


def g_uv(st, rng, m):
    return [ARG, pick(rng, *range(-1, 10), rng.getrandbits(32))]


def g_tint_byte(st, rng, m):
    return [rng.getrandbits(32), rng.getrandbits(32)]


def g_tint(st, rng, m):
    st.put(BODY + 0x12C, "<I", pick(rng, rng.getrandbits(32), rng.getrandbits(24), 0xFF000000 | rng.getrandbits(24)))
    return [BODY, ARG]


def light_world(st, rng, k):
    a = LIGHTS + 0x20 * k
    for i in range(3):
        v = struct.unpack_from("<i", st.scr, BODY + 0x34 + 4 * i - SCR)[0]
        st.put(a + 4 * i, "<i", pick(rng, v + rng.randrange(-0x40000000, 0x40000000),
                                     v + rng.randrange(-0x1000000, 0x1000000), rng.getrandbits(32)))
    st.put(a + 0x10, "<B", pick(rng, rng.randrange(0, 16), rng.getrandbits(8)))
    return a


def strength(st, rng):
    st.put(0x4D4B9C, "<I", STRENGTH)
    for i in range(0x400):
        st.put(STRENGTH + 4 * i, "<i", pick(rng, rng.randrange(0, 0x8000), rng.randrange(0, 0x100), rng.getrandbits(32)))


def g_direction(st, rng, m):
    for i in range(3):
        st.put(BODY + 0x34 + 4 * i, "<i", rng.getrandbits(32))
    strength(st, rng)
    return [BODY + 0x34, light_world(st, rng, 0), ARG + 0x40]


def g_lights(st, rng, m):
    for i in range(3):
        st.put(BODY + 0x34 + 4 * i, "<i", rng.getrandbits(32))
    for i in range(9):
        st.put(BODY + 0xC4 + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001), s16(rng)))
    strength(st, rng)
    for i in range(3):
        st.put(ARG + 4 * i, "<I", pick(rng, 0, light_world(st, rng, i)))
    st.put(BODY, "<I", pick(rng, 0, 0x800, rng.getrandbits(32)))
    cell = pick(rng, 0xFFFFFFFF, rng.randrange(0, 0x100) | (rng.randrange(-8, 16) & 0xFF) << 8 |
                rng.randrange(0, 16) << 16)
    st.put(BODY + 0x11C, "<I", cell)
    st.put(BODY + 0x12C, "<I", pick(rng, rng.getrandbits(32), rng.getrandbits(24), 0xFF000000 | rng.getrandbits(24)))
    st.put(0x6BFAC0, "<I", MAPS)
    for i in range(0x100):
        st.put(MAPS + 4 * i, "<I", MAPS + 0x400)
    st.put(MAPS + 0x400 + 8, "<i", rng.randrange(0, 16))
    st.put(MAPS + 0x400 + 0x20, "<I", MAPS + 0x800)
    st.script["palette_get"] = [PAL]
    return [BODY, ARG]


def g_hit(st, rng, m):
    for i in range(3):
        st.put(ARG + 0x100 + 0x5C + 4 * i, "<i", pick(rng, rng.randrange(-0x40000, 0x40000), rng.getrandbits(32),
                                                      0x30000, -0x30000, 0x2FFFF))
    return [BODY, pick(rng, 0, 0, BODY + 0x200), ARG + 0x100]


def g_trigger(st, rng, m):
    st.put(BODY, "<I", pick(rng, 0x2000, 0, rng.getrandbits(32)))
    return [BODY]


TESTS = {
    "missile_lock_level": (0x458760, g_missile, set()),
    "light_colours_from_ids": (0x459CB0, g_ids, {0x428940}),
    "model_camera_matrix_set": (0x459E80, g_camera, set()),
    "model_draw_lit": (0x45A260, g_draw_lit, {0x45A2A0, 0x45B400, 0x45A430}),
    "model_view_depth": (0x45A2A0, g_depth, set()),
    "prim_uv_rotate": (0x45A2F0, g_uv, set()),
    "model_faces_draw": (0x45A430, g_flat, {0x486350, 0x45AB20}),
    "model_faces_draw_smooth": (0x45AB20, g_smooth, set()),
    "light_colours_set": (0x45B200, g_lights_set, {0x428940}),
    "body_tint_approach": (0x45B390, g_tint, set()),
    "model_lights_set": (0x45B400, g_lights, {0x486350}),
    "tint_byte_approach": (0x45B9F0, g_tint_byte, set()),
    "light_direction_at": (0x45BA30, g_direction, set()),
    "k5_object_hit": (0x45BB70, g_hit, {0x45BE10}),
    "k5_object_trigger": (0x45BBF0, g_trigger, {0x40EE60, 0x45BD30, 0x42AA30}),
}

SIZES = {0x45BB70: 0x80}
RETURNS = {0x458760, 0x45A2A0, 0x45B9F0, 0x45BB70, 0x45BBF0}
SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.uc.reg_write(UC_X86_REG_EBX, 0x11111111)
    m.uc.reg_write(UC_X86_REG_ESI, 0x22222222)
    m.uc.reg_write(UC_X86_REG_EDI, 0x33333333)
    m.uc.reg_write(UC_X86_REG_EBP, 0x44444444)


def run_case(m, stubs, name, address, gen, active, rng):
    st = State(rng)
    args = gen(st, rng, m)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script, active)
        r = m.call(target, args)
        if r[0] == "ok":
            r = (r if address in RETURNS else r[:1]) + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        # 0x6ccd74, the transform's current record: a stack address
        record = struct.unpack("<I", rd(m, 0x6CCD74, 4))[0]
        if E.STACK <= record < E.STACK + E.STACK_SIZE:
            m.write(0x6CCD74, b"STAK")
        mem = rd(m, SCR, SCR_SIZE) + b"".join(rd(m, a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = None
        if first is not None:
            if first < SCR_SIZE:
                where = SCR + first
            else:
                off = first - SCR_SIZE
                for ra, n in REGIONS:
                    if off < n:
                        where = ra + off
                        break
                    off -= n
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True, None
    return None, results[0][0][0] != "ok", results[0][2]


def func_sizes():
    sizes = dict(SIZES)
    try:
        for line in open("build/groups/pk_owned.txt"):
            if not line.startswith("#"):
                parts = line.split()
                sizes[int(parts[0], 16)] = int(parts[2])
    except OSError:
        pass
    return sizes


def instruction_addresses(address, size):
    out = set()
    try:
        for line in open("build/rollcage.asm"):
            parts = line.split(":", 1)
            if len(parts) == 2:
                try:
                    a = int(parts[0], 16)
                except ValueError:
                    continue
                if address <= a < address + size:
                    out.add(a)
    except OSError:
        pass
    return out


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(61)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, active) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        seen = set()
        size = sizes.get(address, 0)
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg, fault, log = run_case(m, stubs, name, address, gen, active, rng)
            faults += fault and not msg
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if "-c" in sys.argv and size:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:12]}" if missed else "") + "]"
        both = f" ({faults} fault the same way)" if faults else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{cov}", flush=True)
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
