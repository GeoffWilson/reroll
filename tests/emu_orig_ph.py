#!/usr/bin/env python3
"""Differential test of orig_ph.c (group PH: object triggers, the glyph sheet
packer, the K&R heap, menu text fitting / cursor bar / sliders / measuring,
option steps, menu tiles and frame sprites, 0x4427a0-0x453130) against the
original, on random state.

Callees outside the group are logging stubs in both runs (the call log
records as many arguments as the callee reads; strings by content) with
scripted return values; 0x4328c0 (the pulsing colour) writes scripted bytes
into its buffer. Pure code runs for real in both: bitmap_scan 0x443260,
mem_fill 0x476f50, mem_copy 0x476f30 and the C runtime's _ftol. GlobalAlloc
is a logging stub in the import slot. The x87 control word is 0x27f, 0x37f
or 0x07f; besides memory, the return value, the call log, ebx / esi / edi /
ebp and the x87 stack top are compared.

    tests/emu_orig_ph.py EXE build/emu_ph.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG, UC_X86_REG_CR4)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x40000
CODE = 0x02000000  # import stubs
STR = SCR + 0x100
STR2 = SCR + 0x200
ROWS = SCR + 0x1000  # menu rows (5 x 0x730) / trigger table / item
OUT = SCR + 0x3000  # glyph records
PAL = SCR + 0x6000
PIX = SCR + 0x8000
ARENA = SCR + 0x10000  # heap blocks (0x8000)
BUF = SCR + 0x20000  # glyph page buffer (0x10800) / sbrk area
GMEM = SCR + 0x38000  # GlobalAlloc's block (only its first bytes are touched)

REGIONS = [
    (0x4ACC38, 8),
    (0x4B0610, 0x20),
    (0x4BE320, 4),
    (0x4C1BF0, 0x414),
    (0x4C2430, 0x50),
    (0x4C2920, 0x1A0),
    (0x4C2AE0, 0x538),
    (0x4C30E0, 0x6800),
    (0x4CB460, 0x180),
    (0x4CD950, 0x21B0),
    (0x6BFB34, 4),
]


def S(name, kinds):
    return (name, kinds)


# argument kinds: v value, s string, b 16-byte buffer filled from the script (0x4328c0)
STUBS = {
    0x441370: S("effect_start", "v"),
    0x47DCA0: S("sound", "v"),
    0x476BC0: S("block_alloc", "v"),
    0x476C00: S("block_free", "v"),
    0x477530: S("image_load", "vvv"),
    0x477810: S("image_release", "v"),
    0x428C80: S("texture_handle", ""),
    0x428C90: S("texture_upload", "vvvvvv"),
    0x41C970: S("fatal", "s"),
    0x42E8F0: S("width_printf", "vvvvvs"),
    0x430EA0: S("sprite_gouraud", "vvvvvvvv"),
    0x4270E0: S("flag2", "vv"),
    0x427AB0: S("ot_add", "vv"),
    0x431670: S("tex_w", "v"),
    0x4316A0: S("tex_h", "v"),
    0x4328C0: S("pulse", "bvvv"),
    0x457D10: S("quad_link", "vvvvv"),
    0x4314D0: S("flat_quad", "vvvvvvv"),
    0x431300: S("sprite_512", "vvvvvvvv"),
    0x429080: S("ot_link", "vvv"),
    0x4290A0: S("dword_store", "vv"),
    0x42E810: S("string_printf", "vvvvvvvvv"),
    0x42F030: S("str_w", "v"),
    0x42F070: S("str_h", "v"),
    0x42F0B0: S("str_marks", "v"),
    0x42F2D0: S("str_clip", "vvv"),
    0x4979F0: S("sprintf", "vsv"),
    0x42DBF0: S("movie_win", "v"),
    0x4474E0: S("hold_clear", ""),
    0x447500: S("hold_reset", ""),
}
IMPORTS = {0x4A3114: ("GlobalAlloc", 2)}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def pick(rng, *choices):
    return rng.choice(choices)


def mk_float(rng):
    k = rng.randrange(10)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 100.0, 255.0]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 1, 0x80000001, 0x7F7FFFFF])
    return fbits(rng.uniform(-50.0, 300.0))


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}

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

    def cstr(self, address, text):
        self.putb(address, text + b"\0")


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        for i, slot in enumerate(IMPORTS):
            a = CODE + i * 0x10
            m.uc.hook_add(UC_HOOK_CODE, self._import(slot), begin=a, end=a)

    def patch_imports(self):
        for i, slot in enumerate(IMPORTS):
            self.m.write(slot, struct.pack("<I", CODE + i * 0x10))

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _string(self, a):
        if a == 0 or not (0x400000 <= a < 0x6E0000 or SCR <= a < SCR + SCR_SIZE):
            return a
        return rd(self.m, a, 0x80).split(b"\0")[0]

    def _hook(self, uc, address, size, _):
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<10I", bytes(uc.mem_read(esp, 40)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind == "s":
                logged.append(self._string(a))
            elif kind == "b":
                data = self._next(name + "_bytes", bytes(16))
                uc.mem_write(a, data)
                logged.append("buf")
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])

    def _import(self, slot):
        name, n = IMPORTS[slot]

        def hook(uc, address, size, _):
            esp = uc.reg_read(UC_X86_REG_ESP)
            words = struct.unpack("<4I", bytes(uc.mem_read(esp, 16)))
            self.log.append((name, tuple(words[1:1 + n])))
            uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
            uc.reg_write(UC_X86_REG_ESP, esp + 4 + 4 * n)
            uc.reg_write(UC_X86_REG_EIP, words[0])
        return hook


# ---------------------------------------------------------------- generators

def g_none(st, rng):
    return [], None


def g_triggers(st, rng):
    for i in range(5):
        kind = pick(rng, 0, 1, 1, 2, 2, 3, 5, 5, 5, 6, 15) if rng.randrange(4) else rng.randrange(16)
        st.put(ROWS + 0x16A + 2 * i, "<H", kind << 12 | rng.randrange(0x1000 if kind != 5 else 0x800))
    st.put(0x6BFB34, "<I", SCR + 0x2000)
    return [ROWS], None


def g_glyph(st, rng):
    stride = rng.randrange(8, 200)
    height = rng.randrange(2, 48)
    st.put(0x4C2434, "<h", stride)
    st.put(0x4C2436, "<h", height)
    st.put(0x4C2432, "<h", pick(rng, stride, rng.randrange(1, stride + 1)))
    st.put(0x4C2440, "<I", PAL)
    st.put(0x4C2444, "<I", PIX)
    pix = bytearray(stride * height + 0x400)
    for _ in range(rng.randrange(0, 30)):
        x, y = rng.randrange(stride), rng.randrange(height)
        w, h = rng.randrange(1, 10), rng.randrange(1, 10)
        v = pick(rng, rng.randrange(1, 256), rng.randrange(256))
        for yy in range(y, min(height, y + h)):
            for xx in range(x, min(stride, x + w)):
                pix[yy * stride + xx] = v
    st.putb(PIX, bytes(pix))
    pal = bytearray(rng.randbytes(0x400))
    for k in range(0x100):
        if rng.randrange(3) == 0:
            pal[4 * k:4 * k + 3] = bytes(rng.randrange(0, 20) for _ in range(3))
    st.putb(PAL, bytes(pal))
    st.put(0x4C2000, "<I", BUF)
    st.put(0x4C1FF0, "<I", rng.randrange(0, 0x40))
    st.put(0x4C1FF4, "<I", rng.randrange(0, 50))
    st.put(0x4C1FF8, "<I", pick(rng, 0, rng.randrange(0, 0x101)))
    st.put(0x4C1FFC, "<I", pick(rng, 0, rng.randrange(0, 0x101)))
    st.cstr(STR, b"FONT.IMG")
    cell_w = pick(rng, rng.randrange(1, 40), rng.randrange(4, 20), 16)
    row = pick(rng, 0, rng.randrange(-2, height + 3), rng.randrange(0, height))
    rows = pick(rng, height, rng.randrange(1, height + 3))
    count = pick(rng, 0, rng.randrange(1, 12), rng.randrange(1, 30))
    if count == 0 and st.scr[0] & 1:
        st.put(0x4C2432, "<h", min(stride, cell_w * rng.randrange(1, 12)))
    st.script = {
        "block_alloc": [BUF],
        "image_load": [pick(rng, 1, 1, 1, 0)],
        "texture_handle": [rng.randrange(0, 100)],
        "texture_upload": [pick(rng, 1, 1, 1, 0) for _ in range(40)],
    }
    return [STR, cell_w, OUT, row, rows, count, pick(rng, 0, 1), pick(rng, 0, 0, 1)], 0xFFFFFFFF


def heap_state(st, rng):
    """A valid K&R heap: blocks in ARENA, some free (the list, address order,
    through the head 0x4c2460), plus an sbrk area at BUF. Returns the
    allocated blocks' addresses."""
    a = ARENA
    free, used = [], []
    while a < ARENA + 0x7000:
        units = pick(rng, rng.randrange(1, 8), rng.randrange(1, 64), rng.randrange(2, 300))
        st.put(a + 4, "<I", units)
        st.put(a, "<I", rng.getrandbits(32))
        (free if rng.randrange(3) == 0 else used).append(a)
        a += units * 8
    nodes = [0x4C2460] + free
    for k, n in enumerate(nodes):
        st.put(n, "<I", nodes[(k + 1) % len(nodes)])
    st.put(0x4C2464, "<I", 0)
    st.put(0x4C2470, "<I", pick(rng, *nodes) if rng.randrange(8) else 0)
    st.put(0x4C2474, "<I", BUF)
    st.put(0x4C2468, "<I", pick(rng, 0x10000, rng.randrange(0, 0x10000)))
    st.put(0x4C246C, "<I", BUF + pick(rng, 0, rng.randrange(0, 0x10000)))
    return used


