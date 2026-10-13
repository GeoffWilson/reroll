#!/usr/bin/env python3
"""Differential test of orig_pq.c (group PQ: the view-drawing file
0x478df0-0x47c730: texture animations, screen-message glyphs, the time warp
wobble, surface tint, depth markers, track visibility runs / textures /
lighting, engine glow packets and textures, debris bursts) against the
original, on random state.

Callees outside the group are logging stubs in both runs (the log records as
many arguments as the callee reads; buffers by content, stack pointers as
"stack") with scripted results: sound_play 0x471c90, the voice stop / pitch
0x471f40 / 0x471f80 (only the bytes / word they read), surface_kind 0x40d6f0,
0x47db80, screen_tint_add 0x42bfc0, draw_prim 0x428460 (packet by content),
the grid links 0x4612a0 / 0x460ff0 / 0x4611d0, vec_normalise 0x480c70, the
effects 0x42aa30, the triggers 0x4427a0, the palette 0x486350, the light
matrix 0x428940 (by content) and the GTE light colour 0x427100 (logs its
inputs, returns a scripted colour). Real in both runs: the transform /
projection pointers' targets 0x426a20 / 0x426b00, the facing value 0x4278f0,
the GTE squares 0x4282d0. The group's own functions run for real in both
(the original's call the original's, ours call ours).

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those two runs (stack leftovers: the
light matrix's unset entries, the first scaled glyph colour's top byte) are
masked before the comparison with ours. Compared: the return value, ebx /
esi / edi / ebp, the x87 stack top / tag word, the globals and scratch
memory, the call log. The x87 control word is 0x27f, 0x37f or 0x07f.

    tests/emu_orig_pq.py EXE build/emu_pq.elf [iterations] [names...] [-v] [-c]
"""
import os
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x40000
CARS = SCR + 0x0000  # 3 cars / glows of 0x2000
CAR_SIZE = 0x2000
FONT = SCR + 0x6000  # font (0x1200)
ANIM = SCR + 0x7400  # texture animation records
FRAMES = SCR + 0x7800  # animation frames (6 bytes)
OT = SCR + 0x7C00  # ordering table entries (0x800)
OTT = SCR + 0x8400  # *0x6ce60c table (0x70 + 0x800)
OTP = SCR + 0x8C80  # *0x6ce608 struct
VIS = SCR + 0x9000  # 0x6bfacc visibility records
RECS = SCR + 0x9800  # 0x6bfac0 kind table and records
QUADS = SCR + 0xA000  # 0x1c-byte quads (0x200)
OUT = SCR + 0xD800  # run lists (0x800 each)
OUT2 = SCR + 0xE000
SRC = SCR + 0xE800  # texture source entries
TEX = SCR + 0xF000  # texture tables
VERTP = SCR + 0x10000  # vertex packets (0x120 each)
LGRID = SCR + 0x13000  # light grids
PAL = SCR + 0x14000  # palette (0x1000)
RECIP = SCR + 0x15000  # reciprocal table (0x1000)
VERTS = SCR + 0x16000  # track vertices (0x4000)
PP = SCR + 0x1A000  # quad index lists
STEPS = SCR + 0x1B000  # step lists
SURFT = SCR + 0x1B800  # *0x6c1e30
PKT = SCR + 0x1C000  # quad packets for the lighting
DEB = SCR + 0x20000  # debris ring (64 x 0x180)
OBJ = SCR + 0x26000  # objects
ARG = SCR + 0x27000
GLOWP = SCR + 0x28000  # glow packets
STUBPAGE = 0x02200000

REGIONS = [(0x4B0000, 0x26000), (0x5D7000, 0x23000), (0x6B8000, 0xA000), (0x6CB000, 0x4000)]

