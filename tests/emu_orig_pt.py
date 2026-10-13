#!/usr/bin/env python3
"""Differential test of orig_pt.c (group PT: the DirectDraw display file and
the DirectInput file, 0x487000-0x489cd0) against the original, on random
state.

Every DirectX object is a fake COM object (per interface kind its own
vtable of logging stubs); the Windows imports (DirectDrawCreate,
DirectDrawEnumerateA, DirectInputCreateA, SetFocus, ShowWindow,
ClientToScreen, SetRect, CreateICA, GetDeviceCaps, DeleteDC,
SendDlgItemMessageA) are stubs through their import slots; the outside cdecl
callees (0x4291f0, 0x428a50, the allocators 0x476ae0 / 0x476b80, 0x489f70)
are stubs at their addresses. Each stub records as many arguments as the
callee reads (structures passed by pointer by content, stack pointers as
"stack") and returns scripted values; object-creating calls hand out new
fake objects in call order. EnumDisplayModes / EnumDevices stand in for
their callbacks (scripted mode table / joystick count); the callbacks
0x487190 / 0x487a80 / 0x488e80 are tested on their own. mem_copy / mem_fill
(0x476f30 / 0x476f50) run for real. The group's own functions run for real
in both (the original's call the original's, ours call ours).

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those two runs (stack leftovers in
the surface descriptions) are masked before the comparison with ours.
Compared: the return value, esp (stdcall pops), ebx / esi / edi / ebp, the
globals and scratch memory, the call log.

    tests/emu_orig_pt.py EXE build/emu_pt.elf [iterations] [names...] [-v] [-c]
"""
import os
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x8000
OBJS = SCR  # 0x200 per kind: 32 objects of 0x10
VTS = SCR + 0x2000  # 0x100 per kind
ARG = SCR + 0x4000  # structures, strings (0x4000)
STUBPAGE = 0x02200000

REGIONS = [(0x4AC000, 0x7000), (0x4BE000, 0x1000), (0x4D5000, 0x5000), (0x6C7000, 0x2000)]

LOST = 0x887601C2
FAIL = 0x80004005

KINDS = ["dd", "dd2", "surf", "clip", "pal", "di", "dev1", "dev", "eff"]
FIRST_NEW = 16  # pre-placed objects use indices below this

# (kind, vtable offset) -> (method, argument count with this)
METHODS = {
    ("dd", 0x0): ("QueryInterface", 3), ("dd", 0x8): ("Release", 1), ("dd", 0x10): ("CreateClipper", 4),
    ("dd", 0x14): ("CreatePalette", 5), ("dd", 0x18): ("CreateSurface", 4), ("dd", 0x20): ("EnumDisplayModes", 5),
    ("dd", 0x2C): ("GetCaps", 3), ("dd", 0x4C): ("RestoreDisplayMode", 1), ("dd", 0x50): ("SetCooperativeLevel", 3),
    ("dd", 0x54): ("SetDisplayMode", 4),
    ("dd2", 0x8): ("Release", 1), ("dd2", 0x5C): ("GetAvailableVidMem", 4),
    ("surf", 0x8): ("Release", 1), ("surf", 0x14): ("Blt", 6), ("surf", 0x1C): ("BltFast", 6),
    ("surf", 0x30): ("GetAttachedSurface", 3), ("surf", 0x58): ("GetSurfaceDesc", 2), ("surf", 0x64): ("Lock", 5),
    ("surf", 0x6C): ("Restore", 1), ("surf", 0x70): ("SetClipper", 2), ("surf", 0x7C): ("SetPalette", 2),
    ("surf", 0x80): ("Unlock", 2),
    ("clip", 0x8): ("Release", 1), ("clip", 0x20): ("SetHWnd", 3),
    ("pal", 0x8): ("Release", 1),
    ("di", 0x8): ("Release", 1), ("di", 0xC): ("CreateDevice", 4), ("di", 0x10): ("EnumDevices", 5),
    ("dev1", 0x0): ("QueryInterface", 3), ("dev1", 0x8): ("Release", 1),
    ("dev", 0x8): ("Release", 1), ("dev", 0xC): ("GetCapabilities", 2), ("dev", 0x10): ("EnumObjects", 4),
    ("dev", 0x14): ("GetProperty", 3), ("dev", 0x18): ("SetProperty", 3), ("dev", 0x1C): ("Acquire", 1),
    ("dev", 0x20): ("Unacquire", 1), ("dev", 0x2C): ("SetDataFormat", 2), ("dev", 0x34): ("SetCooperativeLevel", 3),
    ("dev", 0x48): ("CreateEffect", 5),
    ("eff", 0x8): ("Release", 1), ("eff", 0x18): ("SetParameters", 3), ("eff", 0x1C): ("Start", 3),
    ("eff", 0x20): ("Stop", 1), ("eff", 0x24): ("GetEffectStatus", 2),
}

