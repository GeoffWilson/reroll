#!/usr/bin/env python3
"""Differential test of orig_pr.c (group PR: the warp / ice overlays, the
sprite drawing file 0x47d400-0x47f1a2 and the cars' light trails 0x4821d0)
against the original, on random state.

Callees outside the group that touch DirectDraw, files, allocation or other
groups' state are logging stubs in both runs (the log records as many
arguments as the callee reads; buffers by content, stack pointers as
"stack") with scripted results: the texture lock / unlock / handle / upload
(0x428c30 / 60 / 80 / 90), the BMP loader 0x477530, the allocator 0x476b80,
the palette converter 0x41e1f0, the screen tint / flash / fade (0x42bfc0,
0x42c040, 0x42bf20), the grid unlink 0x4612a0. Pure code runs for real in
both: the transform / projection pointers' targets (0x426a20 / 0x426be0 /
0x426b00), 0x426cd0 / 0x426dc0, the grid record 0x4613e0, fix_sqrt_table,
gte_matrix_save, the vector helpers, the C runtime's sqrt / pow / sin / atan
/ _ftol. The group's own functions run for real in both (the original's call
the original's, ours call ours).

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those two runs (stack leftovers,
e.g. the GTE vector's fourth word 0x6ccc6a) are masked before the comparison
with ours. Compared: the return value, ebx / esi / edi / ebp, the x87 stack
top / tag word, the globals and scratch memory, the call log. The x87
control word is 0x27f, 0x37f or 0x07f.

    tests/emu_orig_pr.py EXE build/emu_pr.elf [iterations] [names...] [-v] [-c]
"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x58000
TABLE = SCR + 0x00000  # warp table (0x10004)
PIX = SCR + 0x10100  # warp picture 256 x 256
PAL = SCR + 0x20100  # picture palette (0x400)
IPIX = SCR + 0x20500  # ice picture 128 x 128
IPAL = SCR + 0x24500  # ice palette (0x400)
SURF = SCR + 0x24900  # texture surface (0x11000)
CARS = SCR + 0x35900  # 3 cars of 0x2200
CAR_SIZE = 0x2200
GRID = SCR + 0x4C000  # grid pool: sprites 0x200 apart (32 + slack)
TEXT = SCR + 0x3DC00  # 0x6c1e14 texture table (0x900)
TEXH = SCR + 0x3E600  # *0x6c1eb0
OT = SCR + 0x3E700  # ordering table entries (0x800)
LIST = SCR + 0x3EF00  # sprite list records
SECT = SCR + 0x3F000  # section pointer table + sections + index tables
OBJIDX = SCR + 0x3F800  # u16 object indices
OBJTAB = SCR + 0x3FC00  # 12-byte object entries (0x300)
ARG = SCR + 0x40000  # vectors, out buffers, strings (0x2000)
STUBPAGE = 0x02200000

REGIONS = [(0x4AC000, 0x2A000), (0x5D6000, 0x4000), (0x6B9000, 0x1000), (0x6BF000, 0x3000),
           (0x6CB000, 0x4000)]

# cdecl callees at their own addresses
STUBS = {
    0x428C30: "tex_lock",
    0x428C60: "tex_unlock",
    0x428C80: "tex_handle",
    0x428C90: "tex_upload",
    0x477530: "bmp_load",
    0x476B80: "alloc",
    0x41E1F0: "rgb_convert",
    0x42BFC0: "tint_add",
    0x42C040: "flash_set",
    0x42BF20: "fade_tint",
    0x4612A0: "grid_unlink2",
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def c_string(uc, a, limit=0x40):
    out = bytes(uc.mem_read(a, limit))
    return out.split(b"\0")[0]


def ptr(a):
    return "stack" if on_stack(a) else a


class Stubs:
    def __init__(self, m):
        self.m = m
        self.calls = dict(STUBS)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _stub(self, uc, address):
        name = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<7I", bytes(uc.mem_read(esp, 28)))
        ret, a = words[0], list(words[1:])
        nx = self._next
        eax = 0
        log = ()
        if name == "tex_lock":
            res, bits, bpp, pitch = nx(name, (0, 0, 0, 0))
            log = (a[0], a[1], ptr(a[2]), ptr(a[3]), ptr(a[4]), ptr(a[5]))
            uc.mem_write(a[2], struct.pack("<I", bits))
            uc.mem_write(a[4], struct.pack("<I", bpp))
            uc.mem_write(a[5], struct.pack("<I", pitch))
            eax = res
        elif name == "tex_unlock":
            log = (a[0], a[1], ptr(a[2]), bytes(uc.mem_read(a[2], 0x200)))
        elif name == "tex_handle":
            eax = nx(name)
        elif name == "tex_upload":
            log = tuple(a[:6])
            eax = nx(name)
        elif name == "bmp_load":
            log = (c_string(uc, a[0]), a[1], a[2])
            pal, pix = nx(name, (0, 0))
            uc.mem_write(a[1] + 0x10, struct.pack("<II", pal, pix))
        elif name == "alloc":
            log = (a[0],)
            eax = nx(name)
        elif name == "rgb_convert":
            log = (a[0], bytes(uc.mem_read(a[0], 4 * min(a[1], 0x100))), a[1], a[2], a[3])
        elif name == "tint_add":
            log = (a[0],)
            eax = nx(name)
        elif name == "flash_set":
            log = tuple(x & 0xFFFF for x in a[:6])
        elif name == "fade_tint":
            log = (a[0],)
        elif name == "grid_unlink2":
            log = (a[0],)
        else:
            raise KeyError(name)
        self.log.append((name, log))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---------------------------------------------------------------- state

class State:
    def __init__(self, rng, base):
        self.rng = rng
        self.mem = {a: bytearray(b) for a, b in base.items()}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.budget = 20000000

    def _where(self, address):
        if SCR <= address < SCR + SCR_SIZE:
            return self.scr, address - SCR
        for a, n in REGIONS:
            if a <= address < a + n:
                return self.mem[a], address - a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        buf, off = self._where(address)
        struct.pack_into(fmt, buf, off, value)

    def putf(self, address, x):
        self.put(address, "<I", f2u(x))

    def putb(self, address, data):
        buf, off = self._where(address)
        buf[off:off + len(data)] = data


def CAR(i):
    return CARS + i * CAR_SIZE


def SLOT(i):
    return 0x6BFB80 + i * 0x274


def world(st, rng):
    """Pointers, the GTE / projection state, ordering tables."""
    st.put(0x4B1960, "<I", ARG + 0x1000)  # race
    st.put(0x4B2198, "<I", ARG + 0x1800)  # config
    for i in range(6):
        st.put(SLOT(i), "<I", CAR(i % 3))
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD7C, "<I", 0x426BE0)
    st.put(0x6CCD80, "<I", 0x426B00)
    st.putf(0x6CCD00, pick(rng, 256.0, 320.0, 200.0))
    st.putf(0x6CCD04, pick(rng, 1.0, 16.0))
    st.putf(0x6CCD08, pick(rng, 6000.0, 20000.0))
    for a in (0x6CCD0C, 0x6CCD10):
        st.putf(a, rng.uniform(-300, 300))
    st.putf(0x6CCD14, pick(rng, rng.uniform(-400, 3000), rng.uniform(-400, 3000), rng.uniform(8000, 19000)))
    for a in (0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40, 0x6CCD44):
        st.putf(a, rng.uniform(-1, 1))
    st.putf(0x6CCD4C, pick(rng, 320.0, 0.0, rng.uniform(-50, 400)))
    st.putf(0x6CCD50, pick(rng, 240.0, 0.0, rng.uniform(-50, 300)))
    st.putf(0x6CCD5C, pick(rng, 320.0, 0.0, rng.uniform(-50, 400)))
    st.putf(0x6CCD60, pick(rng, 240.0, 0.0, rng.uniform(-50, 300)))
    st.putf(0x6CCD64, pick(rng, 0.0, rng.uniform(-20, 20)))
    st.putf(0x6CCD68, pick(rng, 640.0, rng.uniform(300, 700)))
    st.putf(0x6CCD6C, pick(rng, 0.0, rng.uniform(-20, 20)))
    st.putf(0x6CCD70, pick(rng, 480.0, rng.uniform(200, 500)))
    for k in range(9):
        st.put(0x6CCBE0 + 2 * k, "<h", pick(rng, rng.randrange(-4096, 4097), rng.randrange(-64, 64), 0))
    st.put(0x6CCD84, "<I", pick(rng, 0, 1, 4, 5, r32(rng)))
    st.put(0x6CCE44, "<I", pick(rng, 256, 512, rng.randrange(100, 3000), r32(rng)))
    st.put(0x6CD478, "<I", pick(rng, (480 << 16) | 640, (240 << 16) | 320, r32(rng)))
    st.put(0x6CD400, "<i", pick(rng, 0x100, 0x80, rng.randrange(-0x40, 0x200)))
    st.put(0x6CD404, "<i", pick(rng, 0x100, 0x180, rng.randrange(-0x40, 0x400)))
    st.put(0x6CD40C, "<I", pick(rng, 0x100, 0x200, rng.randrange(0, 0x400), r32(rng)))
    st.putf(0x6CD418, pick(rng, 0.0, 8.0, rng.uniform(-30, 60)))
    st.put(0x4ACC38, "<I", pick(rng, 640, 320, 800))
    for k in range(0x200):
        st.put(0x6CDD80 + 4 * k, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0, 0x80000000))
        st.put(0x6CD510 + 4 * k, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0x80000000))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, 1))
    st.put(0x6C1E14, "<I", TEXT)
    st.put(0x6C1EB0, "<I", TEXH)
    st.put(TEXH + 0x2E, "<H", rng.randrange(0, 0x40))
    st.put(0x6C0A50, "<I", r32(rng))
    st.put(0x4D1198, "<I", pick(rng, 0, 0, 1))


# ---------------------------------------------------------------- generators: (args, returns value?)

def g_tables(st, rng):
    st.put(0x4D57EC, "<I", TABLE)
    st.budget = 400000000
    return [f2u(pick(rng, 0.05, 0.045, 0.0475, 0.0525, rng.uniform(0.01, 0.2)))], False


def warp_texture_state(st, rng):
    st.put(0x4D57EC, "<I", TABLE)
    st.put(0x4D57C8 + 0x14, "<I", PIX)
    st.put(0x4D51B4, "<I", IPIX)


def g_tex_update(st, rng):
    warp_texture_state(st, rng)
    st.put(ARG + 0x1800 + 0x29, "<B", pick(rng, 0, 1, 1, 1))
    st.put(0x4B24A0, "<I", pick(rng, 0xFFFFFFFF, 5, 5, 5))
    now = r32(rng)
    st.put(0x4BE3E8, "<I", now)
    st.put(0x4B24A4, "<I", pick(rng, 0xFFFFFFFF, now - 1, now - 5, now - 6, now + 3, now, r32(rng)))
    st.put(0x4B249C, "<I", pick(rng, 1, 2, r32(rng)))
    st.script["tex_lock"] = [(pick(rng, 0, 1, 1, 1), SURF, pick(rng, 8, 8, 16, 32), pick(rng, 0x100, 0x80, 0x104))]
    return [pick(rng, 0, 0xFF, rng.randrange(0, 0x100), r32(rng))], True


def g_tex_render(st, rng):
    warp_texture_state(st, rng)
    bpp = pick(rng, 8, 16, 32)
    pitch = pick(rng, 0x100, 0x104) if bpp == 8 else pick(rng, 0x100, 0x200)
    return [pick(rng, 0, rng.randrange(-128, 128)), pick(rng, 0, rng.randrange(-128, 128)), SURF, pitch, bpp], False


def g_ice_load(st, rng):
    warp_texture_state(st, rng)
    st.put(0x4D51B0, "<I", IPAL)
    st.put(0x4B24A0, "<I", pick(rng, 5, 7))
    st.script["bmp_load"] = [(IPAL, IPIX)]
    st.script["tex_lock"] = [(pick(rng, 0, 1, 1, 1), SURF, pick(rng, 8, 8, 16, 32), pick(rng, 0x100, 0x104, 0x200))]
    return [], False


def g_ice_render(st, rng):
    warp_texture_state(st, rng)
    bpp = pick(rng, 8, 16, 32)
    pitch = pick(rng, 0x100, 0x104) if bpp == 8 else pick(rng, 0x100, 0x200)
    return [SURF, pitch, bpp], False


def g_warp_init(st, rng):
    st.budget = 400000000
    st.put(0x4B11DC, "<I", r32(rng))
    st.put(ARG + 0x1800 + 0x29, "<B", pick(rng, 0, 1))
    st.put(0x4BE3E8, "<I", r32(rng))
    st.put(0x4B24A4, "<I", pick(rng, 0xFFFFFFFF, r32(rng)))
    st.script["alloc"] = [TABLE]
    st.script["bmp_load"] = [(PAL, PIX), (IPAL, IPIX)]
    st.script["tex_handle"] = [pick(rng, 5, 9)]
    st.script["tex_upload"] = [pick(rng, 0, 1, 1)]
    st.script["tex_lock"] = [(pick(rng, 0, 1), SURF, pick(rng, 8, 16), 0x104),
                             (pick(rng, 0, 1), SURF, pick(rng, 8, 16), 0x200)]
    return [], True


def g_warp_draw(st, rng):
    st.put(ARG + 0x1800 + 0x29, "<B", pick(rng, 0, 1, 1))
    st.put(ARG + 0x1800 + 0x32, "<B", pick(rng, 0, 1, 1))
    st.put(0x4D57F0, "<I", pick(rng, 0, 1, 1))
    st.put(0x4B24A0, "<I", pick(rng, 0xFFFFFFFF, 5, r32(rng)))
    st.script["tint_add"] = [r32(rng)]
    return [], False


def g_light_release(st, rng):
    return [rng.randrange(0, 0x40)], False


# --- sprites

def gte_vec(st, rng, a, hi=True):
    """A position near the camera: words (or 16.16 ints when hi)."""
    for k in range(3):
        v = pick(rng, rng.randrange(-600, 600), rng.randrange(-3000, 3000), rng.randrange(-200, 200))
        if hi:
            st.put(a + 4 * k, "<i", (v << 16) | rng.getrandbits(16))
        else:
            st.put(a + 2 * k, "<h", v)


def sprite(st, rng, s, kind):
    st.put(s, "<I", kind)
    gte_vec(st, rng, s + 0x10, hi=False)
    st.put(s + 0x16, "<B", rng.getrandbits(8))
    st.put(s + 0x17, "<B", rng.getrandbits(8))
    gte_vec(st, rng, s + 0x4C)
    gte_vec(st, rng, s + 0x64, hi=False)
    gte_vec(st, rng, s + 0x10 if kind == 0xE else s + 0x88)
    st.put(s + 0x2D, "<B", rng.getrandbits(8))
    st.put(s + 0x95, "<B", rng.getrandbits(8))
    st.put(s + 0x98, "<I", r32(rng))
    st.put(s + 0xA2, "<h", pick(rng, 0x10, -0x8000, 0, 3, rng.randrange(-0x8000, 0x8000)))
    st.put(s + 0xA4, "<I", rng.randrange(0, 0x40) | rng.randrange(0, 0x20) << 16 | rng.getrandbits(8) << 24)
    st.put(s + 0x108, "<I", r32(rng))
    st.put(s + 0x10B, "<B", rng.getrandbits(8))
    for off in (0x6A, 0x5A + 0x10):
        st.put(s + off, "<H", rng.randrange(0, 0x200))
    st.put(s + 0x10 + 0x76, "<H", rng.getrandbits(16))


def view_axis(st, rng):
    st.put(0x5D65E0, "<I", pick(rng, 0, 1, 1))
    for k in range(3):
        st.put(0x5D6DE0 + 2 * k, "<h", pick(rng, rng.randrange(-4096, 4097), rng.randrange(-300, 300)))


def g_kind(kind):
    def gen(st, rng):
        s = GRID
        sprite(st, rng, s, kind)
        view_axis(st, rng)
        st.put(0x4AF4B8, "<I", r32(rng))
        return [s], False
    return gen


def g_flat(st, rng):
    pkt = GRID + 0x10
    sprite(st, rng, GRID, 8)
    st.put(pkt + 0x5A, "<H", rng.randrange(0, 0x200))
    return [pkt], False


def g_colour(st, rng):
    for a in (0x6CCC34, 0x6CCC38, 0x6CCC3C, 0x6CCC60):
        st.put(a, "<I", pick(rng, rng.randrange(0, 0x1000), r32(rng)))
    return [pick(rng, rng.randrange(0, 0x1000), r32(rng)), r32(rng), ARG, ARG + 0x10], False


def g_quad(st, rng):
    gte_vec(st, rng, ARG, hi=False)
    st.put(ARG + 6, "<H", rng.getrandbits(16))
    for a in (0x6CCC34, 0x6CCC38, 0x6CCC3C, 0x6CCC60):
        st.put(a, "<I", rng.randrange(0, 0x1000))
    st.put(0x4ACC38, "<I", pick(rng, 640, 320, 800, rng.randrange(1, 4000)))
    s = GRID + 0x800
    st.put(s + 0x17, "<B", rng.getrandbits(8))
    mode = pick(rng, 10, 2, 3, 1, 0x10006, rng.randrange(0, 0x40), rng.randrange(0, 0x40) | 0x10000)
    flags = pick(rng, 0x80000000, 0x80000001, 0x8000FFFF, 0x80000002, r32(rng))
    size = pick(rng, rng.randrange(0, 0x100) << 8, rng.randrange(0, 0x100) << 10, r32(rng))
    return [ARG, size, GRID + 0x100, mode, flags, pick(rng, 0, s)], False


def g_rotate(st, rng):
    return [r32(rng), pick(rng, rng.randrange(0, 0x400), r32(rng)), pick(rng, rng.randrange(0, 0x400), r32(rng)),
            ARG, ARG + 4], False


def g_markers(st, rng):
    n = pick(rng, 0, 1, 5, 40, rng.randrange(-2, 41))
    st.put(0x4D5800, "<i", n)
    body = ARG + 0x100
    for k in range(3):
        st.put(body + 0x34 + 4 * k, "<i", rng.randrange(-0x4000000, 0x4000000))
    for i in range(40):
        for k in range(3):
            base = struct.unpack_from("<i", st.scr, body + 0x34 + 4 * k - SCR)[0]
            st.put(0x5D6400 + 12 * i + 4 * k, "<i", base + pick(rng, rng.randrange(-0x2000000, 0x2000000),
                                                               rng.randrange(-0x400000, 0x400000), r32(rng)))
    return [body], False


def glare_list(st, rng, n):
    for i in range(n):
        s = GRID + 0x200 * i
        sprite(st, rng, s, pick(rng, 8, 9, 0x22, 0x17))
        st.put(0x5D6600 + 4 * i, "<I", s)
    end = pick(rng, 0, 0, GRID)
    st.put(0x5D6600 + 4 * n, "<I", end)
    return end


def g_glare(st, rng):
    n = pick(rng, 0, 1, 3, 8, rng.randrange(0, 16))
    end = glare_list(st, rng, n)
    st.put(0x4D5804, "<i", pick(rng, n, n, n, -1, n + 1 if end else n))
    view_axis(st, rng)
    st.put(ARG + 0x1000 + 0xA80, "<H", pick(rng, 480, 240, rng.getrandbits(16)))
    st.put(ARG + 0x1000 + 0xA82, "<H", pick(rng, 640, 320, rng.getrandbits(16)))
    st.putf(ARG + 0x1800 + 0x68, pick(rng, 1.0, 0.5, 2.0, rng.uniform(0, 10), rng.uniform(-1, 1)))
    st.put(0x6CD40C, "<I", pick(rng, 0x100, 0x40, 0x10, rng.randrange(0, 0x400)))
    return [], False


def g_axis(st, rng):
    return [], False


def g_streak(st, rng):
    view_axis(st, rng)
    gte_vec(st, rng, ARG, hi=False)
    st.put(ARG + 6, "<H", rng.getrandbits(16))
    return [ARG, pick(rng, 0x8000, rng.randrange(0, 0x40000), r32(rng)), GRID + 0x100,
            pick(rng, 0, 0, 1, -1, rng.randrange(-40, 40))], False


def g_sprites(st, rng):
    """The list of sections, objects and grid chains for 0x47dd70."""
    n_rec = pick(rng, 0, 1, 2, rng.randrange(0, 5))
    st.put(LIST, "<I", n_rec)
    st.put(0x6BFAC0, "<I", SECT)
    st.put(0x6BFAE4, "<I", OBJIDX)
    st.put(0x6BFAF8, "<I", OBJTAB)
    st.put(0x6B9B90, "<I", GRID)
    for k in range(8):
        st.put(SECT + 4 * k, "<I", SECT + 0x40 + 0x20 * k)
        st.put(SECT + 0x40 + 0x20 * k + 0x48, "<I", SECT + 0x200 + 0x80 * k)
        prev = rng.randrange(0, 8)
        for j in range(0x40):
            st.put(SECT + 0x200 + 0x80 * k + 2 * j, "<H", prev)
            prev = min(0x1F0, prev + pick(rng, 0, 1, 2, rng.randrange(0, 6)))
    for i in range(n_rec):
        st.put(LIST + 4 + 8 * i, "<H", rng.randrange(0, 8))
        st.put(LIST + 6 + 8 * i, "<H", rng.randrange(0, 8))
        st.put(LIST + 8 + 8 * i, "<H", pick(rng, 1, 2, 4, rng.randrange(0, 8)))
        st.put(LIST + 10 + 8 * i, "<H", pick(rng, 1, 1, 2, 3))
        st.put(0x5D91C0 + 4 * i, "<I", rng.randrange(0, 0x80))
    for j in range(0x200):
        st.put(OBJIDX + 2 * j, "<H", rng.randrange(0, 0x40))
    for j in range(0x40):
        hi = pick(rng, 0, 0, 4 * rng.randrange(0, 32) + 1)
        lo = pick(rng, 0, rng.getrandbits(16))
        st.put(OBJTAB + 12 * j + 8, "<I", hi << 16 | lo)
    kinds = (2, 5, 8, 9, 0xD, 0xE, 0x17, 0x1B, 0x1C, 0x1E, 0x22, 0, 1, 0x23)
    for r in range(32):
        s = GRID + 0x200 * r
        sprite(st, rng, s, pick(rng, *kinds))
    # chains: sprite r is grid link 4 r + 1 (records of 0x80); the next link, ending in 0, no cycles
    for r in range(32):
        st.put(GRID + 0x200 * r + 4, "<H", pick(rng, 0, 0, 4 * (r + 1) + 1 if r < 31 else 0))
    view_axis(st, rng)
    st.put(0x4AF4B8, "<I", r32(rng))
    st.put(0x4ACC38, "<I", pick(rng, 640, 320))
    st.put(0x4D5804, "<i", 0)
    st.put(ARG + 0x1000 + 0xA80, "<H", 480)
    st.put(ARG + 0x1000 + 0xA82, "<H", 640)
    st.putf(ARG + 0x1800 + 0x68, pick(rng, 1.0, 0.5))
    return [LIST], False


def unit(rng):
    while True:
        v = [rng.uniform(-1, 1) for _ in range(3)]
        n = math.sqrt(sum(x * x for x in v))
        if 0.1 < n <= 1:
            return [x / n for x in v]


def g_trails(st, rng):
    st.put(ARG + 0x1000 + 0x54D, "<B", pick(rng, 0, 1, 2, 3))
    st.put(0x4B11D8, "<I", r32(rng))
    for c in range(3):
        car = CAR(c)
        d = unit(rng)
        speed = pick(rng, rng.randrange(0x370000, 0x500000), rng.randrange(0, 0x2000000), rng.randrange(0, 0x900000))
        st.put(car + 0x654, "<i", speed)
        mag = pick(rng, rng.randrange(0, 0x40000), rng.randrange(0x40000, 0x2000000))
        for k in range(3):
            st.put(car + 0x68 + 4 * k, "<i", int(d[k] * mag))
        f = d if rng.randrange(4) else unit(rng)
        st.put(car + 0xC8, "<h", int(f[0] * 4096))
        st.put(car + 0xCE, "<h", int(f[1] * 4096))
        st.put(car + 0xD4, "<h", int(f[2] * 4096))
        for off in (0xC4, 0xC6, 0xCA, 0xCC, 0xD0, 0xD2):
            st.put(car + off, "<h", rng.randrange(-4096, 4097))
        for k in range(2):
            tr = car + 0x14CC + 0x6D4 * k
            st.put(tr, "<i", rng.randrange(0, 12))
            st.put(tr + 4, "<i", pick(rng, 3, 4, 12, rng.randrange(0, 13)))
            st.put(tr + 8, "<B", rng.getrandbits(8))
            st.put(tr + 0xA, "<B", rng.getrandbits(8))
            base = [rng.randrange(-1500, 1500) for _ in range(3)]
            for p in range(12):
                for q in range(3):
                    st.put(tr + 0x14 + 12 * p + 4 * q, "<i",
                           ((base[q] + rng.randrange(-300, 300)) << 16) | rng.getrandbits(16))
    return [], False


TESTS = {
    "warp_tables_build": (0x47D400, g_tables, 3),
    "warp_texture_update": (0x47D5A0, g_tex_update, None),
    "warp_texture_render": (0x47D6D0, g_tex_render, 60),
    "ice_texture_load": (0x47D7F0, g_ice_load, 60),
    "ice_texture_render": (0x47D8F0, g_ice_render, 60),
    "warp_init": (0x47D980, g_warp_init, 3),
    "warp_overlay_draw": (0x47DB20, g_warp_draw, None),
    "ice_overlay_draw": (0x47DB80, g_warp_draw, None),
    "light_sprite_release": (0x47DCA0, g_light_release, None),
    "sprites_draw": (0x47DD70, g_sprites, None),
    "sprite_line_draw": (0x47E020, g_kind(5), None),
    "sprite_marker_draw": (0x47E1B0, g_kind(2), None),
    "light_sprite_draw": (0x47E220, g_kind(0xD), None),
    "sprite_puff_draw": (0x47E2A0, g_kind(0xE), None),
    "sprite_billboard_draw": (0x47E340, g_kind(0x17), None),
    "sprite_billboard_draw_8": (0x47E340, g_kind(8), None),
    "sprite_flat_insert": (0x47E4C0, g_flat, None),
    "sprite_colour_fill": (0x47E550, g_colour, None),
    "sprite_quad_build": (0x47E5F0, g_quad, None),
    "sprite_corners_rotate": (0x47E980, g_rotate, None),
    "marker_proximity_tint": (0x47EA00, g_markers, None),
    "sprites_glare_draw": (0x47EAA0, g_glare, None),
    "view_side_axis_update": (0x47EE90, g_axis, None),
    "sprite_streak_build": (0x47EF30, g_streak, None),
    "car_light_trails_draw": (0x4821D0, g_trails, None),
}

SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def call(m, address, args, budget):
    esp = E.STACK + E.STACK_SIZE - 0x100
    frame = struct.pack("<I", E.RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args)
    m.uc.mem_write(esp, frame)
    m.uc.reg_write(UC_X86_REG_ESP, esp)
    m.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
    m.trapped = False
    try:
        m.uc.emu_start(address, E.RETURN, count=budget)
    except UcError as e:
        return ("error", str(e))
    eip = m.uc.reg_read(UC_X86_REG_EIP)
    if m.trapped or eip != E.RETURN:
        # A trap (#DE) or the budget: where it stopped. (After the same #DE twice in a row the emulator can
        # stop at it without reporting the interrupt, so a trap and a stop are not told apart.)
        return ("stopped", eip)
    return ("ok", m.uc.reg_read(UC_X86_REG_EAX))


def run_once(m, stubs, st, target, args, cw, fill, returns):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes([fill]) * E.STACK_SIZE)
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    for i, reg in enumerate(SAVED):
        m.uc.reg_write(reg, 0x11111111 * (i + 1))
    stubs.reset(st.script)
    r = call(m, target, args, st.budget)
    if r[0] == "ok":
        r = ("ok", r[1]) if returns else ("ok",)
        r = r + tuple(m.uc.reg_read(x) for x in SAVED)
        r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
    last = struct.unpack("<I", m.read(0x6CCD74, 4))[0]
    if on_stack(last):  # the transforms' record pointer: a stack buffer in both
        m.write(0x6CCD74, b"STK!")
    # The GTE vector's fourth word: written from stack leftovers by the original (0 by ours), never read.
    m.write(0x6CCC6A, b"\0\0")
    # 0x426b00's fstp of its st(0) into 0x4bdf00 (a dead temporary): after *0x6ccd7c popped everything for
    # a negative depth (0x47ef30's second / third point) the stack is empty; the hardware stores the
    # indefinite NaN, the emulator a stale register that differs between the two builds.
    m.write(0x4BDF00, b"\0\0\0\0")
    mem = bytearray(m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS))
    return r, mem, stubs.log


def mask_logs(la, lb, ours):
    """Zero the bytes of logged buffers that differ between the original's two runs (stack leftovers)."""
    for i, (ea, eb) in enumerate(zip(la, lb)):
        if ea[0] != eb[0] or not isinstance(ea[1], tuple) or not isinstance(eb[1], tuple):
            break
        for j, (xa, xb) in enumerate(zip(ea[1], eb[1])):
            if isinstance(xa, bytes) and isinstance(xb, bytes) and len(xa) == len(xb) and xa != xb:
                xo = ours[i][1][j] if i < len(ours) and j < len(ours[i][1]) else None
                ma = bytearray(xa)
                mo = bytearray(xo) if isinstance(xo, bytes) else None
                for k in range(len(xa)):
                    if xa[k] != xb[k]:
                        ma[k] = 0
                        if mo is not None and k < len(mo):
                            mo[k] = 0
                la[i] = (ea[0], ea[1][:j] + (bytes(ma),) + ea[1][j + 1:])
                if mo is not None:
                    ours[i] = (ours[i][0], ours[i][1][:j] + (bytes(mo),) + ours[i][1][j + 1:])
                ea = la[i]


