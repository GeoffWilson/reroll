#!/usr/bin/env python3
"""Differential test of orig_pn.c (group PN: the pause menu's sprites, file
helpers, the CD root name, camera shake, pickup sprites, debug meters, car
shadows and the registry / mutex / event / environment wrappers,
0x463f90-0x46caa0) against the original, on random state.

Every callee outside the group is a logging stub in both runs (as many
arguments as the callee reads: values, strings by content, buffers by
content, stack pointers as "stack") with scripted results; the Windows
calls go through their import slots (stdcall stubs that write their out
parameters). The group's own functions run for real in both (the
original's call the original's, ours call ours), and so does pure code:
__ftol 0x497a80, the vertex projection 0x4288c0 / 0x426b00 and the
transform pair 0x426a20 / 0x426b00 behind 0x6ccd78 / 0x6ccd80.

The original leaves a near vertex's screen x / y in 0x46caa0 as stack
leftovers (ours writes 0): each case runs the original twice with
different stack fills and masks the memory bytes that differ between those
two runs. Compared: the return value (functions that return one), ebx /
esi / edi / ebp, the x87 stack top / tag word, the globals and scratch
memory, the call log. The x87 control word is 0x27f, 0x37f or 0x07f.

    tests/emu_orig_pn.py EXE build/emu_pn.elf [iterations] [names...] [-v] [-c]
"""
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
SCR_SIZE = 0x16000
ARG = SCR + 0x0000  # argument blocks, vectors (0x1000)
TEXHDR = SCR + 0x1000  # *0x6c1eb0 (0x100)
TEX = SCR + 0x2000  # *0x6c1e14 texture records (0x2000)
CARS = SCR + 0x4000  # 2 cars of 0x1800
PICKUPS = SCR + 0x8000  # 6 pickups of 0x600
METERS_A = SCR + 0xB000  # *0x6b8cd8 (0x2000)
METERS_B = SCR + 0xD000  # *0x6b8cdc (0x2000)
OTAREA = SCR + 0x10000  # ordering table slots (0x1000)
BUF = SCR + 0x11000  # file / registry buffers (0x1000)
CTYPE = SCR + 0x12000  # *0x4b4b00 (0x200)
LIST = SCR + 0x13000  # pointer lists (0x100)
STR = SCR + 0x14000  # strings (0x1000)
IATSTUB = 0x02200000

REGIONS = [
    (0x4B1638, 0x40),  # pause sprite kinds
    (0x4B16BC, 4),  # shake seed
    (0x4B1948, 4),  # meter car
    (0x4B1978, 8),  # registry root, kind
    (0x4B24F8, 0x280),  # sine / cosine
    (0x4B4B00, 4),  # _pctype
    (0x4B4D0C, 4),  # __mb_cur_max
    (0x4BDF00, 4),  # projection dump
    (0x4BE3F4, 0x60),  # root name cache
    (0x4D10F4, 0x40),  # pause texts
    (0x4D119C, 0x100),  # root name
    (0x4D3CA0, 0x14),  # meter pairs
    (0x4D3DB4, 0x20),  # registry / mutex / event state
    (0x4D3DE4, 0x1C),  # shadow list
    (0x6B8CD8, 0x10),  # meters, pickups list
    (0x6B8D60, 0xAE0),  # pause menu
    (0x6BFB80, 4),
    (0x6C0A50, 4),
    (0x6C1E14, 4),
    (0x6C1EB0, 4),
    (0x6CCB00, 0x390),  # GTE state, projection constants, flags
    (0x6CD408, 0xC),
    (0x6CDD80, 0x800),  # depth buckets
    (0x6CE5F0, 0x20),  # views
    (0x6CF7E8, 0x540),  # camera shake table
]