# stdcall imports: slot -> (name, argument count)
IAT = {
    0x4A3024: ("DirectDrawCreate", 3),
    0x4A3028: ("DirectDrawEnumerateA", 2),
    0x4A3030: ("DirectInputCreateA", 4),
    0x4A327C: ("SetFocus", 1),
    0x4A329C: ("ShowWindow", 2),
    0x4A323C: ("ClientToScreen", 2),
    0x4A3254: ("SetRect", 5),
    0x4A3058: ("CreateICA", 4),
    0x4A3048: ("GetDeviceCaps", 2),
    0x4A304C: ("DeleteDC", 1),
    0x4A3290: ("SendDlgItemMessageA", 5),
}
# cdecl callees at their own addresses: name, argument count
STUBS = {
    0x4291F0: ("dx_error", 1),
    0x428A50: ("d3d_window_prepare", 5),
    0x476AE0: ("block_alloc", 2),
    0x476B80: ("arena_alloc", 1),
    0x489F70: ("joy_effects_release", 1),
}


def OBJ(kind, i):
    return OBJS + KINDS.index(kind) * 0x200 + i * 0x10


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def ptr(a):
    return "stack" if on_stack(a) else a


def c_string(uc, a, limit=0x200):
    out = b""
    while len(out) < limit:
        try:
            chunk = bytes(uc.mem_read(a + len(out), 0x10))
        except UcError:
            break
        out += chunk
        if b"\0" in chunk:
            break
    return out.split(b"\0")[0][:limit]


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(STUBPAGE, 0x2000)
        m.uc.mem_write(STUBPAGE, b"\xf4" * 0x2000)
        self.calls = {}
        for i, (slot, (name, n)) in enumerate(sorted(IAT.items())):
            address = STUBPAGE + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = ("api", name, n, True)
        self.vtables = b""
        for k, kind in enumerate(KINDS):
            for off in range(0, 0x100, 4):
                address = STUBPAGE + 0x1000 + k * 0x100 + off
                name, n = METHODS.get((kind, off), (f"unknown_{off:#x}", 1))
                self.calls[address] = (kind, name, n, True)
        for address, (name, n) in STUBS.items():
            self.calls[address] = ("c", name, n, False)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def vtable_bytes(self, kind):
        k = KINDS.index(kind)
        return b"".join(struct.pack("<I", STUBPAGE + 0x1000 + k * 0x100 + off) for off in range(0, 0x100, 4))

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.next_obj = {k: FIRST_NEW for k in KINDS}

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _new(self, kind):
        i = self.next_obj[kind]
        self.next_obj[kind] += 1
        return OBJ(kind, i)

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _rd(self, uc, a, n):
        return bytes(uc.mem_read(a, n))

    def _w32(self, uc, a, v):
        uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def _dieffect(self, uc, p):
        raw = bytearray(self._rd(uc, p, 0x34))
        d, cb, ts = struct.unpack_from("<I", raw, 0x24)[0], struct.unpack_from("<I", raw, 0x2C)[0], \
            struct.unpack_from("<I", raw, 0x30)[0]
        struct.pack_into("<I", raw, 0x24, 0)
        struct.pack_into("<I", raw, 0x30, 0)
        dir_log = self._rd(uc, d, 8) if on_stack(d) else d
        ts_log = self._rd(uc, ts, min(cb, 0x40)) if ts else 0
        return (ptr(p), bytes(raw), ptr(d), dir_log, ptr(ts), ts_log)

    def _stub(self, uc, address):
        kind, name, n, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<11I", bytes(uc.mem_read(esp, 44)))
        ret, a = words[0], list(words[1:])
        nx = self._next
        key = name if kind in ("api", "c") else f"{kind}.{name}"
        eax = 0
        log = tuple(a[:n])
        rd, w32 = self._rd, self._w32
        if kind == "c":
            if name in ("block_alloc", "arena_alloc"):
                eax = nx(key)
        elif kind == "api":
            if name == "DirectDrawCreate":
                hr = nx(key)
                log = (rd(uc, a[0], 16) if a[0] else 0, ptr(a[1]), a[2])
                if hr == 0:
                    w32(uc, a[1], self._new("dd"))
                eax = hr
            elif name == "DirectInputCreateA":
                hr = nx(key)
                log = (a[0], a[1], ptr(a[2]), a[3])
                if hr == 0:
                    w32(uc, a[2], self._new("di"))
                eax = hr
            elif name == "ClientToScreen":
                dx, dy = nx(key, (0, 0))
                x, y = struct.unpack("<ii", rd(uc, a[1], 8))
                log = (a[0], ptr(a[1]), x, y)
                uc.mem_write(a[1], struct.pack("<II", (x + dx) & 0xFFFFFFFF, (y + dy) & 0xFFFFFFFF))
                eax = 1
            elif name == "SetRect":
                log = (ptr(a[0]), a[1], a[2], a[3], a[4])
                uc.mem_write(a[0], struct.pack("<4I", a[1], a[2], a[3], a[4]))
                eax = 1
            elif name == "CreateICA":
                log = (c_string(uc, a[0]), a[1], a[2], a[3])
                eax = nx(key, 0x1234)
            elif name == "GetDeviceCaps":
                eax = nx(key, 16)
            elif name == "SendDlgItemMessageA":
                log = tuple(a[:4]) + ((a[4], c_string(uc, a[4])) if a[2] == 0x143 else (a[4],))
                eax = nx(key)
            else:
                eax = nx(key)
        else:
            this = a[0]
            if name == "QueryInterface":
                hr = nx(key)
                log = (this, a[1], ptr(a[2]))
                if hr == 0:
                    w32(uc, a[2], self._new("dd2" if kind == "dd" else "dev"))
                eax = hr
            elif name in ("CreateClipper", "CreateDevice"):
                hr = nx(key)
                if name == "CreateClipper":
                    log = (this, a[1], ptr(a[2]), a[3])
                    if hr == 0:
                        w32(uc, a[2], self._new("clip"))
                else:
                    log = (this, a[1], rd(uc, a[1], 16), ptr(a[2]), a[3])
                    if hr == 0:
                        w32(uc, a[2], self._new("dev1"))
                eax = hr
            elif name == "CreatePalette":
                hr = nx(key)
                log = (this, a[1], a[2], ptr(a[3]), a[4])
                if hr == 0:
                    w32(uc, a[3], self._new("pal"))
                eax = hr
            elif name == "CreateSurface":
                hr = nx(key)
                log = (this, ptr(a[1]), rd(uc, a[1], 0x6C), ptr(a[2]), a[3])
                if hr == 0:
                    w32(uc, a[2], self._new("surf"))
                eax = hr
            elif name == "EnumDisplayModes":
                hr, modes = nx(key, (0, None))
                if modes is not None:  # stands in for the callback
                    self._w32(uc, 0x4D5FD4, len(modes))
                    for i, (w, h, b) in enumerate(modes):
                        uc.mem_write(0x4D5FE4 + 12 * i, struct.pack("<3I", w, h, b))
                eax = hr
            elif name == "EnumDevices":
                hr, joys = nx(key, (0, None))
                if joys is not None:
                    self._w32(uc, 0x4D9554, joys)
                    self._w32(uc, 0x4D75B8, OBJ("dev", 1))
                eax = hr
            elif name == "GetCaps":
                hr, hal, hel = nx(key, (0, b"", b""))
                log = (this, ptr(a[1]), ptr(a[2]), rd(uc, a[1], 0x17C), rd(uc, a[2], 0x17C))
                uc.mem_write(a[1] + 4, hal)
                uc.mem_write(a[2] + 4, hel)
                eax = hr
            elif name == "GetAvailableVidMem":
                hr, total, free = nx(key, (0, 0, 0))
                log = (this, ptr(a[1]), rd(uc, a[1], 4), ptr(a[2]), ptr(a[3]))
                w32(uc, a[2], total)
                w32(uc, a[3], free)
                eax = hr
            elif name == "Blt":
                log = (this, ptr(a[1]), rd(uc, a[1], 16) if a[1] else 0, a[2], ptr(a[3]),
                       rd(uc, a[3], 16) if a[3] else 0, a[4], ptr(a[5]), rd(uc, a[5], 100) if a[5] else 0)
                eax = nx(key)
            elif name == "BltFast":
                log = (this, a[1], a[2], a[3], ptr(a[4]), rd(uc, a[4], 16) if a[4] else 0, a[5])
                eax = nx(key)
            elif name == "GetAttachedSurface":
                hr = nx(key)
                log = (this, ptr(a[1]), rd(uc, a[1], 4), ptr(a[2]))
                if hr == 0:
                    w32(uc, a[2], self._new("surf"))
                eax = hr
            elif name == "GetSurfaceDesc":
                hr, content = nx(key, (0, b""))
                log = (this, ptr(a[1]), rd(uc, a[1], 0x6C))
                uc.mem_write(a[1] + 4, content)
                eax = hr
            elif name == "Lock":
                hr, content = nx(key, (0, b""))
                log = (this, ptr(a[1]), ptr(a[2]), rd(uc, a[2], 4), a[3], a[4])
                uc.mem_write(a[2] + 4, content)
                eax = hr
            elif name == "GetCapabilities":
                hr, content = nx(key, (0, b""))
                log = (this, ptr(a[1]), rd(uc, a[1], 0x2C))
                uc.mem_write(a[1] + 4, content)
                eax = hr
            elif name == "GetProperty":
                hr, content = nx(key, (0, b""))
                log = (this, a[1], ptr(a[2]), rd(uc, a[2], 0x18))
                uc.mem_write(a[2] + 0x10, content)
                eax = hr
            elif name == "SetProperty":
                size = struct.unpack("<I", rd(uc, a[2], 4))[0]
                log = (this, a[1], ptr(a[2]), rd(uc, a[2], min(size, 0x20)))
                eax = nx(key)
            elif name == "CreateEffect":
                hr = nx(key)
                log = (this, a[1], self._dieffect(uc, a[2]), ptr(a[3]), a[4])
                if hr == 0:
                    w32(uc, a[3], self._new("eff"))
                eax = hr
            elif name == "SetParameters":
                log = (this, self._dieffect(uc, a[1]), a[2])
                eax = nx(key)
            elif name == "GetEffectStatus":
                hr, status = nx(key, (0, 0))
                log = (this, ptr(a[1]))
                w32(uc, a[1], status)
                eax = hr
            elif name.startswith("unknown"):
                self.log.append(("unknown", kind, name))
                uc.emu_stop()
                return
            else:
                eax = nx(key)
        self.log.append((key, log))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * n if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---------------------------------------------------------------- state

