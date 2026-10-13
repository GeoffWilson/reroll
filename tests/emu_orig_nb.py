#!/usr/bin/env python3
"""Differential test of orig_nb.c (group NB: the Direct3D polygon clipper and
the Direct3D set-up, 0x417950-0x41df10) against the original, on random state.

Everything outside the group is a logging stub in both runs (the call log
records as many arguments as the callee reads; strings and stack buffers by
content, stack pointers never by value): allocation, the debug output, exit,
the file helpers, the D3D helpers outside the group. COM objects are fake
objects in scratch memory whose vtables point at stdcall logging stubs (one
per method, with the method's argument count; out-pointers get scripted
objects / handles, HRESULTs are scripted). GetSystemInfo's import slot points
at such a stub too. Pure game code the group calls (the frustum clip passes
0x417060..0x4177b0, the facing test 0x4278f0, the projection 0x428900) and the
C runtime (memset, memcpy, atoi, _isctype) run for real. Return value, the
global regions below and the scratch pages are compared bit for bit; the
stack is zeroed before each run. The x87 control word is the game's (0x27f,
single precision) in 3 of 4 runs, 0x37f in the rest.

    tests/emu_orig_nb.py EXE build/emu_nb.elf [iterations] [names...] [-v] [-c]
"""
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
SCR_SIZE = 0x20000
CONFIG = SCR + 0x0000  # 0x1000
RACE = SCR + 0x1000  # 0x2000
OBJS = SCR + 0x3000  # fake COM objects, 0x40 apart
VTBL = SCR + 0x3800  # vtables, 0x80 each
ARG = SCR + 0x4000  # argument blocks
STR = SCR + 0x6000  # strings
LISTS = SCR + 0x7000  # polygon lists
TABLES = SCR + 0x8000  # texture coordinate tables (2 x 0x400)
BLOCKS = SCR + 0xA000  # allocations: 0x200, 0x1800, 0x1130
SCREEN = SCR + 0xE000  # screen polygons
TRAMP = 0x00800000  # COM / import stub code

POOL = 0x6CCE98
LIST = 0x6CD258

REGIONS = [
    (0x4ACC38, 8),
    (0x4B1960, 4),
    (0x4B2198, 4),
    (0x4B7E00, 0x200),
    (0x4B8250, 0x4BCE00 - 0x4B8250),
    (0x4BDD00, 0x110),
    (0x4D5F40, 0x70),
    (0x4D6F18, 4),
    (0x6CCD00, 0x700),
    (0x6CE600, 0x160),
]

KINDS = ("dd", "d3d", "dev", "vp", "surf", "tex")
# COM methods: (kind, vtable offset) -> argument kinds after `this` (v value, g 16 bytes at, d 0x6c bytes at,
# w 0x2c bytes at, o out pointer (logged as a value or None on the stack))
METHODS = {
    ("dd", 0x00): "go",
    ("dd", 0x18): "dov",
    ("d3d", 0x08): "",
    ("d3d", 0x0C): "vv",
    ("d3d", 0x18): "ov",
    ("d3d", 0x20): "gvo",
    ("dev", 0x08): "",
    ("dev", 0x18): "v",
    ("dev", 0x24): "vv",
    ("dev", 0x34): "v",
    ("dev", 0x5C): "vv",
    ("vp", 0x08): "",
    ("vp", 0x14): "w",
    ("surf", 0x08): "",
    ("surf", 0x0C): "v",
    ("tex", 0x08): "",
    ("tex", 0x0C): "vo",
}


def obj_addr(kind, k=0):
    return OBJS + (KINDS.index(kind) * 4 + k) * 0x40


def S(name, kinds, conv="c"):
    return (name, kinds, conv)