# cdecl callees at their own addresses
STUBS = {
    0x471C90: "sound_play",
    0x471F40: "voice_stop",
    0x471F80: "voice_pitch",
    0x40D6F0: "surface_kind",
    0x47DB80: "surface_flash",
    0x42BFC0: "tint_add",
    0x428460: "draw_prim",
    0x4612A0: "grid_unlink2",
    0x460FF0: "grid_link2_at",
    0x480C70: "vec_normalise",
    0x42AA30: "effects",
    0x4611D0: "grid_unlink",
    0x4427A0: "triggers",
    0x486350: "palette",
    0x428940: "light_matrix",
    0x427100: "light_colour",
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


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
        if name == "sound_play":
            log = tuple(a[:5])
            eax = nx(name, 0x7F)
        elif name == "voice_stop":
            log = (a[0] & 0xFF,)
        elif name == "voice_pitch":
            log = (a[0] & 0xFF, a[1] & 0xFFFF)
        elif name == "surface_kind":
            log = (a[0],)
            eax = nx(name)
        elif name == "tint_add":
            log = (a[0],)
            eax = nx(name)
        elif name == "draw_prim":
            log = (ptr(a[0]), bytes(uc.mem_read(a[0], 0x84)))
        elif name in ("grid_unlink2", "grid_unlink", "triggers"):
            log = (a[0],)
        elif name == "grid_link2_at":
            log = tuple(a[:4])
        elif name == "vec_normalise":
            log = (ptr(a[0]), bytes(uc.mem_read(a[0], 12)))
        elif name == "effects":
            log = (ptr(a[0]), bytes(uc.mem_read(a[0], 12)), a[1], a[2], a[3], a[4])
        elif name == "palette":
            eax = nx(name, PAL)
        elif name == "light_matrix":
            log = (ptr(a[0]), bytes(uc.mem_read(a[0], 18)))
        elif name == "light_colour":
            log = (bytes(uc.mem_read(0x6CCC88, 10)), bytes(uc.mem_read(0x6CCC14, 12)),
                   bytes(uc.mem_read(0x6CCE30, 12)))
            uc.mem_write(0x6CCCBC, struct.pack("<I", nx(name, 0x12345678)))
        elif name == "surface_flash":
            pass
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

    def get(self, address, fmt):
        buf, off = self._where(address)
        return struct.unpack_from(fmt, buf, off)[0]


def CAR(i):
    return CARS + i * CAR_SIZE


def world(st, rng):
    """Pointers and the GTE / projection state."""
    st.put(0x4B1960, "<I", ARG + 0x800)  # race
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD7C, "<I", 0x426BE0)
    st.put(0x6CCD80, "<I", 0x426B00)
    st.putf(0x6CCD00, pick(rng, 256.0, 320.0, 200.0))
    st.putf(0x6CCD04, pick(rng, 1.0, 16.0))
    st.putf(0x6CCD08, pick(rng, 600.0, 1600.0, 3000.0, 8000.0, 20000.0, rng.uniform(0, 9000)))
    for a in (0x6CCD0C, 0x6CCD10):
        st.putf(a, rng.uniform(-300, 300))
    st.putf(0x6CCD14, rng.uniform(-400, 3000))
    for a in (0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40, 0x6CCD44):
        st.putf(a, rng.uniform(-1, 1))
    st.putf(0x6CCD5C, 320.0)
    st.putf(0x6CCD60, 240.0)
    st.putf(0x6CCD64, 0.0)
    st.putf(0x6CCD68, 640.0)
    st.putf(0x6CCD6C, 0.0)
    st.putf(0x6CCD70, 480.0)
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CD4A0, 0x6CD4A4))
    for k in range(0x200):
        st.put(0x6CDD80 + 4 * k, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0, 0x80000000))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, 1))
    st.put(0x6C1E14, "<I", TEX)
    st.put(0x6C1EB0, "<I", TEX + 0xC00)
    st.put(0x6C0A50, "<I", r32(rng))


# ---------------------------------------------------------------- generators: (args, returns value?)

def g_anims(st, rng):
    frame = r32(rng)
    st.put(0x6C0A50, "<I", frame)
    st.put(0x4D4B74, "<I", pick(rng, frame, frame - 1, r32(rng)))
    st.put(0x6BFB44, "<I", ANIM)
    st.put(0x6BFB48, "<I", pick(rng, 0, 1, 5, rng.randrange(0, 40)))
    st.put(0x6BFB4C, "<I", FRAMES + 0x60)
    for k in range(40):
        a = ANIM + 0x18 * k
        st.put(a, "<I", pick(rng, 1, 1, 2, 0, rng.randrange(0, 10)))
        st.put(a + 6, "<H", rng.randrange(0, 32))
    for k in range(-16, 40):
        f = FRAMES + 0x60 + 6 * k
        st.put(f, "<h", pick(rng, rng.randrange(0, 32), rng.randrange(-16, 32)))
        st.put(f + 2, "<H", r32(rng) & 0xFFFF)
        st.put(f + 4, "<h", pick(rng, 0, 0, 1, 5, rng.randrange(-5, 100)))
    return [], False