class State:
    def __init__(self, rng, base, stubs):
        self.rng = rng
        self.mem = {a: bytearray(b) for a, b in base.items()}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        for kind in KINDS:
            vt = stubs.vtable_bytes(kind)
            k = KINDS.index(kind)
            self.scr[0x2000 + k * 0x100:0x2000 + k * 0x100 + 0x100] = vt
            for i in range(32):
                struct.pack_into("<I", self.scr, k * 0x200 + i * 0x10, VTS + k * 0x100)
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

    def get(self, address, fmt):
        buf, off = self._where(address)
        return struct.unpack_from(fmt, buf, off)[0]


def JOY(j):
    return 0x4D7374 + j * 0x43C


def DRIVER(n):
    return 0x4D6464 + n * 0x224


def hr_pick(rng, fail=FAIL):
    return pick(rng, 0, 0, 0, 0, fail)


def name_bytes(rng, n=None):
    n = rng.randrange(0, 0x60) if n is None else n
    return bytes(rng.randrange(0x20, 0x7F) for _ in range(n))


def masks(rng):
    r, g, b = pick(rng, (0xF800, 0x7E0, 0x1F), (0x7C00, 0x3E0, 0x1F), (0xFF0000, 0xFF00, 0xFF), (0xE0, 0x1C, 3),
                   (r32(rng) | 1 << rng.randrange(32), r32(rng) | 1 << rng.randrange(32),
                    r32(rng) | 1 << rng.randrange(32)))
    a = pick(rng, 0, 0, 0x8000, 0xFF000000, r32(rng))
    return r, g, b, a