def g_heap_free(st, rng):
    used = heap_state(st, rng)
    if st.mem[0x4C2430][0x40:0x44] == bytes(4):  # freep 0: a list head is needed
        st.put(0x4C2470, "<I", 0x4C2460)
    if used and rng.randrange(6):
        b = pick(rng, *used)
    else:
        b = BUF + rng.randrange(0, 0x100) * 8
        st.put(b + 4, "<I", rng.randrange(1, 300))
    return [b + 8], 0xFFFFFFFF


def g_heap_malloc(st, rng):
    heap_state(st, rng)
    n = pick(rng, rng.randrange(0, 64), rng.randrange(0, 0x800), rng.randrange(0, 0x3000), 0x9C3BF8,
             rng.getrandbits(32))
    return [n], 0xFFFFFFFF


def g_heap_morecore(st, rng):
    heap_state(st, rng)
    if st.mem[0x4C2430][0x40:0x44] == bytes(4):
        st.put(0x4C2470, "<I", 0x4C2460)
    return [pick(rng, rng.randrange(1, 64), rng.randrange(0, 0x1000), rng.randrange(-200, 0))], 0xFFFFFFFF


def g_heap_sbrk(st, rng):
    st.put(0x4C2474, "<I", pick(rng, BUF, rng.getrandbits(32)))
    st.put(0x4C2468, "<I", pick(rng, 0x9C4000, rng.getrandbits(32), rng.randrange(0, 0x10000)))
    st.put(0x4C246C, "<I", pick(rng, BUF, BUF + rng.randrange(0, 0x10000), rng.getrandbits(32)))
    return [pick(rng, rng.randrange(0, 0x2000), rng.getrandbits(32), 0x9C3C00)], 0xFFFFFFFF


