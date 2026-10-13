#!/usr/bin/env python3
"""Differential test of orig_wc.c (group WC: the bodies behind the
trampoline wrappers: the texture upload 0x41e710, the capability probe
0x41f720, the window 0x4771c0, the flip 0x4876f0, race_draw 0x443b50 and
the sound starts 0x471c90 / 0x471da0) against the original, on random state.

Windows imports (stdcall, through their import slots), COM methods (fake
DirectDraw / surface / texture / palette / device objects whose vtables
point at stubs) and the callees outside the group are logging stubs in both
runs, with scripted results; the log records as many arguments as each
callee reads (build/params.txt), with buffers and structures by content and
stack pointers as "stack". The drawing callees of race_draw scribble on the
car records, positions and camera blocks (scripted bytes) so that what
race_draw keeps and puts back is checked. 0x420a30's register argument
(ebx) is logged. Pure helpers run for real in both: the slot search
0x41ec20, the power of two 0x41e1a0, mem_copy 0x476f30, the checksum
0x41f700.

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those runs (stack leftovers) are
masked before the comparison with ours. Compared: the return value, ebx /
esi / edi / ebp / esp after the return, the globals and scratch memory,
the call log.

    tests/emu_orig_wc.py EXE build/emu_wc.elf [iterations] [names...] [-v] [-c]
"""
import os
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_ESI, UC_X86_REG_ESP)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x80000
CFG = SCR + 0x0000  # the config table (0x200)
COMO = SCR + 0x0400  # fake COM objects (0x10 each)
RACE = SCR + 0x0800  # the race record (0x600)
PALETTE = SCR + 0x1000  # caller's palette (0x400)
CTABLE = SCR + 0x1400  # the 16-bit colour table (0x200)
OBJS = SCR + 0x2000  # 8 car bodies (0x100) and 8 camera objects (0x200), 8 holders
PIXELS = SCR + 0x8000  # source pixels (0x18000)
SURFMEM = SCR + 0x20000  # locked surface memory (0x60000)
STUBPAGE = 0x02200000

REGIONS = [(0x4AC000, 0x1000), (0x4AF000, 0x1000), (0x4B1000, 0x2000), (0x4B4000, 0x1000), (0x4B8000, 0x6000),
           (0x4D4000, 0x1000), (0x4D5000, 0x2000), (0x5FF000, 0x4000), (0x6BF000, 0x3000), (0x6C7000, 0x2000),
           (0x6CC000, 0x2000)]

DD, SURF1, SURF2, TEX1, TEX2, PAL1, PAL2, DEV, FRONT, BACK, EXTRA = (COMO + 0x10 * i for i in range(11))
CAR = [OBJS + 0x100 * i for i in range(8)]
CAM = [OBJS + 0x800 + 0x200 * i for i in range(8)]
HOLD = [OBJS + 0x1800 + 4 * i for i in range(8)]

IAT = {
    0x4A3254: ("SetRect", 5),
    0x4A3264: ("GetSystemMetrics", 1),
    0x4A3268: ("AdjustWindowRectEx", 4),
    0x4A326C: ("CreateWindowExA", 12),
    0x4A329C: ("ShowWindow", 2),
}
VTABLES = {
    "dd": {0x14: ("CreatePalette", 5), 0x18: ("CreateSurface", 4)},
    "surf": {0x00: ("QueryInterface", 3), 0x08: ("SurfRelease", 1), 0x20: ("DeleteAttachedSurface", 3),
             0x2C: ("Flip", 3), 0x6C: ("Restore", 1), 0x74: ("SetColorKey", 3), 0x7C: ("SetPalette", 2)},
    "tex": {0x08: ("TexRelease", 1), 0x14: ("Load", 2)},
    "pal": {0x08: ("PalRelease", 1)},
    "dev": {0x5C: ("SetRenderState", 3)},
}
VT_BASE = {"dd": STUBPAGE + 0x400, "surf": STUBPAGE + 0x600, "tex": STUBPAGE + 0x800, "pal": STUBPAGE + 0xA00,
           "dev": STUBPAGE + 0xC00}
OBJ_IFACE = [(DD, "dd"), (SURF1, "surf"), (SURF2, "surf"), (TEX1, "tex"), (TEX2, "tex"), (PAL1, "pal"),
             (PAL2, "pal"), (DEV, "dev"), (FRONT, "surf"), (BACK, "surf"), (EXTRA, "surf")]