# argument kinds: v value, s string, p pointer ("stack" on the stack), m<n> n bytes at a pointer,
# c<i> the bytes at argument i-1 with the length in argument i
IAT = {
    0x4A3000: ("RegDeleteValueA", "v s"),
    0x4A3008: ("RegCreateKeyExA", "v s v v v v v p p"),
    0x4A300C: ("RegCloseKey", "v"),
    0x4A3010: ("RegOpenKeyExA", "v s v v p"),
    0x4A3014: ("RegQueryValueExA", "v s v p p p"),
    0x4A3018: ("RegSetValueExA", "v s v v c5 v"),
    0x4A301C: ("RegUnLoadKeyA", "v s"),
    0x4A308C: ("GetEnvironmentVariableA", "s p v"),
    0x4A3090: ("ResetEvent", "v"),
    0x4A30A8: ("SetEvent", "v"),
    0x4A30AC: ("CreateEventA", "v v v s"),
    0x4A30B8: ("OpenEventA", "v v s"),
    0x4A30FC: ("CreateMutexA", "v v s"),
    0x4A3100: ("CloseHandle", "v"),
    0x4A3104: ("SetFilePointer", "v v v v"),
    0x4A3108: ("CreateFileA", "s v v v v v v"),
    0x4A310C: ("GetLastError", ""),
    0x4A3110: ("Sleep", "v"),
}

STUBS = {
    0x428460: ("draw_prim", "m120"),
    0x4792A0: ("texts_menu", "v v"),
    0x479060: ("text_create", "v v v v v v v"),
    0x4270E0: ("prim_flag2", "v v"),
    0x423CE0: ("camera_target", "m12"),
    0x47E5F0: ("sprite_3d", "m6 v v v v v"),
    0x43B7F0: ("file_read_any", "v p v"),
    0x43B760: ("file_close_any", "v"),
    0x476E50: ("file_write_any", "v c2 v"),
    0x4445E0: ("kr_malloc", "v"),
    0x476EC0: ("debug_printf", "p"),
    0x476F00: ("file_exists", "s"),
    0x498051: ("isctype", "v v"),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def mapped(a):
    return (0x400000 <= a < 0x6E0000) or (SCR <= a < SCR + SCR_SIZE) or on_stack(a)


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(IATSTUB, 0x1000)
        m.uc.mem_write(IATSTUB, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, kinds)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, kinds.split(), True)
        for address, (name, kinds) in STUBS.items():
            self.calls[address] = (name, kinds.split(), False)
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
        name, kinds, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<10I", bytes(uc.mem_read(esp, 40)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            if k == "v":
                logged.append(a)
            elif k == "p":
                logged.append("stack" if on_stack(a) else a)
            elif k == "s":
                logged.append(bytes(uc.mem_read(a, 0x80)).split(b"\0")[0] if a and mapped(a) else a)
            elif k[0] == "m":
                logged.append(bytes(uc.mem_read(a, int(k[1:]))) if a and mapped(a) else a)
            elif k[0] == "c":
                n = args[int(k[1:])]
                logged.append(("stack" if on_stack(a) else a, bytes(uc.mem_read(a, min(n, 0x400))) if a else b""))
        eax = 0
        if name == "file_read_any":
            n = min(args[2], 0x400)
            data = self._next("read_data", bytes(range(256)) * 4)[:n]
            if args[1]:
                uc.mem_write(args[1], data)
            eax = self._next(name, len(data))
        elif name == "isctype":
            eax = struct.unpack("<H", bytes(uc.mem_read(CTYPE + 2 * (args[0] & 0xFF), 2)))[0] & args[1]
        elif name in ("RegCreateKeyExA", "RegOpenKeyExA"):
            eax, handle = self._next(name, (0, 0x1234))
            out = args[7] if name == "RegCreateKeyExA" else args[4]
            if eax == 0:
                uc.mem_write(out, struct.pack("<I", handle))
                if name == "RegCreateKeyExA" and args[8]:
                    uc.mem_write(args[8], struct.pack("<I", 1))
        elif name == "RegQueryValueExA":
            eax, kind, data, size = self._next(name, (0, 4, b"\1\2\3\4", 4))
            if eax in (0, 234):  # ERROR_MORE_DATA: this stub writes what fits there too
                if args[3]:
                    uc.mem_write(args[3], struct.pack("<I", kind))
                cap = struct.unpack("<I", bytes(uc.mem_read(args[5], 4)))[0] if args[5] else 0
                if args[4]:
                    uc.mem_write(args[4], data[:min(cap, len(data))])
                if args[5]:
                    uc.mem_write(args[5], struct.pack("<I", size))
        elif name == "GetEnvironmentVariableA":
            value = self._next(name, None)
            if value is None:
                eax = 0
            elif args[2] > len(value):
                uc.mem_write(args[1], value + b"\0")
                eax = len(value)
            else:
                eax = len(value) + 1
        else:
            eax = self._next(name, 0)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * len(kinds) if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---------------------------------------------------------------- state