def font(st, rng):
    st.put(0x5F6940, "<I", FONT)
    st.put(FONT + 0x10, "<i", rng.randrange(0, 0x40))
    st.put(FONT + 0x14, "<i", rng.randrange(-2, 6))
    for c in range(256):
        st.put(FONT + 0x101C + c, "<B", pick(rng, 0xFF, rng.randrange(0, 0xFE), rng.randrange(0, 0x20)))
    st.put(0x6CD410, "<i", pick(rng, 0x100, 0x80, 0x140, rng.randrange(0, 0x400)))
    st.putf(0x6CCD54, pick(rng, 0.0, 0.5, rng.uniform(-2, 2)))
    st.putf(0x6CCD58, pick(rng, 0.0, 0.5, rng.uniform(-2, 2)))
    st.put(0x6CE608, "<I", OTP)
    st.put(OTP + 4, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0x80000000))


def message(st, rng, rec, owners):
    n = pick(rng, 0, 0, 1, 8, rng.randrange(0, 0x41), 0x40)
    st.put(rec, "<I", pick(rng, 0, 0, *owners))
    st.put(rec + 4, "<h", rng.randrange(-100, 700))
    st.put(rec + 6, "<h", rng.randrange(-100, 500))
    st.put(rec + 8, "<B", n)
    st.put(rec + 9, "<B", pick(rng, 0, 0, 1, 0x20, 0x21, r32(rng) & 0xFF))
    st.put(rec + 0xC, "<h", pick(rng, -1, -1, rng.randrange(0, 0x100), rng.randrange(-0x8000, 0x8000)))
    st.put(rec + 0xE, "<h", pick(rng, 0x100, 0x80, rng.randrange(-0x200, 0x400)))
    st.putb(rec + 0x10, bytes(pick(rng, 0x20, rng.randrange(0x20, 0x80), rng.getrandbits(8)) for _ in range(0x40)))


def g_text(st, rng):
    font(st, rng)
    owners = [5, 7]
    mode = pick(rng, 0, 1, 2)
    if mode == 1:
        st.put(0x6B9824, "<i", pick(rng, 0, 1, 4, 8))
        for k in range(8):
            message(st, rng, 0x6B95A4 + 0x50 * k, owners)
            st.put(0x6B9828 + 2 * k, "<h", pick(rng, -1, rng.randrange(0, 8)))
        st.put(0x6B8D66, "<h", rng.randrange(0, 8))
        st.put(0x6B8D68, "<B", r32(rng) & 0xFF)
    else:
        for k in range(0x1C):
            message(st, rng, 0x5F6944 + 0x50 * k, owners)
    return [pick(rng, 5, 7, 0), mode], False


def g_warp(st, rng):
    st.put(0x5D9B20, "<I", pick(rng, 0, 1, 1, 1))
    car = r32(rng)
    st.put(ARG, "<I", car)
    st.put(0x5D9B24, "<I", pick(rng, car, r32(rng)))
    st.put(0x4B241C, "<B", pick(rng, 0x7F, 0x7F, rng.randrange(0, 64)))
    frame = r32(rng)
    st.put(0x6C0A50, "<I", frame)
    st.put(0x4D4B8C, "<I", pick(rng, frame, r32(rng)))
    st.put(0x5D9B2C, "<I", pick(rng, 0, 0x10000, rng.randrange(0, 0x10001), r32(rng)))
    for a in (0x4D4B88, 0x4B241D, 0x4B241E, 0x4B241F):
        st.put(a, "<B", r32(rng) & 0xFF)
    st.script["sound_play"] = [pick(rng, 0x7F, rng.randrange(0, 64)) for _ in range(2)]
    return [ARG], False


