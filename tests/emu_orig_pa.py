#!/usr/bin/env python3
"""Differential test of orig_pa.c (group PA: the Direct3D set-up's texture,
material and viewport functions and the Direct3D drawing's vertex buffer,
0x41e040-0x420bb0) against the original, on random state.

Everything outside the group is a logging stub in both runs (the call log
records as many arguments as the callee reads; buffers by content, stack
pointers never by value): allocation, the surface helpers 0x487660 / 0x4875a0
/ 0x487610, the texture slot functions 0x41d890 / 0x41d910 / 0x41d940, the
texture page creation 0x41e710, the screen area 0x41f2d0, the sprite packet
0x420be0, the quad closer 0x421800. COM objects are fake objects in scratch
memory whose vtables point at stdcall logging stubs (one per method, with the
method's argument kinds; out-pointers get scripted objects / handles,
HRESULTs are scripted). SetRect's import slot points at a stub that fills the
rectangle. memset / memcpy (0x476f50 / 0x476f30), _ftol and the empty debug
hook 0x4291f0 run for real; so do the group's functions called by others in
the group (the original calls the original, ours ours). Return value, the
global regions below and the scratch pages are compared bit for bit; the
stack is zeroed before each run. The x87 control word is the game's (0x27f,
single precision) in 3 of 4 runs, 0x37f in the rest.

    tests/emu_orig_pa.py EXE build/emu_pa.elf [iterations] [names...] [-v] [-c]
"""
import hashlib
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

SCR = 0x01000000
SCR_SIZE = 0x40000
CONFIG = SCR + 0x0000  # 0x1000
OBJS = SCR + 0x1000  # fake COM objects, 0x40 apart (16 per kind)
VTBL = SCR + 0x5000  # vtables, 0x100 each
ARG = SCR + 0x7000  # argument blocks (0x1000)
OUT = SCR + 0x8000  # output buffers (0x2000)
TABLES = SCR + 0xA000  # u / v tables (2 x 0x400)
VBUF = SCR + 0xB000  # TL vertex buffer (0x400)
VERT = SCR + 0xB800  # clipper vertices (0x30 each)
PIX = SCR + 0xC000  # locked surface bits (0x4000)
BLOCKS = SCR + 0x20000  # allocations: 0x10000, 0x400
IMG = 0x02000000  # source image (read only)
IMG_SIZE = 0x80000
TRAMP = 0x00800000  # COM / import stub code

REGIONS = [
    (0x4ACC38, 8),
    (0x4B2198, 4),
    (0x4B8360, 0x40),
    (0x4B8600, 0xC4),
    (0x4BA6F8, 0x4BCBC0 - 0x4BA6F8),
    (0x4BCCE0, 0x4BDE10 - 0x4BCCE0),
    (0x4D5F40, 0x70),
    (0x4D6F18, 4),
    (0x6CCD40, 0x50),
    (0x6CCE50, 0x40),
    (0x6CD300, 0x100),
    (0x6CE65C, 8),
]

KINDS = ("dd", "dd2", "dd4", "d3d", "dev", "vp", "mat", "surf", "tex", "pal")
# COM methods: (kind, vtable offset) -> argument kinds after `this`:
# v value, o out pointer (scripted dword written), g 16 bytes at, M 0x50 bytes at, W 0x2c bytes at,
# R 16 bytes at, F 100 bytes at, D 0x6c bytes at, P 0x400 bytes at (md5), L lock description (0x6c logged,
# scripted bits written to +0x24), I device identifier out (scripted vendor / device), p pointer (stack: None)
METHODS = {
    ("dd", 0x00): "go",
    ("dd", 0x14): "vPov",
    ("dd2", 0x18): "Dov",
    ("dd4", 0x6C): "Iv",
    ("d3d", 0x14): "ov",
    ("dev", 0x28): "",
    ("dev", 0x2C): "",
    ("dev", 0x5C): "vv",
    ("dev", 0x64): "vv",
    ("dev", 0x74): "vvvvv",
    ("dev", 0x78): "vvvvvvv",
    ("vp", 0x14): "W",
    ("vp", 0x20): "v",
    ("vp", 0x30): "vRv",
    ("mat", 0x0C): "M",
    ("mat", 0x14): "vo",
    ("surf", 0x00): "go",
    ("surf", 0x14): "RvvvF",
    ("surf", 0x50): "o",
    ("surf", 0x64): "vLvv",
    ("surf", 0x7C): "v",
    ("surf", 0x80): "p",
    ("tex", 0x0C): "vo",
    ("tex", 0x14): "v",
    ("pal", 0x10): "vvvp",
}


def obj_addr(kind, k=0):
    return OBJS + (KINDS.index(kind) * 16 + k) * 0x40


def S(name, kinds, conv="c"):
    return (name, kinds, conv)