STUBS = {
    0x476AE0: S("alloc", "vv"),
    0x476B40: S("free", "v"),
    0x477440: S("f477440", ""),
    0x477420: S("f477420", ""),
    0x477450: S("f477450", ""),
    0x41FDA0: S("f41fda0", ""),
    0x41ED00: S("f41ed00", ""),
    0x41F720: S("f41f720", ""),
    0x41F2D0: S("f41f2d0", "vvvv"),
    0x41F060: S("format_choose", "vv"),
    0x487480: S("pixel_format_get", "pv"),
    0x41E0D0: S("f41e0d0", "v"),
    0x4291F0: S("dx_error", "v"),
    0x487660: S("surface_desc", "dv"),
    0x477930: S("make_dir", "s"),
    0x4743E0: S("replays_delete", "v"),
    0x477910: S("temp_name", "ssp"),
    0x4654F0: S("file_save", "svv"),
    0x428310: S("debug_out", "s"),
    0x476F80: S("exit", ""),
    0x497F7A: S("printf", "sv"),
    TRAMP + 0xF00: S("GetSystemInfo", "p", "s"),
}
for i, kind in enumerate(KINDS):
    for off in range(0, 0x80, 4):
        kinds = METHODS.get((kind, off))
        STUBS[TRAMP + i * 0x100 + off] = S(f"{kind}+{off:#x}", "" if kinds is None else kinds, "com")


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def cstr(m, address, limit=0x200):
    if not (address and (SCR <= address < SCR + SCR_SIZE or 0x400000 <= address < 0x700000 or on_stack(address))):
        return ("ptr?", address)
    return rd(m, address, limit).split(b"\0")[0]


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


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.regs = None
        self.mbcs = 1
        # fake COM objects and their vtables
        for i, kind in enumerate(KINDS):
            for k in range(4):
                self.sput(obj_addr(kind, k), "<I", VTBL + i * 0x80)
            for off in range(0, 0x80, 4):
                self.sput(VTBL + i * 0x80 + off, "<I", TRAMP + i * 0x100 + off)
        self.put(0x4B2198, "<I", CONFIG)
        self.put(0x4B1960, "<I", RACE)

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
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<12I", bytes(uc.mem_read(esp, 48)))
        nargs = len(kinds) + (1 if conv == "com" else 0)
        raw = list(words[1:1 + nargs])
        logged = []
        vals = raw[1:] if conv == "com" else raw
        if conv == "com":
            logged.append(raw[0])
        for kind, a in zip(kinds, vals):
            if kind == "v":
                logged.append(a)
            elif kind == "s":
                logged.append(cstr(self.m, a))
            elif kind == "g":
                logged.append(rd(self.m, a, 16) if a else None)
            elif kind == "d":
                logged.append(rd(self.m, a, 0x6C))
            elif kind == "w":
                logged.append(rd(self.m, a, 0x2C))
            elif kind in "po":
                logged.append(None if on_stack(a) else a)
        self.log.append((name, tuple(logged)))
        eax = self._next(name, 0)
        # effects
        if conv == "com" and kinds:
            out = [a for kind, a in zip(kinds, vals) if kind == "o"]
            if out:
                value = self._next(name + ".out", None)
                if value is not None:
                    uc.mem_write(out[0], struct.pack("<I", value))
        if name == "surface_desc":
            caps = self._next("desc_caps", None)
            if caps is not None:
                uc.mem_write(raw[0] + 0x68, struct.pack("<I", caps))
        elif name == "temp_name":
            text = self._next("temp_text", b"..\\TEMP\\ARF1.TMP")
            uc.mem_write(raw[2], text + b"\0")
        elif name == "d3d+0xc" and self._next("enum_adds", 0):  # the callback found a hardware device
            uc.mem_write(0x4BA6F4, struct.pack("<I", 1))
            uc.mem_write(0x4B89D8, struct.pack("<I", 1))
        elif name == "GetSystemInfo":
            info = self._next("info", bytes(0x24))
            uc.mem_write(raw[0], info)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        pop = 4 + (4 * nargs if conv in ("s", "com") else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + pop)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def pick(rng, *choices):
    return rng.choice(choices)


# ---------------------------------------------------------------- clipper state

def rand_vertex(rng, real=True, z_range=(-20.0, 1200.0)):
    v = [0] * 12
    v[0] = pick(rng, 0, 0, rng.getrandbits(32), rng.randrange(16))
    if real and rng.randrange(12):
        v[1] = fbits(pick(rng, rng.uniform(*z_range), rng.uniform(0.5, 50.0), rng.uniform(900.0, 1100.0)))
        v[2] = fbits(pick(rng, rng.uniform(-300.0, 940.0), rng.uniform(-10.0, 650.0), rng.choice([0.0, 640.0, -0.0])))
        v[3] = fbits(pick(rng, rng.uniform(-300.0, 780.0), rng.uniform(-10.0, 490.0), rng.choice([0.0, 480.0])))
        for k in range(4, 12):
            v[k] = fbits(rng.uniform(-300.0, 300.0) if k < 8 else rng.uniform(0.0, 255.0))
    else:
        v = [mk_float(rng) for _ in range(12)]
        v[0] = rng.getrandbits(32)
    return v