def g_wobble(st, rng):
    st.put(0x5D9B2C, "<I", pick(rng, 0, 0x10000, rng.randrange(0, 0x10001), r32(rng)))
    p = pick(rng, 0x4D4B88, ARG)
    st.put(p, "<B", r32(rng) & 0xFF)
    return [p, r32(rng) & pick(rng, 0xFF, 0xFFFFFFFF), rng.randrange(0, 3), rng.randrange(0, 3),
            pick(rng, rng.randrange(0, 9), r32(rng) & 0xFF)], True


def g_none(st, rng):
    return [], True


def g_tint(st, rng):
    slot = ARG
    st.put(slot, "<I", r32(rng))
    st.put(slot + 0x256, "<B", pick(rng, 0, 0, 8, 4, 0xFF, 0xFC, rng.getrandbits(8)))
    st.put(slot + 0x257, "<B", pick(rng, 0, 1, 2, rng.getrandbits(8)))
    st.script["surface_kind"] = [pick(rng, 0, 0, 1, 2)]
    return [slot], False


def g_markers(st, rng):
    st.put(0x6CE60C, "<I", OTT)
    for k in range(0x200):
        st.put(OTT + 0x70 + 4 * k, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0x80000000))
    st.put(0x6CCD98, "<i", pick(rng, rng.randrange(-50, 600), 0, 21, 0x213, 0x214))
    st.put(0x6CCD9C, "<i", pick(rng, rng.randrange(-50, 600), -21, -20, 0x1EB))
    st.put(0x6CCD94, "<i", pick(rng, rng.randrange(1, 600), 0x1FF, 0x200, 1))
    return [], False


def quads(st, rng):
    """0x6bfac0: kinds 0..3 -> records whose +0x4c is the quad array."""
    st.put(0x6BFAC0, "<I", RECS)
    for k in range(4):
        st.put(RECS + 4 * k, "<I", RECS + 0x100 + 0x80 * k)
        st.put(RECS + 0x100 + 0x80 * k + 0x4C, "<I", QUADS + 0x1C * 0x20 * pick(rng, 0, 1))
    far = rng.uniform(100, 3000)
    for q in range(0x1C0):
        a = QUADS + 0x1C * q
        st.put(a + 2, "<h", pick(rng, rng.randrange(-0x400, 0x40), -int(far / 2)))
        for c in range(4):
            st.putb(a + 4 + 4 * c, struct.pack("<2h", rng.randrange(-3000, 3000), rng.randrange(-3000, 3000)))
            st.put(a + 0x14 + 2 * c, "<h", rng.randrange(-3000, 3000))
    for k in range(9):
        st.put(0x6CCBE0 + 2 * k, "<h", rng.randrange(-4096, 4097))


def g_quad(st, rng):
    quads(st, rng)
    return [QUADS + 0x1C * rng.randrange(0x1C0)], True


def g_split(st, rng):
    quads(st, rng)
    k = pick(rng, 0, 0, 1, 3)
    st.put(OUT, "<I", pick(rng, 0, 0, 1, 3))
    lvl = pick(rng, 1, 2, 3, 4, 1)
    sh = lvl - 1
    n = pick(rng, 0, 1, 2, rng.randrange(0, 8), rng.randrange(0, 0x30 >> sh))
    start = rng.randrange(0, 0x40)
    return [OUT, k | pick(rng, 0, 0x10000), start | pick(rng, 0, 0x30000), n | pick(rng, 0, 0x50000), lvl], False


def vis_records(st, rng):
    """0x6bfacc records chained from the cell's index."""
    st.put(0x6BFACC, "<I", VIS)
    first = 2 * rng.randrange(0, 8)
    a = VIS + first
    for r in range(pick(rng, 1, 2, 3, 5)):
        n = pick(rng, 0, 1, 2, 3, 4)
        kind = rng.randrange(0, 4)
        st.put(a, "<H", rng.randrange(0, 0x40) | n << 11)
        st.put(a + 2, "<B", kind << 1 | (1 if r == 4 or rng.randrange(4) == 0 else 0))
        for b in range(n):
            st.put(a + 3 + b, "<B", rng.randrange(0, 8) << 2 | rng.randrange(0, 4))
        a = (a + n + 4) & ~1
    st.put(a + 2, "<B", 1)  # a last record
    st.put(a, "<H", 0)
    return first // 2