# plain stubs: kinds v value, E description out (0x6c scripted bytes written), b 0x10000 bytes (md5),
# q 0x400 bytes (md5), k sprite packet (0x78 bytes), R rect out (SetRect)
STUBS = {
    0x476AE0: S("alloc", "vv"),
    0x476B40: S("free", "v"),
    0x41D890: S("slot_release", "v"),
    0x41D910: S("slots_release", ""),
    0x41D940: S("slot_handle", "v"),
    0x487660: S("surface_desc", "Ev"),
    0x4875A0: S("surface_lock", "vvE"),
    0x487610: S("surface_unlock", "v"),
    0x41E710: S("page_create", "vvvbqv"),
    0x41F2D0: S("screen_area", "vvvv"),
    0x420BE0: S("sprite_draw", "k"),
    0x421800: S("quad_close", ""),
    TRAMP + 0xF00: S("SetRect", "Rvvvv", "s"),
}
for i, kind in enumerate(KINDS):
    for off in range(0, 0x100, 4):
        kinds = METHODS.get((kind, off))
        STUBS[TRAMP + i * 0x100 + off] = S(f"{kind}+{off:#x}", "" if kinds is None else kinds, "com")


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def md5(b):
    return hashlib.md5(b).hexdigest()


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def mk_float(rng):
    k = rng.randrange(10)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 100.0, 640.0, 480.0, 3.0, 255.0, 255.5, 0.5, 1.5]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FA00000, 1, 0x80000001, 0x00800000,
                           0x7F7FFFFF, 0xFF7FFFFF, 0x4F000000, 0xCF000000, 0x5F000000])
    if k == 2:
        return rng.getrandbits(32)
    return fbits(rng.uniform(-1000.0, 1000.0))


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.regs = None
        for i, kind in enumerate(KINDS):
            for k in range(16):
                self.sput(obj_addr(kind, k), "<I", VTBL + i * 0x100)
            for off in range(0, 0x100, 4):
                self.sput(VTBL + i * 0x100 + off, "<I", TRAMP + i * 0x100 + off)
        self.put(0x4B2198, "<I", CONFIG)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        a = self.find(address)
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.mem[a], address - a, value)

    def putb(self, address, data):
        a = self.find(address)
        self.mem[a][address - a:address - a + len(data)] = data

    def sput(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.scr, address - SCR, value)

    def sputb(self, address, data):
        self.scr[address - SCR:address - SCR + len(data)] = data


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
        name, kinds, conv = STUBS[address]
        m = self.m
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<12I", bytes(uc.mem_read(esp, 48)))
        nargs = len(kinds) + (1 if conv == "com" else 0)
        raw = list(words[1:1 + nargs])
        logged = []
        vals = raw[1:] if conv == "com" else raw
        if conv == "com":
            logged.append(raw[0])
        sizes = {"g": 16, "M": 0x50, "W": 0x2C, "R": 16, "F": 100, "D": 0x6C, "L": 0x6C, "k": 0x78}
        for kind, a in zip(kinds, vals):
            if kind == "v":
                logged.append(a)
            elif kind in sizes:
                logged.append(rd(m, a, sizes[kind]) if a else None)
            elif kind == "P" or kind == "q":
                logged.append(md5(rd(m, a, 0x400)) if a else None)
            elif kind == "b":
                logged.append(md5(rd(m, a, 0x10000)) if a else None)
            elif kind in "opEI":
                logged.append(None if on_stack(a) else a)
        if name == "SetRect":  # the rectangle's address is the stack's
            logged[0] = None
        self.log.append((name, tuple(logged)))
        eax = self._next(name, 0)
        # effects
        for kind, a in zip(kinds, vals):
            if kind == "o":
                value = self._next(name + ".out", None)
                if value is not None:
                    uc.mem_write(a, struct.pack("<I", value & 0xFFFFFFFF))
            elif kind == "E":
                desc = self._next(name + ".desc", None)
                if desc is not None:
                    uc.mem_write(a, desc)
            elif kind == "L":
                bits = self._next(name + ".bits", None)
                if bits is not None:
                    uc.mem_write(a + 0x24, struct.pack("<I", bits))
            elif kind == "I":
                ident = self._next(name + ".ident", None)
                if ident is not None:
                    uc.mem_write(a + 0x408, struct.pack("<II", *ident))
        if name == "SetRect":
            uc.mem_write(raw[0], struct.pack("<4I", *raw[1:5]))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        pop = 4 + (4 * nargs if conv in ("s", "com") else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + pop)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def pick(rng, *choices):
    return rng.choice(choices)


def hr(rng):
    return pick(rng, 0, 0, 0, 0, 0x80004005, 1)


def hrs(rng, n=24):
    return [hr(rng) for _ in range(n)]