def surface_desc(rng):
    """GetSurfaceDesc / Lock output: the description after its size."""
    d = bytearray(rng.randbytes(0x68))
    r, g, b, a = masks(rng)
    struct.pack_into("<4I", d, 0x58 - 4, r, g, b, a)
    struct.pack_into("<I", d, 0x68 - 4, pick(rng, 0x4000, 0, r32(rng)) | pick(rng, 0x200, 0x40, 0))
    return bytes(d)


def caps_bytes(rng):
    d = bytearray(rng.randbytes(0x178))
    struct.pack_into("<I", d, 0, pick(rng, r32(rng), 1, 0, 0x2000041))
    return bytes(d)


def world(st, rng):
    """DirectDraw / DirectInput objects and flags."""
    st.put(0x6C8000, "<I", r32(rng))  # hwnd
    st.put(0x6C7FFC, "<I", r32(rng))  # hinstance
    st.put(0x4D5F40, "<I", pick(rng, OBJ("dd", 0), OBJ("dd", 0), OBJ("dd", 0), 0))
    st.put(0x4D5F48, "<I", pick(rng, OBJ("surf", 0), OBJ("surf", 0), 0))
    st.put(0x4D5F4C, "<I", pick(rng, OBJ("surf", 1), OBJ("surf", 1), 0))
    st.put(0x4D5F44, "<I", pick(rng, 0, OBJ("clip", 0)))
    st.put(0x4D5F78, "<I", pick(rng, 0, OBJ("pal", 0)))
    for k in range(5):
        st.put(0x4D5F28 + 4 * k, "<I", pick(rng, 0, OBJ("surf", 2 + k)))
    for a in (0x4D5F80, 0x4D5F84, 0x4BE3E4, 0x4D6458, 0x4BE44C, 0x4D5FA4):
        st.put(a, "<I", pick(rng, 0, 1, 1, r32(rng)))
    st.put(0x4ACC38, "<I", pick(rng, 640, 800, 320, rng.randrange(1, 2000)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600, 240, rng.randrange(1, 2000)))
    st.put(0x4D5F70, "<I", pick(rng, 8, 16, 32, r32(rng)))
    st.put(0x4D6F30, "<I", pick(rng, OBJ("di", 0), OBJ("di", 0), 0))
    st.put(0x4D6F34, "<I", pick(rng, 0, OBJ("dev", 0)))
    st.put(0x4D9554, "<I", pick(rng, 0, 1, 2, 3))
    for j in range(4):
        st.put(JOY(j) + 0x244, "<I", pick(rng, OBJ("dev", 1 + j), OBJ("dev", 1 + j), 0))
        for k in range(6):
            st.put(JOY(j) + 0x314 + 4 * k, "<I", pick(rng, OBJ("eff", 6 * j + k % 6 if j < 2 else k),
                                                     OBJ("eff", k), 0))
    st.put(0x4B2198, "<I", ARG + 0x3000)


# ---------------------------------------------------------------- generators: (args, returns value?)

def g_none(st, rng):
    return [], False


def g_none_ret(st, rng):
    return [], True


def drivers(st, rng, n):
    st.put(0x4D623C, "<i", n)
    for i in range(max(n, 0) + 1):
        st.putb(DRIVER(i), rng.randbytes(0x224))
        st.putb(DRIVER(i) + 0x104, name_bytes(rng) + b"\0")
        st.put(DRIVER(i) + 0x218, "<I", pick(rng, 0, 1, r32(rng)))