def set_clip_params(st, rng):
    st.put(0x6CCD04, "<I", fbits(pick(rng, 1.0, 0.5, rng.uniform(0, 10))))
    st.put(0x6CCD08, "<I", fbits(pick(rng, 1000.0, rng.uniform(100, 2000))))
    st.put(0x6CCD64, "<I", fbits(pick(rng, 0.0, rng.uniform(-10, 100))))
    st.put(0x6CCD68, "<I", fbits(pick(rng, 639.0, 640.0, rng.uniform(300, 800))))
    st.put(0x6CCD6C, "<I", fbits(pick(rng, 0.0, rng.uniform(-10, 100))))
    st.put(0x6CCD70, "<I", fbits(pick(rng, 479.0, 480.0, rng.uniform(200, 600))))
    if rng.randrange(15) == 0:
        for a in (0x6CCD04, 0x6CCD08, 0x6CCD64, 0x6CCD68, 0x6CCD6C, 0x6CCD70):
            if rng.randrange(3) == 0:
                st.put(a, "<I", mk_float(rng))
    st.put(0x6CCD00, "<I", fbits(rng.uniform(100, 1000)))
    for a in (0x6CCD54, 0x6CCD58, 0x6CCD5C, 0x6CCD60):
        st.put(a, "<I", fbits(rng.uniform(0, 400)))
    st.put(0x6CCE94, "<I", pick(rng, 0, 0, 1))


def set_polygon(st, rng, n, real=True):
    """n vertices in pool slots 0..n-1, the list at 0x6cd258."""
    for i in range(n):
        st.putb(POOL + i * 0x30, struct.pack("<12I", *rand_vertex(rng, real)))
    st.put(LIST, "<I", n)
    for i in range(n):
        st.put(LIST + 4 + i * 4, "<I", POOL + i * 0x30)


def set_tables(st, rng):
    st.put(0x6CE660, "<I", TABLES)
    st.put(0x6CE65C, "<I", TABLES + 0x400)
    st.sputb(TABLES, rng.randbytes(0x800))


def g_clip_far(st, rng, m):
    set_clip_params(st, rng)
    n = pick(rng, 3, 4, 3, 4, 5, 6, 1, 0, 2)
    real = rng.randrange(8) != 0
    set_polygon(st, rng, max(n, 1), real)
    plist = [POOL + i * 0x30 for i in range(max(n, 1))]
    st.sputb(LISTS, struct.pack("<I", n) + b"".join(struct.pack("<I", p) for p in plist))
    st.sput(ARG, "<i", pick(rng, n, n, 7, 0))
    return [LISTS, LISTS + 0x100, ARG], 0xFFFFFFFF


def g_clip_2d(st, rng, m):
    set_clip_params(st, rng)
    n = pick(rng, 3, 4, 3, 4, 5, 6, 8, 1, 2, 0)
    real = rng.randrange(8) != 0
    set_polygon(st, rng, max(n, 1), real)
    st.put(LIST, "<I", n)
    return [], 0xFFFFFFFF


def g_clip_drive(st, rng, m):
    set_clip_params(st, rng)
    n = pick(rng, 3, 4, 3, 4, 5)
    real = rng.randrange(8) != 0
    set_polygon(st, rng, n, real)
    if rng.randrange(4) == 0:  # the "no codes" fast path
        for i in range(3):
            st.put(POOL + i * 0x30, "<I", 0)
    # frustum planes: normal and distance
    for k in range(4):
        nx, ny, nz = rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)
        st.putb(0x4B7FA8 + k * 16, struct.pack("<4f", nx, ny, nz, rng.uniform(-50, 50)))
    st.put(0x4B7FEC, "<I", pick(rng, 0, 0, 0, 0x40, rng.getrandbits(32) & ~0x40))
    flags = rng.getrandbits(4) | (0x10 if rng.randrange(2) else 0) | (0x20 if rng.randrange(3) == 0 else 0)
    flags |= (0x80 if rng.randrange(2) else 0) | (0x100 if rng.randrange(2) else 0)
    flags |= 0x40 if rng.randrange(6) == 0 else 0
    return [flags], 0xFFFFFFFF