def set_d3d(st, rng):
    st.put(0x4D5F40, "<I", obj_addr("dd"))
    st.put(0x4B8380, "<I", obj_addr("d3d"))
    st.put(0x4B8384, "<I", obj_addr("dev"))
    st.put(0x4B8388, "<I", obj_addr("vp"))
    st.put(0x4D5F48, "<I", pick(rng, obj_addr("surf", 14), obj_addr("surf", 14), 0))
    st.put(0x4D5F4C, "<I", pick(rng, obj_addr("surf", 15), obj_addr("surf", 15), 0))
    st.put(0x4BDDCC, "<I", pick(rng, obj_addr("mat", 0), obj_addr("mat", 0), 0))
    st.put(0x4BDDD0, "<I", pick(rng, obj_addr("mat", 1), obj_addr("mat", 1), 0))


def set_slots(st, rng):
    for i in range(64):
        base = 0x4BA6FC + i * 0x24
        st.put(base + 4, "<I", pick(rng, 0, obj_addr("surf", rng.randrange(12))))
        st.put(base + 8, "<I", obj_addr("surf", rng.randrange(12)))
        st.put(base + 0xC, "<I", obj_addr("tex", rng.randrange(12)))
        st.put(base + 0x10, "<I", pick(rng, 256, 128, 64, rng.randrange(1, 700)))
        st.put(base + 0x14, "<I", pick(rng, 256, 128, 64, rng.randrange(1, 700)))
        st.put(base + 0x1C, "<I", TABLES)
        st.put(base + 0x20, "<I", TABLES + 0x400)
    for p in range(64):
        st.put(0x6CD300 + p * 4, "<I", rng.randrange(64))
    st.put(0x6CCE54, "<i", pick(rng, 0, 3, 10, 63, -1, rng.randrange(64)))


def set_palettes(st, rng):
    n = pick(rng, 0, 1, 2, 3, 4, 5, -1)
    st.put(0x4BDE04, "<i", n)
    for i in range(4):
        st.put(0x4B836C + i * 4, "<I", pick(rng, rng.randrange(64), rng.randrange(4), rng.getrandbits(32)))
    st.put(0x4BCD78, "<I", pick(rng, 0, 1))


def set_pixfmt(st, rng):
    k = rng.randrange(6)
    if k == 0:  # 565
        sh = [3, 11, 2, 5, 3, 0, 0, 0]
    elif k == 1:  # 555
        sh = [3, 10, 3, 5, 3, 0, 0, 0]
    elif k == 2:  # 1555
        sh = [3, 10, 3, 5, 3, 0, 7, 15]
    elif k == 3:  # 4444
        sh = [4, 8, 4, 4, 4, 0, 4, 12]
    elif k == 4:
        sh = [rng.randrange(9) for _ in range(8)]
        sh[1], sh[3], sh[5], sh[7] = (rng.randrange(17) for _ in range(4))
    else:
        sh = [rng.getrandbits(8) for _ in range(8)]
    for a, v in zip((0x4BCD58, 0x4BCD59, 0x4BCD60, 0x4BCD61, 0x4BCD68, 0x4BCD69, 0x4BCD70, 0x4BCD71), sh):
        st.put(a, "<B", v)
    st.put(0x4B86BC, "<I", pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))


def set_render(st, rng):
    st.put(0x4B86BC, "<I", pick(rng, 0, 1, 2, 8, 0xA, 0xB, rng.getrandbits(4), rng.getrandbits(32)))
    st.put(0x6CCE80, "<I", pick(rng, 0, 1))
    st.put(0x6CCE5C, "<I", pick(rng, rng.getrandbits(10), rng.getrandbits(32)))
    st.put(0x6CCE60, "<I", pick(rng, 0, 0, 1, 2, rng.getrandbits(32)))
    st.put(0x6CCE64, "<I", pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))
    st.put(0x6CCE68, "<I", pick(rng, 0, 1, 2, 3))
    st.put(0x6CCE58, "<I", pick(rng, 0x3F, rng.randrange(64)))
    st.put(0x6CCD84, "<I", rng.getrandbits(32))
    st.put(0x4BDDFC, "<I", TABLES + 0x200)
    st.put(0x6CE65C, "<I", TABLES)
    st.put(0x6CE660, "<I", TABLES + 0x400)


