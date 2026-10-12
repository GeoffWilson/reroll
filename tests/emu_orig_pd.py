#!/usr/bin/env python3
"""Differential test of orig_pd.c (group PD: screen tints, the blast flare
and shock wave rings, the text file reader and the front end's string
objects, 0x42be20-0x42f6a0) against the original, on random state.

Callees outside the group that log, allocate or are empty stand-ins are
logging stubs in both runs (the call log records as many arguments as the
callee reads, strings by content): the debug log 0x496340 and 0x476ec0,
malloc 0x4982c2 (scripted pointers), the empty 0x479ae0 (scripted 0 / 1).
Pure game code runs for real in both: the transform pointers' targets
(0x426a20 / 0x426b00, the 3DNow! 0x4268c0 / 0x4291f0), sin / cos tables,
_ftol, strncpy, _chkstk, the glyph builder 0x42fd90 and its helpers
(0x42fd10, 0x42fd60, 0x42fcd0, 0x42fc40, 0x4270e0), the matrix helpers,
the ordering-table links 0x4290a0 / 0x429080 and 0x485050. The emulated
CPU is a Phenom (3DNow!); the x87 control word is 0x27f, 0x37f or 0x07f.
Compared: return value, ebx / esi / edi / ebp, the x87 stack top and tag
word, the globals and scratch memory, the call log.

    tests/emu_orig_pd.py EXE build/emu_pd.elf [iterations] [names...] [-v] [-c]
"""
import random
import re
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
SCR_SIZE = 0x180000
RACE = SCR + 0x0000
ARG = SCR + 0x1000  # argument blocks, output buffers (0x1000)
STR = SCR + 0x2000  # format strings, names
STR2 = SCR + 0x3000  # string arguments
TEXT = SCR + 0x4000  # the text file (0x4000)
TRACK = SCR + 0x8000
PAL = SCR + 0x9000  # palette (0x2000)
FLARE0 = SCR + 0xB000
FLARE1 = SCR + 0xC000
SHOCK = SCR + 0xD000  # 3 x 0xf14
OTS = SCR + 0x10000
CARS = SCR + 0x11000
VIEW = SCR + 0x13000
FONTS = SCR + 0x14000  # 4 x 0x1200
SPK = SCR + 0x19000  # the single packet (*0x4be2e8)
RECS = SCR + 0x20000  # 128 x 0x14c
GLYPHS = SCR + 0x30000  # 0x10000
PK0 = SCR + 0x40000  # 0x42000
PK1 = SCR + 0x90000  # 0x42000

REGIONS = [
    (0x4AC72C, 4),
    (0x4ACC38, 8),
    (0x4B1960, 4),
    (0x4BE100, 0x220),
    (0x6C1E14, 4),
    (0x6C1EB0, 4),
    (0x6CB4E0, 0x20),
    (0x6CC800, 0x1F00),
]

# argument kinds: v value, s string
STUBS = {
    0x496340: ("debug_log", "ss"),
    0x476EC0: ("bad_char", "sv"),
    0x4982C2: ("malloc", "v"),
    0x479AE0: ("idle", "v"),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


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
        self.note = None
        self.put(0x4B1960, "<I", RACE)

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

    def putb(self, address, data):
        if SCR <= address < SCR + SCR_SIZE:
            self.scr[address - SCR:address - SCR + len(data)] = data
            return
        a = self.find(address)
        self.mem[a][address - a:address - a + len(data)] = data

    def get(self, address, fmt):
        if SCR <= address < SCR + SCR_SIZE:
            return struct.unpack_from(fmt, self.scr, address - SCR)[0]
        a = self.find(address)
        return struct.unpack_from(fmt, self.mem[a], address - a)[0]


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
            else:
                try:
                    logged.append(rd(self.m, a, 0x100).split(b"\0")[0])
                except unicorn.UcError:
                    logged.append(("bad pointer", a))
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


def set_paths(st, rng):
    path = pick(rng, (0x426A20, 0x426B00), (0x426A20, 0x426B00), (0x4268C0, 0x4291F0))
    st.put(0x6CCD78, "<I", path[0])
    st.put(0x6CCD80, "<I", path[1])


def set_ot(st, base, n):
    for k in range(n):
        st.put(base + 4 * k, "<I", 0xFFFFFFFF if k == 0 else (base + 4 * (k - 1)) | 0x80000000)


def set_draw_common(st, rng):
    st.put(0x6C1EB0, "<I", TRACK)
    st.put(TRACK + 0x2E, "<H", rng.randrange(0, 0x40))
    st.put(0x6C1E14, "<I", PAL)
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CDD10, rng.getrandbits(32)))
    st.put(0x6CCD84, "<I", rng.getrandbits(32))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    set_ot(st, 0x6CD510, 0x200)
    set_ot(st, 0x6CDD80, 0x200)