def g_vertex_add(st, rng, m):
    set_tables(st, rng)
    st.put(LIST, "<I", pick(rng, 0, 1, 2, 3, 0, 1, 2))
    src = [rng.getrandbits(32) for _ in range(6)]
    st.sputb(ARG, struct.pack("<6I", *src))
    st.regs = {UC_X86_REG_EAX: rng.getrandbits(32), UC_X86_REG_ECX: rng.randrange(256),
               UC_X86_REG_EDX: rng.randrange(256), UC_X86_REG_EBX: ARG,
               UC_X86_REG_ESI: rng.getrandbits(32), UC_X86_REG_EDI: rng.getrandbits(32),
               UC_X86_REG_EBP: rng.getrandbits(32)}
    return [], None


def g_vertex_add_2d(st, rng, m):
    set_tables(st, rng)
    set_clip_params(st, rng)
    st.put(LIST, "<I", rng.randrange(0, 20))
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], None


def g_none(st, rng, m):
    return [], None


def g_screen(st, rng, m):
    st.sput(RACE + 0x551, "<B", pick(rng, 0, 1, 2, 3, 8, 4, rng.getrandbits(8)))
    st.put(0x6CE704, "<I", pick(rng, 0, 1, 2, rng.getrandbits(32)))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CD4A0, 0, rng.getrandbits(32)))
    st.put(0x6CE708, "<I", ARG)
    st.sput(ARG + 0x14, "<I", SCREEN + 0x800)
    st.sput(ARG + 0x18, "<I", SCREEN + 0x1000)
    for i in range(4):
        st.put(0x6CE710 + i, "<b", rng.randrange(-4, 8))
    return [], None


def g_replay_save(st, rng, m):
    st.sput(RACE + 0x54D, "<B", pick(rng, 0, 1, 2, 3, 4, 5, 6))
    texts = []
    for _ in range(6):
        k = rng.randrange(5)
        if k == 0:
            texts.append(b"ARF%d.TMP" % rng.randrange(1000))
        elif k == 1:
            texts.append(b"\\")
        elif k == 2:
            texts.append(b"")
        else:
            texts.append(b"..\\TEMP\\ARF%X.TMP" % rng.getrandbits(16))
    st.script["temp_text"] = texts
    return [], None


def g_cpu_check(st, rng, m):
    info = bytearray(rng.randbytes(0x24))
    struct.pack_into("<I", info, 0x18, pick(rng, 0x182, 0x1E6, 0x24A, 586, rng.getrandbits(32)))
    if rng.randrange(2):
        struct.pack_into("<H", info, 0, 0)
    st.script["info"] = [bytes(info)]
    return [], 0xFFFFFFFF


def g_ret(st, rng, m):
    return [], 0xFFFFFFFF


def fmt_args(st, rng):
    """A random format string for the Fatal printf and its arguments."""
    parts, args = [], []
    strings = STR + 0x400
    for _ in range(rng.randrange(0, 7)):
        k = rng.randrange(10)
        if k < 3:
            parts.append(bytes(rng.choice(b"abc XYZ:=\n") for _ in range(rng.randrange(1, 10))))
            continue
        width = b""
        if rng.randrange(3) == 0:
            width = pick(rng, b"0", b"00", b"2", b"8", b"08", b"12", b"3")
        conv = rng.choice(b"%sccdiuxXobpd")
        parts.append(b"%" + width + bytes([conv]))
        if conv == ord("s"):
            text = bytes(rng.choice(b"hello world") for _ in range(rng.randrange(0, 12)))
            st.sputb(strings, text + b"\0")
            args.append(strings if rng.randrange(40) else 0)
            strings += 0x20
        elif conv != ord("%"):
            args.append(pick(rng, 0, 1, -1, 10, 255, 0x80000000, rng.getrandbits(32), rng.randrange(-1000, 1000)))
    if rng.randrange(15) == 0:
        parts.append(b"%" + pick(rng, b"q", b"", b"5"))
    return b"".join(parts), args