def set_vbuf(st, rng, count):
    st.put(0x4BCD80, "<I", VBUF)
    st.put(0x4BDDD8, "<i", count)
    st.put(0x4B86B8, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    for i in range(0x400 // 4):
        st.sput(VBUF + i * 4, "<I", mk_float(rng))


def rand_vertex(rng):
    v = [mk_float(rng) for _ in range(12)]
    if rng.randrange(3):
        for k in range(8, 12):
            v[k] = fbits(pick(rng, rng.uniform(0.0, 255.0), float(rng.randrange(256)), rng.uniform(-10, 300)))
    return v


# ---------------------------------------------------------------- generators

def g_viewport_clear(st, rng, m):
    set_d3d(st, rng)
    st.put(0x4D5F98, "<I", pick(rng, 0, 1))
    for a in (0x6CCD64, 0x6CCD68, 0x6CCD6C, 0x6CCD70):
        st.put(a, "<I", pick(rng, mk_float(rng), fbits(rng.uniform(-10, 700))))
    st.script["vp+0x30"] = [hr(rng)]
    return [pick(rng, 0, 1, rng.getrandbits(32))], 0xFFFFFFFF


def g_materials_release(st, rng, m):
    set_d3d(st, rng)
    st.script["slots_release"] = [rng.getrandbits(32)]
    return [], 0xFFFFFFFF


def g_material_init(st, rng, m):
    return [OUT, rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], None


def g_scene_begin(st, rng, m):
    set_d3d(st, rng)
    st.script["dev+0x28"] = [hr(rng)]
    return [], 0xFFFFFFFF


def g_scene_end(st, rng, m):
    set_d3d(st, rng)
    st.put(0x4B8384, "<I", pick(rng, 0, obj_addr("dev")))
    return [], None


def pixels(st, rng, n):
    data = bytearray(rng.randbytes(n * 4))
    for i in range(n):
        if rng.randrange(4) == 0:
            data[i * 4:i * 4 + 3] = b"\0\0\0"
        elif rng.randrange(4) == 0:
            data[i * 4 + rng.randrange(3)] = rng.randrange(8)
    st.sputb(ARG, bytes(data))


def g_rgb_convert(st, rng, m):
    set_pixfmt(st, rng)
    n = pick(rng, 0, 1, 2, 5, 16, 40, rng.randrange(64), -1)
    pixels(st, rng, max(n, 0))
    return [ARG, n, OUT, pick(rng, 0, 1, rng.getrandbits(32))], None


def g_rgb_to_screen(st, rng, m):
    set_pixfmt(st, rng)
    n = pick(rng, 0, 1, 2, 5, 16, 40, rng.randrange(64), -1)
    pixels(st, rng, max(n, 0))
    st.put(0x4BCD7C, "<I", OUT)
    return [ARG, n, pick(rng, 0, 1, rng.getrandbits(32))], None


def g_1555(st, rng, m):
    set_pixfmt(st, rng)
    alpha = pick(rng, 0, 1, rng.getrandbits(32))
    fast = alpha != 0 and st.mem[0x4BCCE0][0x4BCD70 - 0x4BCCE0] != 0
    w = pick(rng, 1, 2, 7, 16, rng.randrange(1, 24))
    if fast and rng.randrange(6) == 0:
        w = pick(rng, 0, -1)
    h = pick(rng, 1, 2, 4, 8, 0, -1)
    pitch = max(w, 0) * 2 + pick(rng, 0, 0, 4, 10)
    words = []
    for _ in range(max(w, 0) * max(h, 0)):
        k = rng.randrange(5)
        words.append(pick(rng, 0, 0x8000) if k == 0 else rng.getrandbits(16))
    st.sputb(ARG, struct.pack(f"<{len(words)}H", *words))
    return [ARG, w, h, OUT, pitch, alpha], None


def g_free_find(st, rng, m):
    set_slots(st, rng)
    k = rng.randrange(4)
    for i in range(64):
        if k == 0:
            st.put(0x4BA700 + i * 0x24, "<I", obj_addr("surf"))  # all in use
        elif k == 1:
            st.put(0x4BA700 + i * 0x24, "<I", 0 if i == 63 else 1)
    return [], 0xFFFFFFFF


def g_background(st, rng, m):
    set_d3d(st, rng)
    st.script["mat+0xc"] = [hr(rng)]
    st.script["mat+0x14"] = [hr(rng)]
    st.script["mat+0x14.out"] = [pick(rng, rng.getrandbits(32), None)]
    st.script["vp+0x20"] = [hr(rng)]

    def c():
        return pick(rng, 0, 255, 128, rng.randrange(256), rng.getrandbits(32), -1)
    return [c(), c(), c()], None


def g_materials_create(st, rng, m):
    set_d3d(st, rng)
    st.put(0x4BA6FC, "<I", rng.getrandbits(32))
    st.put(0x4D5F88, "<I", pick(rng, 640, 800))
    st.put(0x4D5F8C, "<I", pick(rng, 480, 600))
    st.script["d3d+0x14"] = [hr(rng), hr(rng)]
    st.script["d3d+0x14.out"] = [obj_addr("mat", 2), obj_addr("mat", 3)]
    st.script["mat+0xc"] = [hr(rng), hr(rng)]
    st.script["mat+0x14"] = [hr(rng), hr(rng)]
    st.script["mat+0x14.out"] = [pick(rng, rng.getrandbits(32), None), rng.getrandbits(32)]
    st.script["vp+0x20"] = [hr(rng)]
    st.script["dev+0x64"] = [hr(rng)]
    return [], 0xFFFFFFFF


def desc_bytes(rng, w=None, h=None, bits=None, pitch=None, bpp=None):
    d = bytearray(rng.randbytes(0x6C))
    struct.pack_into("<I", d, 0, 0x6C)
    struct.pack_into("<I", d, 8, h if h is not None else pick(rng, 480, 600, rng.getrandbits(32)))
    struct.pack_into("<I", d, 0xC, w if w is not None else pick(rng, 640, 800, rng.getrandbits(32)))
    if bits is not None:
        struct.pack_into("<I", d, 0x24, bits)
    if pitch is not None:
        struct.pack_into("<I", d, 0x10, pitch)
    if bpp is not None:
        struct.pack_into("<I", d, 0x54, bpp)
    return bytes(d)


def g_surfaces_clear(st, rng, m):
    set_d3d(st, rng)
    st.script["surface_desc"] = [pick(rng, 1, 1, 1, 0), pick(rng, 1, 1, 1, 0)]
    st.script["surface_desc.desc"] = [desc_bytes(rng), pick(rng, desc_bytes(rng), None)]
    st.script["surf+0x14"] = [hr(rng), hr(rng)]
    return [], 0xFFFFFFFF


def g_page_show(st, rng, m):
    set_slots(st, rng)
    for i in range(64):
        if rng.randrange(40) == 0:
            st.put(0x4BA70C + i * 0x24, "<I", 0)
    st.put(0x4ACC38, "<I", pick(rng, 640, 800))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600))
    st.put(0x6CCD84, "<I", rng.getrandbits(32))
    return [pick(rng, 0, 1, 5, rng.randrange(64))], 0xFFFFFFFF