def g_dd_create(st, rng):
    drivers(st, rng, 4)
    st.script["DirectDrawCreate"] = [hr_pick(rng)]
    st.script["dd.Release"] = [r32(rng)]
    return [pick(rng, -1, 0, 0, 1, 2, 3, -0x10)], True


def modes(st, rng):
    n = pick(rng, 0, 1, 5, rng.randrange(0, 51), -3)
    st.put(0x4D5FD4, "<i", n)
    table = []
    for i in range(max(n, 0)):
        m = (pick(rng, 640, 800, 320, 1024), pick(rng, 480, 600, 240, 768), pick(rng, 16, 16, 32, 8, 24))
        st.putb(0x4D5FE4 + 12 * i, struct.pack("<3I", *m))
        table.append(m)
    return table


def g_mode_find(st, rng):
    table = modes(st, rng)
    if table and rng.randrange(3):
        w, h, b = rng.choice(table)
        b = 15 if b == 16 and rng.randrange(2) else b
    else:
        w, h, b = pick(rng, 640, 800, 320), pick(rng, 480, 600), pick(rng, 8, 15, 16, 32)
    return [w, h, b], True


def mode_script(rng):
    return (hr_pick(rng), pick(rng, None, [], [(640, 480, 16), (800, 600, 16), (640, 480, 8), (320, 240, 16),
                                               (1024, 768, 32)][:rng.randrange(0, 6)]))


def g_modes_enum(st, rng):
    st.script["dd.EnumDisplayModes"] = [mode_script(rng)]
    return [], True


def g_mode_cb(st, rng):
    st.put(0x4D5FD4, "<i", pick(rng, 0, 1, 10, 49, 50, 51, rng.randrange(0, 50), -2))
    d = ARG
    st.putb(d, rng.randbytes(0x6C))
    st.put(d + 0x54, "<I", pick(rng, 8, 14, 15, 16, 24, 32, r32(rng)))
    return [d, r32(rng)], True


def g_shutdown(st, rng):
    st.script["dd.Release"] = [r32(rng) for _ in range(4)]
    return [], False


def g_mask_loss(st, rng):
    return [pick(rng, 0, 1, 3, 7, 0xF, 0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 2, rng.randrange(0, 0x200), r32(rng))], True


def g_pixel_format(st, rng):
    d = ARG
    st.putb(d, rng.randbytes(0x6C))
    st.putb(d + 0x58, struct.pack("<4I", *masks(rng)))
    return [d, ARG + 0x100], False


def g_surfaces_release(st, rng):
    return [], True


def g_lock(st, rng):
    st.script["surf.Lock"] = [(pick(rng, 0, LOST, FAIL), surface_desc(rng)), (pick(rng, 0, 0, LOST, FAIL), b"")]
    return [pick(rng, 0, ARG + 0x200), pick(rng, OBJ("surf", 0), OBJ("surf", 3), 0), ARG], True


def g_unlock(st, rng):
    st.script["surf.Unlock"] = [pick(rng, 0, LOST, FAIL), pick(rng, 0, LOST, FAIL)]
    return [pick(rng, OBJ("surf", 0), OBJ("surf", 3))], True


def g_desc_get(st, rng):
    st.script["surf.GetSurfaceDesc"] = [(hr_pick(rng), surface_desc(rng))]
    return [ARG, pick(rng, OBJ("surf", 0), OBJ("surf", 3))], True


def g_surface_create(st, rng):
    st.putb(ARG, rng.randbytes(0x6C))
    st.script["dd.CreateSurface"] = [hr_pick(rng)]
    return [ARG, pick(rng, ARG + 0x300, 0x4D5F48), pick(rng, 0, 1, r32(rng))], True


def g_window_origin(st, rng):
    st.script["ClientToScreen"] = [(rng.randrange(-100, 2000), rng.randrange(-100, 2000))]
    return [r32(rng), rng.randrange(-10, 2000), rng.randrange(-10, 2000)], True


def g_primary_clear(st, rng):
    st.script["surf.GetSurfaceDesc"] = [(hr_pick(rng), surface_desc(rng))]
    st.script["surf.Blt"] = [hr_pick(rng)]
    return [], True


def g_blit(st, rng):
    st.script["surf.Blt"] = [pick(rng, 0, LOST, FAIL), pick(rng, 0, LOST, FAIL)]
    st.script["surf.BltFast"] = list(st.script["surf.Blt"])
    st.script["ClientToScreen"] = [(rng.randrange(-100, 500), rng.randrange(-100, 500))]
    w, h = st.get(0x4ACC38, "<i"), st.get(0x4ACC3C, "<i")

    def c(lim):
        return pick(rng, rng.randrange(-50, lim + 50), rng.randrange(0, max(lim, 1)), 0, lim, lim - 1, r32(rng))
    l, t = c(w), c(h)
    r, b = l + pick(rng, rng.randrange(-10, w + 10), 64, 0), t + pick(rng, rng.randrange(-10, h + 10), 64, 0)
    dst = pick(rng, st.get(0x4D5F48, "<I"), OBJ("surf", 1), OBJ("surf", 4))
    return [pick(rng, OBJ("surf", 1), OBJ("surf", 5)), dst, l, t, r, b, c(w), c(h)], True