def g_fatal_format(st, rng, m):
    st.mbcs = pick(rng, 1, 1, 2)
    fmt, args = fmt_args(st, rng)
    st.sputb(STR, fmt + b"\0")
    st.sputb(ARG, b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args) + bytes(16))
    return [STR, ARG], 0xFFFFFFFF


def g_fatal_error(st, rng, m):
    st.mbcs = pick(rng, 1, 1, 2)
    fmt, args = fmt_args(st, rng)
    if rng.randrange(6) == 0:  # too long
        st.sputb(STR + 0x400, bytes(rng.choice(b"long ") for _ in range(0x110)) + b"\0")
        fmt += b"%s"
        args.append(STR + 0x400)
    st.sputb(STR, fmt + b"\0")
    return [STR] + args, None


def g_fatal_number(st, rng, m):
    return [ARG, pick(rng, 0, 1, -1, 10, 255, 0x80000000, rng.getrandbits(32), rng.randrange(-1000, 1000)),
            pick(rng, *b"dixXobpuuc", rng.getrandbits(32)), pick(rng, 0, 0, 1, 3, 8, 12, 20)], 0xFFFFFFFF


def set_d3d_objects(st, rng):
    st.put(0x4D5F40, "<I", obj_addr("dd"))
    st.put(0x4D5F4C, "<I", obj_addr("surf"))
    st.put(0x4D5FA0, "<I", pick(rng, 0, obj_addr("surf", 1)))
    st.put(0x4B8380, "<I", pick(rng, obj_addr("d3d"), obj_addr("d3d"), 0))
    st.put(0x4B8384, "<I", pick(rng, obj_addr("dev"), obj_addr("dev"), 0))
    st.put(0x4B8388, "<I", pick(rng, obj_addr("vp"), obj_addr("vp"), 0))


def hr(rng):
    return pick(rng, 0, 0, 0, 0x80004005, 1)


def g_query(st, rng, m):
    set_d3d_objects(st, rng)
    st.script["dd+0x0"] = [hr(rng)]
    st.script["dd+0x0.out"] = [obj_addr("d3d")]
    return [], 0xFFFFFFFF


def g_enum(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x4B8380, "<I", obj_addr("d3d"))
    st.script["d3d+0xc"] = [hr(rng)]
    return [], 0xFFFFFFFF