def g_format_choose(st, rng, m):
    n = pick(rng, 0, 1, 2, 5, 10, 20, 48, -1)
    st.put(0x4BAFFC, "<i", n)
    for i in range(48):
        r = 0x4BB000 + i * 0x94
        st.put(r + 0x6C, "<I", pick(rng, 0, 0, 0, 1, rng.getrandbits(32)))
        st.put(r + 0x54, "<I", pick(rng, 8, 15, 16, 16, 24, 32, rng.getrandbits(32)))
        st.put(r + 0x90, "<I", pick(rng, 8, 4, 0, rng.getrandbits(32)))
        for mask_off, shift_off in ((0x8C, 0x89), (0x7C, 0x79), (0x74, 0x71)):
            width = pick(rng, 0, 1, 0xF, 0x1F, 0x3F, 0xFF, rng.getrandbits(8))
            s = pick(rng, 0, 4, 5, 10, 11, 12, 15, rng.randrange(32))
            st.put(r + mask_off, "<I", (width << s) & 0xFFFFFFFF if rng.randrange(8) else rng.getrandbits(32))
            st.put(r + shift_off, "<B", s if rng.randrange(8) else rng.getrandbits(8))
    st.put(0x4B86BC, "<I", pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))
    return [pick(rng, 0, 1), pick(rng, 0, 0, 1)], 0xFFFFFFFF


def g_viewport_set(st, rng, m):
    set_d3d(st, rng)
    st.script["vp+0x14"] = [hr(rng)]

    def size():
        return pick(rng, 640, 480, 800, 600, 1, 0, rng.randrange(4096), rng.getrandbits(32), 0x80000000, 0xFFFFFFFF)
    return [rng.getrandbits(32), rng.getrandbits(32), size(), size()], None


def g_snap_set(st, rng, m):
    return [rng.getrandbits(32)], None


def g_pixel_read(st, rng, m):
    set_d3d(st, rng)
    st.put(0x4D5F48, "<I", obj_addr("surf", 14))
    st.put(0x4D5F4C, "<I", obj_addr("surf", 15))
    pitch = pick(rng, 64, 100, 128, 256)
    st.script["surface_lock"] = [pick(rng, 1, 1, 1, 0)]
    st.script["surface_lock.desc"] = [desc_bytes(rng, bits=PIX, pitch=pitch,
                                                 bpp=pick(rng, 8, 15, 16, 24, 32, 0, rng.getrandbits(32)))]
    st.put(0x4D5F54, "<I", pick(rng, 0xF800, 0x7C00, 0xFF0000, rng.getrandbits(32)))
    st.put(0x4D5F5C, "<I", pick(rng, 0x7E0, 0x3E0, 0xFF00, rng.getrandbits(32)))
    st.put(0x4D5F64, "<I", pick(rng, 0x1F, 0xFF, rng.getrandbits(32)))
    for a in (0x4D5F51, 0x4D5F59, 0x4D5F61):
        st.put(a, "<B", pick(rng, 0, 5, 10, 11, 16, rng.getrandbits(8)))
    return [pick(rng, 0, 1, rng.getrandbits(32)), rng.randrange(16), rng.randrange(32),
            pick(rng, 0, 1, 3, 7, rng.getrandbits(32))], 0xFFFFFFFF


def g_quad(st, rng, m):
    set_d3d(st, rng)
    set_slots(st, rng)
    set_render(st, rng)
    set_vbuf(st, rng, rng.randrange(5))
    for a in (0x4BCD8C, 0x4BCD90, 0x4BCD94, 0x4BCD98):
        st.put(a, "<I", rng.randrange(12))
    st.script["dev+0x28"] = [hr(rng)]
    args = [pick(rng, rng.getrandbits(10), rng.getrandbits(32))]
    args += [mk_float(rng) for _ in range(8)]
    args += [mk_float(rng), rng.getrandbits(32), pick(rng, 0, 1), pick(rng, 0, 0, 1, 2, 3)]
    return args, 0xFFFFFFFF