def g_visible(st, rng):
    quads(st, rng)
    idx = vis_records(st, rng)
    st.put(ARG, "<I", r32(rng))
    st.put(ARG + 4, "<I", idx | (r32(rng) & 0xFFFF0000))
    return [ARG, OUT], False


def run_list(st, rng, a, n, kinds=2):
    st.put(a, "<I", n)
    for k in range(n):
        e = a + 4 + 8 * k
        lvl = pick(rng, 1, 2, 3, 4, 1)
        st.put(e, "<H", rng.randrange(0, kinds))
        st.put(e + 2, "<H", rng.randrange(0, 0x20))
        st.put(e + 4, "<H", pick(rng, 0, 1, rng.randrange(0, 0x10 >> (lvl - 1)) + 1))
        st.put(e + 6, "<H", lvl)


def tex_recs(st, rng):
    """Kind records for the texture pass: level 1 stride +8, quad lists."""
    st.put(0x6BFAC0, "<I", RECS)
    for k in range(2):
        rec = RECS + 0x100 + 0x80 * k
        st.put(RECS + 4 * k, "<I", rec)
        st.put(rec + 8, "<i", pick(rng, 1, 2, 4, 0, -1))
        st.put(rec + 0x50, "<I", SRC)
        for lvl in range(2, 5):
            st.put(rec + 0x4C + 4 * lvl, "<I", SRC + 0x100 * lvl)
            st.put(rec + 0x20 + 4 * lvl, "<I", STEPS + 0x10 * lvl)
            st.put(STEPS + 0x10 * lvl, "<H", pick(rng, 1, 2, 3, 5, 0))
    for k in range(0x100):
        st.put(SRC + 8 * k, "<H", rng.randrange(0, 0x20))
        st.put(SRC + 8 * k + 4, "<I", 0x10 * rng.randrange(0, 0x30))
    for k in range(0x180):
        st.put(TEX + 4 * k, "<I", r32(rng))
    st.put(0x6BFAC4, "<I", VERTP)


def g_diff(st, rng):
    tex_recs(st, rng)
    run_list(st, rng, OUT, pick(rng, 0, 1, 3, rng.randrange(0, 12)))
    na = st.get(OUT, "<I")
    # the earlier list: some copies of the current entries, shifted
    nb = pick(rng, 0, 1, 3, rng.randrange(0, 12))
    st.put(OUT2, "<I", nb)
    for k in range(nb):
        e = OUT2 + 4 + 8 * k
        if na and rng.randrange(3):
            src = OUT + 4 + 8 * rng.randrange(na)
            st.putb(e, bytes(st.get(src + i, "<B") for i in range(8)))
            st.put(e + 2, "<H", (st.get(e + 2, "<H") + rng.randrange(-6, 7)) & 0x1F)
            st.put(e + 4, "<H", rng.randrange(0, 0x8))
        else:
            lvl = pick(rng, 1, 2, 3, 4)
            st.putb(e, struct.pack("<4H", rng.randrange(0, 2), rng.randrange(0, 0x20), rng.randrange(0, 8), lvl))
    return [OUT, OUT2], False


def g_flags(st, rng):
    run_list(st, rng, OUT, pick(rng, 0, 1, 3, rng.randrange(0, 8)), kinds=4)
    for k in range(4):
        st.put(0x5D7DE0 + 4 * k, "<I", LGRID + 0x400 * k)
    for k in range(0x400):
        a = LGRID + 4 * k
        if rng.randrange(3):
            st.put(a, "<I", 0)
        else:
            s = rng.randrange(0, 0xA0)
            st.put(a, "<I", rng.randrange(0, 16) << 22 | (s + rng.randrange(0, 0x40)) << 11 | s)
    for k in range(16):
        st.put(0x5D8012 + 0x40 * k, "<H", pick(rng, 0, 0, 0, 1))
    nd = pick(rng, 0, 0, 1, 3, 6)
    st.put(0x5D7FE4, "<I", nd)
    for k in range(nd):
        s = rng.randrange(0, 0xA0)
        st.put(0x5D71E0 + 4 * k, "<I", pick(rng, 0, 1) and 0 or (
            (s + rng.randrange(0, 0x40)) << 18 | s << 7 | rng.randrange(0, 4)))
    nn = pick(rng, 0, 0, 1, 3, 6)
    st.put(0x5D92C0, "<I", nn)
    for k in range(nn):
        lvl = pick(rng, 1, 2, 3, 4)
        st.putb(0x5D92C4 + 8 * k, struct.pack("<4H", rng.randrange(0, 4), rng.randrange(0, 0x30),
                                               rng.randrange(0, 8), lvl))
    return [OUT], False