def g_pick(st, rng, m):
    n = pick(rng, 0, 1, 2, 3, 5, -1)
    st.put(0x4BA6F4, "<i", n)
    for i in range(max(n, 0)):
        st.put(0x4B89D8 + i * 0x338, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    return [], 0xFFFFFFFF


def g_format_pick(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x4B8384, "<I", obj_addr("dev"))
    st.script["dev+0x24"] = [hr(rng)]
    st.script["format_choose"] = [pick(rng, 0, 1, 5, 20, 40, 47)]
    return [], 0xFFFFFFFF


def g_format_cb(st, rng, m):
    # 0x41d790 (lead, after nb): the EnumTextureFormats callback
    st.put(0x4BAFFC, "<I", rng.randrange(50))
    desc = bytearray(rng.getrandbits(8) for _ in range(0x6C))
    desc[0x4C:0x50] = struct.pack("<I", pick(rng, 0x20, 0x08, 0x28, 0x40, 0, rng.getrandbits(32)))
    st.sputb(ARG, bytes(desc))
    st.regs = {UC_X86_REG_EBX: rng.getrandbits(32), UC_X86_REG_ESI: rng.getrandbits(32),
               UC_X86_REG_EDI: rng.getrandbits(32), UC_X86_REG_EBP: rng.getrandbits(32)}
    return [ARG, rng.getrandbits(32)], 0xFFFFFFFF


def g_device_create(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x4B8380, "<I", obj_addr("d3d"))
    st.script["d3d+0x20"] = [hr(rng)]
    st.script["d3d+0x20.out"] = [obj_addr("dev")]
    st.put(0x4B86A4, "<I", pick(rng, 0, 1))
    st.script["dev+0x24"] = [hr(rng)]
    st.script["format_choose"] = [pick(rng, 0, 3, 47)]
    return [], 0xFFFFFFFF


def set_slots(st, rng):
    for i in range(64):
        base = 0x4BA6FC + i * 0x24
        for off in (4, 8, 0xC):
            st.put(base + off, "<I", pick(rng, 0, obj_addr("tex", rng.randrange(4))))
    st.put(0x4BA6F8, "<i", pick(rng, 0, 1, 5, 64, -1, rng.getrandbits(32)))


def g_slot_release(st, rng, m):
    set_slots(st, rng)
    return [pick(rng, 0, 1, 63, rng.randrange(64))], 0xFFFFFFFF


def g_slots_release(st, rng, m):
    set_slots(st, rng)
    return [], 0xFFFFFFFF


def g_slot_handle(st, rng, m):
    set_slots(st, rng)
    set_d3d_objects(st, rng)
    i = rng.randrange(64)
    st.put(0x4BA6FC + i * 0x24 + 0xC, "<I", obj_addr("tex", 2))
    st.script["tex+0xc"] = [hr(rng)]
    st.script["tex+0xc.out"] = [rng.getrandbits(32)]
    return [i], 0xFFFFFFFF


def g_zbuffer(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x6CCE7C, "<I", pick(rng, 0, 1, 1, 1))
    st.put(0x4B86A8, "<I", pick(rng, 0, 1, 1, 1))
    st.put(0x4B86A0, "<I", pick(rng, 0, 1))
    st.put(0x4B8634, "<I", pick(rng, 0x100, 0x200, 0x400, 0x800, 0x600, 0, rng.getrandbits(32)))
    st.script["dd+0x18"] = [hr(rng)]
    st.script["dd+0x18.out"] = [obj_addr("surf", 1)]
    st.script["surf+0xc"] = [hr(rng)]
    st.script["surface_desc"] = [pick(rng, 1, 1, 0)]
    st.script["desc_caps"] = [pick(rng, 0x4000, 0, rng.getrandbits(32))]
    return [pick(rng, 640, 800, rng.getrandbits(32)), pick(rng, 480, 600, rng.getrandbits(32))], 0xFFFFFFFF


def g_states(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x4B86BC, "<I", rng.getrandbits(32))
    st.put(0x4B8618, "<I", rng.getrandbits(32))
    st.put(0x4B86B4, "<I", pick(rng, 0, 1))
    st.put(0x6CCE7C, "<I", pick(rng, 0, 1))
    st.put(0x4D5F98, "<I", pick(rng, 0, 1))
    st.put(0x6CCE90, "<I", pick(rng, 0, 1, 5))
    return [], 0xFFFFFFFF


def g_states_void(st, rng, m):
    g_states(st, rng, m)
    return [], None


def g_fog_colour(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x4B8384, "<I", obj_addr("dev"))
    return [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)], None


def g_viewport(st, rng, m):
    set_d3d_objects(st, rng)
    st.put(0x4B8380, "<I", obj_addr("d3d"))
    st.put(0x4B8384, "<I", obj_addr("dev"))
    for name in ("d3d+0x18", "dev+0x18", "vp+0x14", "dev+0x34"):
        st.script[name] = [hr(rng)]
    st.script["d3d+0x18.out"] = [obj_addr("vp")]
    return [pick(rng, 640, 800, 1024, 1, 0, rng.randrange(4096), rng.getrandbits(32)),
            pick(rng, 480, 600, 768, 1, 0, rng.randrange(4096), rng.getrandbits(32))], 0xFFFFFFFF


def g_tables(st, rng, m):
    for a in (0x4BCD7C, 0x4BCD80, 0x4BDE00):
        st.put(a, "<I", pick(rng, 0, rng.getrandbits(32)))
    st.script["alloc"] = [BLOCKS, BLOCKS + 0x200, BLOCKS + 0x1A00]
    return [], None


def g_shutdown(st, rng, m):
    set_d3d_objects(st, rng)
    for a in (0x4BCD7C, 0x4BCD80, 0x4BDE00):
        st.put(a, "<I", pick(rng, 0, rng.getrandbits(32)))
    return [], 0xFFFFFFFF