class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
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

    def putb(self, address, data):
        buf, off = self._where(address)
        buf[off:off + len(data)] = data


def text(rng, n=None):
    n = rng.randrange(0, 20) if n is None else n
    return bytes(rng.randrange(0x21, 0x7F) for _ in range(n)) + b"\0"


def put_str(st, rng, address, value=None):
    st.putb(address, text(rng) if value is None else value)
    return address


def s16(rng):
    return pick(rng, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), 0, 0x1000)


def scale(st, rng):
    st.put(0x6CD410, "<i", pick(rng, 0x100, 0x100, rng.randrange(0x40, 0x300), r32(rng)))


def screen(st, rng):
    st.put(0x6B8D6C, "<I", pick(rng, 640, 3440, rng.randrange(0, 4000), r32(rng)))
    st.put(0x6B8D70, "<I", pick(rng, 480, 1440, rng.randrange(0, 4000), r32(rng)))


def textures(st, rng):
    st.put(0x6C1EB0, "<I", TEXHDR)
    st.put(TEXHDR + 0x2E, "<H", rng.randrange(0, 0x100))
    st.put(0x6C1E14, "<I", TEX)
    st.put(0x6CCD84, "<I", pick(rng, 0, 4, 2, r32(rng)))


def floats(st, rng, address, n, lo=-1000.0, hi=1000.0):
    for i in range(n):
        st.put(address + 4 * i, "<f", rng.uniform(lo, hi))


# ---- the pause menu

def g_pause_draw(st, rng):
    n = pick(rng, 0, 1, 3, 8, rng.randrange(0, 9))
    st.put(0x6B958E, "<h", n)
    sel = rng.randrange(0, 4)
    st.put(0x6B8D66, "<H", sel)
    for i in range(10):
        st.put(0x6B9590 + 2 * i, "<H", pick(rng, sel, rng.randrange(0, 4), r32(rng)))
    return []


def g_pause_open(st, rng):
    scale(st, rng)
    screen(st, rng)
    textures(st, rng)
    st.put(0x6B958E, "<h", rng.randrange(0, 8))
    st.put(0x6B8D64, "<H", pick(rng, 2, 0, r32(rng)))  # the old kind, kept at 0x6b95a2
    car = CARS
    st.put(0x6BFB80, "<I", car)
    st.put(car + 0x2470, "<I", CARS + 0x1800)
    kind = pick(rng, 0, 1, 2, 3, 10, 11, 20, r32(rng))
    return [kind, r32(rng)]


def g_item_add(st, rng):
    scale(st, rng)
    screen(st, rng)
    textures(st, rng)
    st.put(0x6B958E, "<h", rng.randrange(0, 9))
    return [pick(rng, *range(8), rng.randrange(0, 0x20)), r32(rng)]


def g_slider(st, rng):
    scale(st, rng)
    screen(st, rng)
    textures(st, rng)
    return [rng.randrange(0, 9), pick(rng, *range(8)), r32(rng)]


# ---- files

def file_scripts(st, rng):
    fail = rng.randrange(4)
    handle = pick(rng, 0x40, r32(rng), 0)
    if fail == 0:
        st.script["CreateFileA"] = [handle]
    elif fail == 1:
        st.script["CreateFileA"] = [0xFFFFFFFF, 0xFFFFFFFF, handle]
        st.script["GetLastError"] = [0x21, 0x21]
    elif fail == 2:
        st.script["CreateFileA"] = [0xFFFFFFFF]
        st.script["GetLastError"] = [pick(rng, 2, 3, 5, 0x20)]
    else:
        st.script["CreateFileA"] = [0xFFFFFFFF, 0xFFFFFFFF]
        st.script["GetLastError"] = [0x21, pick(rng, 2, 5)]


def path_arg(st, rng):
    return put_str(st, rng, STR, pick(rng, b"DBUG\0", b"DBU\0", b"DBUGX\0", text(rng)))