def g_sum8(st, rng, m):
    st.sputb(ARG, rng.randbytes(0x100))
    return [ARG, pick(rng, 0, 1, 4, 0x100, rng.randrange(0x100), -1)], 0xFF


def g_pvrc(st, rng, m):
    set_d3d(st, rng)
    st.script["dd+0x0"] = [hr(rng)]
    st.script["dd+0x0.out"] = [obj_addr("dd2")]
    st.script["dd2+0x18"] = [hr(rng)]
    st.script["dd2+0x18.out"] = [obj_addr("surf", 3)]
    st.script["surf+0x64"] = [hr(rng)]
    st.script["surf+0x64.bits"] = [PIX]
    st.script["surf+0x80"] = [hr(rng)]
    st.script["surf+0x0"] = [hr(rng)]
    st.script["surf+0x0.out"] = [obj_addr("tex", 3)]
    st.script["tex+0xc"] = [hr(rng)]
    st.script["tex+0xc.out"] = [rng.getrandbits(32)]
    st.sput(PIX + 4, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    return [], 0xFFFFFFFF


def devid_script(st, rng, n):
    st.script["dd+0x0"] = [pick(rng, 0, 0, 0, 0x80004005) for _ in range(n)]
    st.script["dd+0x0.out"] = [obj_addr("dd4")] * n
    st.script["dd4+0x6c"] = [pick(rng, 0, 0, 0, 0x80004005) for _ in range(n)]
    st.script["dd4+0x6c.ident"] = [(pick(rng, 0x12D2, 0x10DE, 0x10DE, 0x121A, rng.getrandbits(32)),
                                    pick(rng, 0x18, 0x19, 0x20, 0x28, 0x2C, 0x29, rng.getrandbits(32)))
                                   for _ in range(n)]


def g_devid(st, rng, m):
    set_d3d(st, rng)
    devid_script(st, rng, 1)
    return [], 0xFFFFFFFF


def g_blend_choose(st, rng, m):
    set_d3d(st, rng)
    devid_script(st, rng, 2)
    st.put(0x4B86BC, "<I", pick(rng, 0, 1, 2, 4, 8, 6, 0xE, 3, rng.getrandbits(4), rng.getrandbits(32)))
    st.put(0x4B8608, "<I", rng.getrandbits(32))
    st.put(0x4B860C, "<I", rng.getrandbits(32))
    st.sput(CONFIG + 0x434, "<I", pick(rng, 0, 1, 2, 3, 4, 5, rng.getrandbits(32)))
    return [], None


def lock_script(st, rng):
    st.script["surface_lock"] = [pick(rng, 1, 1, 1, 0)]
    st.script["surface_lock.desc"] = [desc_bytes(rng, bits=PIX, pitch=rng.getrandbits(32), bpp=rng.getrandbits(32))]


def g_tex_lock(st, rng, m):
    set_d3d(st, rng)
    set_slots(st, rng)
    set_palettes(st, rng)
    page = pick(rng, 0, 1, 2, 5, 63)
    slot = rng.randrange(64)
    st.put(0x6CD300 + page * 4, "<I", slot)
    if rng.randrange(2):
        st.put(0x4B836C + rng.randrange(4) * 4, "<I", slot)
    lock_script(st, rng)
    st.script["surf+0x50"] = [hr(rng)]
    st.script["surf+0x50.out"] = [pick(rng, obj_addr("pal", 2), 0, None)]
    return [page, rng.getrandbits(32), OUT, OUT + 0x10, OUT + 0x800, OUT + 0x804], 0xFFFFFFFF


def g_tex_unlock(st, rng, m):
    set_slots(st, rng)
    set_palettes(st, rng)
    page = pick(rng, 0, 1, 2, 5, 63)
    slot = rng.randrange(64)
    st.put(0x6CD300 + page * 4, "<I", slot)
    if rng.randrange(2):
        st.put(0x4B836C + rng.randrange(4) * 4, "<I", slot)
    st.sputb(ARG, rng.randbytes(0x400))
    return [page, rng.getrandbits(32), ARG], 0xFFFFFFFF


def reload_script(st, rng, n):
    st.script["surf+0x0"] = [pick(rng, 0, 0, 0, 0, 0x80004005) for _ in range(2 * n)]
    st.script["surf+0x0.out"] = [obj_addr("tex", rng.randrange(12, 16)) for _ in range(2 * n)]
    st.script["slot_handle"] = [pick(rng, 1, 1, 1, 0) for _ in range(n)]
    st.script["tex+0x14"] = [hr(rng) for _ in range(n)]


def g_palettes_apply(st, rng, m):
    set_d3d(st, rng)
    set_slots(st, rng)
    set_palettes(st, rng)
    for i in range(4):
        st.put(0x4B836C + i * 4, "<I", rng.randrange(64))
    reload_script(st, rng, 6)
    st.script["dd+0x14"] = hrs(rng)
    st.script["dd+0x14.out"] = [obj_addr("pal", rng.randrange(16)) for _ in range(6)]
    st.script["surf+0x7c"] = hrs(rng)
    return [], None


def g_reload(st, rng, m):
    set_d3d(st, rng)
    set_slots(st, rng)
    reload_script(st, rng, 1)
    return [pick(rng, 0, 63, rng.randrange(64))], 0xFFFFFFFF


def g_upload(st, rng, m):
    set_slots(st, rng)
    a = pick(rng, BLOCKS, BLOCKS, BLOCKS, BLOCKS, 0)
    b = pick(rng, BLOCKS + 0x10000, BLOCKS + 0x10000, BLOCKS + 0x10000, 0)
    st.script["alloc"] = [a, b]
    st.script["page_create"] = [pick(rng, 1, 1, 1, 1, 1, 0) for _ in range(12)]
    w = pick(rng, 1, 16, 256, 257, 300, 500, 0, -1, rng.randrange(1, 520))
    h = pick(rng, 1, 16, 256, 257, 300, 0, -1, rng.randrange(1, 300))
    pitch = max(w, 1) + pick(rng, 0, 0, 3, 16)
    st.sputb(ARG, rng.randbytes(0x400))
    return [IMG + rng.randrange(0x100), ARG, w, h, pitch], 0xFFFFFFFF


def g_tiles_draw(st, rng, m):
    st.put(0x6CCD84, "<I", rng.getrandbits(32))

    def c():
        return pick(rng, 0, 100, 640, 480, rng.randrange(-100, 800))
    rect = [c(), c(), c(), c()]
    st.sputb(ARG, struct.pack("<4i", *rect))
    w = pick(rng, 1, 16, 256, 257, 300, 600, 0, -5, rng.randrange(1, 700))
    h = pick(rng, 1, 16, 256, 257, 300, 600, 0, -5, rng.randrange(1, 700))
    return [pick(rng, -1, 0, 1, 7, rng.randrange(64)), w, h, ARG, pick(rng, 0, 1), rng.getrandbits(32)], None


def g_slots_thunk(st, rng, m):
    st.script["slots_release"] = [rng.getrandbits(32)]
    return [], 0xFFFFFFFF


def g_page_next(st, rng, m):
    st.put(0x6CCE54, "<I", rng.getrandbits(32))
    return [], 0xFFFFFFFF


def g_snap(st, rng, m):
    set_vbuf(st, rng, pick(rng, 1, 2, 3, 4, 8, 9))
    return [], None


def g_draw(st, rng, m):
    set_d3d(st, rng)
    count = pick(rng, 0, 1, 3, 4, 8, 9, 12, -3)
    set_vbuf(st, rng, count)
    if count <= 0:
        st.put(0x4B86B8, "<I", 0)
    st.script["dev+0x74"] = [hr(rng)]
    st.script["dev+0x78"] = hrs(rng)
    return [], None


def g_blend_set(st, rng, m):
    set_d3d(st, rng)
    set_render(st, rng)
    for a in (0x4BCD8C, 0x4BCD90, 0x4BCD94, 0x4BCD98):
        st.put(a, "<I", rng.randrange(12))
    flags = pick(rng, 0, 1, 2, 3, rng.getrandbits(32))
    if rng.randrange(3) == 0:
        st.put(0x6CCE64, "<I", flags)
    return [flags, pick(rng, 0, 1, 2, 3, rng.getrandbits(32))], None


def g_tex_set(st, rng, m):
    set_d3d(st, rng)
    set_slots(st, rng)
    set_render(st, rng)
    return [pick(rng, 0x3F, 0, 1, rng.randrange(63))], None


def all_regs(rng, **kw):
    regs = {UC_X86_REG_EAX: rng.getrandbits(32), UC_X86_REG_ECX: rng.getrandbits(32),
            UC_X86_REG_EDX: rng.getrandbits(32), UC_X86_REG_EBX: rng.getrandbits(32),
            UC_X86_REG_ESI: rng.getrandbits(32), UC_X86_REG_EDI: rng.getrandbits(32),
            UC_X86_REG_EBP: rng.getrandbits(32)}
    regs.update(kw)
    return regs


def g_state_select(st, rng, m):
    set_d3d(st, rng)
    set_slots(st, rng)
    set_render(st, rng)
    state = pick(rng, rng.getrandbits(10), rng.getrandbits(32))
    if rng.randrange(4) == 0:
        st.put(0x6CCE5C, "<I", state & 0x3FF)
    st.regs = all_regs(rng)
    st.regs[UC_X86_REG_EBX] = state
    return [], None


def g_emit_indexed(st, rng, m):
    set_render(st, rng)
    set_vbuf(st, rng, rng.randrange(10))
    st.sputb(TABLES, rng.randbytes(0x800))
    st.sputb(VERT, struct.pack("<12I", *rand_vertex(rng)))
    st.regs = all_regs(rng)
    st.regs[UC_X86_REG_EDX] = VERT
    return [], None


def g_emit(st, rng, m):
    set_render(st, rng)
    set_vbuf(st, rng, rng.randrange(10))
    st.sputb(VERT, struct.pack("<12I", *rand_vertex(rng)))
    st.regs = all_regs(rng)
    st.regs[UC_X86_REG_EDX] = VERT
    return [], None


def g_list_fan(st, rng, m):
    set_d3d(st, rng)
    set_render(st, rng)
    set_vbuf(st, rng, rng.randrange(5))
    n = pick(rng, 0, 1, 3, 4, 5, -1)
    ptrs = []
    for i in range(max(n, 0)):
        st.sputb(VERT + i * 0x30, struct.pack("<12I", *rand_vertex(rng)))
        ptrs.append(VERT + rng.randrange(max(n, 1)) * 0x30)
    st.sputb(ARG, struct.pack("<i", n) + b"".join(struct.pack("<I", p) for p in ptrs))
    st.script["dev+0x74"] = [hr(rng)]
    return [pick(rng, ARG, ARG, ARG, 0)], None


# name: (address, generator)
TESTS = {
    "viewport_clear": (0x41E040, g_viewport_clear),
    "d3d_materials_release": (0x41E0D0, g_materials_release),
    "material_init": (0x41E110, g_material_init),
    "d3d_scene_begin": (0x41E170, g_scene_begin),
    "d3d_scene_end": (0x41E190, g_scene_end),
    "pixels_rgb_to_screen": (0x41E1D0, g_rgb_to_screen),
    "pixels_rgb_convert": (0x41E1F0, g_rgb_convert),
    "pixels_1555_convert": (0x41E440, g_1555),
    "texture_slot_free_find": (0x41EC20, g_free_find),
    "background_colour_set": (0x41EC40, g_background),
    "materials_create": (0x41ED00, g_materials_create),
    "surfaces_clear": (0x41EEB0, g_surfaces_clear),
    "texture_page_show": (0x41EFE0, g_page_show),
    "texture_format_choose": (0x41F060, g_format_choose),
    "d3d_viewport_set": (0x41F210, g_viewport_set),
    "vertex_snap_set": (0x41F3F0, g_snap_set),
    "surface_pixel_read": (0x41F400, g_pixel_read),
    "screen_quad_draw": (0x41F550, g_quad),
    "bytes_sum8": (0x41F700, g_sum8),
    "pvrc_texture_probe": (0x41FB60, g_pvrc),
    "ddraw_device_id_check": (0x41FCF0, g_devid),
    "blend_modes_choose": (0x41FDA0, g_blend_choose),
    "texture_lock": (0x41FEF0, g_tex_lock),
    "texture_unlock": (0x420000, g_tex_unlock),
    "texture_palettes_apply": (0x420090, g_palettes_apply),
    "texture_reload": (0x420150, g_reload),
    "image_tiles_upload": (0x4201F0, g_upload),
    "image_tiles_draw": (0x4203A0, g_tiles_draw),
    "texture_slots_release_thunk": (0x4205E0, g_slots_thunk),
    "texture_page_next": (0x4205F0, g_page_next),
    "vertices_snap": (0x420600, g_snap),
    "vertices_snap_thunk": (0x420650, g_snap),
    "vertices_draw_list_noclip": (0x420660, g_draw),
    "vertices_draw_list": (0x4206A0, g_draw),
    "vertices_draw_fan": (0x4206E0, g_draw),
    "vertices_draw_quads": (0x420720, g_draw),
    "render_blend_set": (0x420770, g_blend_set),
    "render_texture_set": (0x4209C0, g_tex_set),
    "render_state_select": (0x420A30, g_state_select),
    "vertex_emit_indexed": (0x420A90, g_emit_indexed),
    "vertex_emit": (0x420B10, g_emit),
    "vertex_list_draw_fan": (0x420BB0, g_list_fan),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.write(0x4A3254, struct.pack("<I", TRAMP + 0xF00))
    if st.regs:
        for reg, value in st.regs.items():
            m.uc.reg_write(reg, value)


SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, mask = gen(st, rng, m)
    cw = 0x27F if rng.randrange(4) else 0x37F
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
            if st.regs:
                r = r + tuple(m.uc.reg_read(x) for x in SAVED)
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
            logs = f"\n  calls from #{k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs
    return None


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pa_owned.txt"):
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
    m.uc.mem_map(IMG, IMG_SIZE)
    m.uc.mem_write(IMG, random.Random(7).randbytes(IMG_SIZE))
    m.uc.mem_map(TRAMP, 0x1000)
    m.uc.mem_write(TRAMP, b"\xc3" * 0x1000)
    stubs = Stubs(m)
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
        bad = 0
        seen = set()
        size = sizes.get(address, 0)
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, gen, rng)
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