# ---------------------------------------------------------------- effect generators

def g_shake(st, rng, m):
    st.put(0x6CCB60, "<H", pick(rng, 0, 1, 1, 1, rng.getrandbits(16)))
    st.put(0x6CCB62, "<H", pick(rng, 0, 1, 2))
    st.put(0x6CCB64, "<i", pick(rng, 0, 1, 2, rng.randrange(0, 20), rng.randrange(0, 20), rng.getrandbits(32)))
    st.put(0x6CCB68, "<i", pick(rng, rng.randrange(0, 12), rng.randrange(0, 12), rng.getrandbits(32)))
    st.put(RACE + 0x553, "<B", pick(rng, 0, 0, 0, 1, 2))
    near = rng.randrange(4)
    for k in range(3):
        o = rng.getrandbits(32)
        st.put(0x6CCB6C + 4 * k, "<I", o)
        d = pick(rng, rng.randrange(-0x400000, 0x400000), rng.randrange(-0x4000000, 0x4000000),
                 rng.getrandbits(32)) if near else rng.getrandbits(32)
        st.put(CARS + 0x34 + 4 * k, "<I", o + d)
    st.put(0x4BE134, "<I", rng.getrandbits(32))
    return [CARS], None


def g_fade(st, rng, m):
    st.put(0x6CCD90, "<I", rng.getrandbits(32))
    st.put(0x4BE134, "<I", pick(rng, 0, rng.getrandbits(32)))
    return [pick(rng, rng.randrange(-0x40, 0x220), rng.randrange(0x1F0, 0x210), rng.getrandbits(32),
                 -rng.randrange(0, 0x80000))], None


def g_tint_add(st, rng, m):
    st.put(0x4BE134, "<I", pick(rng, 0, rng.getrandbits(32), rng.getrandbits(24)))
    return [rng.getrandbits(32)], 0xFFFFFFFF


def g_flash_set(st, rng, m):
    st.put(0x4AC72E, "<H", pick(rng, 0x63, rng.randrange(-5, 0x70), rng.getrandbits(16)))
    return [pick(rng, rng.randrange(-5, 0x70), rng.getrandbits(32))] + [rng.getrandbits(32) for _ in range(5)], None


