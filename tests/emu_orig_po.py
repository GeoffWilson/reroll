#!/usr/bin/env python3
"""Differential test of orig_po.c (group PO: the shield / tyre trail / skid
mark drawing, the QMixer / EAX helpers, the replay-file, CD-check and
process-launch helpers, 0x46d0c0-0x474fa0) against the original, on random
state.

Callees outside the group that touch DirectX, QMixer, Windows, the registry
or files are logging stubs in both runs (the log records as many arguments
as the callee reads; strings by content, buffers passed by pointer by
content, stack pointers as "stack") with scripted results: the texture
lock / unlock, the camera matrix 0x459e80, 0x486270, the QMixer channel
helpers, the EAX property set 0x471700, the mixer, chdir, the registry,
text lines, file probes, the C runtime's path functions and _getcwd, the
event helpers; imports (stdcall) through their import slots, COM methods
through a fake object's vtable. Pure game code runs for real in both: the
transform / projection pointers' targets (0x426a20 / 0x426b00), 0x4266d0,
0x426620, gte_rotate 0x428090, the facing value 0x4278f0, the C runtime's
sqrt 0x4981d4. The group's own functions run for real in both (the
original's call the original's, ours call ours).

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those two runs (stack leftovers,
e.g. the skid marks' 0x6ccc6a) are masked before the comparison with ours.
Compared: the return value, ebx / esi / edi / ebp, the x87 stack top / tag
word, the globals and scratch memory, the call log. The x87 control word is
0x27f, 0x37f or 0x07f.

    tests/emu_orig_po.py EXE build/emu_po.elf [iterations] [names...] [-v] [-c]
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
SCR_SIZE = 0x2C000
CARS = SCR + 0x0000  # 3 cars of 0x1600
CAR_SIZE = 0x1600
RECS = SCR + 0x4400  # 3 shield records of 0x100
MESH = SCR + 0x4800  # shield mesh header
MPOS = SCR + 0x4900  # mesh vertices (8 bytes)
MFACES = SCR + 0x4B00  # mesh faces (8 bytes)
COLS = SCR + 0x4D00  # vertex colours
SPKT = SCR + 0x5000  # shield packet buffers, 6 x 0xc00
TEXT = SCR + 0x9800  # 0x6c1e14 texture table (0x800)
TEXH = SCR + 0xA000  # *0x6c1eb0
OT = SCR + 0xA100  # ordering table entries (0x800)
MODEL = SCR + 0xB000  # trail car model
TPOS = SCR + 0xB100
TFACES = SCR + 0xB300
TVERT = SCR + 0xB500
SURFT = SCR + 0xB800  # *0x6c1e30
SURFP = SCR + 0xB900  # surface pointers (0x400)
SURFR = SCR + 0xBD00  # surface records (16 x 8)
IDX16 = SCR + 0xBE00
MODELS = SCR + 0xBE80
UVREC = SCR + 0xC000  # 0x84-byte records
TPKT = SCR + 0xD800  # trail packets (0x4940)
SKID = SCR + 0x12400  # skid records (8 x 0x124)
SURF = SCR + 0x13000  # texture surface bits (0x10800)
COM = SCR + 0x24000  # fake COM objects
ARG = SCR + 0x25000  # strings, out buffers (0x3000)
STUBPAGE = 0x02200000

REGIONS = [(0x4B0000, 0x25000), (0x5FF000, 0x2000), (0x6BF000, 0x3000), (0x6CB000, 0x4000)]

# stdcall imports: slot -> (name, argument count)
IAT = {
    0x4A30E8: ("DeleteFileA", 1),
    0x4A30A0: ("FindFirstFileA", 2),
    0x4A309C: ("FindNextFileA", 2),
    0x4A3098: ("FindClose", 1),
    0x4A306C: ("GetDriveTypeA", 1),
    0x4A3280: ("MessageBoxA", 4),
    0x4A3060: ("CreateProcessA", 10),
    0x4A32B4: ("timeGetTime", 0),
    0x4A3064: ("WaitForSingleObject", 2),
    0x4A31E8: ("SetSourceVelocity", 4),
    0x4A31D0: ("StopProfile", 3),
}
# COM methods (stdcall): vtable offset -> (name, argument count with this)
VTABLE = {0x0: ("QueryInterface", 3), 0xC: ("PropGet", 8), 0x10: ("PropSet", 7), 0x14: ("QuerySupport", 4)}
# cdecl callees at their own addresses
STUBS = {
    0x428C30: "tex_lock",
    0x428C60: "tex_unlock",
    0x459E80: "camera_matrix",
    0x486270: "count_min",
    0x46FEC0: "chan_busy",
    0x46F8F0: "chan_buffer",
    0x471700: "eax_first",
    0x4722D0: "sound_mix",
    0x475030: "chdir",
    0x46B650: "reg_read",
    0x46B770: "reg_delete",
    0x46B2F0: "reg_key_delete",
    0x42E6D0: "text_line",
    0x476F00: "file_exists",
    0x474DC0: "token_copy",
    0x41C550: "replays_save",
    0x474260: "boot_save",
    0x46B850: "event_close",
    0x46B8B0: "event_create",
    0x46B950: "event_set",
    0x46B890: "event_free",
    0x474E80: "unquote",
    0x4988C5: "splitpath",
    0x498834: "makepath",
    0x498A0C: "getcwd",
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


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


def ptr(a):
    return "stack" if on_stack(a) else a


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
        self.vtable = {}
        for i, (off, (name, n)) in enumerate(sorted(VTABLE.items())):
            address = STUBPAGE + 0x800 + i * 0x10
            self.vtable[off] = address
            self.calls[address] = (name, n, True)
        for address, name in STUBS.items():
            self.calls[address] = (name, 0, False)
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
        name, n, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<11I", bytes(uc.mem_read(esp, 44)))
        ret, a = words[0], list(words[1:])
        nx = self._next
        eax = 0
        log = ()
        if name == "tex_lock":
            res, bits, bpp, pitch = nx(name, (0, 0, 0, 0))
            log = (a[0], a[1], ptr(a[2]), a[3], ptr(a[4]), ptr(a[5]))
            uc.mem_write(a[2], struct.pack("<I", bits))
            uc.mem_write(a[4], struct.pack("<I", bpp))
            uc.mem_write(a[5], struct.pack("<I", pitch))
            eax = res
        elif name == "tex_unlock":
            log = (a[0], a[1], a[2])
        elif name == "camera_matrix":
            log = (a[0], bytes(uc.mem_read(a[0] + 0x34, 12)))
        elif name in ("count_min", "eax_first", "sound_mix", "replays_save", "boot_save", "event_close"):
            eax = nx(name)
        elif name in ("chan_busy", "chan_buffer", "event_set", "event_free"):
            log = (a[0],)
            eax = nx(name)
        elif name in ("chdir", "reg_key_delete", "file_exists"):
            log = (c_string(uc, a[0]),)
            eax = nx(name)
        elif name == "reg_read":
            res, text = nx(name, (0, b""))
            log = (c_string(uc, a[0]), c_string(uc, a[1]), ptr(a[2]), a[3])
            uc.mem_write(a[2], text + b"\0")
            eax = res
        elif name == "reg_delete":
            log = (c_string(uc, a[0]), c_string(uc, a[1]))
            eax = nx(name)
        elif name == "text_line":
            res, text = nx(name, (0, b""))
            log = (ptr(a[0]), a[1], a[2], a[3])
            uc.mem_write(a[0], text + b"\0")
            eax = res
        elif name == "token_copy":
            s = c_string(uc, a[1])
            log = (ptr(a[0]), s)
            uc.mem_write(a[0], s.split(b" ")[0] + b"\0")
            eax = a[0]
        elif name == "event_create":
            res, h = nx(name, (0, 0))
            log = (ptr(a[0]), c_string(uc, a[1]))
            if a[0]:
                uc.mem_write(a[0], struct.pack("<I", h))
            eax = res
        elif name == "unquote":
            s = c_string(uc, a[1])
            log = (ptr(a[0]), s)
            uc.mem_write(a[0], s.replace(b'"', b"")[:0x103] + b"\0")
            eax = a[0]
        elif name == "splitpath":
            s = c_string(uc, a[0])
            log = (ptr(a[0]), s, ptr(a[1]), ptr(a[2]), ptr(a[3]), ptr(a[4]))
            drive, rest = (s[:2], s[2:]) if len(s) > 1 and s[1:2] == b":" else (b"", s)
            cut = max(rest.rfind(b"\\"), rest.rfind(b"/")) + 1
            d, f = rest[:cut], rest[cut:]
            dot = f.rfind(b".")
            fn, ext = (f[:dot], f[dot:]) if dot >= 0 else (f, b"")
            for p, v in zip(a[1:5], (drive, d, fn, ext)):
                if p:
                    uc.mem_write(p, v + b"\0")
        elif name == "makepath":
            parts = [c_string(uc, p) if p else b"" for p in a[1:5]]
            log = (ptr(a[0]),) + tuple(parts)
            uc.mem_write(a[0], b"|".join(parts) + b"\0")
        elif name == "getcwd":
            res, text = nx(name, (0, b""))
            log = (ptr(a[0]), a[1])
            uc.mem_write(a[0], text + b"\0")
            eax = a[0] if res else 0
        # stdcall
        elif name == "DeleteFileA":
            log = (c_string(uc, a[0]),)
            eax = nx(name)
        elif name == "FindFirstFileA":
            h, fname = nx(name, (0xFFFFFFFF, b""))
            log = (c_string(uc, a[0]), ptr(a[1]))
            uc.mem_write(a[1] + 0x2C, fname + b"\0")
            eax = h
        elif name == "FindNextFileA":
            res, fname = nx(name, (0, b""))
            log = (a[0], ptr(a[1]))
            uc.mem_write(a[1] + 0x2C, fname + b"\0")
            eax = res
        elif name == "FindClose":
            log = (a[0],)
        elif name == "GetDriveTypeA":
            log = (c_string(uc, a[0]),)
            eax = nx(name)
        elif name == "MessageBoxA":
            log = (a[0], c_string(uc, a[1]), c_string(uc, a[2]), a[3])
            eax = nx(name)
        elif name == "CreateProcessA":
            log = (c_string(uc, a[0]), c_string(uc, a[1])) + tuple(a[2:7]) + (
                c_string(uc, a[7]) if a[7] else 0, bytes(uc.mem_read(a[8], 0x44)), bytes(uc.mem_read(a[9], 16)))
            eax = nx(name)
        elif name == "timeGetTime":
            eax = nx(name)
        elif name == "WaitForSingleObject":
            log = (a[0], a[1])
            eax = nx(name)
        elif name == "SetSourceVelocity":
            log = (a[0], a[1], a[2], bytes(uc.mem_read(a[3], 12)))
            eax = nx(name)
        elif name == "StopProfile":
            log = (a[0], ptr(a[1]), a[2])
            uc.mem_write(a[1], struct.pack("<5I", *nx(name, (0, 0, 0, 0, 0))))
            eax = 0
        elif name == "QueryInterface":
            hr, obj = nx(name, (0, 0))
            log = (a[0], a[1], ptr(a[2]))
            uc.mem_write(a[2], struct.pack("<I", obj))
            eax = hr
        elif name == "QuerySupport":
            hr, support = nx(name, (0, 0))
            log = (a[0], a[1], a[2], ptr(a[3]))
            uc.mem_write(a[3], struct.pack("<I", support))
            eax = hr
        elif name == "PropSet":
            log = (a[0], a[1], a[2], a[3], a[4], bytes(uc.mem_read(a[5], 4)), a[6])
            eax = nx(name)
        elif name == "PropGet":
            hr, value = nx(name, (0, 0))
            log = (a[0], a[1], a[2], a[3], a[4], ptr(a[5]), a[6], ptr(a[7]))
            uc.mem_write(a[5], struct.pack("<I", value))
            uc.mem_write(a[7], struct.pack("<I", 4))
            eax = hr
        else:
            raise KeyError(name)
        self.log.append((name, log))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * n if stdcall else 0))
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


def SLOT(i):
    return 0x6BFB80 + i * 0x274


def world(st, rng):
    """Pointers and the GTE / projection state."""
    st.put(0x4B1960, "<I", ARG + 0x2000)  # race
    st.put(0x4B2198, "<I", ARG + 0x2400)  # config
    for i in range(6):
        st.put(SLOT(i), "<I", CAR(i % 3))
    # transform / projection (0x426a20 / 0x426b00)
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD7C, "<I", 0x426BE0)
    st.put(0x6CCD80, "<I", 0x426B00)
    st.putf(0x6CCD00, pick(rng, 256.0, 320.0, 200.0))
    st.putf(0x6CCD04, pick(rng, 1.0, 16.0))
    st.putf(0x6CCD08, pick(rng, 6000.0, 20000.0))
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
    for k in range(9):
        st.put(0x6CCBE0 + 2 * k, "<h", rng.randrange(-4096, 4097))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CD4A0, 0x6CD4A4))
    # ordering tables: entries into OT, sometimes with the top bit
    for k in range(0x200):
        st.put(0x6CDD80 + 4 * k, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0, 0x80000000))
        st.put(0x6CD510 + 4 * k, "<I", (OT + 4 * rng.randrange(0x200)) | pick(rng, 0, 0x80000000))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, 1))
    st.put(0x6C1E14, "<I", TEXT)
    st.put(0x6C1EB0, "<I", TEXH)
    st.put(TEXH + 0x2E, "<H", rng.randrange(0, 12))
    st.put(0x6C0A50, "<I", r32(rng))


# ---------------------------------------------------------------- generators: (args, returns value?)

def g_tex_animate(st, rng):
    st.put(0x4B24A0, "<I", pick(rng, 0xFFFFFFFF, 5, 5, 5, 7))
    now = r32(rng)
    st.put(0x4BE3E8, "<I", now)
    st.put(0x4B1C28, "<I", pick(rng, 0xFFFFFFFF, now - 1, now - 5, now - 6, now + 3, r32(rng)))
    st.script["tex_lock"] = [(pick(rng, 0, 1, 1, 1), SURF, pick(rng, 8, 8, 16, 32), pick(rng, 0x100, 0x80, 0x104))]
    return [], True


def g_ripples(st, rng):
    return [rng.randrange(0, 8), SURF, pick(rng, 0x100, 0x80, 0x104), pick(rng, 8, 8, 16, 0)], False


def shield_mesh(st, rng):
    n = pick(rng, 0, 1, 3, rng.randrange(0, 40))
    nf = pick(rng, 0, 1, rng.randrange(0, 30))
    st.put(0x4D44A4, "<I", MESH)
    st.put(MESH + 0x40, "<I", MPOS)
    st.put(MESH + 4, "<H", n)
    st.put(MESH + 0xC, "<I", MFACES)
    st.put(MESH + 6, "<H", nf)
    for k in range(n + 1):
        st.putb(MPOS + 8 * k, struct.pack("<4h", *(rng.randrange(-2000, 2000) for _ in range(4))))
    for k in range(nf):
        st.putb(MFACES + 8 * k, bytes(rng.randrange(1, n + 2) for _ in range(3)) + rng.randbytes(5))
    for k in range(n + 2):
        st.put(COLS + 4 * k, "<I", r32(rng))
    st.put(0x6CCE94, "<I", pick(rng, 0, 1))
    st.put(ARG + 0x2400 + 0x2A, "<B", pick(rng, 0, 1))
    st.put(0x4B24A0, "<I", pick(rng, 0xFFFFFFFF, 5))
    st.putb(0x6CCC40, rng.randbytes(8))


def shield_record(st, rng, k):
    rec = RECS + k * 0x100
    st.put(rec, "<B", pick(rng, 0, 1, 1))
    st.put(rec + 0x64, "<I", CAR(k))
    st.put(rec + 0x68, "<I", SPKT + 0x1800 * k)
    st.put(rec + 0x6C, "<I", SPKT + 0x1800 * k + 0xC00)
    st.put(rec + 0x70, "<I", COLS + 4)
    return rec


def g_shields(st, rng):
    shield_mesh(st, rng)
    st.put(ARG + 0x2000 + 0x54D, "<B", pick(rng, 0, 1, 3, 6))
    for k in range(3):
        rec = shield_record(st, rng, k)
        st.put(CAR(k) + 0x6AC, "<I", pick(rng, rec, rec, 0xFFFFFFFF))
    return [], False


def g_shield_build(st, rng):
    shield_mesh(st, rng)
    return [shield_record(st, rng, rng.randrange(3))], False


def g_shield_link(st, rng):
    car = CAR(rng.randrange(3))
    rec = RECS
    st.put(car + 0x6AC, "<I", pick(rng, rec, rec, rec, 0xFFFFFFFF))
    st.put(rec, "<B", pick(rng, 0, 1, 1))
    st.put(rec + 0x74, "<I", pick(rng, SPKT, SPKT, 0xFFFFFFFF))
    for off in (0x78, 0x7C, 0x80):
        st.put(rec + off, "<I", pick(rng, 0, SPKT + 0x100 * rng.randrange(1, 8)))
    st.put(car + 0x14B4, "<i", pick(rng, rng.randrange(-0x100, 0x2000), rng.randrange(0, 0x40000), r32(rng)))
    return [car, OT + 4 * rng.randrange(0x200) | pick(rng, 0, 0x80000000),
            OT + 4 * rng.randrange(0x200) | pick(rng, 0, 0x80000000)], False


def trail_tables(st, rng):
    for k in range(0x60):
        st.putb(TEXT + 16 * k, rng.randbytes(16))


def g_trail_texture(st, rng):
    trail_tables(st, rng)
    trail = CAR(0) + 0x10AC
    st.put(trail, "<I", TPKT)
    return [trail, pick(rng, 0x56, 0x57, rng.randrange(0, 0x50))], False


def trail_car(st, rng, car):
    trail_tables(st, rng)
    st.put(ARG + 0x2400 + 0x24, "<B", pick(rng, 0, 1, 1, 1))
    st.put(0x6CCD84, "<B", pick(rng, 0, 0, 4, rng.getrandbits(8)))
    st.put(car + 0x10AC, "<I", TPKT)
    st.put(car + 0x10B0, "<I", TPKT + 0x2490)
    st.put(car + 0x1104, "<B", pick(rng, 0, 1, 1))
    st.put(car + 0x1105, "<B", pick(rng, 0, 1, 2))
    st.put(car + 0x11C, "<i", pick(rng, -1, rng.randrange(0, 16), rng.randrange(0, 0x10000)))
    st.put(0x6C1E30, "<I", SURFT)
    st.put(SURFT, "<I", SURFP)
    for k in range(0x100):
        st.put(SURFP + 4 * k, "<I", SURFR + 8 * rng.randrange(16))
    st.put(SURFT + 0x58, "<I", IDX16)
    st.put(SURFT + 0x18, "<I", MODELS)
    for k in range(0x40):
        st.put(IDX16 + 2 * k, "<h", rng.randrange(0, 0x20))
        st.put(MODELS + 4 * k, "<I", pick(rng, MODEL, MODEL + 0x10, r32(rng)))
    # the model
    n = pick(rng, 0, 2, rng.randrange(0, 40))
    st.put(car + 0x10, "<I", MODEL)
    st.put(MODEL + 4, "<H", n)
    st.put(MODEL + 0x40, "<I", TPOS)
    st.put(MODEL + 0xC, "<I", TFACES)
    st.put(MODEL + 0x48, "<I", TVERT)
    for k in range(n + 1):
        st.putb(TVERT + 8 * k, struct.pack("<4h", *(rng.randrange(-3000, 3000) for _ in range(4))))
        st.putb(TPOS + 8 * k, struct.pack("<4h", *(rng.randrange(-3000, 3000) for _ in range(4))))
    for f in range(0x20):
        b = [rng.randrange(1, n + 2) for _ in range(4)]
        if rng.randrange(3) == 0:
            b[rng.randrange(4)] = 0 if rng.randrange(2) else b[0]
        st.putb(TFACES + 8 * f, bytes(b) + rng.randbytes(4))
    st.put(car + 0x1100, "<I", UVREC)
    nl = pick(rng, 0, 1, 5, rng.randrange(0, 0x48))
    st.putb(car + 0x10B4, bytes(rng.randrange(0, 0x20) for _ in range(nl)))
    st.put(car + 0x10FC, "<I", car + 0x10B4 + pick(rng, nl, nl, 0, rng.randrange(0, nl + 1)))
    st.put(car + 0x34, "<i", r32(rng))
    st.put(car + 0x3C, "<i", r32(rng))
    for k in range(5):
        st.putb(car + 0xC4 + 4 * k, struct.pack("<2h", rng.randrange(-4096, 4097), rng.randrange(-4096, 4097)))
    for k in range(0x20):  # z floats of the face records' first vertex (+0xc)
        st.putf(UVREC + 0x84 * k + 0xC, pick(rng, rng.uniform(-100, 0x9000), rng.uniform(0, 0x4000)))
    st.script["count_min"] = [pick(rng, 0, 1, 6, rng.randrange(0, 0x400), r32(rng)) for _ in range(2)]
    for k in range(7):
        st.put(0x4B1C34 + 4 * k, "<i", pick(rng, 0x70, 0x80, 0xA0, rng.randrange(-4, 0x200)))


def g_trail_draw(st, rng):
    car = CAR(rng.randrange(3))
    trail_car(st, rng, car)
    return [car, OT + 4 * rng.randrange(0x200) | pick(rng, 0, 0x80000000)], False


def g_trail_build(st, rng):
    car = CAR(rng.randrange(3))
    trail_car(st, rng, car)
    return [car, OT + 4 * rng.randrange(0x200) | pick(rng, 0, 0x80000000)], False


def g_trail_uv(st, rng):
    for k in range(9):
        st.put(0x6CCBE0 + 2 * k, "<h", rng.randrange(-4096, 4097))
    st.putb(ARG, struct.pack("<4h", *(rng.randrange(-3000, 3000) for _ in range(4))))
    st.putb(ARG + 0x10, struct.pack("<3i", *(rng.randrange(-0x40000, 0x40000) for _ in range(3))))
    return [ARG, ARG + 0x20, ARG + 0x21, ARG + 0x10], False


def g_skid(st, rng):
    st.put(0x6CCD84, "<B", pick(rng, 2, 2, 0, rng.getrandbits(8)))
    st.put(ARG + 0x2400 + 0x2C, "<B", pick(rng, 0, 1, 1))
    st.put(0x6002D4, "<I", SKID)
    st.put(0x4B1C68, "<I", pick(rng, 0, 1, 4, 8))
    for k in range(8):
        r = SKID + 0x124 * k
        for p in (r, r + 0xC):
            st.putb(p, struct.pack("<4h", *(rng.randrange(-2000, 2000) for _ in range(4))))
        st.put(r + 8, "<I", r32(rng) | pick(rng, 0x8000, 0x8000, 0))
        st.put(r + 0x18, "<H", pick(rng, 0, 1))
        st.put(r + 0x1A, "<H", pick(rng, 0, 1))
    return [], False


def g_velocity(st, rng):
    st.put(0x4D4524, "<I", pick(rng, 1, 1, 1, 0))
    st.put(0x4D4528, "<I", pick(rng, 1, 1, 1, 0))
    chan = pick(rng, 0, 5, 63, 64, -1, rng.randrange(0, 64))
    v = [pick(rng, rng.uniform(-100, 100), rng.uniform(-400, 400), 0.0) for _ in range(3)]
    c = chan & 63
    if rng.randrange(2):
        for k in range(3):
            st.putf(0x6CB51C + c * 0x3C + 4 * k, v[k])
    st.script["SetSourceVelocity"] = [pick(rng, 0, 1)]
    return [chan] + [f2u(x) for x in v], True


def g_profile(st, rng):
    st.put(0x4D4524, "<I", pick(rng, 1, 1, 1, 0))
    st.put(0x4D4558, "<I", pick(rng, 1, 1, 0))
    for a in range(0x600160, 0x6002C8, 8):
        st.put(a, "<d", pick(rng, 0.0, rng.uniform(0, 1000), rng.uniform(0, 3)))
    st.put(0x4B1C8C, "<i", pick(rng, 0, 8, 32, -1))
    for k in range(32):
        st.putb(0x6CB500 + 0x3C * k, bytes([rng.getrandbits(8), pick(rng, 0, 1)]))
    st.script["StopProfile"] = [tuple(pick(rng, 0, 1, 100, r32(rng)) for _ in range(5))]
    st.script["chan_busy"] = [pick(rng, 0, 1) for _ in range(32)]
    return [], True


def com_object(st, stubs):
    st.put(COM, "<I", COM + 0x10)
    for off, address in stubs.vtable.items():
        st.put(COM + 0x10 + off, "<I", address)


def g_eax_props(st, rng, stubs):
    com_object(st, stubs)
    st.script["chan_buffer"] = [pick(rng, 0, COM, COM)]
    st.script["QueryInterface"] = [(pick(rng, 0, 0, 0x80004002), pick(rng, COM, COM, 0))]
    st.script["QuerySupport"] = [(pick(rng, 0, 0, 0x80000000), pick(rng, 3, 3, 1, 7)) for _ in range(2)]
    return [rng.randrange(0, 32)], True


def g_reverb(st, rng, stubs):
    com_object(st, stubs)
    st.put(0x4D4524, "<I", pick(rng, 1, 1, 1, 0))
    st.put(0x4D4528, "<I", pick(rng, 1, 1, 1, 0))
    voice = pick(rng, 0, 5, 63, 64, -1, rng.randrange(0, 64))
    level = pick(rng, 0.0, 1.0, 0.5, -0.5, 2.0, rng.uniform(-1, 2))
    c = voice & 63
    st.put(0x6CB534 + 0x3C * c, "<I", pick(rng, COM, COM, 0))
    st.putf(0x6CB530 + 0x3C * c, pick(rng, level, max(0.0, min(1.0, level)), rng.uniform(0, 1)))
    st.script["PropSet"] = [pick(rng, 0, 0x80000000)]
    return [voice, f2u(level)], True


def g_env_set(st, rng, stubs):
    com_object(st, stubs)
    st.script["eax_first"] = [pick(rng, COM, COM, 0)]
    st.script["PropSet"] = [pick(rng, 0, 0x80000000)]
    return [pick(rng, 0, 5, 25, 26, r32(rng))], True


def g_get(st, rng, stubs):
    com_object(st, stubs)
    st.script["eax_first"] = [pick(rng, COM, COM, 0)]
    st.script["PropGet"] = [(pick(rng, 0, 0x80000000), r32(rng))]
    return [ARG], True


def g_vol_set(st, rng, stubs):
    com_object(st, stubs)
    st.put(0x4D4524, "<I", pick(rng, 1, 1, 1, 0))
    st.put(0x4D4528, "<I", pick(rng, 1, 1, 1, 0))
    st.script["eax_first"] = [pick(rng, COM, COM, 0)]
    st.script["PropSet"] = [pick(rng, 0, 0x80000000)]
    return [f2u(pick(rng, 0.0, 1.0, 0.5, -0.5, 2.0, rng.uniform(-1, 2)))], True


def g_mix(st, rng):
    st.script["sound_mix"] = [r32(rng)]
    return [], True


def name_bytes(rng, n=None):
    n = pick(rng, 0, 1, 8, 12, rng.randrange(0, 40)) if n is None else n
    return bytes(rng.choice(b"ABCDEFGHIJ.0123 \\:\"") for _ in range(n))


def g_file_delete(st, rng):
    st.putb(ARG, name_bytes(rng) + b"\0")
    return [ARG], False


def g_delete_all(st, rng):
    names = [pick(rng, b"REPLAY1.ARF", b".", b"..", name_bytes(rng, 10)) for _ in range(5)]
    st.script["FindFirstFileA"] = [(pick(rng, 0x1234, 0x1234, 0xFFFFFFFF), names[0])]
    k = rng.randrange(0, 5)
    st.script["FindNextFileA"] = [(1, nm) for nm in names[1:k]] + [(0, b"")]
    return [], False


def g_chdir(st, rng):
    st.putb(ARG, name_bytes(rng) + b"\0")
    st.script["chdir"] = [r32(rng)]
    return [pick(rng, ARG, ARG, 0)], True


def g_cd(st, rng):
    st.script["reg_read"] = [(pick(rng, 0, -1, 5), pick(rng, b"D:\\", b"E:\\SETUP\\", name_bytes(rng)))]
    st.script["GetDriveTypeA"] = [pick(rng, 5, 5, 3, 1)]
    st.script["text_line"] = [(0, pick(rng, b"", b"Insert the CD", name_bytes(rng)))]
    st.script["file_exists"] = [pick(rng, 0, 1)]
    st.script["MessageBoxA"] = [pick(rng, 1, 2, 7)]
    return [pick(rng, 0, 1, r32(rng))], True


def g_launch(st, rng):
    st.putb(ARG, name_bytes(rng) + b"\0")
    st.putb(ARG + 0x100, name_bytes(rng) + b"\0")
    st.putb(ARG + 0x200, name_bytes(rng) + b"\0")
    st.putb(0x4D4674, pick(rng, b"\0", b"-x\0", name_bytes(rng, 5) + b"\0"))
    return [ARG, pick(rng, ARG + 0x100, 0), pick(rng, ARG + 0x200, 0)], True


def g_setup(st, rng):
    st.script["reg_read"] = [(pick(rng, 0, 0, -1), pick(rng, b"C:\\WIN\\UNINST.EXE -f x", name_bytes(rng)))]
    st.script["MessageBoxA"] = [pick(rng, 6, 7, 2, 1)]
    st.putb(ARG + 0x2400 + 0x17CE, name_bytes(rng) + b"\0")
    st.putb(ARG + 0x2400 + 0x180E, name_bytes(rng) + b"\0")
    return [], True


def g_chain(st, rng):
    st.put(ARG + 0x2000 + 0x58C, "<B", pick(rng, 0, 1))
    t = r32(rng)
    st.script["timeGetTime"] = [t] + [t + rng.randrange(0, 12000) for _ in range(6)]
    st.script["getcwd"] = [(pick(rng, 1, 1, 0), pick(rng, b"C:\\GAMES\\RC", name_bytes(rng)))]
    st.script["event_create"] = [(pick(rng, 0, 0, -1), r32(rng))]
    st.script["WaitForSingleObject"] = [pick(rng, 0, 0x102, 0x102) for _ in range(6)]
    st.script["event_close"] = [r32(rng), r32(rng)]
    st.putb(0x4D4674, pick(rng, b"\0", b"-x\0"))
    return [], True


def g_dir_make(st, rng):
    st.putb(ARG + 0x100, pick(rng, b'"C:\\GAMES\\RC"', b"C:\\A\\B.EXE", name_bytes(rng)) + b"\0")
    return [pick(rng, ARG, ARG, 0), pick(rng, ARG + 0x100, ARG + 0x100, 0)], True


def g_cwd(st, rng):
    st.script["getcwd"] = [(pick(rng, 1, 1, 0), pick(rng, b"C:\\GAMES\\RC", name_bytes(rng)))]
    return [pick(rng, ARG, ARG, 0)], True


TESTS = {
    "shield_texture_animate": (0x46D0C0, g_tex_animate),
    "shield_texture_ripples": (0x46D170, g_ripples),
    "shield_texture_brighten": (0x46D1F0, g_ripples),
    "shields_draw": (0x46DC50, g_shields),
    "shield_packets_build": (0x46DD20, g_shield_build),
    "shield_packets_link": (0x46DFF0, g_shield_link),
    "car_trail_texture_set": (0x46E450, g_trail_texture),
    "car_trail_draw": (0x46E4B0, g_trail_draw),
    "car_trail_packets_build": (0x46E540, g_trail_build),
    "car_trail_uv": (0x46EA10, g_trail_uv),
    "skid_marks_draw": (0x46F230, g_skid),
    "snd_chan_velocity": (0x470160, g_velocity),
    "snd_profile_update": (0x470790, g_profile),
    "snd_channel_eax_props": (0x471400, g_eax_props),
    "eax_voice_reverb_set": (0x4714C0, g_reverb),
    "eax_environment_set": (0x4716C0, g_env_set),
    "eax_environment_get": (0x471740, g_get),
    "eax_volume_set": (0x471780, g_vol_set),
    "eax_volume_get": (0x471810, g_get),
    "sound_mix_jump": (0x472040, g_mix),
    "replay_file_delete": (0x474360, g_file_delete),
    "replay_files_delete_all": (0x474470, g_delete_all),
    "dir_change_sub": (0x4747B0, g_chdir),
    "cd_find_data_path": (0x474840, g_cd),
    "process_launch": (0x474A10, g_launch),
    "setup_program_launch": (0x474B90, g_setup),
    "chain_launch_and_wait": (0x474CF0, g_chain),
    "path_dir_make": (0x474ED0, g_dir_make),
    "cwd_path_quoted": (0x474FA0, g_cwd),
}
NEEDS_STUBS = {g_eax_props, g_reverb, g_env_set, g_get, g_vol_set}

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
    args, returns = gen(st, rng, stubs) if gen in NEEDS_STUBS else gen(st, rng)
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
        msg += f"\n  calls from #{k}: orig {la[k:k + 2]}\n                ours {lo[k:k + 2]}"
    return msg, True


def func_sizes():
    sizes = {}
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "groups", "po_owned.txt")
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
    rng = random.Random(0x90)
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