# cdecl callees: address -> (name, argument dwords logged)
STUBS = {
    0x41E1D0: ("colour_table", 3),
    0x4875A0: ("surface_lock", 3),
    0x487610: ("surface_unlock", 1),
    0x487660: ("surface_desc", 2),
    0x4876A0: ("surface_create", 3),
    0x41E440: ("convert", 6),
    0x41D890: ("slot_release", 1),
    0x41D940: ("slot_keep", 1),
    0x41F3F0: ("state_mode", 1),
    0x41EC40: ("clear", 3),
    0x41F2D0: ("viewport", 4),
    0x41DDD0: ("probe_a", 0),
    0x41DE60: ("probe_b", 0),
    0x41DEC0: ("probe_c", 0),
    0x41E040: ("begin", 1),
    0x4876F0: ("flip", 0),
    0x41F400: ("pixel", 4),
    0x41F550: ("quad", 13),
    0x41DE80: ("extra_attach", 0),
    0x41FB60: ("pvrc", 0),
    0x420A30: ("state_select", 0),
    0x4878D0: ("blit", 8),
    0x4291F0: ("dx_error", 1),
    0x40D040: ("view_count", 0),
    0x4386F0: ("hud_update", 1),
    0x40B000: ("camera_zoom", 2),
    0x408270: ("tv_begin", 1),
    0x479BE0: ("draw_view", 1),
    0x423C40: ("draw_flush", 0),
    0x40AD60: ("tv_end", 0),
    0x45EF80: ("net_on", 0),
    0x48CEF0: ("net_me", 0),
    0x472050: ("listener", 4),
    0x428A80: ("mirror_begin", 0),
    0x423F00: ("render_mode", 1),
    0x40D090: ("mirror_camera", 2),
    0x428B00: ("mirror_end", 0),
    0x4611D0: ("grid_unlink", 1),
    0x460FB0: ("grid_link", 1),
    0x472920: ("voice_alloc", 5),
    0x471EB0: ("voice_start", 6),
}
SCRIBBLERS = ("hud_update", "camera_zoom", "tv_begin", "draw_view", "mirror_camera")

LOST, STILL = 0x887601C2, 0x887600E1


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def ptr(a):
    return "stack" if on_stack(a) else a


def rd(uc, a, n):
    try:
        return bytes(uc.mem_read(a, n))
    except UcError:
        return ("unmapped", a)


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(STUBPAGE, 0x1000)
        m.uc.mem_write(STUBPAGE, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, n)) in enumerate(sorted(IAT.items())):
            address = STUBPAGE + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, n, True)
        for iface, methods in VTABLES.items():
            table = VT_BASE[iface]
            for off, (name, n) in methods.items():
                address = table + 0x100 + off
                m.uc.mem_write(table + off, struct.pack("<I", address))
                self.calls[address] = (name, n, True)
        for address, (name, n) in STUBS.items():
            self.calls[address] = (name, n, False)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.target = None
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address == self.target:
            return  # the function under test itself (the flip is also a callee of the probe)
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _stub(self, uc, address):
        name, n, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<15I", bytes(uc.mem_read(esp, 60)))
        ret, a = words[0], list(words[1:])
        nx = self._next
        w = uc.mem_write
        eax = nx(name)
        log = tuple(ptr(x) for x in a[:n])
        if name == "state_select":
            log = (uc.reg_read(UC_X86_REG_EBX),)
        elif name == "colour_table":
            log = (ptr(a[0]), rd(uc, a[0], 0x400), a[1], a[2])
        elif name == "surface_lock":
            log = (a[0], a[1], ptr(a[2]), rd(uc, a[2], 0x6C))
            pitch, ok = nx("lock_info", (0x400, 1))
            w(a[2] + 0x10, struct.pack("<I", pitch))
            w(a[2] + 0x24, struct.pack("<I", SURFMEM))
            eax = ok
        elif name == "surface_desc":
            log = (ptr(a[0]), rd(uc, a[0], 0x6C), a[1])
            w(a[0] + 0x68, struct.pack("<I", nx("caps", 0x4000)))
            eax = nx("desc_ok", 1)
        elif name == "surface_create":
            log = (ptr(a[0]), rd(uc, a[0], 0x6C), a[1], a[2])
            w(a[1], struct.pack("<I", SURF2))
            eax = nx("create_ok", 1)
        elif name in SCRIBBLERS:
            data = nx("scribble", None)
            if data:
                for addr, blob in data:
                    w(addr, blob)
            if name == "draw_view":
                log = (a[0], rd(uc, a[0] + 8, 0x20))
        elif name == "listener":
            log = (a[0], a[1], a[2], a[3])
        # stdcall imports
        elif name == "SetRect":
            log = (ptr(a[0]), a[1], a[2], a[3], a[4])
            w(a[0], struct.pack("<4I", *a[1:5]))
            eax = 1
        elif name == "AdjustWindowRectEx":
            log = (ptr(a[0]), rd(uc, a[0], 16), a[1], a[2], a[3])
            r = struct.unpack("<4i", rd(uc, a[0], 16))
            dl, dt, dr, db = nx("adjust", (-4, -23, 4, 4))
            w(a[0], struct.pack("<4i", r[0] + dl, r[1] + dt, r[2] + dr, r[3] + db))
        # COM
        elif name == "CreateSurface":
            log = (a[0], rd(uc, a[1], 0x6C), a[2], a[3])
            w(a[2], struct.pack("<I", SURF1))
        elif name == "CreatePalette":
            log = (a[0], a[1], ptr(a[2]), rd(uc, a[2], 0x400), ptr(a[3]), a[4])
            obj = nx("pal_obj", PAL1)
            if obj:
                w(a[3], struct.pack("<I", obj))
        elif name == "QueryInterface":
            log = (a[0], a[1], ptr(a[2]))
            w(a[2], struct.pack("<I", TEX1 if a[0] == SURF1 else TEX2))
        elif name == "SetColorKey":
            log = (a[0], a[1], rd(uc, a[2], 8))
        stack_ret = n * 4 if stdcall else 0
        self.log.append((name,) + log)
        uc.reg_write(E.UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + stack_ret)
        uc.reg_write(E.UC_X86_REG_EIP, ret)


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

    def putb(self, address, data):
        buf, off = self._where(address)
        buf[off:off + len(data)] = data

    def get(self, address, fmt):
        buf, off = self._where(address)
        return struct.unpack_from(fmt, buf, off)[0]