def g_tint_draw(st, rng, m):
    st.put(0x6CCD84, "<I", pick(rng, 0, 4, rng.getrandbits(32)))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, VIEW, VIEW))
    st.put(0x6CD4A0 + 0x74, "<I", (OTS + 4 * rng.randrange(0, 0x100)) | pick(rng, 0, 0x80000000))
    st.put(VIEW + 0x74, "<I", (OTS + 4 * rng.randrange(0, 0x100)) | pick(rng, 0, 0x80000000))
    st.put(0x4BE138, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(0x4BE134, "<I", pick(rng, 0, rng.getrandbits(32)))
    st.script["idle"] = [pick(rng, 0, 0, 1)]
    return [rng.getrandbits(32)], None


def g_flare(st, rng, m):
    set_gte_float(st, rng)
    set_paths(st, rng)
    set_draw_common(st, rng)
    st.put(0x4BE1A8, "<i", pick(rng, 0, 1, 10, 11, 19, 20, rng.randrange(1, 21), rng.randrange(1, 21),
                                rng.randrange(-3, 40)))
    for k in range(21):
        st.put(0x4BE156 + 2 * k, "<H", pick(rng, rng.randrange(0, 0x2000), rng.getrandbits(16)))
    st.put(0x4BE180, "<I", s16(rng, 3000) | s16(rng, 3000) << 16)
    st.put(0x4BE184, "<I", s16(rng, 6000) | rng.getrandbits(16) << 16)
    st.put(0x4BE1A0, "<I", FLARE0)
    st.put(0x4BE1A4, "<I", FLARE1)
    return [], None


def g_shock(st, rng, m):
    set_gte_float(st, rng)
    set_paths(st, rng)
    set_draw_common(st, rng)
    st.put(0x6CC800, "<I", SHOCK)
    for r in range(3):
        row = SHOCK + r * 0xF14
        st.put(row + 0x0, "<i", rng.randrange(-2000, 2000) << 16 | rng.getrandbits(16))
        st.put(row + 0x4, "<i", rng.randrange(-500, 500) << 16 | rng.getrandbits(16))
        st.put(row + 0x8, "<i", pick(rng, rng.randrange(-500, 4000), rng.randrange(20000, 32000)) << 16 |
               rng.getrandbits(16))
        st.put(row + 0xC, "<i", pick(rng, 0, 1, 2, rng.randrange(3, 30), rng.randrange(3, 30), 40,
                                     rng.randrange(-0x7000, 0x7000)) if rng.randrange(40) else rng.getrandbits(32))
        st.put(row + 0x10, "<I", pick(rng, rng.randrange(0, 6), rng.getrandbits(32)))
    return [], None


# ---------------------------------------------------------------- text generators

NAMES = [b"CREDITS", b"MENU", b"AB", b"OPTIONS2", b"X"]
LANGS = [b"ENGLISH", b"FRENCH", b"GERMAN", b"ITALIAN", b"SPANISH", b"JAPANESE"]


def body(rng, n):
    out = b""
    for _ in range(n):
        out += pick(rng, b"Hello", b"x", b" ", b"  ", b"\0", b"\0~", b"\0 \t", b"\x01", b"\t", b"\0  ", b"ab cd",
                    b"\r", b"~", b"Z9")
    return out


def make_text(rng):
    parts = [rng.randbytes(rng.randrange(0, 8)).replace(b"[", b"(")]
    for name in rng.sample(NAMES, rng.randrange(1, len(NAMES))):
        parts.append(b"[" + name + pick(rng, b")", b")", b"]"))
        for lang in rng.sample(LANGS, rng.randrange(1, 4)):
            parts.append(pick(rng, b" ", b"\r\n", b"") + lang + pick(rng, b"", b" ", b"\r\n\t") + b"[" +
                         pick(rng, b"", b"\r\n", b"\x01\x02") + body(rng, rng.randrange(0, 12)) +
                         pick(rng, b"]", b"]", b"\0 ]", b"\0~ ]") + b"\r\n")
    return b"".join(parts)


def set_text_file(st, rng):
    text = make_text(rng)[:0x3800]
    st.putb(TEXT, text + b"\0" * 8)
    st.put(0x4BE2EC, "<I", pick(rng, TEXT, TEXT, TEXT, TEXT, 0))
    st.put(0x4BE2F0, "<I", pick(rng, len(text), len(text), len(text), len(text) - rng.randrange(0, 20),
                                rng.randrange(0, 40)) & 0xFFFFFFFF)
    st.put(0x4BE2F4, "<I", pick(rng, 0, 1, 2, 3, 4, 5, 6, rng.getrandbits(32)))


def put_name(st, rng):
    name = pick(rng, *NAMES, b"MISSING", b"", b"A")
    st.putb(STR, name + b"\0")
    return STR


def g_entry_copy(st, rng, m):
    set_text_file(st, rng)
    name = put_name(st, rng) if rng.randrange(10) else 0
    return [pick(rng, ARG + 0x200, ARG + 0x200, 0), pick(rng, -1, -1, 0, 1, 2, 5, 0x40, 0x800, -5), name], 0xFFFFFFFF


def g_section_find(st, rng, m):
    set_text_file(st, rng)
    return [put_name(st, rng)], 0xFFFFFFFF


def g_bytes_find(st, rng, m):
    set_text_file(st, rng)
    n = rng.randrange(0, 0x41)
    if rng.randrange(2):
        k = rng.randrange(0, 0x3000)
        st.putb(STR, bytes(st.scr[TEXT - SCR + k:TEXT - SCR + k + n]))
    return [STR, TEXT, n, pick(rng, 0, 1, rng.randrange(0, 0x300), rng.randrange(0, 0x3000))], 0xFFFFFFFF


def g_bytes_differ(st, rng, m):
    a = rng.randbytes(0x40)
    st.putb(STR, a)
    b = bytearray(a)
    if rng.randrange(2):
        b[rng.randrange(0, 0x40)] ^= 1 << rng.randrange(8)
    st.putb(STR2, bytes(b))
    return [STR, STR2, rng.randrange(0, 0x41)], 0xFFFFFFFF


def g_line_get(st, rng, m):
    set_text_file(st, rng)
    name = put_name(st, rng) if rng.randrange(10) else 0
    return [pick(rng, ARG + 0x200, ARG + 0x200, 0), pick(rng, -1, -1, 0, 1, 2, 5, 0x40, -5), name,
            pick(rng, 0, 0, 1, 2, 3, rng.randrange(0, 10))], 0xFFFFFFFF


def set_fonts(st, rng):
    n = pick(rng, 0, 1, 2, 3, 4, 4, 4)
    st.put(0x4BE314, "<i", n)
    ids = rng.sample(range(8), 4)
    for k in range(4):
        f = FONTS + k * 0x1200
        st.put(0x6CB4E0 + 4 * k, "<I", f)
        st.put(f + 0, "<I", ids[k])
        st.put(f + 4, "<I", pick(rng, 0x100, 0x80, 0x180, rng.randrange(0, 0x400)))
        st.put(f + 8, "<I", pick(rng, 16, 20, rng.randrange(0, 40)))
        st.put(f + 0x10, "<I", pick(rng, 6, 10, rng.randrange(0, 20)))
        st.put(f + 0x14, "<I", pick(rng, 0, 1, rng.randrange(0, 8)))
        st.put(f + 0x18, "<I", pick(rng, 0, 1))
        for c in range(256):
            st.put(f + 0x101C + c, "<B", pick(rng, rng.randrange(0, 0x80), rng.randrange(0, 0x80), 0xFF))
        for g in range(0x80):
            rec = f + 0x1C + 16 * g
            st.put(rec + 2, "<H", rng.randrange(0, 8))
            st.put(rec + 4, "<H", rng.randrange(1, 24))
            st.put(rec + 6, "<H", rng.randrange(1, 24))
    return ids


def set_strings(st, rng):
    st.put(0x4BE310, "<I", RECS)
    st.put(0x4BE304, "<I", GLYPHS)
    st.put(0x4BE308, "<I", PK0)
    st.put(0x4BE30C, "<I", PK1)
    st.put(0x4BE2E8, "<I", SPK)
    st.put(0x4BE2FC, "<I", pick(rng, rng.randrange(0, 0x100), rng.randrange(0, 0x7F0), rng.randrange(0x7F8, 0x808),
                                rng.randrange(0, 0x100) | rng.getrandbits(16) << 16))
    st.put(0x4BE2F8, "<i", pick(rng, rng.randrange(0, 0x80), 0x7F, -1, 0x80, rng.getrandbits(32)))
    words = []
    for _ in range(0x800 * 4):
        words += [rng.randrange(-300, 700), rng.randrange(-300, 500), pick(rng, 0, 0, 0, -1), rng.randrange(-9, 9)]
    st.putb(GLYPHS, struct.pack("<%dh" % len(words), *words))
    for i in range(0x80):
        r = RECS + i * 0x14C
        flags = rng.getrandbits(16)
        if rng.randrange(3):
            flags |= 0xC000
        st.put(r, "<H", flags)
        first = rng.randrange(0, 0x700)
        count = pick(rng, 0, rng.randrange(1, 0x40), rng.randrange(1, 0x40))
        lines = pick(rng, 0, 1, rng.randrange(1, 8), rng.randrange(1, 8), rng.randrange(0, 33))
        st.put(r + 2, "<H", first)
        for off in (4, 8, 0xC):
            st.put(r + off, "<i", pick(rng, rng.randrange(-200, 400), rng.getrandbits(32)))
        st.put(r + 0x74, "<H", rng.getrandbits(16))
        st.put(r + 0x76, "<H", rng.getrandbits(16))
        st.put(r + 0x78, "<H", lines)
        st.put(r + 0x7A, "<H", count)
        starts = sorted(rng.randrange(first, first + count + 1) for _ in range(lines + 1))
        for k in range(min(lines + 1, 33)):
            st.put(r + 0xBC + 2 * k, "<H", starts[k])
        for k in range(min(lines, 32)):
            st.put(r + 0xFE + 2 * k, "<h", pick(rng, -1, -1, rng.randrange(starts[k], starts[k + 1] + 1),
                                               rng.randrange(first, first + count + 2)))
        st.put(r + 0x140, "<I", GLYPHS + 32 * first)
        st.put(r + 0x144, "<I", PK0 + 0x84 * first)
        st.put(r + 0x148, "<I", PK1 + 0x84 * first)


def index(rng):
    return pick(rng, rng.randrange(0, 0x80), rng.randrange(0, 0x80), rng.randrange(0, 0x80), -1, -2, 0x80, 0x7F)


def safe_index(rng):
    return rng.randrange(0, 0x80)


def g_font_find(st, rng, m):
    ids = set_fonts(st, rng)
    return [pick(rng, *ids, rng.randrange(0, 10), rng.getrandbits(32))], 0xFFFFFFFF


def g_alloc(st, rng, m):
    st.put(0x4BE300, "<I", pick(rng, 0, 0, 1))
    st.put(0x4BE310, "<I", RECS)
    st.script["malloc"] = [pick(rng, PK0, PK0, 0), pick(rng, PK1, PK1, 0), pick(rng, GLYPHS, GLYPHS, 0),
                           pick(rng, RECS, RECS, RECS, 0)]
    return [], 0xFFFFFFFF


COLOURS = ARG + 0x800
FMT_TOKENS = [b"A", b"b", b"Hi", b" ", b"  ", b"\t", b"~>", b"~P0", b"~P1", b"~P2", b"~P3", b"~N1", b"~S", b"~D", b"~05",
              b"~0", b"~09", b"~012", b"~X", b"~", b"\0", b"\0x", b"xyz", b"\xd6", b"7"]


def set_va(st, rng):
    """va: [number, string, number, string]."""
    va = ARG + 0x900
    st.put(va, "<I", pick(rng, 0, 7, 123456789, rng.getrandbits(32), rng.randrange(0, 100000)))
    s = pick(rng, b"", b"ab", b"a b", b"x~>y", b"\tq", b"long string here", b"~")
    st.putb(STR2, s + b"\0")
    st.put(va + 4, "<I", STR2)
    st.put(va + 8, "<I", pick(rng, 0, 42, rng.getrandbits(32)))
    st.putb(STR2 + 0x100, pick(rng, b"", b"zz", b"  ") + b"\0")
    st.put(va + 12, "<I", STR2 + 0x100)
    return va


def make_format(rng, va_ok=True):
    out = b""
    for _ in range(rng.randrange(0, 14)):
        t = rng.choice(FMT_TOKENS)
        if t in (b"~S", b"~D", b"~05", b"~0", b"~09", b"~012"):
            t = (b"~P1" if t == b"~S" else b"~P0") + t if rng.randrange(4) else t
        out += t
    # a number width over 61 makes the original read its saved registers (skip < -0x34): not reproduced
    return re.sub(rb"~0(\d+)", lambda mo: b"~0" + (mo.group(1) if int(mo.group(1)) <= 61 else b"5"), out)


def g_build(st, rng, m):
    set_strings(st, rng)
    ids = set_fonts(st, rng)
    va = set_va(st, rng)
    st.putb(COLOURS, rng.randbytes(16))
    if rng.randrange(25) == 0:  # every record in use: no free one (0x42fd10 -1)
        for i in range(0x80):
            st.put(RECS + i * 0x14C + 1, "<B", st.get(RECS + i * 0x14C + 1, "<B") | 0x40)
    fmt = make_format(rng)
    st.putb(STR, fmt + b"]" + b"\0")
    st.note = fmt
    return [pick(rng, *ids, ids[0], 9), STR, pick(rng, COLOURS, 0), rng.getrandbits(32) & pick(rng, 0xFFFF, 0x18, 0x8),
            pick(rng, 0, 0, 12, rng.getrandbits(32)), va], 0xFFFFFFFF


def g_printf(st, rng, m):
    set_strings(st, rng)
    ids = set_fonts(st, rng)
    va = set_va(st, rng)
    st.putb(COLOURS, rng.randbytes(16))
    fmt = make_format(rng)
    if rng.randrange(20) == 0:
        fmt = (b"ab" * 0x90)[:rng.randrange(0xFD, 0x110)]
    st.putb(STR, fmt + b"\0")
    st.note = fmt
    vals = [st.get(va + 4 * k, "<I") for k in range(4)]
    return [pick(rng, *ids, ids[0], 9), pick(rng, COLOURS, 0), rng.getrandbits(32) & pick(rng, 0xFFFF, 0x18, 0x8),
            pick(rng, 0, 0, 12, rng.getrandbits(32)), STR] + vals, 0xFFFFFFFF


def g_index(st, rng, m):
    set_strings(st, rng)
    return [index(rng)], 0xFFFFFFFF


def g_index_void(st, rng, m):
    set_strings(st, rng)
    return [index(rng)], None


def g_on(st, rng, m):
    set_strings(st, rng)
    return [index(rng)] + [rng.getrandbits(32) for _ in range(4)], None


def g_line_range(st, rng, m):
    set_strings(st, rng)
    return [safe_index(rng), pick(rng, -1, -1, 0, 1, 2, rng.randrange(-3, 34)), ARG, ARG + 4, ARG + 8], 0xFFFFFFFF


def g_limits(st, rng, m):
    set_strings(st, rng)
    outs = [pick(rng, ARG + 4 * k, 0) for k in range(4)]
    return [index(rng), pick(rng, -1, -1, 0, 1, 2, rng.randrange(-3, 34))] + outs, 0xFFFFFFFF


def g_limits_all(st, rng, m):
    set_strings(st, rng)
    return [index(rng)] + [pick(rng, ARG + 4 * k, 0) for k in range(4)], 0xFFFFFFFF


def g_set_position(st, rng, m):
    set_strings(st, rng)
    return [index(rng), rng.randrange(-100, 700), rng.randrange(-100, 500), pick(rng, 0, 1, 2, 3, -1),
            pick(rng, 0, 1, 2, 3, -1)], 0xFFFFFFFF


def g_set_position_j(st, rng, m):
    set_strings(st, rng)
    return [index(rng), rng.randrange(-100, 700), pick(rng, 0, 1, 2, 3, -1)], 0xFFFFFFFF


def g_colour_range(st, rng, m):
    set_strings(st, rng)
    st.putb(COLOURS, rng.randbytes(16))
    return [index(rng), pick(rng, -1, -1, 0, 1, 2, rng.randrange(-3, 34)),
            pick(rng, -1, -1, 0, 1, 3, -2, -3, rng.randrange(-40, 40)), pick(rng, -1, 0, 1, 5), COLOURS], 0xFFFFFFFF


def g_colour_set(st, rng, m):
    set_strings(st, rng)
    st.putb(COLOURS, rng.randbytes(16))
    return [index(rng), pick(rng, -1, 0, 1, 5), COLOURS], 0xFFFFFFFF


def g_clip(st, rng, m):
    set_strings(st, rng)
    return [index(rng), pick(rng, 0, -1, 1, rng.randrange(1, 400), rng.randrange(1, 400)), pick(rng, 0, 1, 2, 3, 4)], None


def g_draw(st, rng, m):
    set_strings(st, rng)
    st.put(0x4ACC38, "<I", pick(rng, 640, 800, 1024, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600, 768, rng.randrange(1, 4000), rng.getrandbits(32)))
    for i in range(0x80):  # most records hidden, so the chains stay short
        r = RECS + i * 0x14C
        if rng.randrange(6):
            st.put(r, "<H", st.get(r, "<H") & 0x7FFF)
    ot = OTS + 4 * rng.randrange(0, 0x100)
    return [ot | pick(rng, 0, 0x80000000), rng.getrandbits(32), pick(rng, 0, 1, rng.getrandbits(32))], 0xFFFFFFFF


TESTS = {
    "shake_flash_tint": (0x42BE20, g_shake),
    "fade_tint_add": (0x42BF20, g_fade),
    "screen_tint_add": (0x42BFC0, g_tint_add),
    "screen_flash_set": (0x42C040, g_flash_set),
    "screen_tint_draw": (0x42C0A0, g_tint_draw),
    "blast_flare_draw": (0x42C420, g_flare),
    "shock_waves_draw": (0x42C950, g_shock),
    "font_by_id": (0x42E050, g_font_find),
    "text_buffers_alloc": (0x42E2A0, g_alloc),
    "text_entry_copy": (0x42E3D0, g_entry_copy),
    "text_section_find": (0x42E4B0, g_section_find),
    "text_bytes_find": (0x42E620, g_bytes_find),
    "text_bytes_differ": (0x42E690, g_bytes_differ),
    "text_line_get": (0x42E6D0, g_line_get),
    "string_printf": (0x42E810, g_printf),
    "string_width_printf": (0x42E8F0, g_printf),
    "string_active": (0x42EA00, g_index),
    "string_on": (0x42EA40, g_on),
    "string_off": (0x42EAB0, g_index_void),
    "string_line_range": (0x42EAE0, g_line_range),
    "string_limits": (0x42EBC0, g_limits),
    "string_limits_all": (0x42ED10, g_limits_all),
    "string_set_position": (0x42ED40, g_set_position),
    "string_set_position_j": (0x42EED0, g_set_position_j),
    "string_get_width": (0x42F030, g_index),
    "string_get_height": (0x42F070, g_index),
    "string_has_marks": (0x42F0B0, g_index),
    "string_colour_range": (0x42F0F0, g_colour_range),
    "string_colour_set": (0x42F2B0, g_colour_set),
    "string_clip_x": (0x42F2D0, g_clip),
    "strings_draw": (0x42F3C0, g_draw),
    "string_build": (0x42F6A0, g_build),
}


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


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, mask = gen(st, rng, m)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
            r = r + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        last = struct.unpack("<I", rd(m, 0x6CCD74, 4))[0]
        if E.STACK <= last < E.STACK + E.STACK_SIZE:  # the transforms' record pointer: a stack buffer in both
            m.write(0x6CCD74, b"STK!")
        mem = rd(m, SCR, SCR_SIZE) + b"".join(rd(m, a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None) if a != b else None
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
            print("  differ:", [(hex(SCR + i if i < SCR_SIZE else i), a[i:i + 4].hex(), b[i:i + 4].hex())
                                for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if st.note is None else f" [{st.note!r}]") + ("" if where is None else f" (memory differs at {where:#x})") + logs, True
    return None, results[0][0][0] != "ok"


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pd_owned.txt"):
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
    stubs = Stubs(m)
    rng = random.Random(37)
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
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg, fault = run_case(m, stubs, name, address, gen, rng)
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
