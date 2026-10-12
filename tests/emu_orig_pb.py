#!/usr/bin/env python3
"""Differential test of orig_pb.c (group PB: the Direct3D packet drawer and
DrawOTag, and the car drawing, 0x420be0-0x422f50) against the original, on
random state.

Everything outside the group that talks to Direct3D or other subsystems is a
logging stub in both runs (the call log records as many arguments as the
callee reads, stack buffers by content, never stack pointers): the back end's
register-argument helpers 0x420a30 (ebx), 0x420a90 (eax, ebx, the vertex at
edx), 0x4180c0 (eax, ecx, edx, the six dwords at ebx), the clipper entries,
the batch flushes, the filter setters, the texture reload 0x420150, the
lighting set-up 0x459cb0 / 0x45b400, the depth 0x45a2a0, the effects
0x46e4b0 / 0x47b470 / 0x46dff0 / 0x475580 / 0x46c9d0 and 0x4231f0. Surfaces
are fake COM objects whose vtables point at stdcall logging stubs. The GTE
helpers run for real (transform 0x426a20, projection 0x426b00, lighting
0x427400 through their pointers, the projection 0x4266d0, the normal clip
0x4278a0), as do the facing test 0x4278f0 and the tag list clear 0x427a40.
Return value, the scratch pages and the global regions are compared bit for
bit, except 0x6ccd74 (the transform keeps the address of its stack record
there). The stack is zeroed before each run. The x87 control word is the
game's (0x27f) in 3 of 4 runs, 0x37f in the rest.

    tests/emu_orig_pb.py EXE build/emu_pb.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG, UC_X86_REG_EIP)

SCR = 0x01000000
SCR_SIZE = 0x400000
RACE = SCR + 0x0000  # 0x800
WORLD = SCR + 0x0800  # 0x100
OBJS = SCR + 0x0900  # fake COM objects, 0x20 apart
VTBL = SCR + 0x0C00  # one vtable, 0x80
ARG = SCR + 0x0D00
OT = SCR + 0x1000  # 0x800 ordering table slots
BATCH = SCR + 0x2000  # 0x4bcd80's vertex batch (0x2000)
HEAP = SCR + 0x8000  # bump allocation for models and cars
TRAMP = 0x00800000  # COM stub code

REGIONS = [
    (0x4B1960, 4),
    (0x4BA6F0, 0x4BB010 - 0x4BA6F0),
    (0x4BCD80, 4),
    (0x4BDDD8, 4),
    (0x4D1198, 4),
    (0x4D5F40, 0x70),
    (0x4D6F18, 4),
    (0x6BFB80, 8 * 0x274),
    (0x6C1EB0, 4),
    (0x6CCC00, 0x6CE700 - 0x6CCC00),
]
IGNORE = [(0x6CCD74, 4)]

# stubs: address -> (name, kinds, convention). Kinds: v value, l light vector (12 bytes at), c car (pointer
# and its position +0x34, 12 bytes); conventions: c cdecl, s stdcall, com, and the register helpers.
STUBS = {
    0x420A30: ("state_select", "", "r_state"),
    0x420A90: ("batch_vertex", "", "r_batch"),
    0x4180C0: ("clip_vertex", "", "r_clip"),
    0x418170: ("clip_vertex_2d", "vvv", "c"),
    0x417AF0: ("clip_a", "v", "c"),
    0x417CE0: ("clip_b", "v", "c"),
    0x418290: ("clip_2d", "", "c"),
    0x418260: ("clip_restart", "", "c"),
    0x420BB0: ("batch_list", "v", "c"),
    0x420720: ("flush_quads", "", "c"),
    0x4206A0: ("flush_triangles", "", "c"),
    0x41DD90: ("filter_set", "", "c"),
    0x41DDD0: ("filter_nearest", "", "c"),
    0x420150: ("texture_reload", "v", "c"),
    0x459CB0: ("light_set", "vp", "c"),
    0x45B400: ("model_light", "vl", "c"),
    0x45A2A0: ("depth", "c", "c"),
    0x4231F0: ("f4231f0", "", "c"),
    0x46E4B0: ("f46e4b0", "vv", "c"),
    0x47B470: ("f47b470", "vvv", "c"),
    0x46DFF0: ("f46dff0", "vvv", "c"),
    0x475580: ("f475580", "vvv", "c"),
    0x46C9D0: ("f46c9d0", "vv", "c"),
    TRAMP + 0x60: ("IsLost", "", "com"),
    TRAMP + 0x6C: ("Restore", "", "com"),
}
for off in range(0, 0x80, 4):
    if TRAMP + off not in STUBS:
        STUBS[TRAMP + off] = (f"surf+{off:#x}", "", "com")


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def pick(rng, *choices):
    return rng.choice(choices)


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
        self.regs = {}
        self.heap = HEAP
        for k in range(8):
            self.sput(OBJS + k * 0x20, "<I", VTBL)
        for off in range(0, 0x80, 4):
            self.sput(VTBL + off, "<I", TRAMP + off)
        self.put(0x4B1960, "<I", RACE)
        self.put(0x6C1EB0, "<I", WORLD)

    def alloc(self, n, align=4):
        self.heap = (self.heap + align - 1) & ~(align - 1)
        a = self.heap
        self.heap += n
        assert self.heap <= SCR + SCR_SIZE
        return a

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
            return self.sputb(address, data)
        a = self.find(address)
        self.mem[a][address - a:address - a + len(data)] = data

    def sput(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.scr, address - SCR, value)

    def sputb(self, address, data):
        self.scr[address - SCR:address - SCR + len(data)] = data

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
        name, kinds, conv = STUBS[address]
        reg = uc.reg_read
        esp = reg(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        nargs = len(kinds) + (1 if conv == "com" else 0)
        raw = list(words[1:1 + nargs])
        logged = []
        if conv == "r_state":
            logged.append(reg(UC_X86_REG_EBX) & 0xFFFF)
        elif conv == "r_batch":
            edx = reg(UC_X86_REG_EDX)
            logged += [reg(UC_X86_REG_EAX), reg(UC_X86_REG_EBX), rd(self.m, edx + 4, 12)]
        elif conv == "r_clip":
            logged += [reg(UC_X86_REG_EAX), reg(UC_X86_REG_ECX), reg(UC_X86_REG_EDX),
                       rd(self.m, reg(UC_X86_REG_EBX), 24)]
        vals = raw[1:] if conv == "com" else raw
        if conv == "com":
            logged.append(raw[0])
        for kind, a in zip(kinds, vals):
            if kind == "v":
                logged.append(a)
            elif kind == "p":
                logged.append(None if on_stack(a) else a)
            elif kind == "l":
                logged.append(rd(self.m, a, 12))
            elif kind == "c":
                logged.append((a, rd(self.m, a + 0x34, 12)))
        self.log.append((name, tuple(logged)))
        eax = self._next(name, 0)
        if name == "light_set":
            uc.mem_write(raw[1], self._next("light", bytes(12)))
        # the helpers keep ebx / esi / edi / ebp; eax / ecx / edx are left scrambled
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ECX, 0x0BADC0DE)
        uc.reg_write(UC_X86_REG_EDX, 0x0DEFACED)
        pop = 4 + (4 * nargs if conv in ("s", "com") else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + pop)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- packets

PACKET_CODES = [0x8C, 0x98, 0xB4, 0x44, 0x64, 0xAC, 0xA8, 0x9C, 0x94, 0xA0, 0xB0, 0x2C, 0x30]


def rand_vrec(rng):
    """A clipper / batch vertex record: code, z, x, y, two more."""
    code = pick(rng, 0, 0, 0, rng.getrandbits(32), 1 << rng.randrange(7))
    if rng.randrange(8) == 0:
        return [code] + [mk_float(rng) for _ in range(5)]
    return [code, fbits(rng.uniform(1.0, 2000.0)), fbits(rng.uniform(-50.0, 700.0)), fbits(rng.uniform(-50.0, 500.0)),
            rng.getrandbits(32), rng.getrandbits(32)]


def make_packet(st, rng, at, code=None):
    data = bytearray(rng.randbytes(0x90))
    if code is None:
        code = pick(rng, *PACKET_CODES) if rng.randrange(10) else rng.getrandbits(8)
    code |= rng.randrange(4) if rng.randrange(3) == 0 else 0
    data[7] = code
    kind = code & 0xFC
    if kind in (0x8C, 0x9C):
        for k in range(4):
            struct.pack_into("<6I", data, 8 + k * 0x20, *rand_vrec(rng))
    elif kind in (0x98, 0x94):
        for k in range(4):
            struct.pack_into("<6I", data, 8 + k * 0x1C, *rand_vrec(rng))
    elif kind == 0xA8:
        for k in range(4):
            struct.pack_into("<6I", data, 8 + k * 0x18, *rand_vrec(rng))
        if rng.randrange(2):
            for k in range(4):
                struct.pack_into("<I", data, 8 + k * 0x18, 0)
    elif kind == 0xAC:
        struct.pack_into("<6I", data, 8, *rand_vrec(rng))
        struct.pack_into("<6I", data, 0x24, *rand_vrec(rng))
        if rng.randrange(3):
            struct.pack_into("<I", data, 8, 0)
            struct.pack_into("<I", data, 0x24, 0)
    elif kind in (0x44, 0x64):
        struct.pack_into("<4h", data, 8, rng.randrange(-100, 700), rng.randrange(-100, 500), 0, 0)
        data[0xC], data[0xD] = rng.getrandbits(8), rng.getrandbits(8)
        struct.pack_into("<2h", data, 0x10, pick(rng, rng.randrange(-20, 300), rng.randrange(-0x8000, 0x8000)),
                         pick(rng, rng.randrange(-20, 300), rng.randrange(-0x8000, 0x8000)))
    elif kind in (0xA0, 0xB0):
        data[4] = pick(rng, 0, 1, 2, rng.getrandbits(8))
    if kind in (0x8C, 0x98, 0x9C, 0x94) and rng.randrange(2):
        stride = 0x20 if kind in (0x8C, 0x9C) else 0x1C
        for k in range(4):
            struct.pack_into("<I", data, 8 + k * stride, 0)
    st.sputb(at, bytes(data))


def set_batch(st, rng):
    st.put(0x6CD41C, "<I", pick(rng, fbits(rng.uniform(0.2, 3.0)), mk_float(rng)))
    st.put(0x6CD258, "<I", rng.randrange(0, 8))
    st.script["clip_a"] = [pick(rng, 0, ARG, rng.getrandbits(32)) for _ in range(2)]
    st.script["clip_b"] = [pick(rng, 0, ARG, rng.getrandbits(32)) for _ in range(2)]
    st.script["clip_2d"] = [pick(rng, 0, ARG, rng.getrandbits(32))]


def g_packet(st, rng, m):
    set_batch(st, rng)
    p = ARG + 0x40
    make_packet(st, rng, p)
    return [p], None


def g_vertex_repeat(st, rng, m):
    st.put(0x4BCD80, "<I", BATCH)
    st.put(0x4BDDD8, "<I", pick(rng, 2, 3, 4, 5, 6, 7, 8, rng.randrange(2, 40)))
    return [], None


def set_surfaces(st, rng):
    st.put(0x4D5F4C, "<I", pick(rng, OBJS, OBJS, 0))
    st.put(0x4D5F98, "<I", pick(rng, 0, 1))
    st.put(0x4D5FA0, "<I", pick(rng, 0, OBJS + 0x20))
    for i in range(64):
        slot = 0x4BA6FC + i * 0x24
        st.put(slot + 4, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
        st.put(slot + 8, "<I", pick(rng, 0, OBJS + 0x40 + 0x20 * rng.randrange(4)))
    st.script["IsLost"] = [pick(rng, 0, 0, 0x887601C2, 0x887601C2, 1) for _ in range(70)]
    st.script["Restore"] = [pick(rng, 0, 0, 0x80004005) for _ in range(70)]
    st.regs[UC_X86_REG_ECX] = 0  # the original's start value of its "failed" flag when there is no back buffer


def g_restore(st, rng, m):
    set_surfaces(st, rng)
    return [], 0xFFFFFFFF


def g_otag(st, rng, m):
    set_surfaces(st, rng)
    set_batch(st, rng)
    st.script["clip_a"] = [pick(rng, 0, ARG) for _ in range(20)]
    st.script["clip_b"] = [pick(rng, 0, ARG) for _ in range(20)]
    st.script["clip_2d"] = [pick(rng, 0, ARG) for _ in range(20)]
    n = rng.randrange(0, 6)
    ot = OT
    prev = ot
    for k in range(n):
        p = st.alloc(0x90)
        make_packet(st, rng, p)
        tag = rng.randrange(4) == 0
        st.sput(prev, "<I", p | (0x80000000 if tag else 0))
        prev = p
    st.sput(prev, "<I", pick(rng, 0xFFFFFFFF, 0, 0x80000000))
    return [ot], None


# ---------------------------------------------------------------- GTE, view, ordering tables

def set_gte(st, rng, safe=False):
    """safe: every vertex in front of the near plane (the projection skips near-clipped records, whose screen
    x / y the original then takes from leftover stack: only one model per run may have them)."""
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD80, "<I", 0x426B00)
    st.put(0x6CCE3C, "<I", 0x427400)
    s = rng.uniform(0.5, 2.0)
    mat = [s, 0, 0, 0, s, 0, 0, 0, s]
    if rng.randrange(2):
        mat = [rng.uniform(-2, 2) for _ in range(9)]
    if safe:
        mat = [rng.uniform(-1, 1) for _ in range(9)]
    for k, a in enumerate([0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40, 0x6CCD44]):
        st.put(a, "<f", mat[k])
    st.put(0x6CCD0C, "<f", rng.uniform(-100, 100))
    st.put(0x6CCD10, "<f", rng.uniform(-100, 100))
    st.put(0x6CCD14, "<f", rng.uniform(1000, 3000) if safe else
           pick(rng, rng.uniform(-50, 3000), rng.uniform(-50, 3000), rng.uniform(3000, 30000)))
    st.put(0x6CCD00, "<f", rng.uniform(150, 400))
    st.put(0x6CCD04, "<f", pick(rng, 1.0, rng.uniform(0, 50)))
    if safe:
        st.put(0x6CCD08, "<f", 4000.0)
    st.put(0x6CCD08, "<f", pick(rng, 4000.0, rng.uniform(500, 5000)))
    st.put(0x6CCD5C, "<f", 320.0)
    st.put(0x6CCD60, "<f", 240.0)
    st.put(0x6CCD64, "<f", 0.0)
    st.put(0x6CCD68, "<f", 640.0)
    st.put(0x6CCD6C, "<f", 0.0)
    st.put(0x6CCD70, "<f", 480.0)
    for a in range(0x6CCDD0, 0x6CCE34, 4):
        st.put(a, "<f", rng.uniform(-1, 1))
    st.put(0x6CCE94, "<I", pick(rng, 0, 0, 1))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CD4A0, 0x6CDD10))
    st.put(0x6CCE84, "<I", pick(rng, 0, 0, 1))
    st.put(0x6CD404, "<i", pick(rng, 256, 256, 128, 512, rng.randrange(-1000, 1000)))
    st.put(0x6CD478, "<I", (480 << 16) | 640)
    st.put(0x6CCE44, "<I", pick(rng, rng.randrange(100, 3000), rng.getrandbits(32)))
    st.put(0x6CCD84, "<I", rng.getrandbits(32))
    st.put(0x6CCE40, "<I", rng.getrandbits(32))
    # ordering table slot pointers
    for base in (0x6CD510, 0x6CDD80):
        for j in range(0x202):
            st.put(base + j * 4, "<I", (OT + 4 * rng.randrange(0x200)) | (0x80000000 if rng.randrange(4) == 0 else 0))


def s16s(rng, n, lo=-200, hi=200):
    return [rng.randrange(lo, hi) for _ in range(n)]


def make_points(st, rng, n):
    a = st.alloc(n * 8)
    for k in range(n):
        st.sputb(a + k * 8, struct.pack("<4h", *s16s(rng, 3), rng.randrange(-0x8000, 0x8000)))
    return a


def make_lod(st, rng, count, big=False):
    """Faces, packets and a header for one level of detail; returns (header fields, faces, packets)."""
    nb = rng.randrange(1, 6)
    flags = []
    for j in range(nb):
        f = rng.randrange(4)
        if j == 0:
            f |= 2
        if j == nb - 1:
            f |= 4
        flags.append(f)
    nfs = [rng.randrange(1, 5) if rng.randrange(8) else rng.randrange(10, 60) for _ in range(nb)]
    if big:
        nfs = [rng.randrange(40, 60) for _ in range(nb)]
    total = sum(nfs)
    faces = st.alloc(total * 8)
    pk = st.alloc(total * 0x84 + 4)
    n = count + 1
    k = 0
    for j in range(nb):
        left = 8 * (nfs[j] - 1) + (flags[j] if flags[j] else 8)
        st.sput(pk + k * 0x84 + 0x82, "<H", left)
        for _ in range(nfs[j]):
            a, b, c = (rng.randrange(1, n + 1) for _ in range(3))
            d = pick(rng, 0, rng.randrange(1, n + 1))
            st.sput(faces + k * 8, "<I", a | b << 8 | c << 16 | d << 24)
            st.sput(faces + k * 8 + 4, "<I", rng.getrandbits(32))
            k += 1
    written = sum(1 for f in flags if f & 2)
    tables = []
    for _ in range(2):
        t = st.alloc(0x200)
        for j in range(0x80):
            v = 0
            for s in range(rng.randrange(1, 5)):
                v |= rng.randrange(1, written + 1) << (4 * s)
            st.sput(t + j * 4, "<I", v)
        tables.append(t)
    return tables, faces, pk


def make_model(st, rng, base, kind=None, big=False):
    """A model instance at base (fields up to +0x1104)."""
    counts = [rng.randrange(1, 12) for _ in range(3)]
    st.sput(base, "<I", kind if kind is not None else pick(rng, 3, 3, 0x203, 1, rng.getrandbits(32)))
    hdr = st.alloc(0x50 * 3, 4)
    st.sputb(hdr, rng.randbytes(0x50 * 3))
    st.sput(hdr + 0x1C, "<H", 0x50)
    st.sput(hdr + 0x1E, "<H", 0xA0)
    st.sput(base + 0x10, "<I", hdr)
    st.sput(base + 0x24, "<I", make_points(st, rng, max(counts) + 1))
    for lod, (cnt_off, face_off, pk_offs) in enumerate([(0x10D, 0x28, (0x14, 0x18)), (0x162, 0x2C, (0x1C, 0x20)),
                                                         (0x163, 0x30, (0x14C, 0x150))]):
        st.sput(base + cnt_off, "<B", counts[lod])
        tables, faces, pk = make_lod(st, rng, counts[lod], big)
        h = hdr + 0x50 * lod
        st.sput(h + 0x18, "<I", tables[0])
        st.sput(h + 0x44, "<I", pick(rng, 0, tables[1]))
        st.sput(h + 0x48, "<I", make_points(st, rng, counts[lod] + 1))
        st.sput(base + face_off, "<I", faces)
        for off in pk_offs:
            st.sput(base + off, "<I", pk)
    return base


CAR_SIZE = 0x2500


def make_car(st, rng, kind=None, wheels=True):
    car = st.alloc(CAR_SIZE, 16)
    st.sputb(car, rng.randbytes(CAR_SIZE))
    make_model(st, rng, car, kind)
    hdr = st.get(car + 0x10, "<I")
    # shadow points: model + model +0x4c
    shadow = st.alloc(0x10)
    for k in range(2):
        st.sputb(shadow + k * 8, struct.pack("<3hBB", *s16s(rng, 3), pick(rng, 2, 2, 2, rng.getrandbits(8)),
                                             rng.getrandbits(8)))
    st.sput(hdr + 0x4C, "<H", shadow - hdr)
    st.sputb(car + 0x34, struct.pack("<3i", *[rng.randrange(-0x1000000, 0x1000000) for _ in range(3)]))
    for k in range(4):
        w = car + 0x324 + k * 0xC4
        st.sput(w, "<B", rng.getrandbits(8))
        if wheels:
            wm = st.alloc(0x1200, 16)
            st.sputb(wm, rng.randbytes(0x1200))
            make_model(st, rng, wm)
            st.sput(w + 0x6C, "<I", wm)
        for j in range(3):
            st.sputb(w + 0x84 + j * 8, struct.pack("<4h", *s16s(rng, 3), 0))
    # extra points: +0x110c model (+0x24 points, +0x10d count)
    pm = st.alloc(0x110, 4)
    st.sputb(pm, rng.randbytes(0x110))
    cnt = rng.randrange(0, 20)
    st.sput(pm + 0x10D, "<B", cnt)
    st.sput(pm + 0x24, "<I", make_points(st, rng, cnt + 1))
    st.sput(car + 0x110C, "<I", pm)
    # packet ring
    st.sput(car + 0x14B0, "<I", car + 0x1408 + 0x38 * rng.randrange(3))
    st.sput(car + 0x14B4, "<i", pick(rng, rng.randrange(-200, 3000), rng.randrange(-100, -60)))
    st.sput(car + 0x63E, "<B", pick(rng, 0, 0x20, 0x21, rng.getrandbits(8)))
    st.sput(car + 0x63F, "<B", pick(rng, 0, 1))
    st.sput(car + 0x228C, "<I", pick(rng, 0, OT + 4 * rng.randrange(0x200)))
    return car


def set_race(st, rng, cars):
    st.sput(RACE + 0x54D, "<B", len(cars))
    st.sput(RACE + 0x54E, "<B", pick(rng, 0, 1))
    for i, (car, refl) in enumerate(cars):
        rec = 0x6BFB80 + i * 0x274
        st.put(rec, "<I", car)
        st.put(rec + 0x260, "<I", refl)
        st.put(rec + 0x264, "<B", pick(rng, 0, 1))
        st.sput(car + 0x64E, "<H", rng.randrange(len(cars)))


def depth_script(st, rng, n=40):
    st.script["depth"] = [pick(rng, rng.randrange(1, 5000), rng.randrange(-100, 100), 0) for _ in range(n)]


def g_model_lod(st, rng, m):
    set_gte(st, rng)
    base = st.alloc(0x1200, 16)
    st.sputb(base, rng.randbytes(0x1200))
    big = rng.randrange(8) == 0
    make_model(st, rng, base, 3 if big else None, big)
    if big:
        return [base, OT, 0], None
    return [base, (OT + 4 * rng.randrange(0x200)) | (0x80000000 if rng.randrange(3) == 0 else 0),
            pick(rng, rng.randrange(0, 5000), rng.randrange(-100, 8000))], None


def g_model_at(st, rng, m):
    args, _ = g_model_lod(st, rng, m)
    st.sputb(ARG, rng.randbytes(12))
    return [args[0], ARG, args[1], args[2]], None


def g_points(st, rng, m):
    set_gte(st, rng)
    return [make_car(st, rng, wheels=False)], None


def g_wheel_facing(st, rng, m):
    set_gte(st, rng)
    car = make_car(st, rng, wheels=False)
    return [car + 0x324 + 0xC4 * rng.randrange(4)], 0xFFFFFFFF


def g_depth_offset(st, rng, m):
    car = make_car(st, rng, wheels=False)
    st.sputb(ARG, rng.randbytes(12))
    depth_script(st, rng)
    return [car, ARG], 0xFFFFFFFF


def g_wheels(st, rng, m):
    set_gte(st, rng, True)
    car = make_car(st, rng)
    depth_script(st, rng)
    st.sputb(ARG, rng.randbytes(12))
    return [car, ARG], None


def g_car_model(st, rng, m):
    set_gte(st, rng, True)
    car = make_car(st, rng)
    depth_script(st, rng)
    st.sputb(ARG, rng.randbytes(12))
    return [car, ARG, pick(rng, 0, 0x28, rng.randrange(-10, 10))], None


def g_ot_insert(st, rng, m):
    set_gte(st, rng)
    car = make_car(st, rng, wheels=False)
    return [car, pick(rng, rng.randrange(0, 20000), rng.getrandbits(32)), pick(rng, 0, 0x28, rng.getrandbits(32))], None


def g_shadow(st, rng, m):
    set_gte(st, rng)
    car = make_car(st, rng, wheels=False)
    depth_script(st, rng)
    return [car, (OT + 4 * rng.randrange(0x200)) | (0x80000000 if rng.randrange(3) == 0 else 0)], None


def race_cars(st, rng, wheels=True):
    n = pick(rng, 0, 1, 2, 3, 4)
    cars = [(make_car(st, rng, wheels=wheels), make_car(st, rng, wheels=wheels)) for _ in range(n)]
    set_race(st, rng, cars)
    return cars


def g_packets_next(st, rng, m):
    car = make_car(st, rng, wheels=False)
    st.sput(car + 0x14B0, "<I", car + 0x1408 + 0x38 * rng.randrange(-1, 5))
    return [car], None


def g_cars_packets(st, rng, m):
    for car, refl in race_cars(st, rng, wheels=False):
        st.sput(car + 0x14B0, "<I", car + 0x1408 + 0x38 * rng.randrange(0, 3))
        st.sput(refl + 0x14B0, "<I", refl + 0x1408 + 0x38 * rng.randrange(0, 3))
    return [], None


def g_ride(st, rng, m):
    car = make_car(st, rng, wheels=False)
    st.sput(car + 0x14C8, "<i", pick(rng, 0, 24, -24, rng.randrange(-100, 100), rng.getrandbits(32)))
    for k in range(4):
        st.sput(car + 0x324 + k * 0xC4, "<B", pick(rng, 0, 0, 1, rng.getrandbits(8)))
    st.put(0x4D1198, "<I", pick(rng, 0, 0, 1))
    return [car], None


def g_effects(st, rng, m):
    set_gte(st, rng)
    race_cars(st, rng, wheels=False)
    st.script["f47b470"] = [pick(rng, 0, OT + 4 * rng.randrange(0x200)) for _ in range(20)]
    st.put(0x6CCE40, "<I", rng.getrandbits(32) & ~(0xF << 7) | (rng.randrange(5) << 7))
    return [], None


def g_cars_draw(st, rng, m):
    set_gte(st, rng, True)
    cars = race_cars(st, rng)
    for car, _ in cars:
        st.sput(car + 0x646, "<B", pick(rng, 0, 1, 1))
    for k in range(6):
        if rng.randrange(3) == 0 or not cars:
            st.put(0x6CE640 + k * 4, "<I", 0 if rng.randrange(2) else make_car(st, rng))
        else:
            st.put(0x6CE640 + k * 4, "<I", cars[rng.randrange(len(cars))][1])
        st.put(0x6CE620 + k * 4, "<I", pick(rng, 0, 1, 2, rng.getrandbits(8)))
    st.put(0x6CE640 + rng.randrange(6) * 4, "<I", 0)
    for k in range(6):
        c = st.get(0x6CE640 + k * 4, "<I")
        if c:
            st.sput(c + 0x646, "<B", pick(rng, 0, 1, 1))
            st.sput(c + 0x64E, "<H", rng.randrange(max(len(cars), 1)))
    depth_script(st, rng, 200)
    st.script["light"] = [rng.randbytes(12) for _ in range(20)]
    st.script["f47b470"] = [pick(rng, 0, OT + 4 * rng.randrange(0x200)) for _ in range(20)]
    # water boxes for the reflections
    boxes = st.alloc(8 * 8)
    st.sput(WORLD + 0xD4, "<I", boxes)
    st.sput(WORLD + 0xFF, "<B", rng.randrange(0, 8))
    for j in range(8):
        st.sputb(boxes + j * 8, struct.pack("<4h", *s16s(rng, 3, -300, 300), rng.randrange(0, 400)))
    return [], None


def g_matrix_mirror(st, rng, m):
    st.sputb(ARG, rng.randbytes(0x40))
    return [ARG + pick(rng, 0x20, 0x20, 0x10, 0), ARG], None


def g_position_mirror(st, rng, m):
    st.sputb(ARG, rng.randbytes(0x40))
    return [ARG + pick(rng, 0x20, 0x20, 4, 0), ARG, pick(rng, rng.randrange(-0x1000000, 0x1000000),
                                                       rng.getrandbits(32))], None


def water(st, rng, car):
    boxes = st.alloc(8 * 8)
    st.sput(WORLD + 0xD4, "<I", boxes)
    st.sput(WORLD + 0xFF, "<B", pick(rng, 0, 1, 3, 8))
    x, y, z = (st.get(car + 0x34 + 4 * k, "<i") >> 16 for k in range(3))
    for j in range(8):
        r = rng.randrange(0, 400)
        st.sputb(boxes + j * 8, struct.pack("<4h", *[max(-0x8000, min(0x7FFF, c + rng.randrange(-500, 500)))
                                                      for c in (x, y, z)], r))


def g_water(st, rng, m):
    car = make_car(st, rng, wheels=False)
    st.sputb(car + 0x34, struct.pack("<3i", *[rng.randrange(-0x3000000, 0x3000000) for _ in range(3)]))
    water(st, rng, car)
    st.sput(ARG, "<I", 0x5A5A5A5A)
    return [car, ARG], 0xFFFFFFFF


def g_reflection(st, rng, m):
    src = make_car(st, rng)
    dst = make_car(st, rng)
    return [src, dst, rng.getrandbits(32)], None


def g_reflections(st, rng, m):
    cars = race_cars(st, rng)
    if cars:
        water(st, rng, cars[0][0])
    return [], None


# name: (address, generator)
TESTS = {
    "d3d_packet_draw": (0x420BE0, g_packet),
    "d3d_vertex_repeat": (0x421800, g_vertex_repeat),
    "d3d_draw_otag": (0x421860, g_otag),
    "d3d_surfaces_restore": (0x4218E0, g_restore),
    "car_ot_insert": (0x421E60, g_ot_insert),
    "cars_draw": (0x421EF0, g_cars_draw),
    "cars_packets_next": (0x422080, g_cars_packets),
    "car_packets_next": (0x422100, g_packets_next),
    "car_ride_offset_update": (0x422140, g_ride),
    "cars_effects_draw": (0x422210, g_effects),
    "car_model_draw": (0x422440, g_car_model),
    "car_wheels_draw": (0x4224A0, g_wheels),
    "car_points_transform": (0x422540, g_points),
    "wheel_facing_away": (0x4225C0, g_wheel_facing),
    "car_depth_offset": (0x422650, g_depth_offset),
    "model_draw_at": (0x4226A0, g_model_at),
    "model_draw_lod": (0x4226D0, g_model_lod),
    "cars_reflections_update": (0x422C80, g_reflections),
    "car_reflection_build": (0x422D30, g_reflection),
    "matrix_mirror_y": (0x422E30, g_matrix_mirror),
    "position_mirror_y": (0x422E80, g_position_mirror),
    "car_water_find": (0x422EB0, g_water),
    "car_shadow_draw": (0x422F50, g_shadow),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    for reg in (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP):
        m.uc.reg_write(reg, 0x12345678)
    for reg, value in st.regs.items():
        m.uc.reg_write(reg, value)


def snapshot(m):
    for a, n in IGNORE:
        m.write(a, bytes(n))
    return rd(m, SCR, SCR_SIZE) + b"".join(rd(m, a, n) for a, n in REGIONS)


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
        results.append((r, snapshot(m), stubs.log))
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
        for line in open("build/groups/pb_owned.txt"):
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