def where(off):
    if off < SCR_SIZE:
        return SCR + off
    off -= SCR_SIZE
    for a, n in REGIONS:
        if off < n:
            return a + off
        off -= n
    return None


def run_case(m, stubs, base, name, address, gen, rng):
    st = State(rng, base)
    world(st, rng)
    args, returns = gen(st, rng)
    args = [a & 0xFFFFFFFF for a in args]
    cw = pick(rng, 0x27F, 0x27F, 0x37F, 0x07F)
    a = run_once(m, stubs, st, address, args, cw, 0xCC, returns)
    b = run_once(m, stubs, st, address, args, cw, 0x5A, returns)
    sym = name[:-2] if name.endswith("_8") else name
    o = run_once(m, stubs, st, m.symbols[sym], args, cw, 0xCC, returns)
    for k in range(len(a[1])):
        if a[1][k] != b[1][k]:
            a[1][k] = 0
            o[1][k] = 0
    la, lo = list(a[2]), list(o[2])
    mask_logs(la, list(b[2]), lo)
    if a[0] == o[0] and a[1] == o[1] and la == lo:
        return None, a[0][0] != "ok"
    msg = f"{name}{tuple(hex(x) for x in args)} cw {cw:#x}: original {a[0]} ours {o[0]}"
    if a[1] != o[1]:
        x, y = a[1], o[1]
        diff = [i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]]
        msg += f"\n  memory differs at {[hex(where(i)) for i in diff[:8]]} ({len(diff)} words)"
        if "-v" in sys.argv:
            msg += f"\n  orig {[x[i:i + 4].hex() for i in diff[:8]]}\n  ours {[y[i:i + 4].hex() for i in diff[:8]]}"
    if la != lo:
        k = next((i for i in range(min(len(la), len(lo))) if la[i] != lo[i]), min(len(la), len(lo)))
        msg += f"\n  calls from #{k}: orig {la[k:k + 2]}\n                ours {lo[k:k + 2]}"
    return msg, True