def g_file_open(st, rng):
    file_scripts(st, rng)
    return [path_arg(st, rng), pick(rng, 0, 0x40000000, r32(rng))]


def g_file_load(st, rng):
    file_scripts(st, rng)
    st.script["SetFilePointer"] = [r32(rng), pick(rng, rng.randrange(0, 0x200), r32(rng)), r32(rng)]
    st.script["kr_malloc"] = [pick(rng, BUF, 0)]
    st.script["read_data"] = [rng.randbytes(0x400)]
    return [path_arg(st, rng)]


def g_file_append(st, rng):
    file_scripts(st, rng)
    st.script["file_close_any"] = [r32(rng)]
    return [path_arg(st, rng), BUF, rng.randrange(0, 0x100)]


def g_three(st, rng):
    st.script["SetFilePointer"] = [r32(rng), r32(rng), r32(rng)]
    st.script["file_close_any"] = [r32(rng)]
    st.script["file_read_any"] = [r32(rng)]
    st.script["file_write_any"] = [r32(rng)]
    return [r32(rng), BUF, rng.randrange(0, 0x100)]


def g_root_name(st, rng):
    st.put(0x4BE3F4, "<B", pick(rng, 0, 0, 0, 0x41))
    st.script["file_exists"] = [pick(rng, 0, 1) for _ in range(3)]
    st.script["CreateFileA"] = [pick(rng, 0xFFFFFFFF, 0x50, 0) for _ in range(3)]
    st.script["GetLastError"] = [pick(rng, 2, 5) for _ in range(3)]
    st.put(0x4B4D0C, "<i", pick(rng, 1, 1, 2, r32(rng)))
    st.put(0x4B4B00, "<I", CTYPE)
    for c in range(256):
        v = pick(rng, 0x157, 0x101, 0x20, 0, r32(rng)) if 0x20 <= c < 0x7F else pick(rng, 0, 0x20, r32(rng))
        st.put(CTYPE + 2 * c, "<H", 0 if c == 0 else v)
    data = bytes(pick(rng, rng.randrange(0x20, 0x7F), rng.randrange(0x20, 0x7F), rng.randrange(256))
                 for _ in range(0x50))
    st.script["read_data"] = [data]
    st.put(0x4D119C + rng.randrange(0x50, 0x100), "<B", 0)
    return []


# ---- camera shake, pickups, meters

def g_shake(st, rng):
    car = CARS
    st.put(car + 0x6A8, "<h", pick(rng, 0, 1, 2, rng.randrange(0, 40), r32(rng)))
    st.put(car + 0x6AA, "<h", pick(rng, 0, 0xBB8, 0x5000, rng.randrange(0, 0x8000), r32(rng)))
    st.put(0x6CCE40, "<I", pick(rng, 0, 0x10, r32(rng)))
    view = rng.randrange(0, 4)
    st.put(0x6CD408, "<I", view)
    st.put(0x6CF7E8 + view * 0x150, "<I", pick(rng, 0, 1, r32(rng)))
    st.put(0x6CF7EC + view * 0x150, "<i", pick(rng, 0, 0x3000, rng.randrange(0, 0x6000), r32(rng)))
    return [ARG, car]


def pickup(st, rng, p):
    st.put(p + 0x182, "<h", pick(rng, 0x81, 0x82, 0x83, 0x84, 0x80, 0x85, 0x01, 0x181, rng.getrandbits(16)))
    st.put(p + 0x180, "<h", pick(rng, 0, 0x11, 0x12, 0x1A, rng.randrange(0, 0x20), s16(rng)))
    for off in (0x34, 0x38, 0x3C):
        st.put(p + off, "<i", pick(rng, rng.randrange(-0x1000000, 0x1000000), r32(rng)))


def views(st, rng):
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CD4A0, r32(rng)))
    st.put(0x6CE608, "<I", pick(rng, 0x6CE5F0, 0x6CE5F0, r32(rng)))


def g_pickups(st, rng):
    views(st, rng)
    n = pick(rng, 0, 1, 3, 6, rng.randrange(-2, 7))
    st.put(0x6B8CE4, "<i", n)
    st.put(0x6B8CE0, "<I", LIST)
    for i in range(6):
        p = PICKUPS + i * 0x600
        st.put(LIST + 4 * i, "<I", p)
        pickup(st, rng, p)
    return []