def world(st, rng):
    for obj, iface in OBJ_IFACE:
        st.put(obj, "<I", VT_BASE[iface])
    st.put(0x4B2198, "<I", CFG)
    st.put(0x4B1960, "<I", RACE)
    st.put(0x4D5F40, "<I", DD)
    st.put(0x4D5F48, "<I", FRONT)
    st.put(0x4D5F4C, "<I", BACK)
    st.put(0x4B8384, "<I", DEV)
    st.put(0x4ACC38, "<i", pick(rng, 640, 800, 1024, 1280, 3440, 321, 17))
    st.put(0x4ACC3C, "<i", pick(rng, 480, 600, 768, 1440, 241, 9))


# ---- generators: (args, returns) ----

def g_upload(st, rng):
    s = st.script
    st.put(0x4BA6F8, "<I", pick(rng, 0, 5, 0x3E, 0x3F, r32(rng) & 0x3F))
    free = rng.random()
    for i in range(64):
        rec = 0x4BA700 + i * 0x24
        st.put(rec, "<I", 0 if rng.random() < free * 0.3 else r32(rng) | 1)
    st.put(0x4B86C0, "<I", pick(rng, 0, 0, 1))
    st.put(0x4B86A4, "<I", pick(rng, 1, 1, 1, 0))
    st.put(0x4B86A0, "<I", pick(rng, 0, 1))
    st.put(0x4BCD54, "<I", pick(rng, 0, 1))
    st.put(0x4BCD7C, "<I", CTABLE)
    st.put(0x6CCE54, "<i", rng.randrange(-2, 0x40))
    st.put(0x4D5FA4, "<I", r32(rng))
    page = rng.randrange(0, 0x40)
    w = pick(rng, 0, 1, 2, 7, 16, 32, 33, 64, rng.randrange(1, 130), -3)
    h = pick(rng, 0, 1, 3, 16, 64, 48, rng.randrange(1, 130), -1)
    pixels = pick(rng, PIXELS, PIXELS, 0)
    palette = pick(rng, PALETTE, PALETTE, 0)
    flags = rng.randrange(0, 8)
    s["CreateSurface"] = [pick(rng, 0, 0, 0, 0x80004005)]
    pw = 1
    while pw < max(w, 1) and pw < 1 << 31:
        pw <<= 1
    s["lock_info"] = [(pick(rng, 2 * pw, 2 * pw + 8, pw, pw + 4), pick(rng, 1, 1, 1, 0))]
    s["CreatePalette"] = [pick(rng, 0, 0, 0x80004005), pick(rng, 0, 0, 0x80004005)]
    s["pal_obj"] = [pick(rng, PAL1, PAL1, 0), PAL2]
    s["SetPalette"] = [pick(rng, 0, 0, 5), pick(rng, 0, 0, 5)]
    s["QueryInterface"] = [pick(rng, 0, 0, 0, 7), pick(rng, 0, 0, 0, 7)]
    s["desc_ok"] = [pick(rng, 1, 1, 1, 0), pick(rng, 1, 1, 1, 0)]
    s["caps"] = [r32(rng), pick(rng, 0x4000, 0, r32(rng))]
    s["create_ok"] = [pick(rng, 1, 1, 1, 0)]
    s["Load"] = [pick(rng, 0, 0, 0, 9)]
    s["SetColorKey"] = [r32(rng)]
    return [page, w, h, pixels, palette, flags], True