def func_sizes():
    sizes = {}
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "groups", "pr_owned.txt")
    try:
        for line in open(path):
            if not line.startswith("#"):
                parts = line.split()
                sizes[int(parts[0], 16)] = int(parts[2])
    except OSError:
        pass
    return sizes


def instruction_addresses(address, size):
    out = set()
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "rollcage.asm")
    try:
        for line in open(path):
            parts = line.split(":", 1)
            if len(parts) == 2:
                try:
                    a = int(parts[0], 16)
                except ValueError:
                    continue
                if address <= a < address + size and "nop" not in parts[1]:
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
    base = {a: m.read(a, n) for a, n in REGIONS}
    rng = random.Random(0x9A)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, cap) in TESTS.items():
        if only and name not in only:
            continue
        sym = name[:-2] if name.endswith("_8") else name
        if sym not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        count = min(iterations, cap) if cap else iterations
        bad = faults = 0
        seen = set()
        size = sizes.get(address, 0)
        hook = None
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address,
                                 end=address + size - 1)
        for _ in range(count):
            msg, fault = run_case(m, stubs, base, name, address, gen, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
            else:
                faults += fault
        cov = ""
        if hook is not None:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:12]}" if missed else "") + "]"
        both = f" ({faults} fault the same way)" if faults else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {count} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