def g_light(st, rng):
    n_ent = pick(rng, 0, 1, 2, rng.randrange(0, 5))
    st.put(OUT, "<I", n_ent)
    st.put(0x6C1E30, "<I", SURFT)
    st.put(SURFT, "<I", SURFT + 0x40)
    st.put(SURFT + 4, "<I", PKT)
    st.put(0x4D4B9C, "<I", RECIP)
    for k in range(0x400):
        st.put(RECIP + 4 * k, "<i", pick(rng, 0, rng.randrange(0, 0x1000)))
    same = rng.randrange(4) != 0
    v = r32(rng)
    st.put(0x4B2C60, "<I", v)
    st.put(0x4D58E8, "<I", v if same else r32(rng))
    v = r32(rng)
    st.put(0x4D58F4, "<I", v)
    st.put(0x4D58F8, "<I", v if (same or rng.randrange(2)) else r32(rng))
    # the kind records
    for k in range(2):
        rec = SURFT + 0x100 + 0x100 * k
        st.put(SURFT + 0x40 + 4 * k, "<I", rec)
        st.put(rec + 8, "<I", pick(rng, 2, 3, 4, rng.randrange(0, 8)))
        st.put(rec + 0x20, "<I", VERTS + 0x800 * k)
        st.put(rec + 0x50, "<I", PP + 0x100 * k)
        for lvl in range(2, 5):
            st.put(rec + 0x4C + 4 * lvl, "<I", PP + 0x200 + 0x100 * lvl + 0x400 * k)
            sl = STEPS + 0x100 * k + 0x40 * lvl
            st.put(rec + 0x20 + 4 * lvl, "<I", sl)
            cnt = pick(rng, 2, 3, 5, rng.randrange(0, 9))
            st.put(sl, "<I", cnt | (r32(rng) & 0xFFFF0000))
            for j in range(cnt + 2):
                st.put(sl + 4 + 2 * j, "<H", pick(rng, 8, 8, 16, 24))
    for k in range(0x1E0):
        st.put(PP + 8 * k, "<H", rng.randrange(0, 16))
    for k in range(0x800):
        a = VERTS + 8 * k
        st.putb(a, struct.pack("<2h", rng.randrange(-0x2000, 0x2000), rng.randrange(-0x2000, 0x2000)))
        st.put(a + 4, "<I", rng.randrange(0, 0x400) << 22 | rng.getrandbits(1) << 18 | rng.randrange(0, 0x10000)
               | rng.getrandbits(2) << 16)
    for k in range(0x400):
        st.put(PAL + 4 * k, "<I", r32(rng))
    # the light records
    for k in range(16):
        a = 0x5D8000 + 0x40 * k
        for c in range(3):
            st.put(a + 4 * c, "<i", rng.randrange(-0x2000, 0x2000) << 16 | rng.getrandbits(16))
        st.put(a + 0xC, "<I", r32(rng))
        st.put(a + 0x10, "<H", pick(rng, 14, 16, 18, 20, rng.randrange(0, 32)))
    # the strip offsets and flags
    total = 0
    for i in range(n_ent):
        e = OUT + 4 + 8 * i
        lvl = pick(rng, 1, 2, 3, 4)
        rows = pick(rng, 0, 1, 2, 3, rng.randrange(0, 6))
        st.putb(e, struct.pack("<4H", rng.randrange(0, 2), rng.randrange(0, 4), rows, lvl))
        st.put(0x5D91C0 + 4 * i, "<I", total)
        total += rows + 1
    st.put(0x5D91C0 + 4 * n_ent, "<I", total)
    lights = [0] + [rng.randrange(1, 16) << 20 * 0 | rng.randrange(0, 16) << 10 | rng.randrange(0, 16) << 20
                    for _ in range(4)]
    for k in range(total + 4):
        st.put(0x5D7540 + 4 * k, "<I", pick(rng, 0, 0x80000000) | pick(rng, *lights))
    st.script["palette"] = [PAL]
    st.script["light_colour"] = [r32(rng) for _ in range(0x200)]
    return [OUT, pick(rng, 0, 1, 1, 2)], False