def bytes_sum(st, a, n):
    return sum(st.get(a + k, "<B") for k in range(n)) & 0xFF


def g_probe(st, rng):
    s = st.script
    st.putb(0x4B8690, rng.randbytes(16))
    total = bytes_sum(st, 0x4B8690, 16)
    st.put(CFG + 0x98, "<B", total if rng.random() < 0.3 else r32(rng))
    for off in (0x99, 0x9A, 0x9B):
        st.put(CFG + off, "<B", pick(rng, 0, 1, r32(rng)))
    st.put(0x6CCE7C, "<I", pick(rng, 0, 1, r32(rng)))
    st.put(0x4D5FA0, "<I", pick(rng, 0, EXTRA))
    vals = [pick(rng, 0, 1)]
    s["pixel"] = [rng.choice(vals + [vals[0] ^ (rng.random() < 0.2)]) for _ in range(12)]
    s["pvrc"] = [pick(rng, 0, 1)]
    return [], False


def g_window(st, rng):
    s = st.script
    st.put(0x6C7FFC, "<I", r32(rng))
    s["GetSystemMetrics"] = [pick(rng, 1024, 3440, 1, -5) for _ in range(4)]
    s["CreateWindowExA"] = [pick(rng, 0, 0x10010, r32(rng))]
    s["ShowWindow"] = [r32(rng)]
    s["adjust"] = [(rng.randrange(-10, 1), rng.randrange(-30, 1), rng.randrange(0, 10), rng.randrange(0, 10))]
    args = [pick(rng, 0, 10, -7, r32(rng)), pick(rng, 0, 5, r32(rng)), pick(rng, 0, 20, r32(rng)),
            pick(rng, 0, 640, 1024), pick(rng, 0, 480, 768), pick(rng, 0, 1, r32(rng))]
    return args, True


def g_flip(st, rng):
    s = st.script
    st.put(0x4D5F80, "<I", pick(rng, 0, 1, r32(rng)))
    st.put(0x4D5F88, "<I", pick(rng, 640, 1024, r32(rng)))
    st.put(0x4D5F8C, "<I", pick(rng, 480, 768, r32(rng)))
    s["Flip"] = [pick(rng, 0, LOST, STILL, 0x80004005) for _ in range(2)]
    s["Restore"] = [r32(rng), r32(rng)]
    s["blit"] = [r32(rng)]
    return [], True


def scribble(rng, n_cars):
    out = []
    for _ in range(rng.randrange(1, 4)):
        k = rng.randrange(0, n_cars)
        rec = 0x6BFB80 + k * 0x274
        kind = rng.randrange(4)
        if kind == 0:
            out.append((rec + 8, rng.randbytes(0x20)))
        elif kind == 1:
            out.append((CAM[k] + 0x34, rng.randbytes(12)))
        elif kind == 2:
            out.append((CAM[k] + 0x34 + rng.randrange(0, 0xD0), rng.randbytes(8)))
        else:
            out.append((CAR[k] + 0x34, rng.randbytes(0x40)))
    return out


def g_race(st, rng):
    s = st.script
    st.put(0x4AF5AC, "<I", pick(rng, 1, 1, 1, 0))
    n = pick(rng, 0, 1, 2, 4, 5, 6)  # six car records (0x6bfb80, 0x274 each), the view list after them
    st.put(RACE + 0x54D, "<B", n)
    for k in range(6):
        rec = 0x6BFB80 + k * 0x274
        st.put(rec, "<I", CAR[k])
        st.put(rec + 0x28, "<I", HOLD[k])
        st.put(HOLD[k], "<I", CAM[k])
    for i in range(6):
        st.put(0x6C0A40 + 4 * i, "<I", rng.randrange(0, 6))
    st.put(0x6CCE40, "<I", r32(rng) & 0xFFFFFF7F)
    st.put(CFG + 0x25, "<B", pick(rng, 0, 1))
    s["view_count"] = [pick(rng, 0, 1, 2, 4, -1)]
    s["net_on"] = [pick(rng, 0, 1) for _ in range(8)]
    s["net_me"] = [rng.randrange(-1, 10) for _ in range(8)]
    s["scribble"] = [scribble(rng, 6) for _ in range(40)]
    return [], False


