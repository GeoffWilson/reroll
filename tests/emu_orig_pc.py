#!/usr/bin/env python3
"""Differential test of orig_pc.c (group PC: the views, the GTE emulation's
state and float transforms, ordering tables, the smoke clouds, 0x4231f0-
0x42b9a0) against the original, on random state.

Callees outside the group that touch DirectX, the debug output or the
network are logging stubs in both runs (the call log records as many
arguments as the callee reads; packets, rectangles and views passed by
pointer are logged by content): draw_otag / draw_prim, the projection
0x417ed0, the viewport 0x41f2d0, the render-state setters behind the thunks,
the clear colour 0x41ec40, the view keep / viewport apply 0x426600 /
0x4265d0, the texture surface calls, the debug printf, the network slot.
Pure game code runs for real in both: the transform pointers' targets
(0x426a20 / 0x426b00, the 3DNow! 0x4268c0 / 0x4291f0), sin / cos
tables, fix_div8, the C runtime's _ftol and sqrt. The emulated CPU is a
Phenom (3DNow!). The x87 control word is 0x27f, 0x37f or 0x07f; besides
memory, the return value, the call log, ebx / esi / edi / ebp, the x87
stack top and tag word are compared. Functions returning in st(0) run from a
trampoline that stores st(0) (80 bits); 0x426cd0 runs paired with 0x426dc0
as the game calls them.

    tests/emu_orig_pc.py EXE build/emu_pc.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import unicorn  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_CPU_X86_PHENOM, UC_X86_REG_CR4, UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW,
                               UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402


def _phenom_uc(arch, mode):
    uc = unicorn.Uc(arch, mode)
    uc.ctl_set_cpu_model(UC_CPU_X86_PHENOM)  # 3DNow!
    return uc


E.Uc = _phenom_uc

SCR = 0x01000000
SCR_SIZE = 0x20000
RACE = SCR + 0x0000  # 0x1000
ARG = SCR + 0x1000  # argument blocks (0x1000)
STR = SCR + 0x2000
RES = SCR + 0x2200  # st(0) stored by the trampolines
TRACK = SCR + 0x3000
TEX = SCR + 0x4000  # 0x2000
CARS = SCR + 0x6000  # 8 x 0x1500
CAR_SIZE = 0x1500
OTS = SCR + 0x11000  # 0x1000
SMOKE = SCR + 0x12000  # 5 x 0x1e2c
TRAMP = 0x00800000  # trampoline code

REGIONS = [
    (0x4ACC38, 8),
    (0x4B1960, 4),
    (0x4BDE00, 0x320),
    (0x6BFB80, 0x13A0),
    (0x6C1E14, 4),
    (0x6C1EB0, 4),
    (0x6CC900, 0x1E00),
]


def S(name, kinds):
    return (name, kinds)


# argument kinds: v value, s string, p 0x6c-byte packet, w 0x5c-byte view, r 8-byte rectangle
STUBS = {
    0x428470: S("draw_otag", "v"),
    0x428460: S("draw_prim", "p"),
    0x417ED0: S("projection", "vvvvv"),
    0x41F2D0: S("viewport", "vvvv"),
    0x41DD90: S("filter_set", ""),
    0x41DE40: S("fog_on", ""),
    0x41DE60: S("fog_off", ""),
    0x41DE80: S("zbuffer_on", ""),
    0x41EC40: S("clear_colour", "vvv"),
    0x426600: S("view_keep", "w"),
    0x4265D0: S("viewport_apply", "r"),
    0x41FEF0: S("surface_lock", "vvvvvv"),
    0x420000: S("surface_unlock", "vvv"),
    0x41E710: S("surface_upload", "vvvvvv"),
    0x4205F0: S("handle_new", ""),
    0x4219C0: S("f4219c0", ""),
    0x41E190: S("f41e190", ""),
    0x41E040: S("f41e040", "v"),
    0x41E170: S("f41e170", ""),
    0x496340: S("debug_printf", "s"),
    0x48CF20: S("net_slot", ""),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def mk_float(rng):
    k = rng.randrange(10)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 100.0, 640.0, 480.0, 3.0]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FA00000, 1, 0x80000001, 0x00800000,
                           0x7F7FFFFF, 0xFF7FFFFF])
    if k == 2:
        return rng.getrandbits(32)
    return fbits(rng.uniform(-1000.0, 1000.0))


def pick(rng, *choices):
    return rng.choice(choices)


def s16(rng, scale=0x8000):
    return rng.randrange(-scale, scale) & 0xFFFF


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.code = None  # trampoline bytes (original target, ours)
        self.put(0x4B1960, "<I", RACE)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        if SCR <= address < SCR + SCR_SIZE:
            return self.sput(address, fmt, value)
        a = self.find(address)
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.mem[a], address - a, value)

    def putb(self, address, data):
        if SCR <= address < SCR + SCR_SIZE:
            self.scr[address - SCR:address - SCR + len(data)] = data
            return
        a = self.find(address)
        self.mem[a][address - a:address - a + len(data)] = data

    def sput(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.scr, address - SCR, value)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind == "s":
                logged.append(rd(self.m, a, 0x100).split(b"\0")[0])
            elif kind == "p":
                logged.append(rd(self.m, a, 0x6C))
            elif kind == "w":
                logged.append(rd(self.m, a, 0x5C))
            elif kind == "r":
                logged.append(rd(self.m, a, 8))
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- state helpers

def set_gte_float(st, rng):
    """Float matrix rows, translation, projection and clip parameters."""
    rows = (0x6CCD1C, 0x6CCD2C, 0x6CCD3C)
    for r in rows:
        for k in range(3):
            st.put(r + 4 * k, "<I", fbits(rng.uniform(-1.0, 1.0)))
    st.put(0x6CCD0C, "<I", fbits(rng.uniform(-2000, 2000)))
    st.put(0x6CCD10, "<I", fbits(rng.uniform(-2000, 2000)))
    st.put(0x6CCD14, "<I", fbits(rng.uniform(-500, 3000)))
    st.put(0x6CCD00, "<I", fbits(pick(rng, 400.0, rng.uniform(100, 800))))
    st.put(0x6CCD04, "<I", fbits(pick(rng, 1.0, 16.0, rng.uniform(0.5, 50))))
    st.put(0x6CCD08, "<I", fbits(pick(rng, 20000.0, rng.uniform(500, 30000))))
    for a in (0x6CCD4C, 0x6CCD50, 0x6CCD5C, 0x6CCD60):
        st.put(a, "<I", fbits(rng.uniform(0, 400)))
    st.put(0x6CCD64, "<I", fbits(pick(rng, 0.0, rng.uniform(-10, 100))))
    st.put(0x6CCD68, "<I", fbits(pick(rng, 640.0, rng.uniform(300, 800))))
    st.put(0x6CCD6C, "<I", fbits(pick(rng, 0.0, rng.uniform(-10, 100))))
    st.put(0x6CCD70, "<I", fbits(pick(rng, 480.0, rng.uniform(200, 600))))
    st.put(0x6CCC40, "<I", rng.randrange(0, 400) << 16 | rng.getrandbits(16))
    st.put(0x6CCC44, "<I", rng.randrange(0, 300) << 16 | rng.getrandbits(16))
    if rng.randrange(12) == 0:
        for a in [r + 4 * k for r in rows for k in range(3)] + [0x6CCD0C, 0x6CCD10, 0x6CCD14, 0x6CCD00, 0x6CCD04,
                                                                0x6CCD08, 0x6CCD64, 0x6CCD68]:
            if rng.randrange(3) == 0:
                st.put(a, "<I", mk_float(rng))


def set_gte_vector(st, rng):
    scale = pick(rng, 0x8000, 3000, 500)
    st.put(0x6CCC64, "<H", s16(rng, scale))
    st.put(0x6CCC66, "<H", s16(rng, scale))
    st.put(0x6CCC68, "<H", s16(rng, scale))


def set_view_struct(st, rng, address, scale=4096):
    for k in range(9):
        st.put(address + 2 * k, "<H", s16(rng, scale))
    st.put(address + 0x14, "<i", rng.randrange(-3000, 3000))
    st.put(address + 0x18, "<i", rng.randrange(-3000, 3000))
    st.put(address + 0x1c, "<i", rng.randrange(-1000, 5000))


def set_paths(st, rng):
    path = pick(rng, (0x426A20, 0x426B00), (0x426A20, 0x426B00), (0x4268C0, 0x4291F0))  # the two set-ups (0x426090)
    st.put(0x6CCD78, "<I", path[0])
    st.put(0x6CCD80, "<I", path[1])


def set_ot(st, base, n, rng):
    for k in range(n):
        st.put(base + 4 * k, "<I", 0xFFFFFFFF if k == 0 else (base + 4 * (k - 1)) | pick(rng, 0x80000000, 0x80000000, 0))


# ---------------------------------------------------------------- generators

def g_none(st, rng, m):
    return [], None


def g_ret(st, rng, m):
    return [], 0xFFFFFFFF


def g_cars(st, rng, m):
    set_gte_float(st, rng)
    set_paths(st, rng)
    set_view_struct(st, rng, 0x6CE5D0, pick(rng, 4096, 0x8000))
    st.sput(RACE + 0x54D, "<B", pick(rng, 0, 1, 2, 4, 6, 8, 8))
    for i in range(8):
        car = CARS + i * CAR_SIZE
        st.put(0x6BFB80 + i * 0x274, "<I", car)
        for k, off in enumerate((0x34, 0x38, 0x3C)):
            v = rng.randrange(-3000, 3000) if k != 2 else rng.randrange(-1000, 6000)
            st.sput(car + off, "<I", (v << 16 | rng.getrandbits(16)) & 0xFFFFFFFF)
    flags = rng.getrandbits(32) & ~0x780
    flags |= rng.randrange(0, 9) << 7
    st.put(0x6CCE40, "<I", flags)
    return [], None


def g_smoke(st, rng, m):
    set_gte_float(st, rng)
    set_paths(st, rng)
    st.put(0x6CCB54, "<I", SMOKE)
    for i in range(5):
        r = SMOKE + i * 0x1E2C
        st.sput(r + 0x0, "<i", rng.randrange(-2000, 2000) << 16 | rng.getrandbits(16))
        st.sput(r + 0x4, "<i", rng.randrange(-500, 500) << 16 | rng.getrandbits(16))
        st.sput(r + 0x8, "<i", pick(rng, rng.randrange(-500, 4000), rng.randrange(20000, 32000)) << 16 |
                rng.getrandbits(16))
        st.sput(r + 0xC, "<I", pick(rng, 0, 1, 2, rng.randrange(3, 60), rng.randrange(3, 60), 0x80000005))
        st.sput(r + 0x10, "<I", rng.randrange(0, 12))
        st.sput(r + 0x18, "<I", pick(rng, 0x86, 0x91, rng.randrange(0, 0x90)))
        st.sput(r + 0x1C, "<I", rng.randrange(1, 12))
        st.sput(r + 0x20, "<I", rng.randrange(1, 40))
        st.sput(r + 0x24, "<I", rng.randrange(0, 80))
        st.sput(r + 0x28, "<I", pick(rng, rng.randrange(0, 0x400), rng.randrange(0, 0x4000), rng.getrandbits(16)))
    st.put(0x6C1EB0, "<I", TRACK)
    st.sput(TRACK + 0x2E, "<H", rng.randrange(0, 0x40))
    st.put(0x6C1E14, "<I", TEX)
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CDD10, rng.getrandbits(32)))
    st.put(0x6CCD84, "<I", rng.getrandbits(32))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    set_ot(st, 0x6CD510, 0x200, rng)
    set_ot(st, 0x6CDD80, 0x200, rng)
    return [], None


def g_frame_flush(st, rng, m):
    st.put(0x6CE608, "<I", pick(rng, 0x6CE5F0, 0x6CE5F8, 0x6CE5F0, 0x6CE5F8, 0x6CE600))
    return [], None


def g_ptr(st, rng, m):
    return [ARG], None


def g_ptr2(st, rng, m):
    return [ARG, ARG + 0x200], None


def g_camera_build(st, rng, m):
    set_view_struct(st, rng, 0x6CD480, pick(rng, 4096, 0x8000))
    set_view_struct(st, rng, ARG + 0x100, pick(rng, 4096, 0x8000))
    for k in range(3):
        st.sput(ARG + 0x200 + 4 * k, "<I", pick(rng, rng.getrandbits(32), rng.randrange(-3000, 3000) & 0xFFFFFFFF))
    return [ARG, ARG + 0x100, ARG + 0x200], None


def g_camera_load(st, rng, m):
    set_view_struct(st, rng, ARG, pick(rng, 4096, 0x8000))
    if rng.randrange(4) == 0:
        st.sput(ARG + 0x14, "<I", rng.getrandbits(32))
    return [ARG], None


def zoom(rng):
    return pick(rng, 0x96, 0x100, 0x68, 0x67, 0, 1, rng.randrange(0, 0x400), rng.getrandbits(16),
                rng.getrandbits(32))


def set_screen(st, rng):
    st.put(0x4ACC38, "<I", pick(rng, 640, 800, 1024, 320, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600, 768, 240, rng.randrange(1, 4000), rng.getrandbits(32)))


def g_zoom(st, rng, m):
    set_screen(st, rng)
    return [zoom(rng)], None


def g_full_screen(st, rng, m):
    st.sput(RACE + 0x552, "<B", pick(rng, 0, 0, 1, 2, 4))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CDD10, ARG))
    return [], 0xFFFFFFFF


def g_two_values(st, rng, m):
    return [pick(rng, rng.randrange(-1000, 1000), rng.getrandbits(32)),
            pick(rng, rng.randrange(-1000, 1000), rng.getrandbits(32))], None


def g_matrix_arg(st, rng, m):
    for k in range(10):
        st.sput(ARG + 2 * k, "<H", s16(rng, pick(rng, 4096, 0x8000)))
    for k in range(3):
        st.sput(ARG + 0x14 + 4 * k, "<I", pick(rng, rng.getrandbits(32), rng.randrange(-5000, 5000) & 0xFFFFFFFF))
    return [ARG], None


def g_transform(st, rng, m):
    set_gte_float(st, rng)
    set_gte_vector(st, rng)
    return [], None


def g_transform_rec(st, rng, m):
    set_gte_float(st, rng)
    set_gte_vector(st, rng)
    return [ARG], None


def g_light_float(st, rng, m):
    set_gte_vector(st, rng)
    for base in (0x6CCDD0, 0x6CCDE0, 0x6CCDF0, 0x6CCE00, 0x6CCE10, 0x6CCE20):
        for k in range(3):
            st.put(base + 4 * k, "<I", fbits(rng.uniform(-1.5, 1.5)) if rng.randrange(12) else mk_float(rng))
    for a in (0x6CCE30, 0x6CCE34, 0x6CCE38):
        st.put(a, "<I", fbits(rng.uniform(0, 4095)) if rng.randrange(12) else mk_float(rng))
    return [], None


def rand_vertex(st, rng, address):
    st.sput(address + 0x0, "<I", pick(rng, 0, rng.getrandbits(32)))
    st.sput(address + 0x4, "<I", fbits(pick(rng, rng.uniform(-50, 5000), rng.uniform(0.5, 20.0))) if rng.randrange(10)
            else mk_float(rng))
    for off in (0x8, 0xC, 0x10, 0x14):
        st.sput(address + off, "<I", fbits(rng.uniform(-1000, 1000)) if rng.randrange(10) else mk_float(rng))


def g_facing(st, rng, m):
    for k in range(3):
        rand_vertex(st, rng, ARG + 0x20 * k)
    return [ARG, ARG + 0x20, ARG + 0x40], None


def g_vertex(st, rng, m):
    set_gte_float(st, rng)
    rand_vertex(st, rng, ARG)
    return [ARG], None


def g_normalise(st, rng, m):
    for k in range(3):
        st.sput(ARG + 4 * k, "<i", pick(rng, rng.randrange(-0x1000, 0x1000), rng.randrange(-0x100000, 0x100000),
                                        rng.getrandbits(32), 0))
    if rng.randrange(20) == 0:
        st.sput(ARG, "<i", 0), st.sput(ARG + 4, "<i", 0), st.sput(ARG + 8, "<i", 0)
    return [ARG, pick(rng, ARG + 0x40, ARG)], 0xFFFFFFFF


def g_rotate_vec(st, rng, m):
    g_matrix_arg(st, rng, m)
    for k in range(3):
        st.sput(ARG + 0x100 + 2 * k, "<H", s16(rng))
    return [ARG, ARG + 0x100, ARG + 0x200], None


def g_mul_into(st, rng, m):
    g_matrix_arg(st, rng, m)
    for k in range(9):
        st.sput(ARG + 0x100 + 2 * k, "<H", s16(rng, pick(rng, 4096, 0x8000)))
    return [ARG, pick(rng, ARG + 0x100, ARG + 0x100, ARG)], None


def g_light_int(st, rng, m):
    for k in range(9):
        st.put(0x6CCC20 + 2 * k, "<H", s16(rng, pick(rng, 4096, 0x8000, 512, 64)))
    for a in (0x6CCC88, 0x6CCC8C, 0x6CCC90):
        st.put(a, "<I", pick(rng, s16(rng, 4096), rng.getrandbits(32)))
    for a in (0x6CCC14, 0x6CCC18, 0x6CCC1C):
        st.put(a, "<I", pick(rng, rng.randrange(0, 0x1000), rng.getrandbits(32)))
    if rng.randrange(3) == 0:  # near saturation: no light, back colour about 0x1000, colour 0xff
        for k in range(9):
            st.put(0x6CCC20 + 2 * k, "<H", 0)
        for a in (0x6CCC14, 0x6CCC18, 0x6CCC1C):
            st.put(a, "<I", rng.randrange(0xFF0, 0x1012))
        for a in (0x6CCC7C, 0x6CCC7D, 0x6CCC7E):
            st.put(a, "<B", pick(rng, 0xFF, 0xFE, rng.getrandbits(8)))
    return [], None


def g_nclip(st, rng, m):
    for k in range(6):
        st.put(0x6CCC94 + 2 * k, "<H", s16(rng, pick(rng, 1000, 0x8000)))
    return [], 0xFFFFFFFF


def g_light_at(st, rng, m):
    g_light_float(st, rng, m)
    st.sput(ARG, "<I", rng.getrandbits(32))
    st.sput(ARG + 4, "<I", rng.getrandbits(32))
    st.sput(ARG + 0x20, "<I", rng.getrandbits(32))
    return [ARG, ARG + 0x20, ARG + 0x40], None


def g_ot_clear(st, rng, m):
    return [OTS + 4 * rng.randrange(0, 8), pick(rng, 2, 3, 0x10, rng.randrange(2, 0x200))], 0xFFFFFFFF


def g_ot_add(st, rng, m):
    entry = OTS + 4 * rng.randrange(0, 0x100)
    return [entry | pick(rng, 0, 0x80000000), ARG + 4 * rng.randrange(0, 0x100)], 0xFFFFFFFF


def g_zbuffer(st, rng, m):
    st.put(0x6CCE7C, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    return [], 0xFFFFFFFF


def g_debug_line(st, rng, m):
    text = rng.choice([b"", b"\n", b"\r\n", b"abc\r\n", b"abc\n", b"x\r", b"\n\r", b"line", b"a\r\n\n"])
    text += rng.choice([b"", b"\n", b"\r\n"]) if rng.randrange(3) == 0 else b""
    st.scr[STR - SCR:STR - SCR + len(text) + 1] = text + b"\0"
    st.script["debug_printf"] = [rng.getrandbits(32)]
    return [STR], 0xFFFFFFFF


def g_three(st, rng, m):
    st.script["clear_colour"] = [rng.getrandbits(32)]
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], 0xFFFFFFFF


def g_depth_range(st, rng, m):
    a = pick(rng, rng.randrange(0, 0x400), rng.getrandbits(32), 0x96)
    b = pick(rng, rng.randrange(0, 0x400), rng.getrandbits(32), a, 400)
    return [a, b, rng.getrandbits(32)], 0xFFFFFFFF


def g_five(st, rng, m):
    return [rng.getrandbits(32) for _ in range(5)], None


def rect(rng):
    return [rng.randrange(-50, 700) & 0xFFFF, rng.randrange(-50, 500) & 0xFFFF,
            pick(rng, rng.randrange(0, 700), rng.getrandbits(16), 0) & 0xFFFF,
            pick(rng, rng.randrange(-5, 500), rng.getrandbits(16), 0) & 0xFFFF]


def g_border(st, rng, m):
    set_screen(st, rng)
    st.putb(ARG, struct.pack("<4H", *rect(rng)))
    st.putb(ARG + 0x100, struct.pack("<4H", *rect(rng)))
    return [ARG, ARG + 0x100, pick(rng, 0, 1, rng.getrandbits(32))], None


def g_viewport(st, rng, m):
    st.putb(ARG, struct.pack("<4H", *rect(rng)))
    st.put(0x6CCE7C, "<I", pick(rng, 0, 1))
    return [ARG, pick(rng, 0, 1, 2, rng.randrange(0, 20), rng.getrandbits(32))], None


def g_push(st, rng, m):
    st.script["net_slot"] = [rng.getrandbits(32)]
    st.sput(RACE + 0x58B, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    st.put(0x6CCD08, "<I", mk_float(rng))
    return [], None


def g_pop(st, rng, m):
    set_screen(st, rng)
    st.sput(RACE + 0x58B, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    st.put(0x4BDEFC, "<I", pick(rng, fbits(rng.uniform(0, 1000)), mk_float(rng)))
    return [], None


def g_far_plane(st, rng, m):
    return [pick(rng, fbits(rng.uniform(0, 40000)), fbits(20000.0), mk_float(rng))], None


def g_six(st, rng, m):
    for name in ("surface_lock", "surface_upload", "surface_unlock", "handle_new", "f4219c0"):
        st.script[name] = [rng.getrandbits(32)]
    return [rng.getrandbits(32) for _ in range(6)], 0xFFFFFFFF


def g_thunk(st, rng, m):
    for name in ("handle_new", "f4219c0"):
        st.script[name] = [rng.getrandbits(32)]
    return [], 0xFFFFFFFF


def g_store(st, rng, m):
    return [ARG + 4 * rng.randrange(0, 0x100), rng.getrandbits(32)], None


def g_transpose(st, rng, m):
    g_matrix_arg(st, rng, m)
    return [ARG, pick(rng, ARG + 0x100, ARG + 4)], None


# mode: "c" plain call; "st0" call from a trampoline that stores st(0) at RES; "pair" a call then 0x426dc0's
TESTS = {
    "cars_view_flags_update": (0x4231F0, g_cars, "c"),
    "ot_frame_flush": (0x423C40, g_frame_flush, "c"),
    "view_matrix_store": (0x423D10, g_ptr, "c"),
    "view_camera_build": (0x423D70, g_camera_build, "c"),
    "view_camera_load": (0x423DF0, g_camera_load, "c"),
    "view_lod_set": (0x423E10, g_zoom, "c"),
    "view_zoom_set": (0x423F00, g_zoom, "c"),
    "view_full_screen": (0x423F20, g_full_screen, "c"),
    "gte_screen_offset_set": (0x426200, g_two_values, "c"),
    "gte_screen_offset_get": (0x426240, g_ptr2, "c"),
    "projection_h_set": (0x426260, g_zoom, "c"),
    "gte_matrix_load": (0x426330, g_matrix_arg, "c"),
    "gte_matrix_save": (0x426480, g_ptr, "c"),
    "gte_translation_load": (0x426530, g_matrix_arg, "c"),
    "view_rect_saved_get": (0x426590, g_ptr, "c"),
    "view_saved_get": (0x4265B0, g_ptr, "c"),
    "gte_rt_float": (0x426620, g_transform, "c"),
    "gte_rtps_float": (0x4266D0, g_transform, "c"),
    "gte_rtps_3dnow": (0x4268C0, g_transform_rec, "c"),
    "gte_rt_depth": (0x426CD0, g_transform_rec, "pair"),
    "matrix_rotate_vec": (0x426E10, g_rotate_vec, "c"),
    "matrix_mul_into": (0x426E90, g_mul_into, "c"),
    "matrix_translation_set": (0x426F60, g_ptr2, "c"),
    "gte_light_colour": (0x427100, g_light_int, "c"),
    "gte_light_colour_3dnow": (0x427250, g_light_float, "c"),
    "gte_light_colour_float": (0x427400, g_light_float, "c"),
    "gte_nclip": (0x4278A0, g_nclip, "c"),
    "vec_facing_float": (0x4278F0, g_facing, "st0"),
    "ot_clear_forward": (0x427A40, g_ot_clear, "c"),
    "ot_clear_reverse": (0x427A70, g_ot_clear, "c"),
    "ot_add_prim": (0x427AB0, g_ot_add, "c"),
    "filter_set_thunk": (0x428240, g_none, "c"),
    "fog_on_thunk": (0x428250, g_none, "c"),
    "fog_off_thunk": (0x428260, g_none, "c"),
    "zbuffer_restore": (0x428270, g_zbuffer, "c"),
    "debug_line_print": (0x428310, g_debug_line, "c"),
    "clear_colour_store": (0x428390, g_three, "c"),
    "depth_range_set": (0x4283D0, g_depth_range, "c"),
    "view_border_draw": (0x4284B0, g_border, "c"),
    "view_viewport_apply": (0x428770, g_viewport, "c"),
    "vertex_project_codes": (0x4288C0, g_vertex, "c"),
    "vertex_project_float": (0x428900, g_vertex, "c"),
    "light_matrix_load": (0x428940, g_matrix_arg, "c"),
    "render_flags_set": (0x428A50, g_five, "c"),
    "projection_push": (0x428A80, g_push, "c"),
    "projection_pop": (0x428B00, g_pop, "c"),
    "far_plane_set": (0x428B90, g_far_plane, "c"),
    "far_plane_get": (0x428BD0, g_push, "st0"),
    "matrix_transpose_s16": (0x428BE0, g_transpose, "c"),
    "texture_surface_lock": (0x428C30, g_six, "c"),
    "texture_surface_unlock": (0x428C60, g_six, "c"),
    "texture_handle_new": (0x428C80, g_thunk, "c"),
    "texture_surface_upload": (0x428C90, g_six, "c"),
    "dword_store": (0x4290A0, g_store, "c"),
    "vec_normalise_x87": (0x4290B0, g_normalise, "c"),
    "dx_error_ignore": (0x4291F0, g_none, "c"),
    "light_colour_at": (0x4292B0, g_light_at, "c"),
    "textures_unusable_thunk": (0x429340, g_thunk, "c"),
    "smoke_clouds_draw": (0x42B9A0, g_smoke, "c"),
}


def trampoline(mode, target, pair_target):
    """Code that copies three stack arguments and calls target; "st0" then
    stores st(0) (80 bits) at RES; "pair" then calls pair_target (eax kept)."""
    code = b"\xff\x74\x24\x0c" * 3  # push the caller's three arguments
    code += b"\xb9" + struct.pack("<I", target) + b"\xff\xd1" + b"\x83\xc4\x0c"
    if mode == "st0":
        code += b"\xdb\x3d" + struct.pack("<I", RES)
    else:
        code += b"\x50\xb9" + struct.pack("<I", pair_target) + b"\xff\xd1\x58"
    return code + b"\xc3"


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


SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, name, address, gen, mode, rng):
    st = State(rng)
    args, mask = gen(st, rng, m)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for k, target in enumerate((address, m.symbols[name])):
        apply(m, st, cw)
        stubs.reset(st.script)
        if mode != "c":  # a slot per function and side: Unicorn keeps translated code
            pair = (0x426DC0, m.symbols["gte_project_pending"])[k]
            slot = TRAMP + (list(TESTS).index(name) * 2 + k) * 0x40
            m.write(slot, trampoline(mode, target, pair))
            target = slot
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
            r = r + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        last = struct.unpack("<I", rd(m, 0x6CCD74, 4))[0]
        if on_stack(last):  # the transforms' record pointer: a stack buffer in both
            m.write(0x6CCD74, b"STK!")
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
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True
    return None, results[0][0][0] != "ok"


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pc_owned.txt"):
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
    m.uc.reg_write(UC_X86_REG_CR4, m.uc.reg_read(UC_X86_REG_CR4) | 0x600)  # SSE on (OSFXSR / OSXMMEXCPT)
    m.uc.mem_map(SCR, SCR_SIZE)
    m.uc.mem_map(TRAMP, 0x2000)
    stubs = Stubs(m)
    rng = random.Random(31)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, mode) in TESTS.items():
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
            msg, fault = run_case(m, stubs, name, address, gen, mode, rng)
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