def glow(st, rng, g):
    rec = g + 0x1800
    mdl = g + 0x1880
    faces = g + 0x1900
    count = pick(rng, 0, 1, 4, rng.randrange(0, 16))
    st.put(g + 0x1108, "<B", pick(rng, 0, 1, 1, 1))
    st.put(g + 0x1109, "<B", rng.randrange(0, 16))
    st.put(g + 0x110C, "<I", rec)
    st.put(rec + 0x10, "<I", mdl)
    st.put(mdl + 6, "<H", count)
    st.put(rec + 0x28, "<I", faces)
    st.put(rec + 0x14, "<I", GLOWP)
    st.put(rec + 0x18, "<I", GLOWP + 0x900)
    for f in range(16):
        b = [rng.randrange(1, 13) for _ in range(3)] + [pick(rng, 0, 0, rng.randrange(1, 13))]
        st.putb(faces + 8 * f, bytes(b) + rng.randbytes(4))
    for v in range(1, 13):  # record 0 overlaps the glow's header (+0x1108)
        a = g + 0x10FC + 0x18 * v
        st.put(a, "<I", pick(rng, 0, 0, 1, 2, 4, 8, 0x10, 0x20, 3))
        st.putf(a + 4, pick(rng, rng.uniform(-100, 0x9000), rng.uniform(0, 0x4000), rng.uniform(0, 300)))
        st.putf(a + 8, rng.uniform(-50, 700))
        st.putf(a + 0xC, rng.uniform(-50, 500))
        st.putf(a + 0x10, rng.uniform(-2, 2))
        st.putf(a + 0x14, rng.uniform(-2, 2))
    for c in range(3):
        st.put(g + 0x13F8 + c, "<B", pick(rng, 0, 0xFF, rng.getrandbits(8)))
    st.put(g + 0x13FB, "<B", rng.getrandbits(8))
    st.put(g + 0x13FC, "<B", rng.getrandbits(8))
    st.put(g + 0x13FD, "<B", pick(rng, 0, 0, 1, 2, 3))
    idx = [pick(rng, -1, rng.randrange(0, 16)) for _ in range(8)]
    st.putb(g + 0x13FE, bytes(i & 0xFF for i in idx))


def g_glow(st, rng):
    g = CAR(rng.randrange(3))
    glow(st, rng, g)
    st.put(0x6CCE94, "<I", pick(rng, 0, 1))
    st.put(0x6CCD84, "<B", rng.getrandbits(8))
    return [g, OT + 4 * rng.randrange(0x200) | pick(rng, 0, 0x80000000),
            OT + 4 * rng.randrange(0x200) | pick(rng, 0, 0x80000000)], True


def glow_tex(st, rng, g):
    st.put(g + 0xA, "<h", pick(rng, 0, 3, 4, 5, -1, rng.randrange(-1, 0x40)))
    st.put(g + 4, "<I", g + 0x400)
    st.put(g + 0x400 + 0x14, "<I", GLOWP)
    st.put(g + 0x400 + 0x18, "<I", GLOWP + 0x900)
    st.putb(g + 0x2F6, bytes(pick(rng, 0xFF, rng.randrange(0, 16)) for _ in range(8)))


def g_glow_tex(st, rng):
    st.put(TEX + 0xC00 + 0x2E, "<H", rng.randrange(0, 12))
    g = ARG + 0x100
    glow_tex(st, rng, g)
    return [g if rng.randrange(4) else g], False