def g_glow(st, rng):
    views(st, rng)
    pickup(st, rng, PICKUPS)
    return [PICKUPS, pick(rng, 0x7F0F7F, r32(rng))]


def g_rings(st, rng):
    views(st, rng)
    pickup(st, rng, PICKUPS)
    return [PICKUPS]


def meters(st, rng):
    views(st, rng)
    st.put(0x6B8CD8, "<I", METERS_A)
    st.put(0x6B8CDC, "<I", METERS_B)
    car = CARS
    st.put(0x4B1948, "<I", pick(rng, car, car, car, 0xFFFFFFFF))
    for off, lo, hi in ((0x227C, 0x40000, 0x120000), (0x2274, -80000, 80000), (0x2280, 0, 3), (0x2278, -3500, 5000)):
        st.put(car + off, "<i", pick(rng, rng.randrange(lo, hi + 1), lo, hi, rng.randrange(lo * 2 - 10, hi * 2 + 10)))
    for base in (METERS_A, METERS_B):
        for i in range(4):
            st.put(base + i * 0x298 + 0x294, "<H", pick(rng, 0, 0xFF, rng.randrange(0, 0x100), rng.getrandbits(16)))


def g_meters_colour(st, rng):
    meters(st, rng)
    return [r32(rng)]


def g_meter_level(st, rng):
    meters(st, rng)
    return [pick(rng, 0, 1, 2, 3, rng.randrange(0, 4), rng.randrange(4, 7))]


def g_meters_link(st, rng):
    views(st, rng)
    st.put(0x6B8CD8, "<I", METERS_A)
    st.put(0x6B8CDC, "<I", METERS_B)
    return [OTAREA + 4 * rng.randrange(0, 0x100) + pick(rng, 0, 0x80000000), ARG]


# ---- registry, handles, environment

def reg_state(st, rng):
    st.put(0x4D3DB8, "<I", pick(rng, 0, 1, 1, r32(rng)))
    st.put(0x4D3DB4, "<I", r32(rng))
    st.put(0x4B1978, "<I", pick(rng, 0x80000002, r32(rng)))
    st.put(0x4D3DC0, "<I", pick(rng, 0, 4, rng.randrange(0, 0x40)))
    ret = lambda: pick(rng, 0, 0, 0, 2, 5)  # noqa: E731
    st.script["RegCloseKey"] = [ret() for _ in range(3)]
    st.script["RegOpenKeyExA"] = [(ret(), r32(rng)) for _ in range(2)]
    st.script["RegCreateKeyExA"] = [(ret(), r32(rng)) for _ in range(2)]
    st.script["RegDeleteValueA"] = [ret()]
    st.script["RegSetValueExA"] = [ret()]
    st.script["RegUnLoadKeyA"] = [ret()]
    queries = []
    for _ in range(3):
        kind = pick(rng, 0, 1, 2, 3, 4, 5, 6, 7, 8, r32(rng))
        n = pick(rng, 4, 4, 0, rng.randrange(0, 0x30))
        data = rng.randbytes(n)
        queries.append((pick(rng, ret(), ret(), 234), kind, data, pick(rng, n, n, n, 0xFFFFFFFF)))
    st.script["RegQueryValueExA"] = queries


def name_arg(st, rng, at=STR):
    return put_str(st, rng, at, pick(rng, b"\0", text(rng), text(rng)))


def size_arg(rng):
    return pick(rng, -1, 0, 4, rng.randrange(-4, 0x40), r32(rng) | 0x80000000)


def g_reg0(st, rng):
    reg_state(st, rng)
    return []


def g_reg1(st, rng):
    reg_state(st, rng)
    return [pick(rng, name_arg(st, rng), name_arg(st, rng), 0)]


def g_reg_read(st, rng):
    reg_state(st, rng)
    return [name_arg(st, rng), pick(rng, BUF, BUF, 0), size_arg(rng)]


def g_reg_write(st, rng):
    reg_state(st, rng)
    return [name_arg(st, rng), pick(rng, BUF, BUF, 0), size_arg(rng), pick(rng, 1, 3, 4, r32(rng))]