def g_driver_cb(st, rng):
    n = pick(rng, 0, 1, 3, 5)
    drivers(st, rng, n)
    guid = pick(rng, 0, ARG + 0x400, ARG + 0x400)
    st.putb(ARG + 0x400, rng.randbytes(16))
    st.putb(ARG + 0x500, name_bytes(rng) + b"\0")
    st.putb(ARG + 0x600, name_bytes(rng) + b"\0")
    st.script["DirectDrawCreate"] = [hr_pick(rng)]
    st.script["dd.GetCaps"] = [(hr_pick(rng), caps_bytes(rng), caps_bytes(rng))]
    st.script["dd.QueryInterface"] = [hr_pick(rng)]
    st.script["dd2.GetAvailableVidMem"] = [(hr_pick(rng), r32(rng), r32(rng))]
    return [guid, ARG + 0x500, ARG + 0x600, r32(rng)], True


def g_drivers_enum(st, rng):
    st.script["DirectDrawEnumerateA"] = [hr_pick(rng)]
    return [], True


def g_probe(st, rng):
    drivers(st, rng, 2)
    st.script["DirectDrawCreate"] = [pick(rng, 0, 0, 0, 0, 0, FAIL)]
    st.script["dd.GetCaps"] = [(hr_pick(rng), caps_bytes(rng), caps_bytes(rng))]
    st.script["dd.EnumDisplayModes"] = [mode_script(rng)]
    return [], True


def start_scripts(st, rng):
    drivers(st, rng, 3)
    modes(st, rng)
    st.put(0x4BE448, "<I", pick(rng, 0, 1, 2, 0x10001))
    for a in (0x4ACC44, 0x4BE444, 0x4ACC48, 0x4ACC4C, 0x4ACC58):
        st.put(a, "<I", r32(rng))
    st.script["DirectDrawCreate"] = [pick(rng, 0, 0, 0, 0, 0, 0, 0, FAIL)]
    st.script["dd.GetCaps"] = [(pick(rng, 0, 0, 0, 0, FAIL), caps_bytes(rng), caps_bytes(rng))]
    st.script["dd.SetCooperativeLevel"] = [pick(rng, 0, 0, 0, 0, FAIL)]
    st.script["dd.EnumDisplayModes"] = [mode_script(rng)]
    st.script["GetDeviceCaps"] = [pick(rng, 8, 16, 32, 8)]
    st.script["dd.SetDisplayMode"] = [pick(rng, 0, 0, 0, 0, FAIL)]
    st.script["dd.CreateSurface"] = [pick(rng, 0, 0, 0, FAIL) for _ in range(6)]
    st.script["surf.GetAttachedSurface"] = [pick(rng, 0, 0, 0, FAIL) for _ in range(3)]
    st.script["surf.GetSurfaceDesc"] = [(pick(rng, 0, 0, 0, FAIL), surface_desc(rng)) for _ in range(6)]
    st.script["surf.Blt"] = [hr_pick(rng) for _ in range(3)]
    st.script["block_alloc"] = [pick(rng, 0, ARG + 0x800)]
    st.script["arena_alloc"] = [pick(rng, 0, ARG + 0x800)]
    st.script["dd.CreatePalette"] = [hr_pick(rng)]
    st.script["dd.CreateClipper"] = [hr_pick(rng)]
    st.script["dd.Release"] = [r32(rng)]


def start_args(st, rng):
    return [pick(rng, 0, 1, 1, r32(rng)), pick(rng, 0, 1, 1), pick(rng, 640, 800, 320, 1280),
            pick(rng, 480, 600, 240, 960), pick(rng, 0, 1, 1), pick(rng, 0, 1, r32(rng)), pick(rng, 8, 15, 16, 32)]


def g_start_3d(st, rng):
    start_scripts(st, rng)
    return start_args(st, rng), True


def g_list_fill(st, rng):
    n = pick(rng, 0, 1, 3, 6, -1)
    drivers(st, rng, n)
    st.put(ARG + 0x3000 + 0x432, "<H", pick(rng, 0, 1, 2, 5, 7, rng.randrange(0, 0x10000)))
    st.script["SendDlgItemMessageA"] = [r32(rng) for _ in range(10)]
    return [r32(rng), pick(rng, 0x3E8, r32(rng))], False


def g_start_2d(st, rng):
    start_scripts(st, rng)
    return start_args(st, rng), True


def g_input_init(st, rng):
    st.script["DirectInputCreateA"] = [pick(rng, 0, 0, 0, FAIL)]
    st.script["di.EnumDevices"] = [(pick(rng, 0, 0, 0, FAIL), pick(rng, None, 0, 1, 2))]
    st.script["dev.Acquire"] = [pick(rng, 0, 1, FAIL, 0), r32(rng)]
    di_scripts(st, rng)
    return [], True


