#!/usr/bin/env python3
"""Differential test of crt_ca.c (the debug log, the data archive, the CD
audio player with its aux / mixer volume code, strlwr / strupr, _hypot and
the DirectSoundCreate thunk) against the original, on random state.

Every callee left original is a logging stub in both runs: the C runtime
(fopen / fread / fseek / fclose / fputs / fflush / _vsnprintf / malloc / free
/ strcpy, the locale lock and LCMapString, _hypot's error reporting) at its
address, the GT20 unpacker, the game allocator, and the Windows calls (MCI,
aux, mixer, timeGetTime, Interlocked*, DirectSoundCreate) through import slots
pointed at stdcall stubs. Stubs log their arguments by content where they
point into the stack or at strings / structures, and return scripted values
(filling the structures the real API fills). sqrt 0x4981d4 and strrchr run
for real. Compared: return value (st0 for _hypot), the stack pointer after
the return (stdcall), the log / archive / CD / locale globals, the scratch
page and the call log.

    tests/emu_crt_ca.py EXE build/emu_ca.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESP,  # noqa: E402
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

SCR, SCR_SIZE = 0x01000000, 0x10000
STR = SCR + 0x0000  # string arguments
IDX = SCR + 0x0400  # archive index (32 records)
CAPS = SCR + 0x0800  # caps / control buffers passed in
BUFS = SCR + 0x1000  # archive buffers
HEAP = SCR + 0x2000  # malloc results, 0x800 apart
MIX = SCR + 0x8000  # mixer nodes (0x80 apart), values +0x1000, saved +0x2000
AUXT = SCR + 0xB000  # aux table
FP = 0x0BADF11E  # FILE * handed out by fopen
CODE = 0x02000000  # import stubs (0x10 apart), the return snippets
RET = CODE + 0x1000  # hlt
RET_FP = CODE + 0x1010  # fstp qword [RESULT]; hlt
SNIP = CODE + 0x1020  # fld qword [CONST]; ret (a stub returning a double)
CONST = CODE + 0x1100
RESULT = CODE + 0x1110

REGIONS = [(0x4B4848, 0x260), (0x4DB1C0, 0xD0), (0x6D0EE0, 0x30), (0x4DB37C, 4), (0x6D1248, 8), (SCR, SCR_SIZE)]

# import slot: (name, argument count)
IMPORTS = {
    0x4A32D0: ("mci", 4), 0x4A32B4: ("timeGetTime", 0), 0x4A32C8: ("mixerSetControlDetails", 3),
    0x4A32CC: ("mixerOpen", 5), 0x4A32D4: ("auxSetVolume", 2), 0x4A32D8: ("auxGetVolume", 2),
    0x4A32DC: ("auxGetDevCaps", 3), 0x4A32E0: ("auxGetNumDevs", 0), 0x4A32E4: ("mixerClose", 1),
    0x4A32E8: ("mixerGetLineInfo", 3), 0x4A32EC: ("mixerGetNumDevs", 0), 0x4A32F0: ("mixerGetDevCaps", 3),
    0x4A32F4: ("mixerGetLineControls", 3), 0x4A32F8: ("mixerGetControlDetails", 3),
    0x4A31AC: ("InterlockedIncrement", 1), 0x4A31C8: ("InterlockedDecrement", 1),
    0x4A3040: ("DirectSoundCreate", 3),
}
# cdecl stubs at original addresses: name, argument count
CDECL = {
    0x499459: ("fopen", 2), 0x49908E: ("fread", 4), 0x4991A5: ("fseek", 3), 0x4994F9: ("fclose", 1),
    0x4996DC: ("fputs", 2), 0x499576: ("fflush", 1), 0x49972A: ("vsnprintf", 4), 0x4982C2: ("malloc", 1),
    0x49827A: ("free", 1), 0x4445E0: ("alloc2", 1), 0x444570: ("free2", 1), 0x498450: ("strcpy", 2),
    0x497860: ("gt20", 2), 0x49B161: ("lock", 1), 0x49B1C2: ("unlock", 1), 0x49BA3B: ("lcmap", 8),
    0x4A0F1A: ("except", 9), 0x4A0EBA: ("qnan", 6),
}


def pick(rng, *c):
    return rng.choice(c)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.uc = m.uc
        self.log = []
        self.script = {}
        self.heap = 0
        for i, (slot, (name, n)) in enumerate(IMPORTS.items()):
            addr = CODE + i * 0x10
            self.uc.hook_add(UC_HOOK_CODE, self._make(name, n, True), begin=addr, end=addr)
        for addr, (name, n) in CDECL.items():
            self.uc.hook_add(UC_HOOK_CODE, self._make(name, n, False), begin=addr, end=addr)

    def patch_imports(self):
        for i, slot in enumerate(IMPORTS):
            self.m.write(slot, struct.pack("<I", CODE + i * 0x10))

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.heap = 0

    def nxt(self, name, default=0):
        v = self.script.get(name)
        return v.pop(0) if v else default

    # memory helpers
    def rd(self, a, n):
        return bytes(self.uc.mem_read(a, n))

    def u32(self, a):
        return struct.unpack("<I", self.rd(a, 4))[0]

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def cstr(self, a, limit=0x900):
        if a == 0:
            return None
        out = bytearray()
        while len(out) < limit:
            b = self.rd(a + len(out), 1)[0]
            if b == 0:
                break
            out.append(b)
        return bytes(out)

    @staticmethod
    def ptr(a):
        return "stack" if E.STACK <= a < E.STACK + E.STACK_SIZE else a

    def _make(self, name, nargs, stdcall):
        handler = getattr(self, "h_" + name)

        def hook(uc, address, size, _):
            esp = uc.reg_read(UC_X86_REG_ESP)
            words = struct.unpack("<10I", bytes(uc.mem_read(esp, 40)))
            ret = handler(list(words[1:1 + nargs]))
            if name in ("except", "qnan"):
                uc.mem_write(CONST, struct.pack("<d", ret))
                uc.reg_write(UC_X86_REG_EIP, SNIP)  # fld the result, ret
                return
            uc.reg_write(UC_X86_REG_EAX, ret & 0xFFFFFFFF)
            uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * nargs if stdcall else 0))
            uc.reg_write(UC_X86_REG_EIP, words[0])
        return hook

    def L(self, *entry):
        self.log.append(entry)

    # ---- C runtime
    def h_fopen(self, a):
        self.L("fopen", self.cstr(a[0]), self.cstr(a[1]))
        return self.nxt("fopen", FP)

    def h_fread(self, a):
        buf, size, count, fp = a
        self.L("fread", self.ptr(buf), size, count, fp)
        n = min((size * count) & 0xFFFFFFFF, 0x300)
        seed = self.nxt("fread_data", 1)
        self.uc.mem_write(buf, random.Random(seed).randbytes(n))
        return self.nxt("fread", count)

    def h_fseek(self, a):
        self.L("fseek", *a)
        return 0

    def h_fclose(self, a):
        self.L("fclose", *a)
        return 0

    def h_fputs(self, a):
        self.L("fputs", self.cstr(a[0]), a[1])
        return 0

    def h_fflush(self, a):
        self.L("fflush", *a)
        return 0

    def h_vsnprintf(self, a):
        buf, n, fmt, ap = a
        # the arguments only matter for the test's format (the banner 0x4b4a5c has none: the
        # stack after it is the callers' frames, which differ)
        self.L("vsnprintf", self.ptr(buf), n, fmt, self.rd(ap, 12) if fmt == STR + 0x200 else None)
        length = self.nxt("vsn_len", 10)
        text = bytes(0x41 + (i * 7) % 26 for i in range(length))
        if length >= n:
            self.uc.mem_write(buf, text[:n])
            return 0xFFFFFFFF
        self.uc.mem_write(buf, text + b"\0")
        return length

    def _alloc(self, name, a):
        self.L(name, a[0])
        v = self.nxt("malloc", None)
        if v is None:
            v = HEAP + self.heap * 0x800
            self.heap += 1
        return v

    def h_malloc(self, a):
        return self._alloc("malloc", a)

    def h_alloc2(self, a):
        return self._alloc("alloc2", a)

    def h_free(self, a):
        self.L("free", *a)
        return 0

    def h_free2(self, a):
        self.L("free2", *a)
        return 0

    def h_strcpy(self, a):
        s = self.cstr(a[1])
        self.L("strcpy", self.ptr(a[0]), s)
        self.uc.mem_write(a[0], s + b"\0")
        return a[0]

    def h_gt20(self, a):
        self.L("gt20", *a)
        return 0

    def h_lock(self, a):
        self.L("lock", *a)
        return 0

    def h_unlock(self, a):
        self.L("unlock", *a)
        return 0

    def h_lcmap(self, a):
        self.L("lcmap", a[0], a[1], self.cstr(a[2]), a[3], self.ptr(a[4]), a[5], a[6], a[7])
        r = self.nxt("lcmap", 5)
        if a[4] and a[5]:
            n = min(a[5], 0x40)
            self.uc.mem_write(a[4], bytes(0x61 + i % 26 for i in range(n - 1)) + b"\0")
        return r

    def h_except(self, a):
        self.L("except", a[0], a[1], tuple(a[2:8]), a[8])
        return self.nxt("fpret", 1.5)

    def h_qnan(self, a):
        self.L("qnan", a[0], tuple(a[1:5]), a[5])
        return self.nxt("fpret", 2.5)

    # ---- Windows
    def h_mci(self, a):
        dev, msg, flags, params = a
        self.L("mci", dev, msg, flags, params, self.rd(params, 16) if params else None)
        r = self.nxt("mci")
        if r == 0 and params:
            if msg == 0x803:
                self.w32(params + 4, self.nxt("mci_dev", 7))
            elif msg == 0x814:
                self.w32(params + 4, self.nxt("mci_val", 3))
        return r

    def h_timeGetTime(self, a):
        self.L("timeGetTime")
        return self.nxt("time", 12345)

    def h_mixerOpen(self, a):
        self.L("mixerOpen", self.ptr(a[0]), *a[1:])
        r = self.nxt("mixer_open")
        if r == 0:
            self.w32(a[0], self.nxt("handle", 0x1234))
        return r

    def h_mixerClose(self, a):
        self.L("mixerClose", *a)
        return 0

    def h_mixerGetNumDevs(self, a):
        self.L("mixerGetNumDevs")
        return self.nxt("mixer_devs", 0)

    def h_mixerGetDevCaps(self, a):
        dev, caps, size = a
        self.L("mixerGetDevCaps", dev, self.ptr(caps), size)
        r = self.nxt("mixer_caps")
        self.uc.mem_write(caps, random.Random(dev).randbytes(0x30))
        self.w32(caps + 0x2C, self.nxt("mixer_dests", 1))
        return r

    def h_mixerGetLineInfo(self, a):
        h, line, flags = a
        self.L("mixerGetLineInfo", h, flags, self.u32(line), self.u32(line + 4),
               self.u32(line + 8) if flags == 1 else None)
        r = self.nxt("line_ret")
        if r == 0:
            self.w32(line + 0xC, self.nxt("line_id", 0x55))
            self.w32(line + 0x18, self.nxt("line_type", 4))
            self.w32(line + 0x1C, self.nxt("line_ch", 2))
            self.w32(line + 0x20, self.nxt("line_conns", 1))
            self.w32(line + 0x24, self.nxt("line_ctls", 1))
        return r

    def _control(self, at):
        self.uc.mem_write(at, random.Random(self.nxt("ctl_seed", 3)).randbytes(0x94))
        self.w32(at + 4, self.nxt("ctl_id", 9))
        self.w32(at + 8, self.nxt("ctl_type", 0x50030001))
        self.w32(at + 0xC, self.nxt("ctl_flags", 0))
        self.w32(at + 0x10, self.nxt("ctl_items", 0))
        self.w32(at + 0x64, self.nxt("ctl_min", 0))
        self.w32(at + 0x68, self.nxt("ctl_max", 0xFFFF))

    def h_mixerGetLineControls(self, a):
        h, lc, flags = a
        words = struct.unpack("<6I", self.rd(lc, 24))
        self.L("mixerGetLineControls", h, flags, words[0], words[1] if flags == 0 else None,
               words[2] if flags == 1 else None, words[3] if flags == 0 else None, words[4], self.ptr(words[5]))
        r = self.nxt("lc_ret")
        if r == 0:
            count = min(words[3], 4) if flags == 0 else 1
            for i in range(count):
                self._control(words[5] + i * 0x94)
        return r

    def h_mixerGetControlDetails(self, a):
        h, d, flags = a
        self.L("mixerGetControlDetails", h, flags, self.rd(d, 0x18))
        r = self.nxt("gcd_ret")
        if r == 0:
            self.w32(self.u32(d + 0x14), self.nxt("gcd_val", 0x8000))
        return r

    def h_mixerSetControlDetails(self, a):
        h, d, flags = a
        self.L("mixerSetControlDetails", h, flags, self.rd(d, 0x18), self.rd(self.u32(d + 0x14), 32))
        return 0

    def h_auxGetNumDevs(self, a):
        self.L("auxGetNumDevs")
        return self.nxt("aux_devs", 0)

    def h_auxGetDevCaps(self, a):
        dev, caps, size = a
        self.L("auxGetDevCaps", dev, self.ptr(caps), size)
        r = self.nxt("aux_caps")
        self.uc.mem_write(caps, random.Random(dev + 99).randbytes(0x30))
        self.uc.mem_write(caps + 0x28, struct.pack("<H", self.nxt("aux_tech", 1)))
        self.w32(caps + 0x2C, self.nxt("aux_support", 1))
        return r

    def h_auxGetVolume(self, a):
        self.L("auxGetVolume", a[0], self.ptr(a[1]))
        r = self.nxt("aux_getvol")
        if r == 0:
            self.w32(a[1], self.nxt("aux_vol", 0x4000))
        return r

    def h_auxSetVolume(self, a):
        self.L("auxSetVolume", *a)
        return 0

    def _interlocked(self, name, a, d):
        self.L(name, a[0])
        if self.nxt("lc_clear"):  # the locale handle reset meanwhile (it is read again after the lock)
            self.w32(0x4DB37C, 0)
        v = (self.u32(a[0]) + d) & 0xFFFFFFFF
        self.w32(a[0], v)
        return v

    def h_InterlockedIncrement(self, a):
        return self._interlocked("inc", a, 1)

    def h_InterlockedDecrement(self, a):
        return self._interlocked("dec", a, -1)

    def h_DirectSoundCreate(self, a):
        self.L("DirectSoundCreate", a[0], self.ptr(a[1]), a[2])
        return self.nxt("ds", 0)


def rnd(rng, n):
    return [rng.getrandbits(32) for _ in range(n)]


def script_for(rng):
    def lst(f, n=10):
        return [f() for _ in range(n)]
    s = {
        "fopen": lst(lambda: pick(rng, FP, FP, FP, 0)),
        "fread": lst(lambda: rng.getrandbits(32)),
        "fread_data": lst(lambda: rng.getrandbits(16)),
        "vsn_len": lst(lambda: pick(rng, 0, 5, 40, 0x7CD, 0x7CE, 0x7CF, 0x900)),
        "lcmap": lst(lambda: pick(rng, 0, 5, 12, 0x40)),
        "fpret": lst(lambda: rng.uniform(-10, 10)),
        "mci": lst(lambda: pick(rng, 0, 0, 0, 0, 0x101, rng.getrandbits(32))),
        "mci_dev": lst(lambda: rng.getrandbits(16)),
        "mci_val": lst(lambda: pick(rng, 0, 1, 2, 5, 12, 33, 40, 0x20C, 0x20D, 0x20E, 0x212, rng.getrandbits(32))),
        "time": lst(lambda: rng.getrandbits(32)),
        "mixer_open": lst(lambda: pick(rng, 0, 0, 0, 0, 1)),
        "handle": lst(lambda: rng.getrandbits(32)),
        "mixer_devs": lst(lambda: pick(rng, 0, 1, 1, 2, 3)),
        "mixer_caps": lst(lambda: pick(rng, 0, 0, 0, 1)),
        "mixer_dests": lst(lambda: pick(rng, 0, 1, 2, 3)),
        "line_ret": lst(lambda: pick(rng, 0, 0, 0, 1)),
        "line_id": lst(lambda: rng.getrandbits(32)),
        "line_type": lst(lambda: pick(rng, 4, 5, 0x1005, 0x1005, 3, 6, 0x1004)),
        "line_ch": lst(lambda: pick(rng, 1, 2, 2, 0, 3)),
        "line_conns": lst(lambda: pick(rng, 0, 1, 2, 3)),
        "line_ctls": lst(lambda: pick(rng, 0, 1, 2, 3, 4)),
        "ctl_seed": lst(lambda: rng.getrandbits(16)),
        "ctl_id": lst(lambda: rng.getrandbits(32)),
        "ctl_type": lst(lambda: pick(rng, 0x50030001, 0x50030001, 0x50000000, 0x40000000, 0x50030002,
                                     rng.getrandbits(32))),
        "ctl_flags": lst(lambda: pick(rng, 0, 1, 2, 3, 0x80000000, rng.getrandbits(32))),
        "ctl_items": lst(lambda: pick(rng, 0, 1, 2, 3)),
        "ctl_min": lst(lambda: pick(rng, 0, 0, 100, 0x8000, rng.getrandbits(32))),
        "ctl_max": lst(lambda: pick(rng, 0xFFFF, 0xFFFF, 100, 0, 0xFFFFFFFF, rng.getrandbits(32))),
        "lc_ret": lst(lambda: pick(rng, 0, 0, 0, 1)),
        "gcd_ret": lst(lambda: pick(rng, 0, 0, 0, 1)),
        "gcd_val": lst(lambda: pick(rng, 0, 0x8000, 0xFFFF, rng.getrandbits(32))),
        "aux_devs": lst(lambda: pick(rng, 0, 1, 2, 3, 4)),
        "aux_caps": lst(lambda: pick(rng, 0, 0, 0, 1)),
        "aux_tech": lst(lambda: pick(rng, 1, 1, 1, 2, 0x101)),
        "aux_support": lst(lambda: pick(rng, 0, 1, 2, 3, rng.getrandbits(32))),
        "aux_getvol": lst(lambda: pick(rng, 0, 0, 0, 1)),
        "aux_vol": lst(lambda: rng.getrandbits(32)),
        "ds": lst(lambda: rng.getrandbits(32)),
        "lc_clear": lst(lambda: pick(rng, 0, 0, 0, 1)),
    }
    if rng.randrange(3):  # malloc failures now and then
        s["malloc"] = [pick(rng, HEAP + i * 0x800, HEAP + i * 0x800, HEAP + i * 0x800, 0) for i in range(8)]
    return s


PATHS = [b"rollcage.img", b"data\\rollcage.img", b"D:\\R.IMG", b"a.b.img", b"x.i", b"c:\\dbug", b"log.txt",
         b"..\\out\\file.ext"]


def make_state(rng, pristine):
    mem = {a: bytearray(pristine[a]) if a != SCR else bytearray(rng.randbytes(SCR_SIZE)) for a, n in REGIONS}

    def put(addr, fmt, v):
        for a, n in REGIONS:
            if a <= addr < a + n:
                if isinstance(v, int):
                    v &= (1 << (8 * struct.calcsize(fmt))) - 1
                    fmt = fmt.upper()
                struct.pack_into(fmt, mem[a], addr - a, v)
                return
        raise KeyError(hex(addr))

    def putb(addr, data):
        for a, n in REGIONS:
            if a <= addr < a + n:
                mem[a][addr - a:addr - a + len(data)] = data
                return
        raise KeyError(hex(addr))

    # debug log
    putb(0x4B4848, pick(rng, *PATHS) + b"\0")
    put(0x4B494C, "<i", pick(rng, 0, 1, 1, 1))
    put(0x4B4950, "<i", pick(rng, 0, 1))
    put(0x4DB1C8, "<I", pick(rng, 0, FP + 0x10))
    put(0x4DB1CC, "<i", pick(rng, 0, 1))
    # allocator pointers, archive
    alloc = pick(rng, (0x4982C2, 0x49827A), (0x4445E0, 0x444570))
    put(0x4B4A84, "<I", alloc[0])
    put(0x4B4A88, "<I", alloc[1])
    put(0x4B4A8C, "<H", pick(rng, 0, 1, 0xFFFF, 2, rng.getrandbits(16)))
    put(0x4DB1DC, "<I", pick(rng, FP + 0x20, 0))
    pos = pick(rng, 0, rng.randrange(0, 0x100000), rng.getrandbits(32) - 0x80000000)
    put(0x6D0F00, "<i", pos)
    put(0x6D0F04, "<h", 32)
    put(0x6D0F08, "<I", IDX)
    idx = bytearray()
    for i in range(32):
        stored = pick(rng, 0, 0x10, rng.randrange(0, 0x300))
        unpacked = pick(rng, stored, stored, rng.randrange(0, 0x400))
        nxt = pick(rng, 0, 0, rng.randrange(i + 1, 33) % 32 if i < 31 else 0)
        if nxt <= i:
            nxt = 0
        offset = pick(rng, rng.randrange(0, 0x100000), rng.getrandbits(32),
                      (pos + 0x100) & 0xFFFFFFFF, (pos - 0x100) & 0xFFFFFFFF, pos & 0xFFFFFFFF)  # equal distances
        idx += struct.pack("<IIIhh", offset,
                           stored, unpacked, pick(rng, 0, 4, 16, rng.randrange(-16, 17)), nxt)
    putb(IDX, idx)
    # CD player
    put(0x4B4A90, "<i", pick(rng, 0, 1, 2, 5, 33, -1, rng.getrandbits(32)))
    put(0x4B4A94, "<i", pick(rng, 0, 1, 2, 5, 33, -1, rng.getrandbits(32)))
    put(0x4B4A98, "<i", pick(rng, 0, 1))
    put(0x4DB224, "<I", rng.getrandbits(16))
    put(0x4DB238, "<i", pick(rng, -1, 0, 1, 2, 5, 20, 32, 40, rng.getrandbits(32)))
    put(0x4DB250, "<i", pick(rng, 0, 1, 2, 3, 2, 3, 4))
    put(0x4DB254, "<i", pick(rng, 0, 0, 0, 1))
    put(0x4DB258, "<i", pick(rng, 0, 1, 2, 2, 3))
    put(0x4DB25C, "<i", pick(rng, 0, 1))
    put(0x4DB260, "<I", pick(rng, 0, 0xFFFFFFFF, 0xFFFFFFFE, rng.getrandbits(32)))
    put(0x4DB264, "<I", rng.getrandbits(32))
    # aux table
    put(0x4DB268, "<i", pick(rng, 0, 1, 1))
    put(0x4DB26C, "<i", pick(rng, 0, 1, 2, 3, -1))
    put(0x4DB270, "<I", pick(rng, 0, AUXT, AUXT))
    # mixer list
    nodes = pick(rng, 0, 1, 2, 3, 4)
    for i in range(nodes):
        n = MIX + i * 0x80
        cnt = pick(rng, 0, 1, 2, 4, 8)
        put(n, "<I", MIX + (i + 1) * 0x80 if i + 1 < nodes else 0)
        put(n + 4, "<I", rng.getrandbits(8))
        put(n + 0x14, "<I", pick(rng, 0, 100, rng.getrandbits(32)))
        put(n + 0x1C, "<I", pick(rng, 0xFFFF, 100, 0, rng.getrandbits(32)))
        put(n + 0x34, "<I", MIX + 0x1000 + i * 0x40)
        put(n + 0x38, "<I", MIX + 0x1000 + i * 0x40)
        put(n + 0x3C, "<I", MIX + 0x2000 + i * 0x40)
        put(n + 0x48, "<I", cnt)
        put(n + 0x4C, "<I", cnt * 4)
    put(0x4DB278, "<I", MIX if nodes and rng.randrange(4) else 0)
    put(0x4DB27C, "<I", MIX + max(nodes - 1, 0) * 0x80)
    put(0x4DB280, "<I", pick(rng, nodes, nodes, nodes + 1, max(nodes - 1, 0), 0))
    put(0x4DB284, "<i", pick(rng, 0, 1))
    put(0x4DB1EC, "<i", pick(rng, 0, 1))
    put(0x4DB208, "<i", pick(rng, 0, 1))
    # locale
    put(0x4DB37C, "<I", pick(rng, 0, 0, 0, 0x409, rng.getrandbits(32)))
    put(0x6D1248, "<i", pick(rng, 0, 1))
    put(0x6D124C, "<i", rng.randrange(-2, 3))
    # strings
    s = bytes(rng.choice(b"abcxyzABCXYZ019_.\\ \x80\xe9\xc9") for _ in range(rng.randrange(0, 40)))
    putb(STR, s + b"\0")
    putb(STR + 0x100, pick(rng, *PATHS[:6]) + b"\0")
    putb(STR + 0x200, b"%s %d\0")
    return mem


def make_args(rng, kinds):
    args = []
    for k in kinds:
        if k == "S":
            args.append(pick(rng, STR + 0x100, STR + 0x100, 0))
        elif k == "F":
            args.append(STR + 0x200)
        elif k == "P":
            args.append(STR + 0x100)
        elif k == "s":
            args.append(STR)
        elif k == "c":
            args.append(pick(rng, 0x158, 1, 0, 32, -1, rng.getrandbits(32)))
        elif k == "w":
            args.append(pick(rng, 0, 1, 2, rng.randrange(32), rng.randrange(32) | rng.getrandbits(16) << 16,
                             0xFFFF, rng.getrandbits(32)))
        elif k == "e":
            args.append(pick(rng, rng.randrange(32), rng.randrange(32), rng.randrange(32) | 0x12340000))
        elif k == "b":
            args.append(pick(rng, 0, BUFS))
        elif k == "p":
            args.append(pick(rng, 0, BUFS, rng.getrandbits(32)))
        elif k == "t":
            args.append(pick(rng, 0, 1, 2, 5, 20, -1, rng.getrandbits(32)))
        elif k == "v":
            args.append(pick(rng, 0, 0xFFFF, 0x10000, -1, 0x8000, rng.randrange(0, 0x10000), rng.getrandbits(32)))
        elif k == "n":
            args.append(pick(rng, 0, 1, -1, 2, 5, 0x20, 0x7F, -5, rng.getrandbits(32)))
        elif k == "r":
            args.append(pick(rng, 0, 1, 0, rng.getrandbits(32)))
        elif k == "m":
            args.append(pick(rng, 0x271A, 0x2724, 0x272E, 0x271A, 0x2710, rng.getrandbits(32)))
        elif k == "d":
            args.append(pick(rng, 0, 1, 2, 5))
        elif k == "C":
            args.append(CAPS)
        elif k == "V":
            args.append(pick(rng, 0, CAPS + 0x100))
        elif k == "h":
            args.append(rng.getrandbits(32))
        else:
            args.append(pick(rng, 0, 1, 2, 3, rng.randrange(0, 0x100), rng.getrandbits(32)))
    return args


# name: (address, argument kinds, return mask (None = void))
TESTS = {
    "debug_log_set_path": (0x4962C0, "S", None),
    "debug_log_printf": (0x496340, "Fiii", None),
    "debug_log_print": (0x4963C0, "Fiii", None),
    "debug_log_close": (0x496410, "", None),
    "archive_open": (0x496430, "Pc", None),
    "archive_select": (0x496510, "w", None),
    "archive_get_record": (0x496570, "w", 0xFFFFFFFF),
    "archive_entry_load": (0x496590, "eb", 0xFFFFFFFF),
    "archive_free": (0x496660, "p", None),
    "archive_state": (0x496680, "", 0xFFFF),
    "cdaudio_open": (0x496690, "", None),
    "cdaudio_device_open": (0x4966B0, "", 0xFFFFFFFF),
    "cdaudio_close": (0x496700, "", None),
    "cdaudio_device_close": (0x496730, "", 0xFFFFFFFF),
    "cdaudio_play": (0x496770, "t", None),
    "cdaudio_stop": (0x4967A0, "", None),
    "cdaudio_pause": (0x4967F0, "", None),
    "cdaudio_resume": (0x496890, "", None),
    "cdaudio_poll": (0x4968B0, "", None),
    "cdaudio_play_track": (0x496920, "r", None),
    "cdaudio_choose_track": (0x496A90, "r", 0xFFFFFFFF),
    "cdaudio_random": (0x496BE0, "n", 0xFFFFFFFF),
    "cdaudio_set_shuffle": (0x496C50, "i", None),
    "cdaudio_set_skip_first": (0x496C80, "i", None),
    "cdaudio_set_volume": (0x496C90, "vv", None),
    "aux_set_volume": (0x496D00, "vv", None),
    "aux_scan": (0x496D70, "", None),
    "aux_query": (0x496E50, "dCV", 0xFFFFFFFF),
    "aux_free": (0x496ED0, "", None),
    "mixer_set_volume": (0x496F30, "rvv", None),
    "mixer_scan": (0x497040, "m", 0xFFFFFFFF),
    "mixer_scan_device": (0x4970C0, "d", 0xFFFFFFFF),
    "mixer_scan_destination": (0x497160, "dhd", 0xFFFFFFFF),
    "mixer_scan_source": (0x4971F0, "dhdd", 0xFFFFFFFF),
    "mixer_check_control": (0x497320, "dhddCdd", 0xFFFFFFFF),
    "mixer_add_control": (0x497410, "dhdddddC", 0xFFFFFFFF),
    "crt_dsound_create": (0x4A1C36, "hhh", 0xFFFFFFFF),
    "crt_strlwr": (0x4A1C3C, "s", 0xFFFFFFFF),
    "crt_strupr": (0x4A1D70, "s", 0xFFFFFFFF),
    "crt_hypot": (0x4A1EA4, "DD", "fp"),
}


def rand_double(rng):
    k = rng.randrange(10)
    if k == 0:
        return struct.pack("<d", pick(rng, 0.0, -0.0, 1.0, -1.0, 3.0, 4.0, 1e-310, -5e-324, 1e308, -1e308,
                                      float("inf"), float("-inf"), float("nan")))
    if k == 1:  # signalling NaN, odd NaNs, denormals
        return struct.pack("<Q", pick(rng, 0x7FF0000000000001, 0xFFF4000000000000, 0x7FF8000000000001,
                                      rng.getrandbits(52), rng.getrandbits(52) | 1 << 63))
    if k == 2:
        return struct.pack("<Q", rng.getrandbits(64))
    if k < 6:  # single-precision values, as the game passes
        return struct.pack("<d", struct.unpack("<f", struct.pack("<f", rng.uniform(-2000, 2000)))[0])
    return struct.pack("<d", rng.uniform(-1e6, 1e6) * pick(rng, 1, 1e-3, 1e3, 1e150, 1e-160))


def call(m, address, args, fp):
    esp0 = E.STACK + E.STACK_SIZE - 0x100
    frame = struct.pack("<I", RET_FP if fp else RET) + b"".join(
        a if isinstance(a, bytes) else struct.pack("<I", a & 0xFFFFFFFF) for a in args)
    m.uc.mem_write(esp0, frame)
    m.uc.reg_write(UC_X86_REG_ESP, esp0)
    m.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
    try:
        m.uc.emu_start(address, RET_FP + 6 if fp else RET, count=5000000)
    except UcError as e:
        return ("error", str(e))
    eip = m.uc.reg_read(UC_X86_REG_EIP)
    if eip != (RET_FP + 6 if fp else RET):
        return ("cut off", hex(eip))
    esp = m.uc.reg_read(UC_X86_REG_ESP) - esp0
    if fp:
        return ("ok", m.read(RESULT, 8).hex(), m.uc.reg_read(UC_X86_REG_FPCW), esp)
    return ("ok", m.uc.reg_read(UC_X86_REG_EAX), esp)


def run_case(m, stubs, pristine, name, address, kinds, mask, rng):
    mem = make_state(rng, pristine)
    script = script_for(rng)
    if mask == "fp":
        args = [rand_double(rng), rand_double(rng)]
        if rng.randrange(5) == 0:  # zero, over- and underflow
            pair = pick(rng, (0.0, -0.0), (-0.0, 0.0), (1e308, 1e308), (-1.5e308, 1e308), (1e-320, 3e-321),
                        (5e-324, 0.0), (1e-310, 1e-310))
            args = [struct.pack("<d", pair[0]), struct.pack("<d", pair[1])]
        cw = pick(rng, 0x27F, 0x27F, 0x37F, 0x7F, 0x25F)
    else:
        args = make_args(rng, kinds)
        cw = 0x27F
    results = []
    for target in (address, m.symbols[name]):
        m.uc.reg_write(UC_X86_REG_FPCW, cw)
        m.uc.reg_write(UC_X86_REG_FPSW, 0)
        m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        m.write(E.STACK, bytes(E.STACK_SIZE))
        for a, data in mem.items():
            m.write(a, bytes(data))
        stubs.patch_imports()
        stubs.reset(script)
        r = call(m, target, args, mask == "fp")
        if r[0] == "ok" and mask != "fp":
            r = ("ok", None if mask is None else r[1] & mask, r[2])
        out = b"".join(m.read(a, n) for a, n in REGIONS)
        results.append((r, out, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            off = first
            for ra, n in REGIONS:
                if off < n:
                    where = f" (memory differs at {ra + off:#x})"
                    break
                off -= n
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {str(la[k:k + 3])[:400]}\n  ours:          {str(lb[k:k + 3])[:400]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}{where}{logs}"
    return None


# original ranges whose instruction coverage -c reports
COVER = [(0x496260, 0x496f30), (0x496F30, 0x49701A), (0x497020, 0x497628), (0x4977E0, 0x49785F),
         (0x4A1C36, 0x4A1EC1), (0x4A1EC1, 0x4A2146)]


def report_coverage(covered):
    import re
    asm = __file__.rsplit("/", 2)[0] + "/build/rollcage.asm"
    insns = []
    for line in open(asm):
        mt = re.match(r"\s*([0-9a-f]+):\s+(\w+)", line)
        if mt and mt.group(2) != "nop":
            insns.append(int(mt.group(1), 16))
    for lo, hi in COVER:
        want = [a for a in insns if lo <= a < hi]
        miss = [a for a in want if a not in covered]
        print(f"coverage {lo:#x}-{hi:#x}: {len(want) - len(miss)}/{len(want)}"
              + (f" missing {' '.join(hex(a) for a in miss[:40])}" if miss else ""))


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    m.uc.mem_map(CODE, 0x2000)
    m.write(CODE, b"\xf4" * 0x1000)
    m.write(RET, b"\xf4")
    m.write(RET_FP, b"\xdd\x1d" + struct.pack("<I", RESULT) + b"\xf4")
    m.write(SNIP, b"\xdd\x05" + struct.pack("<I", CONST) + b"\xc3")
    pristine = {a: m.read(a, n) for a, n in REGIONS if a != SCR}
    stubs = Stubs(m)
    covered = set()
    if "-c" in sys.argv:  # instruction coverage of the original (ours runs at 0x10000000+)
        m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, _: covered.add(a), begin=0x496000, end=0x4A2200)
    rng = random.Random(23)
    failed = 0
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    for name, (address, kinds, mask) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, pristine, name, address, kinds, mask, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    if "-c" in sys.argv:
        report_coverage(covered)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