def g_reg_dword(st, rng):
    reg_state(st, rng)
    return [name_arg(st, rng), pick(rng, BUF, 0)]


def g_reg_key_name_buf(st, rng):
    reg_state(st, rng)
    return [name_arg(st, rng, STR + 0x100), name_arg(st, rng), pick(rng, BUF, BUF, 0), size_arg(rng)]


def g_reg_key_name_out(st, rng):
    reg_state(st, rng)
    return [name_arg(st, rng, STR + 0x100), name_arg(st, rng), pick(rng, BUF, 0)]


def g_reg_key_name(st, rng):
    reg_state(st, rng)
    return [name_arg(st, rng, STR + 0x100), name_arg(st, rng)]


def handle_state(st, rng):
    st.put(0x4D3DC8, "<I", pick(rng, 0, r32(rng)))
    st.script["CloseHandle"] = [pick(rng, 0, 1) for _ in range(2)]
    st.script["GetLastError"] = [pick(rng, 0, 0xB7, 5, r32(rng)) for _ in range(2)]
    for name in ("CreateMutexA", "CreateEventA", "OpenEventA"):
        st.script[name] = [pick(rng, 0, 0x88, r32(rng))]
    st.script["SetEvent"] = [r32(rng)]
    st.script["ResetEvent"] = [r32(rng)]


def g_handle(st, rng):
    handle_state(st, rng)
    return [pick(rng, 0, 0x88, r32(rng))]


def g_handle0(st, rng):
    handle_state(st, rng)
    return []


def g_handle_out(st, rng):
    handle_state(st, rng)
    return [pick(rng, BUF, 0, 0x4D3DC8), name_arg(st, rng)]


def g_handle_name(st, rng):
    handle_state(st, rng)
    return [name_arg(st, rng)]


def env_script(st, rng):
    st.script["GetEnvironmentVariableA"] = [pick(rng, None, b"", b"x", text(rng), text(rng, 30))] * 2


def g_env_get(st, rng):
    env_script(st, rng)
    return [pick(rng, name_arg(st, rng), name_arg(st, rng), 0), pick(rng, BUF, BUF, 0), size_arg(rng)]


def g_env1(st, rng):
    env_script(st, rng)
    return [pick(rng, name_arg(st, rng), 0)]


# ---- shadows

def gte(st, rng):
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD80, "<I", 0x426B00)
    for a in range(0x6CCD0C, 0x6CCD48, 4):
        st.put(a, "<f", rng.uniform(-1.5, 1.5))
    st.put(0x6CCD0C, "<f", rng.uniform(-500, 500))
    st.put(0x6CCD10, "<f", rng.uniform(-500, 500))
    st.put(0x6CCD14, "<f", pick(rng, rng.uniform(-200, 3000), rng.uniform(-50, 50), rng.uniform(0, 40000)))
    st.put(0x6CCD00, "<f", rng.uniform(100, 600))
    st.put(0x6CCD04, "<f", pick(rng, 1.0, 10.0, rng.uniform(0, 300)))
    st.put(0x6CCD08, "<f", pick(rng, 2000.0, rng.uniform(100, 5000)))
    for a in (0x6CCD5C, 0x6CCD60):
        st.put(a, "<f", rng.uniform(-400, 400))
    for a in (0x6CCD64, 0x6CCD68, 0x6CCD6C, 0x6CCD70):
        st.put(a, "<f", rng.uniform(-1000, 1000))


def shadow_block(st, rng, s):
    for k in range(4):
        st.put(s + 8 * k, "<h", rng.randrange(-600, 600))
        st.put(s + 8 * k + 2, "<h", rng.randrange(-600, 600))
        st.put(s + 8 * k + 4, "<h", rng.randrange(-600, 600))
    for v in (0, 1):  # the two views' quads: floats in the vertex records
        prim = s + 0x20 + 0x84 * v
        for k in range(4):
            floats(st, rng, prim + 8 + 0x20 * k + 4, 1, -200, 3000)
            floats(st, rng, prim + 8 + 0x20 * k + 0x10, 2)