def di_scripts(st, rng):
    st.script["di.CreateDevice"] = [pick(rng, 0, 0, 0, 0, FAIL)]
    st.script["dev1.QueryInterface"] = [pick(rng, 0, 0, 0, 0, FAIL)]
    for k in ("dev.SetCooperativeLevel", "dev.SetDataFormat", "dev.Acquire", "dev1.Release"):
        st.script.setdefault(k, []).extend(r32(rng) for _ in range(2))


def g_device_create(st, rng):
    st.putb(ARG + 0x400, rng.randbytes(16))
    di_scripts(st, rng)
    return [OBJ("di", 0), ARG + 0x400, pick(rng, ARG + 0x300, 0x4D6F34), pick(rng, 0, r32(rng))], True


def ff_scripts(st, rng):
    st.script["dev.CreateEffect"] = [pick(rng, 0, 0, 0, FAIL) for _ in range(6)]
    for k in ("dev.Acquire", "dev.Unacquire"):
        st.script[k] = [r32(rng)]


def g_enum_joystick(st, rng):
    j = pick(rng, 0, 1, 2, 3)
    st.put(0x4D9554, "<I", j)
    st.put(0x4D6F30, "<I", OBJ("di", 0))
    inst = ARG + 0xC00
    st.putb(inst, rng.randbytes(0x244))
    di_scripts(st, rng)
    st.script["di.CreateDevice"] = [pick(rng, 0, 0, 0, 0, 0, FAIL)]
    caps = bytearray(rng.randbytes(0x28))
    struct.pack_into("<I", caps, 0, pick(rng, 0x100, 0, r32(rng)))
    st.script["dev.GetCapabilities"] = [(hr_pick(rng), bytes(caps))]
    st.script["dev.EnumObjects"] = [hr_pick(rng)]
    ff_scripts(st, rng)
    return [inst, r32(rng)], True


def joy_arg(st, rng):
    return pick(rng, 0, 0, 1, 2, 3)


def g_deadzone(st, rng):
    return [joy_arg(st, rng), pick(rng, 0, 99, 100, 101, rng.randrange(-1000, 10000), r32(rng)), r32(rng)], False


def g_range(st, rng):
    return [joy_arg(st, rng), r32(rng)], False


def g_effect_defaults(st, rng):
    return [ARG], False


def g_create_ff(st, rng):
    ff_scripts(st, rng)
    return [joy_arg(st, rng)], False


def g_property_get(st, rng):
    st.script["dev.GetProperty"] = [(hr_pick(rng), rng.randbytes(8))]
    return [joy_arg(st, rng), ARG, pick(rng, 4, 5, 6, r32(rng)), r32(rng)], True


def g_input_shutdown(st, rng):
    return [], False


def g_init_keyboard(st, rng):
    di_scripts(st, rng)
    return [], True


def near(rng, v, d):
    return pick(rng, -1, v + rng.randrange(-d - 10, d + 10), v + rng.randrange(-2 * d, 2 * d), r32(rng),
                v + pick(rng, d, -d, d - 1, 1 - d))


def status_script(st, rng):
    st.script["eff.GetEffectStatus"] = [(hr_pick(rng), pick(rng, 0, 1, 2, 3, r32(rng))) for _ in range(2)]
    st.script["eff.SetParameters"] = [hr_pick(rng)]
    st.script["eff.Start"] = [hr_pick(rng)]


def g_road(st, rng):
    status_script(st, rng)
    angle, mag = pick(rng, 0, 9000, rng.randrange(0, 36000), r32(rng)), pick(rng, 0, rng.randrange(-10000, 10001))
    st.put(0x4B2CA8, "<i", near(rng, mag, 1000))
    st.put(0x4D6F28, "<i", near(rng, angle, 0xE10))
    return [joy_arg(st, rng), angle, mag], False


def g_stop(st, rng):
    st.script["eff.Stop"] = [r32(rng)]
    return [joy_arg(st, rng)], False


def g_spring(last):
    def gen(st, rng):
        status_script(st, rng)
        v = pick(rng, 0, rng.randrange(-10000, 10001), r32(rng))
        st.put(last, "<i", near(rng, v, 1000))
        return [joy_arg(st, rng), v], False
    return gen


def g_engine(st, rng):
    status_script(st, rng)
    period = pick(rng, -1, -1, rng.randrange(0, 200000), r32(rng))
    mag = pick(rng, 0, rng.randrange(-10000, 10001))
    st.put(0x4B2CB4, "<i", near(rng, period, 12000))
    st.put(0x4D6F20, "<i", near(rng, mag, 1000))
    return [joy_arg(st, rng), period, mag], False