def g_heap_init(st, rng):
    st.put(0x4C2478, "<I", pick(rng, 0, 0, 0, rng.getrandbits(32)))
    st.put(0x4C2470, "<I", rng.getrandbits(32))
    st.script = {"GlobalAlloc": [pick(rng, GMEM, GMEM, 0)]}
    return [], None


def g_text_fit(st, rng):
    n = pick(rng, 0, rng.randrange(0, 12), rng.randrange(0, 60))
    text = bytes(rng.randrange(0x20, 0x7F) for _ in range(n))
    st.cstr(STR2, text)
    dst = pick(rng, STR, STR, STR, STR2 + 1, 0)
    st.script = {"width_printf": [rng.randrange(0, 400) for _ in range(n + 2)]}
    return [dst, STR2, pick(rng, -1, rng.randrange(0, 300), rng.randrange(0, 50)), pick(rng, 1, 2, 3, 0, 4),
            rng.getrandbits(32)], None


def g_cursor_bar(st, rng):
    st.put(0x4CB49C, "<I", pick(rng, 0, 1, 3, 3, 3))
    st.put(0x4CB500, "<I", pick(rng, 0, 0, 0, 0, 1))
    st.put(0x4CB50C, "<I", pick(rng, 1, 1, 1, 1, 0))
    st.put(0x4CB4FC, "<I", pick(rng, 0, 0, 1))
    st.put(0x4CB508, "<I", pick(rng, 0, 0, 1))
    st.put(0x4CB4EC, "<I", pick(rng, 0, 1, 1))
    st.put(0x4CB4F0, "<I", pick(rng, 0, 1, 1))
    st.put(0x4BE320, "<I", rng.getrandbits(32))
    for a in (0x4CB474, 0x4CB47C):
        st.put(a, "<i", pick(rng, rng.randrange(0, 640), rng.randrange(-100000, 100000)))
    for a in (0x4CB478, 0x4CB480, 0x4CB510, 0x4CB518, 0x4CB520):
        st.put(a, "<i", pick(rng, rng.randrange(0, 480), rng.randrange(-100000, 100000)))
    st.put(0x4ACC38, "<i", pick(rng, 640, 800, 1024, 3440, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<i", pick(rng, 480, 600, 768, 1440, rng.randrange(1, 3000), rng.getrandbits(32)))
    st.script = {
        "tex_w": [rng.randrange(-5, 200) for _ in range(3)],
        "tex_h": [rng.randrange(-5, 200) for _ in range(3)],
        "pulse_bytes": [rng.randbytes(16)],
    }
    return [rng.getrandbits(32), rng.getrandbits(32), pick(rng, 0, 1), rng.getrandbits(32), STR,
            rng.getrandbits(32), rng.getrandbits(32), pick(rng, rng.randrange(0, 20), rng.randrange(-50, 50))], None


def g_slider(st, rng):
    count = pick(rng, 0, 1, 2, 3, 4, 5)
    st.put(0x4CB49C, "<i", count)
    st.put(0x4CB4B8, "<I", ROWS)
    for i in range(5):
        r = ROWS + i * 0x730
        st.put(r, "<B", pick(rng, 4, 4, 4, 1, 0, 5))
        st.put(r + 1, "<B", pick(rng, 1, 1, 0, 0xFF))
        st.put(r + 0x72C, "<I", pick(rng, 0, rng.getrandbits(32)))
        rng_ = rng.uniform(0.5, 100.0)
        v = rng.uniform(-10, 100)
        st.put(r + 0x534, "<I", pick(rng, fbits(v), mk_float(rng)))
        st.put(r + 0x540, "<I", pick(rng, fbits(v + rng.uniform(-20, rng_ + 20)), mk_float(rng)))
        st.put(r + 0x53C, "<I", pick(rng, fbits(rng_), fbits(rng_), 0, mk_float(rng)))
        st.put(r + 0x54C, "<i", pick(rng, rng.randrange(0, 640), rng.randrange(-100000, 100000)))
        st.put(r + 0x550, "<i", pick(rng, rng.randrange(0, 480), rng.randrange(-100000, 100000)))
    st.put(0x4ACC38, "<i", pick(rng, 640, 800, 3440, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<i", pick(rng, 480, 600, 1440, rng.randrange(1, 3000), rng.getrandbits(32)))
    st.script = {
        "tex_w": [rng.randrange(-5, 300) for _ in range(5)],
        "tex_h": [rng.randrange(-5, 100) for _ in range(5)],
    }
    return [rng.getrandbits(32), pick(rng, 0, 1)], None


def g_onoff(st, rng):
    return [pick(rng, 0, 1, 2, rng.getrandbits(32)), rng.getrandbits(32),
            pick(rng, rng.randrange(0xC, 0x13), rng.randrange(0, 0x20), rng.getrandbits(32))], 0xFFFFFFFF


def g_number(st, rng):
    lo = rng.randrange(-3, 5)
    hi = lo + rng.randrange(-2, 60)
    return [pick(rng, rng.randrange(0x10, 0x18), rng.randrange(0x12, 0x17), rng.getrandbits(32)), STR,
            pick(rng, 0, rng.randrange(-5, 70)), rng.randrange(-5, 70), lo, hi], 0xFFFFFFFF


def item_state(st, rng, a):
    st.put(a + 4, "<B", pick(rng, 0, 1, 1, 2, 2, 3, 0xFF))
    st.put(a + 0x50, "<B", pick(rng, 0, 0x41, 0x41, 0x41))
    st.put(a + 0x190, "<I", pick(rng, 0, rng.getrandbits(32)))
    for off in (0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x48, 0x4C):
        st.put(a + off, "<i", pick(rng, -1, -1, 0, rng.randrange(-20, 400), rng.getrandbits(32)))


def measure_script(st, rng, n):
    st.script = {
        "string_printf": [pick(rng, -1, rng.randrange(0, 50), rng.randrange(0, 50)) for _ in range(n)],
        "str_w": [rng.randrange(-10, 500) for _ in range(n)],
        "str_h": [rng.randrange(-10, 100) for _ in range(n)],
        "str_marks": [pick(rng, 0, 0, 1, 2) for _ in range(n)],
    }
    for a in (0x4CB4BC, 0x4CB4C0, 0x4CB4C4, 0x4CB4C8):
        st.put(a, "<i", pick(rng, 0, rng.randrange(-10, 400), rng.getrandbits(32)))


def g_item(st, rng):
    item_state(st, rng, ROWS)
    measure_script(st, rng, 2)
    return [ROWS], None


def g_measure(st, rng):
    item_state(st, rng, 0x4C2920)
    for k in range(64):
        item_state(st, rng, 0x4C30E0 + k * 0x1A0)
    count = pick(rng, 0, 1, 2, 3, 4)
    st.put(0x4CB49C, "<i", count)
    st.put(0x4CB4B8, "<I", ROWS)
    for i in range(4):
        r = ROWS + i * 0x730
        st.put(r, "<B", pick(rng, 0, 1, 2, 3, 4, 5, 6, 0xFF))
        st.put(r + 3, "<B", pick(rng, 0, 0, 0, 1))
        for off in (0xC, 0x1AC, 0x34C):
            item_state(st, rng, r + off)
    measure_script(st, rng, 80)
    return [], None


def g_tiles_draw(st, rng):
    return [rng.getrandbits(32), pick(rng, 0, 1, rng.getrandbits(32)), STR], None


# name: (original address, generator)
TESTS = {
    "body_triggers_apply": (0x4427A0, g_triggers),
    "glyph_sheet_pack": (0x4432D0, g_glyph),
    "kr_heap_init": (0x444500, g_heap_init),
    "kr_free": (0x444570, g_heap_free),
    "kr_malloc": (0x4445E0, g_heap_malloc),
    "kr_morecore": (0x444670, g_heap_morecore),
    "kr_sbrk": (0x4446C0, g_heap_sbrk),
    "menu_text_fit": (0x445340, g_text_fit),
    "menu_bar_sprites_init": (0x446250, g_none),
    "menu_cursor_bar_draw": (0x446300, g_cursor_bar),
    "menu_slider_bars_draw": (0x446EA0, g_slider),
    "menu_hold_clear_thunk": (0x447F00, g_none),
    "menu_hold_reset_thunk": (0x447F10, g_none),
    "option_onoff_step": (0x447F20, g_onoff),
    "movie_win_lang6": (0x447FF0, g_none),
    "menu_measure_all": (0x44CC50, g_measure),
    "menu_item_measure": (0x44CCF0, g_item),
    "menu_loose_items_measure": (0x44CE20, g_measure),
    "menu_title_measure": (0x44CE40, g_measure),
    "option_number_step": (0x450050, g_number),
    "menu_tiles_init": (0x453020, g_none),
    "menu_tiles_draw": (0x4530D0, g_tiles_draw),
    "menu_frame_sprites_init": (0x453130, g_none),
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
    args, mask = gen(st, rng)
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
        for line in open("build/groups/ph_owned.txt"):
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
    m.uc.mem_map(CODE, 0x1000)
    stubs = Stubs(m)
    stubs.patch_imports()
    rng = random.Random(31)
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