def g_cars_glow(st, rng):
    st.put(TEX + 0xC00 + 0x2E, "<H", rng.randrange(0, 12))
    st.put(ARG + 0x800 + 0x54D, "<B", pick(rng, 0, 1, 3, 6))
    for i in range(6):
        st.put(0x6BFB80 + 0x274 * i, "<I", CAR(i % 3))
        glow_tex(st, rng, CAR(i % 3) + 0x1108)
    return [], False


def debris(st, rng):
    st.put(0x5D9290, "<I", DEB)
    st.put(0x5D928C, "<I", pick(rng, 0, 63, rng.randrange(0, 64)))
    st.put(0x5D9288, "<I", r32(rng))
    st.put(0x4B11D8, "<I", r32(rng))
    for k in range(64):
        st.put(DEB + 0x180 * k + 0x2C, "<B", pick(rng, 0, 0xFF, 1))


def g_burst(st, rng):
    debris(st, rng)
    return [r32(rng), r32(rng), r32(rng), pick(rng, 0, 1, 3, rng.randrange(0, 70)), r32(rng), r32(rng)], False


def g_break(st, rng):
    debris(st, rng)
    obj = OBJ
    st.put(0x6BFB14, "<I", ARG + 0x400)
    kind = rng.randrange(0, 8)
    st.put(obj + 0x190, "<H", kind)
    for k in range(8):
        st.put(ARG + 0x400 + 16 * k + 6, "<B", pick(rng, 0, 1, 3, 6))
    st.put(0x6BFB10, "<I", ARG + 0x600)
    st.put(obj + 0x18E, "<H", rng.randrange(0, 16))
    for k in range(32):
        st.put(ARG + 0x600 + 8 * k + 3, "<B", pick(rng, 0, 1, 7, rng.getrandbits(8)))
    st.put(obj + 0x10, "<I", OBJ + 0x400)
    st.put(OBJ + 0x400 + 0x34, "<H", 0x40)
    st.put(obj + 0x194, "<I", OBJ + 0x800)
    for k in range(8):
        st.put(OBJ + 0x800 + 4 * k, "<I", pick(rng, 0x2000000, r32(rng), 0x2123456))
    return [obj], True


TESTS = {
    "texture_anims_step": (0x478DF0, g_anims),
    "text_messages_draw": (0x4792A0, g_text),
    "time_warp_view_effect": (0x479940, g_warp),
    "time_warp_wobble_step": (0x479A80, g_wobble),
    "ps_wait_none": (0x479AE0, g_none),
    "view_surface_tint": (0x479B30, g_tint),
    "view_depth_markers_link": (0x479F60, g_markers),
    "track_runs_visible": (0x47A0D0, g_visible),
    "track_run_split": (0x47A1D0, g_split),
    "track_quad_in_range": (0x47A440, g_quad),
    "track_runs_diff_texture": (0x47A500, g_diff),
    "car_glow_packets_draw": (0x47B470, g_glow),
    "glow_texture_cycle": (0x47BC80, g_glow_tex),
    "cars_glow_texture_cycle": (0x47BD20, g_cars_glow),
    "debris_burst": (0x47BE70, g_burst),
    "object_debris_break": (0x47C110, g_break),
    "track_runs_light_flags": (0x47C420, g_flags),
    "track_runs_light": (0x47C730, g_light),
}

SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


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
    r = m.call(target, args)
    if r[0] == "ok":
        r = ("ok", r[1]) if returns else ("ok",)
        r = r + tuple(m.uc.reg_read(x) for x in SAVED)
        r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
    last = struct.unpack("<I", m.read(0x6CCD74, 4))[0]
    if on_stack(last):  # the transforms' record pointer: a stack buffer in both
        m.write(0x6CCD74, b"STK!")
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
    o = run_once(m, stubs, st, m.symbols[name], args, cw, 0xCC, returns)
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
        msg += f"\n  calls from #{k} (of {len(la)} / {len(lo)}): orig {la[k:k + 2]}\n                ours {lo[k:k + 2]}"
    return msg, True


def func_sizes():
    sizes = {}
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "groups", "pq_owned.txt")
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
    base = {a: m.read(a, n) for a, n in REGIONS}
    rng = random.Random(0x91)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        seen = set()
        size = sizes.get(address, 0)
        hook = None
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address,
                                 end=address + size - 1)
        for _ in range(iterations):
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