def g_sound(st, rng):
    s = st.script
    st.put(0x4D459C, "<I", pick(rng, 0, 1))
    st.put(0x4AC984, "<I", pick(rng, 0, 0, 0, 1))
    s["voice_alloc"] = [(r32(rng) & 0xFFFFFF00) | pick(rng, 0x7F, rng.randrange(0, 64), rng.randrange(0, 256))]
    s["voice_start"] = [r32(rng)]
    args = [r32(rng), r32(rng), r32(rng), r32(rng), pick(rng, 0, r32(rng))]
    return args, True


def g_moving(st, rng):
    args, _ = g_sound(st, rng)
    return [r32(rng), args[0], args[1], args[2], args[3], r32(rng), r32(rng)], True


TESTS = {
    "texture_upload_body": (0x41E710, g_upload),
    "vk_probe_body": (0x41F720, g_probe),
    "create_window_body": (0x4771C0, g_window),
    "flip_body": (0x4876F0, g_flip),
    "race_draw_body": (0x443B50, g_race),
    "sound_play_body": (0x471C90, g_sound),
    "sound_moving_body": (0x471DA0, g_moving),
}
SIZES = {0x41E710: 1284, 0x41F720: 1073, 0x4771C0: 287, 0x4876F0: 214, 0x443B50: 1274, 0x471C90: 185,
         0x471DA0: 179}

SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP)


def run_once(m, stubs, st, target, args, fill, returns):
    m.write(E.STACK, bytes([fill]) * E.STACK_SIZE)
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    for i, reg in enumerate(SAVED[:4]):
        m.uc.reg_write(reg, 0x11111111 * (i + 1))
    stubs.reset(st.script)
    stubs.target = target
    r = m.call(target, args)
    if r[0] == "ok":
        r = ("ok", r[1]) if returns else ("ok",)
        r = r + tuple(m.uc.reg_read(x) for x in SAVED)
    mem = bytearray(m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS))
    return r, mem, stubs.log


def mask_logs(la, lb, ours):
    """Zero the bytes of logged buffers that differ between the original's two runs (stack leftovers)."""
    def fix(xa, xb, xo):
        if isinstance(xa, bytes) and isinstance(xb, bytes) and len(xa) == len(xb) and xa != xb:
            ma = bytearray(xa)
            mo = bytearray(xo) if isinstance(xo, bytes) else None
            for k in range(len(xa)):
                if xa[k] != xb[k]:
                    ma[k] = 0
                    if mo is not None and k < len(mo):
                        mo[k] = 0
            return bytes(ma), (bytes(mo) if mo is not None else xo)
        if isinstance(xa, tuple) and isinstance(xb, tuple) and len(xa) == len(xb):
            oa, oo = [], []
            for k in range(len(xa)):
                yo = xo[k] if isinstance(xo, tuple) and k < len(xo) else None
                ya, yo2 = fix(xa[k], xb[k], yo)
                oa.append(ya)
                oo.append(yo2)
            if isinstance(xo, tuple) and len(xo) == len(xa):
                return tuple(oa), tuple(oo)
            return tuple(oa), xo
        return xa, xo

    for i, (ea, eb) in enumerate(zip(la, lb)):
        if ea[0] != eb[0]:
            break
        xo = ours[i] if i < len(ours) else None
        na, no = fix(ea, eb, xo)
        la[i] = na
        if i < len(ours):
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


def mask_memory(a, b, o):
    step = 0x1000
    for c in range(0, len(a), step):
        if a[c:c + step] != b[c:c + step]:
            for k in range(c, min(c + step, len(a))):
                if a[k] != b[k]:
                    a[k] = 0
                    o[k] = 0


def run_case(m, stubs, base, name, address, gen, rng):
    st = State(rng, base)
    world(st, rng)
    args, returns = gen(st, rng)
    args = [a & 0xFFFFFFFF for a in args]
    a = run_once(m, stubs, st, address, args, 0xCC, returns)
    b = run_once(m, stubs, st, address, args, 0x5A, returns)
    o = run_once(m, stubs, st, m.symbols[name], args, 0xCC, returns)
    mask_memory(a[1], b[1], o[1])
    la, lo = list(a[2]), list(o[2])
    mask_logs(la, list(b[2]), lo)
    if a[0] == o[0] and a[1] == o[1] and la == lo:
        if a[0][0] != "ok" and "-f" in sys.argv:
            print(f"  fault {name}{tuple(hex(x) for x in args)}: {a[0]} after {la[-2:]}")
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
    rng = random.Random(0x3C)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c", "-f")]
    for name, (address, gen) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        seen = set()
        size = SIZES.get(address, 0)
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