def g_shadows_draw(st, rng):
    gte(st, rng)
    views(st, rng)
    n = pick(rng, 0, 1, 2, 6, rng.randrange(0, 7))
    st.put(0x4D3DFC, "<I", n)
    for i in range(6):
        s = CARS + (i % 2) * 0x1800 + 0x720
        st.put(0x4D3DE4 + 4 * i, "<I", s)
        st.put(s + 6, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
        shadow_block(st, rng, s)
    return []


def g_shadow_project(st, rng):
    gte(st, rng)
    s = CARS + 0x720
    shadow_block(st, rng, s)
    return [s, pick(rng, 0, 1)]


def g_shadow_subdivide(st, rng):
    gte(st, rng)
    s = CARS + 0x720
    prim = s + pick(rng, 0x20, 0xA4)
    st.put(s + 0x128, "<I", prim)
    floats(st, rng, prim, 0x20)
    for k in range(4):
        st.put(prim + 8 + 0x20 * k + 4, "<f", pick(rng, rng.uniform(-200, 3000), rng.uniform(0, 20)))
    for off in (4, 5, 6, 0x20, 0x21, 0x24, 0x25, 0x26, 0x40, 0x41, 0x44, 0x45, 0x46, 0x60, 0x61):
        st.put(prim + off, "<B", rng.getrandbits(8))
    return [s, r32(rng)]


def g_shadow_add(st, rng):
    gte(st, rng)
    car = CARS
    s = car + 0x720
    st.put(car + 0x848, "<I", pick(rng, s + 0x20, s + 0xA4, 0))
    st.put(car + 0x14B4, "<i", pick(rng, rng.randrange(-0x100, 0x8000), rng.randrange(0, 0x40000), r32(rng)))
    st.put(0x6CCD84, "<I", pick(rng, 2, 0, r32(rng)))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, r32(rng)))
    for j in range(0x200):
        st.put(0x6CDD80 + 4 * j, "<I", OTAREA + 4 * rng.randrange(0, 0x400) + pick(rng, 0, 0x80000000))
    for prim in (s + 0x20, s + 0xA4):
        floats(st, rng, prim, 0x20)
        for k in range(4):
            st.put(prim + 8 + 0x20 * k + 4, "<f", rng.uniform(-200, 3000))
    st.put(s + 0x128, "<I", pick(rng, s + 0x20, s + 0xA4))
    return [car, OTAREA + 4 * rng.randrange(0, 0x400) + pick(rng, 0, 0x80000000)]