TESTS = {
    "dd_surfaces_clear": (0x487000, g_none),
    "dd_create": (0x487020, g_dd_create),
    "dd_mode_find": (0x4870D0, g_mode_find),
    "dd_modes_enum": (0x487160, g_modes_enum),
    "dd_mode_enum_cb": (0x487190, g_mode_cb),
    "dd_shutdown": (0x487210, g_shutdown),
    "dd_mask_loss": (0x487300, g_mask_loss),
    "dd_pixel_format_get": (0x487480, g_pixel_format),
    "dd_surfaces_release": (0x487570, g_surfaces_release),
    "dd_surface_lock": (0x4875A0, g_lock),
    "dd_surface_unlock": (0x487610, g_unlock),
    "dd_surface_desc_get": (0x487660, g_desc_get),
    "dd_surface_create": (0x4876A0, g_surface_create),
    "dd_window_origin": (0x4877D0, g_window_origin),
    "dd_primary_clear": (0x487830, g_primary_clear),
    "dd_blit": (0x4878D0, g_blit),
    "dd_driver_enum_cb": (0x487A80, g_driver_cb),
    "dd_drivers_enum": (0x487D50, g_drivers_enum),
    "dd_probe": (0x487D70, g_probe),
    "dd_start_3d": (0x487E10, g_start_3d),
    "dd_driver_list_fill": (0x4883C0, g_list_fill),
    "dd_start_2d": (0x4885D0, g_start_2d),
    "input_init": (0x488D90, g_input_init),
    "di_device_create": (0x488E20, g_device_create),
    "input_enum_joystick": (0x488E80, g_enum_joystick),
    "joy_deadzone_set": (0x489180, g_deadzone),
    "joy_range_set": (0x4891F0, g_range),
    "ff_effect_defaults": (0x489250, g_effect_defaults),
    "input_create_ff_effects": (0x4892B0, g_create_ff),
    "joy_property_get": (0x489500, g_property_get),
    "input_shutdown": (0x489560, g_input_shutdown),
    "input_init_keyboard": (0x489910, g_init_keyboard),
    "ff_road_set": (0x489980, g_road),
    "ff_road_stop": (0x489A80, g_stop),
    "ff_spring_a_set": (0x489AB0, g_spring(0x4B2CAC)),
    "ff_spring_a_stop": (0x489B90, g_stop),
    "ff_spring_b_set": (0x489BC0, g_spring(0x4B2CB0)),
    "ff_spring_b_stop": (0x489CA0, g_stop),
    "ff_engine_set": (0x489CD0, g_engine),
}

SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP)


def run_once(m, stubs, st, target, args, fill, returns):
    m.write(E.STACK, bytes([fill]) * E.STACK_SIZE)
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    for i, reg in enumerate(SAVED[:4]):
        m.uc.reg_write(reg, 0x11111111 * (i + 1))
    stubs.reset(st.script)
    r = m.call(target, args)
    if r[0] == "ok":
        r = ("ok", r[1]) if returns else ("ok",)
        r = r + tuple(m.uc.reg_read(x) for x in SAVED)
    mem = bytearray(m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS))
    return r, mem, stubs.log


def mask_logs(la, lb, ours):
    """Zero the bytes of logged buffers that differ between the original's two runs (stack leftovers)."""
    def walk(xa, xb, xo):
        if isinstance(xa, bytes) and isinstance(xb, bytes) and len(xa) == len(xb) and xa != xb:
            ma = bytearray(xa)
            mo = bytearray(xo) if isinstance(xo, bytes) and len(xo) == len(xa) else None
            for k in range(len(xa)):
                if xa[k] != xb[k]:
                    ma[k] = 0
                    if mo is not None:
                        mo[k] = 0
            return bytes(ma), (bytes(mo) if mo is not None else xo)
        if isinstance(xa, tuple) and isinstance(xb, tuple) and len(xa) == len(xb):
            ra, ro = [], []
            xo_t = xo if isinstance(xo, tuple) and len(xo) == len(xa) else None
            for j in range(len(xa)):
                a2, o2 = walk(xa[j], xb[j], xo_t[j] if xo_t else None)
                ra.append(a2)
                ro.append(o2)
            return tuple(ra), (tuple(ro) if xo_t is not None else xo)
        return xa, xo
    for i, (ea, eb) in enumerate(zip(la, lb)):
        if ea[0] != eb[0]:
            break
        xo = ours[i] if i < len(ours) else None
        na, no = walk(ea, eb, xo)
        la[i] = na
        if xo is not None:
            ours[i] = no


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
    st = State(rng, base, stubs)
    world(st, rng)
    args, returns = gen(st, rng)
    args = [a & 0xFFFFFFFF for a in args]
    a = run_once(m, stubs, st, address, args, 0xCC, returns)
    b = run_once(m, stubs, st, address, args, 0x5A, returns)
    o = run_once(m, stubs, st, m.symbols[name], args, 0xCC, returns)
    for k in range(len(a[1])):
        if a[1][k] != b[1][k]:
            a[1][k] = 0
            o[1][k] = 0
    la, lo = list(a[2]), list(o[2])
    mask_logs(la, list(b[2]), lo)
    if a[0] == o[0] and a[1] == o[1] and la == lo:
        return None, a[0][0] != "ok"
    msg = f"{name}{tuple(hex(x) for x in args)}: original {a[0]} ours {o[0]}"
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
    sizes = {0x487190: 0x7D, 0x487A80: 0x2C4}
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "groups", "pt_owned.txt")
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
                if address <= a < address + size and "nop" not in parts[1] and "int3" not in parts[1]:
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
    rng = random.Random(0x97)
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