def g_init(st, rng, m):
    set_d3d_objects(st, rng)
    st.script["alloc"] = [BLOCKS, BLOCKS + 0x200, BLOCKS + 0x1A00]
    st.script["dd+0x0"] = [pick(rng, 0, 0, 0, 0, 0, 1)]
    st.script["dd+0x0.out"] = [obj_addr("d3d")]
    st.script["d3d+0x20.out"] = [obj_addr("dev")]
    st.script["d3d+0x18.out"] = [obj_addr("vp")]
    st.script["dd+0x18.out"] = [obj_addr("surf", 1)]
    for name in ("d3d+0x20", "d3d+0x18", "dev+0x18", "vp+0x14", "dev+0x34", "dd+0x18", "surf+0xc", "dev+0x24"):
        st.script[name] = [pick(rng, 0, 0, 0, 0, 0, 0, 0, 0x80004005)]
    st.script["f41ed00"] = [pick(rng, 1, 1, 1, 1, 0)]
    st.script["surface_desc"] = [pick(rng, 1, 1, 0)]
    st.script["desc_caps"] = [pick(rng, 0x4000, 0)]
    st.script["format_choose"] = [pick(rng, 0, 3)]
    st.put(0x6CCE7C, "<I", pick(rng, 0, 1))
    st.put(0x4B86A8, "<I", 1)
    st.put(0x4D5F88, "<I", pick(rng, 640, 800))
    st.put(0x4D5F8C, "<I", pick(rng, 480, 600))
    st.put(0x4B8634, "<I", 0x400)
    st.script["enum_adds"] = [pick(rng, 1, 1, 1, 0)]  # the enumeration (a stub) finds a hardware device
    return [rng.getrandbits(32), pick(rng, 0, 1)], 0xFFFFFFFF


# name: (address, generator, stubbed callee addresses (None: all))
TESTS = {
    "clip_far_z": (0x417950, g_clip_far),
    "clip_polygon_2d": (0x418290, g_clip_2d),
    "clip_polygon_a": (0x417AF0, g_clip_drive),
    "clip_polygon_b": (0x417CE0, g_clip_drive),
    "clip_vertex_add": (0x4180C0, g_vertex_add),
    "clip_vertex_add_2d": (0x418170, g_vertex_add_2d),
    "clip_vertices_restart": (0x418260, g_none),
    "screen_colour_apply": (0x41B2F0, g_screen),
    "replay_files_save": (0x41C550, g_replay_save),
    "cpuid_available": (0x41C8C0, g_ret),
    "cpu_features_read": (0x41C8F0, g_none),
    "cpu_check": (0x41C930, g_cpu_check),
    "fatal_error": (0x41C970, g_fatal_error),
    "fatal_format": (0x41C9D0, g_fatal_format),
    "fatal_number": (0x41CD00, g_fatal_number),
    "debug_hook_nop": (0x41CEC0, g_none),
    "d3d_init": (0x41CED0, g_init),
    "d3d_tables_alloc": (0x41CFD0, g_tables),
    "ddraw_query_d3d": (0x41D100, g_query),
    "d3d_shutdown": (0x41D130, g_shutdown),
    "d3d_device_create": (0x41D1F0, g_device_create),
    "d3d_devices_enumerate": (0x41D250, g_enum),
    "d3d_device_pick": (0x41D6D0, g_pick),
    "d3d_texture_format_pick": (0x41D730, g_format_pick),
    "d3d_texture_format_cb": (0x41D790, g_format_cb),
    "texture_slot_release": (0x41D890, g_slot_release),
    "texture_slots_release": (0x41D910, g_slots_release),
    "texture_slot_handle": (0x41D940, g_slot_handle),
    "zbuffer_create": (0x41D980, g_zbuffer),
    "d3d_render_states": (0x41DB00, g_states),
    "d3d_filter_set": (0x41DD90, g_states_void),
    "d3d_filter_nearest": (0x41DDD0, g_states),
    "d3d_fog_colour_set": (0x41DE10, g_fog_colour),
    "d3d_fog_on": (0x41DE40, g_states_void),
    "d3d_fog_off": (0x41DE60, g_states_void),
    "d3d_zbuffer_on": (0x41DE80, g_states_void),
    "d3d_zbuffer_off": (0x41DEC0, g_states_void),
    "d3d_state_defaults": (0x41DEE0, g_none),
    "d3d_viewport_create": (0x41DF10, g_viewport),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.write(0x4B4D0C, struct.pack("<I", st.mbcs))
    m.write(0x4A313C, struct.pack("<I", TRAMP + 0xF00))
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
        for line in open("build/groups/nb_owned.txt"):
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
    m.uc.mem_map(TRAMP, 0x1000)
    m.uc.mem_write(TRAMP, b"\xc3" * 0x1000)
    stubs = Stubs(m)
    rng = random.Random(29)
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