# name: (address, generator, returns a value)
TESTS = {
    "pause_menu_draw": (0x463F90, g_pause_draw, False),
    "pause_menu_open": (0x464650, g_pause_open, False),
    "pause_menu_item_add": (0x464A20, g_item_add, True),
    "pause_menu_slider_set": (0x464C10, g_slider, False),
    "file_load_whole": (0x465340, g_file_load, True),
    "file_open_retry": (0x465390, g_file_open, True),
    "file_read_handle": (0x465460, g_three, True),
    "file_close_handle": (0x465480, g_three, True),
    "file_size_get": (0x465490, g_three, True),
    "file_seek_set": (0x4654D0, g_three, True),
    "file_append_data": (0x4654F0, g_file_append, True),
    "file_write_handle": (0x465530, g_three, True),
    "root_name_get": (0x465550, g_root_name, True),
    "camera_shake_apply": (0x466F70, g_shake, False),
    "pickups_draw": (0x467EC0, g_pickups, False),
    "pickup_glow_draw": (0x467F30, g_glow, False),
    "pickup_rings_draw": (0x467FF0, g_rings, False),
    "meters_colour_set": (0x469D60, g_meters_colour, False),
    "meter_level_get": (0x469E80, g_meter_level, True),
    "meters_link": (0x469F80, g_meters_link, False),
    "reg_key_close": (0x46B200, g_reg0, True),
    "reg_key_open": (0x46B240, g_reg1, True),
    "reg_key_create": (0x46B290, g_reg1, True),
    "reg_key_unload": (0x46B2F0, g_reg1, True),
    "reg_value_type_query": (0x46B330, g_reg1, True),
    "reg_value_read": (0x46B3F0, g_reg_read, True),
    "reg_value_write": (0x46B490, g_reg_write, True),
    "reg_value_delete": (0x46B4E0, g_reg1, True),
    "reg_string_read": (0x46B550, g_reg_read, True),
    "reg_dword_read": (0x46B5A0, g_reg_dword, True),
    "reg_binary_read": (0x46B600, g_reg_read, True),
    "reg_string_get": (0x46B650, g_reg_key_name_buf, True),
    "reg_dword_get": (0x46B690, g_reg_key_name_out, True),
    "reg_binary_get": (0x46B6D0, g_reg_key_name_buf, True),
    "reg_binary_write": (0x46B710, g_reg_read, True),
    "reg_binary_set": (0x46B730, g_reg_key_name_buf, True),
    "reg_value_remove": (0x46B770, g_reg_key_name, True),
    "win_handle_close": (0x46B7D0, g_handle, True),
    "win_mutex_create": (0x46B800, g_handle_out, True),
    "app_mutex_close": (0x46B850, g_handle0, True),
    "app_mutex_create": (0x46B870, g_handle_name, True),
    "win_handle_close_quiet": (0x46B890, g_handle, True),
    "win_event_create": (0x46B8B0, g_handle_out, True),
    "win_event_open": (0x46B900, g_handle_out, True),
    "win_event_set": (0x46B940, g_handle, True),
    "win_event_reset": (0x46B950, g_handle, True),
    "env_var_get": (0x46B960, g_env_get, True),
    "env_var_length": (0x46B9C0, g_env1, True),
    "env_var_exists": (0x46B9E0, g_env1, True),
    "shadows_draw": (0x46C840, g_shadows_draw, False),
    "shadow_project": (0x46C890, g_shadow_project, True),
    "shadow_add": (0x46C9D0, g_shadow_add, False),
    "shadow_subdivide": (0x46CAA0, g_shadow_subdivide, False),
}

SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def apply(m, st, cw, fill):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes([fill]) * E.STACK_SIZE)
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.uc.reg_write(UC_X86_REG_EBX, 0x11111111)
    m.uc.reg_write(UC_X86_REG_ESI, 0x22222222)
    m.uc.reg_write(UC_X86_REG_EDI, 0x33333333)
    m.uc.reg_write(UC_X86_REG_EBP, 0x44444444)


def snapshot(m):
    # 0x6ccd74, the projection's current record: a stack address in 0x46caa0
    record = struct.unpack("<I", m.read(0x6CCD74, 4))[0]
    if E.STACK <= record < E.STACK + E.STACK_SIZE:
        m.write(0x6CCD74, b"STAK")
    return m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS)


def where(offset):
    if offset < SCR_SIZE:
        return SCR + offset
    offset -= SCR_SIZE
    for a, n in REGIONS:
        if offset < n:
            return a + offset
        offset -= n
    return None


def run_case(m, stubs, name, address, gen, returns, rng):
    st = State(rng)
    args = gen(st, rng)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target, fill in ((address, 0x5A), (address, 0xA5), (m.symbols[name], 0x5A)):
        apply(m, st, cw, fill)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok", r[1] if returns else None) + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        else:
            r = (r[0],)
        results.append((r, snapshot(m), stubs.log))
    (ra, ma, la), (_, mb, _), (rc, mc, lc) = results
    fault = ra[0] != "ok"
    bad = None
    if ra != rc:
        bad = "result"
    elif not fault and la != lc:
        bad = "calls"
    elif not fault and ma != mc:
        diff = [i for i in range(len(ma)) if ma[i] != mc[i] and ma[i] == mb[i]]
        if diff:
            bad = f"memory at {where(diff[0]):#x}"
            if "-v" in sys.argv:
                print("  differ:", [(hex(where(i)), ma[i], mc[i]) for i in diff[:12]])
    if bad:
        msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: {bad}: original {ra} ours {rc}"
        if la != lc:
            k = next((i for i in range(min(len(la), len(lc))) if la[i] != lc[i]), min(len(la), len(lc)))
            msg += f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lc[k:k + 2]}"
        return msg, fault
    return None, fault


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pn_owned.txt"):
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
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(61)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, returns) in TESTS.items():
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
            msg, fault = run_case(m, stubs, name, address, gen, returns, rng)
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
