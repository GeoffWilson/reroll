#!/usr/bin/env python3
"""Differential test of crt_cb.c (the C runtime entry points 0x497000-0x4a0000:
strings, ctype, sprintf / vsprintf, rand, malloc / calloc / free, __allmul,
_ftol, _chkstk, the x87 maths, the path helpers, clock, exit, _beginthread)
against the original, on random arguments and state.

Original runtime functions that ours still calls by address (_getptd, _errno,
__doserrno, _dosmaperr, _lock / _unlock, _toupper_lk / _tolower_lk,
__crtGetStringTypeA, _initptd, the exit lock) and the CD helpers are logging
stubs in both runs; every import used is a logging stdcall stub (its slot in
the IAT points at a stub address). Internal runtime code both sides reach
(_87except and its exception handling, _powhlp, _mbsdec, _mbsnbcpy) runs for
real. The allocator tests set the small-block heap threshold (0x4b5184) to 0
so the original takes its HeapAlloc path for every size (ours never uses the
small-block heap; its _lock(9) / _unlock(9) are dropped from the logs).

The x87 tests set the control word and the precision flag, call the function
with st(0) / stack arguments as the original's callers do and compare st(0)
(80 bits), the control word, the stack top, the exception flags and errno.

    tests/emu_crt_cb.py EXE build/emu_cb.elf [iterations] [names...] [-v]
"""
import math
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EIP  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x40000
STR_A = SCR
STR_B = SCR + 0x1000
OUT = SCR + 0x2000  # 0x2000 bytes
ARGS = SCR + 0x4000
PTD = SCR + 0x5000
ERRNO = SCR + 0x5100
DOSERRNO = SCR + 0x5104
GST_OUT = SCR + 0x5108
FPIO = SCR + 0x5200  # 0 cw in, 0x10 value in (tbyte), 0x20 three, 0x30 value out, 0x40 sw out, 0x44 cw out
HEAP = SCR + 0x6000  # HeapAlloc results
WSTR = SCR + 0x8000
TABLES = SCR + 0x9000
CODE = 0x00310000
IATSTUB = 0x00320000
CALLBACK = 0x00330000  # callback stubs (new handler, atexit functions)

DATA_REGIONS = [(0x4B4A90, 0x20), (0x4B4D00, 0xB0), (0x4B5180, 0x10), (0x4DB1E0, 0x220), (0x6D1000, 0x300), (0x6D2280, 0x20)]

# IAT slot: (name, stdcall argument count)
IAT = {
    0x4A31C4: ("HeapAlloc", 3),
    0x4A31C0: ("HeapFree", 3),
    0x4A31AC: ("InterlockedIncrement", 1),
    0x4A31C8: ("InterlockedDecrement", 1),
    0x4A310C: ("GetLastError", 0),
    0x4A31A8: ("SetCurrentDirectoryA", 1),
    0x4A31B8: ("GetCurrentDirectoryA", 2),
    0x4A30EC: ("SetEnvironmentVariableA", 2),
    0x4A31A4: ("GetFileAttributesA", 1),
    0x4A31BC: ("GetFullPathNameA", 4),
    0x4A317C: ("GetSystemTimeAsFileTime", 1),
    0x4A3148: ("CreateThread", 6),
    0x4A3194: ("ResumeThread", 1),
    0x4A3198: ("GetCurrentProcess", 0),
    0x4A319C: ("TerminateProcess", 2),
    0x4A31A0: ("ExitProcess", 1),
    0x4A3134: ("WideCharToMultiByte", 8),
    0x4A30F4: ("RaiseException", 4),
    0x4A3174: ("EnterCriticalSection", 1),
    0x4A3170: ("LeaveCriticalSection", 1),
}

# original function stubs: address: (name, cdecl argument count)
STUBS = {
    0x49BCF1: ("_getptd", 0),
    0x49BCDE: ("_initptd", 1),
    0x49D48E: ("_errno", 0),
    0x49D497: ("__doserrno", 0),
    0x49D41B: ("_dosmaperr", 1),
    0x49B161: ("_lock", 1),
    0x49B1C2: ("_unlock", 1),
    0x4985AF: ("_toupper_lk", 1),
    0x4986EA: ("_tolower_lk", 1),
    0x49AEE3: ("__crtGetStringTypeA", 7),
    0x498D50: ("_lockexit", 0),
    0x498D59: ("_unlockexit", 0),
    0x498FCD: ("_amsg_exit", 1),
    0x497040: ("mixer_scan", 1),
    0x496E50: ("aux_query", 3),
}

# kinds of call: "c" plain cdecl, "f" x87 (st(0) in or out), "k" _chkstk
TESTS = {
    "cd_volume_get": (0x497630, "c"),
    "cd_volume_available": (0x4977B0, "c"),
    "msvc_strcpy": (0x498450, "c"),
    "msvc_strcat": (0x498460, "c"),
    "msvc_strncpy": (0x498350, "c"),
    "msvc_strncmp": (0x4993F0, "c"),
    "msvc_strstr": (0x4980D0, "c"),
    "msvc_isctype": (0x498051, "c"),
    "msvc_toupper": (0x498540, "c"),
    "msvc_tolower": (0x49867B, "c"),
    "msvc_atoi": (0x498046, "c"),
    "msvc_rand": (0x4987B5, "c"),
    "msvc_malloc": (0x4982C2, "c"),
    "msvc_calloc": (0x49946C, "c"),
    "msvc_free": (0x49827A, "c"),
    "msvc_allmul": (0x498180, "c"),
    "msvc_clock": (0x499016, "c"),
    "msvc_sprintf": (0x4979F0, "c"),
    "msvc_vsprintf": (0x498C0B, "c"),
    "msvc_vsnprintf": (0x49972A, "c"),
    "sprintf_float": (0x4979F0, "c"),
    "msvc_makepath": (0x498834, "c"),
    "msvc_splitpath": (0x4988C5, "c"),
    "msvc_getcwd": (0x498A0C, "c"),
    "msvc_chdir": (0x498B3F, "c"),
    "msvc_access": (0x498BC5, "c"),
    "msvc_exit": (0x498C89, "c"),
    "msvc_beginthread": (0x498D7C, "c"),
    "msvc_ftol": (0x497A80, "f"),
    "msvc_chkstk": (0x497A50, "k"),
    "msvc_cos": (0x497AC4, "f"),
    "msvc_sin": (0x497B74, "f"),
    "msvc_atan": (0x497C24, "f"),
    "msvc_sqrt": (0x4981D4, "f"),
    "msvc_pow": (0x497CC9, "f"),
}


SYMBOL = {"sprintf_float": "msvc_sprintf"}
VOID = {"msvc_makepath", "msvc_splitpath", "msvc_free", "msvc_exit"}


def pick(rng, *choices):
    return rng.choice(choices)


def s32(v):
    return v & 0xFFFFFFFF


class Env:
    """Stubs and logs shared by both runs."""

    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.filter_lock9 = False
        m.uc.mem_map(SCR, SCR_SIZE)
        m.uc.mem_map(CODE, 0x1000)
        m.uc.mem_map(IATSTUB, 0x1000)
        m.uc.mem_write(IATSTUB, b"\xf4" * 0x1000)
        m.uc.mem_map(CALLBACK, 0x1000)
        m.uc.mem_write(CALLBACK, b"\xf4" * 0x1000)
        self.iat_at = {}
        for i, (slot, (name, nargs)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.iat_at[address] = (name, nargs)
            m.uc.hook_add(UC_HOOK_CODE, self._iat, begin=address, end=address)
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._stub, begin=address, end=address)
        m.uc.hook_add(UC_HOOK_CODE, self._callback, begin=CALLBACK, end=CALLBACK + 0xFFF)
        self.pristine = {a: bytes(m.read(a, n)) for a, n in DATA_REGIONS}

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def next(self, name, default=0):
        v = self.script.get(name)
        return v.pop(0) if v else default

    def _args(self, uc, n):
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (n + 1), bytes(uc.mem_read(esp, 4 * (n + 1))))
        return esp, words[0], list(words[1:])

    def _ret(self, uc, esp, ret, pop, eax):
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def _content(self, uc, a):
        if E.STACK <= a < E.STACK + E.STACK_SIZE or SCR <= a < SCR + SCR_SIZE:
            raw = bytes(uc.mem_read(a, 16))
            return ("ptr", raw.split(b"\0")[0]) if a < SCR else ("ptr", a, raw)
        return a

    def _iat(self, uc, address, size, _):
        name, nargs = self.iat_at[address]
        esp, ret, args = self._args(uc, nargs)
        shown = list(args)
        eax = 0
        if name == "HeapAlloc":
            eax = self.next(name, 0)
        elif name == "HeapFree":
            eax = 1
        elif name in ("InterlockedIncrement", "InterlockedDecrement", "EnterCriticalSection",
                      "LeaveCriticalSection"):
            pass
        elif name == "GetCurrentDirectoryA":
            text = self.next("cwd", b"C:\\GAMES")
            length = self.next("cwd_len", len(text))
            if len(text) < args[0]:
                uc.mem_write(args[1], text + b"\0")
            shown[1] = "buf"
            eax = length
        elif name == "GetSystemTimeAsFileTime":
            uc.mem_write(args[0], struct.pack("<Q", self.next("time", 0)))
            shown[0] = "buf"
        elif name == "CreateThread":
            uc.mem_write(args[5], struct.pack("<I", 0x1234))
            eax = self.next(name, 0)
        elif name == "WideCharToMultiByte":
            eax = self.next(name, 0)
            wc = struct.unpack("<H", bytes(uc.mem_read(args[2], 2)))[0]
            if eax > 0:
                uc.mem_write(args[4], bytes([0x30 + (wc & 7)] * eax))
            uc.mem_write(args[7], struct.pack("<I", self.next("used", 0)))
            shown = [args[0], args[1], wc, args[3], "out", args[5], args[6], "used"]
        elif name == "GetCurrentProcess":
            eax = 0xFFFFFFFF
        else:
            eax = self.next(name, 0)
        if name in ("SetCurrentDirectoryA", "GetFileAttributesA", "SetEnvironmentVariableA"):
            shown = [self._content(uc, a) for a in args]
        self.log.append((name, tuple(shown)))
        self._ret(uc, esp, ret, 4 * nargs, eax)

    def _stub(self, uc, address, size, _):
        name, nargs = STUBS[address]
        esp, ret, args = self._args(uc, nargs)
        eax = 0
        if name == "_getptd":
            eax = PTD
        elif name == "_errno":
            eax = ERRNO
        elif name == "__doserrno":
            eax = DOSERRNO
        elif name == "aux_query":
            uc.mem_write(args[1] + 0x2C, struct.pack("<I", self.next("aux_caps", 0)))
            uc.mem_write(args[2], struct.pack("<I", self.next("aux_volume", 0)))
            args = [args[0], "caps", "volume"]
            eax = self.next(name, -1)
        elif name == "__crtGetStringTypeA":
            uc.mem_write(args[3], struct.pack("<H", self.next("gst_out", 0)))
            raw = bytes(uc.mem_read(args[1], 4))
            args = [args[0], raw[:args[2]], args[2], "out"] + args[4:]
            eax = self.next(name, 1)
        else:
            eax = self.next(name, 0)
        if self.filter_lock9 and name in ("_lock", "_unlock") and args[0] == 9:
            pass
        else:
            self.log.append((name, tuple(args)))
        self._ret(uc, esp, ret, 0, eax)

    def _callback(self, uc, address, size, _):
        esp, ret, args = self._args(uc, 1)
        self.log.append(("callback", address - CALLBACK, args[0] if address == CALLBACK + 0x10 else None))
        self._ret(uc, esp, ret, 0, self.next("callback", 0))


# ---- random data ----

ALPHA = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"


def rand_string(rng, n=None, alphabet=None):
    if n is None:
        n = pick(rng, 0, 1, 2, 3, rng.randrange(0, 12), rng.randrange(0, 40), rng.randrange(0, 200))
    if alphabet is None:
        alphabet = pick(rng, b"ab", b"abc", ALPHA, bytes(range(1, 256)))
    return bytes(rng.choice(alphabet) for _ in range(n))


def rand_double(rng):
    k = rng.randrange(14)
    if k == 0:
        return struct.pack("<d", rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 0.125, 0.375, 2.5, 0.005, 0.015, 1e-5, 9.995,
                                             99.995, 0.05, 1e21, 1e-300, 123456789.0, 1e17, 0.3, 2.0 / 3]))
    if k == 1:
        return struct.pack("<d", rng.uniform(-1000, 1000))
    if k == 2:
        return struct.pack("<d", rng.uniform(-1, 1))
    if k == 3:
        return struct.pack("<d", float(rng.randrange(-100000, 100000)) / 100)
    if k == 4:
        return struct.pack("<d", float(rng.randrange(-100000, 100000)) / 8)
    if k == 5:
        return struct.pack("<d", rng.uniform(-1, 1) * 10 ** rng.randrange(-30, 30))
    if k == 6:
        return struct.pack("<d", float(struct.unpack("<f", struct.pack("<f", rng.uniform(-500, 500)))[0]))
    if k == 7:
        return struct.pack("<Q", rng.getrandbits(64))
    if k == 8:  # special values
        hi = rng.choice([0x7FF00000, 0xFFF00000, 0x7FF80000, 0xFFF80000, 0x7FF40000, 0x00000000, 0x80000000,
                         0x000FFFFF, 0x7FEFFFFF])
        lo = rng.choice([0, 0, 1, 0xFFFFFFFF])
        return struct.pack("<II", lo, hi)
    if k == 9:  # near halfway in decimal
        d = rng.randrange(1, 6)
        n = rng.randrange(0, 10 ** 6)
        return struct.pack("<d", (n + 0.5) / 10 ** d)
    if k == 10:
        return struct.pack("<d", rng.uniform(0, 100))
    if k == 11:
        return struct.pack("<d", 10.0 ** rng.randrange(-20, 25) * rng.choice([1, -1, 9.99999999, 0.999999]))
    if k == 12:
        return struct.pack("<d", rng.uniform(-1e6, 1e6))
    return struct.pack("<d", math.ldexp(rng.uniform(-10, 10), rng.randrange(-1070, 1020)))


def quiet(raw):
    """Signalling NaNs made quiet: the original's x87 load quiets them on the
    hardware, the emulator does not (ours follows the hardware)."""
    lo, hi = struct.unpack("<II", raw)
    if hi & 0x7FF00000 == 0x7FF00000 and (hi & 0xFFFFF or lo):
        hi |= 0x80000
    return struct.pack("<II", lo, hi)


def rand_format(rng, args, wide_ok=True):
    """A random printf format and its argument words (appended to args)."""
    out = []
    for _ in range(rng.randrange(1, 6)):
        if rng.randrange(3) == 0:
            out.append(rand_string(rng, rng.randrange(0, 6), b"abc XYZ:.\\-01").replace(b"%", b""))
            continue
        spec = b"%"
        for f in b"-+ #0":
            if rng.randrange(6) == 0:
                spec += bytes([f])
        r = rng.randrange(6)
        if r == 1:
            spec += str(rng.randrange(0, 25)).encode()
        elif r == 2:
            spec += b"*"
            args.append(s32(pick(rng, rng.randrange(-30, 30), 0, 5)))
        r = rng.randrange(5)
        if r == 1:
            spec += b"." + str(rng.randrange(0, 22)).encode()
        elif r == 2:
            spec += b"."
        elif r == 3:
            spec += b".*"
            args.append(s32(pick(rng, rng.randrange(-5, 25), -1, 0)))
        conv = rng.choice("ddiuxXocspeEfgGfffgn%sS" + ("CZ" if wide_ok else ""))
        size = b""
        if conv in "diuxXo" and rng.randrange(3) == 0:
            size = rng.choice([b"h", b"l", b"I64", b"I", b"w", b"L", b"F"])
        elif conv in "cs" and rng.randrange(4) == 0:
            size = rng.choice([b"l", b"w", b"h"])
        spec += size + conv.encode()
        if conv in "diuxXoc":
            if size == b"I64":
                v = rng.getrandbits(64) if rng.randrange(2) else rng.randrange(-1000, 1000) & (2 ** 64 - 1)
                args += [v & 0xFFFFFFFF, v >> 32]
            else:
                args.append(s32(pick(rng, rng.randrange(-100, 100), rng.getrandbits(32), 0, 0x41, 0x7FFFFFFF,
                                     0x80000000, rng.randrange(0, 70000))))
        elif conv == "C":
            args.append(pick(rng, 0x41, 0x7A, 0x100, 0xE9, rng.getrandbits(16)))
        elif conv in "sS":
            wide = conv == "S" and size != b"h" or size in (b"l", b"w")
            args.append(pick(rng, 0, STR_A if not wide else WSTR, STR_B if not wide else WSTR + 2))
        elif conv == "Z":
            args.append(pick(rng, 0, TABLES + 0x800, TABLES + 0x800))
        elif conv == "p":
            args.append(rng.getrandbits(32))
        elif conv in "eEfgG":
            args += list(struct.unpack("<II", quiet(rand_double(rng))))
        elif conv == "n":
            args.append(OUT + 0x1F00 + 4 * rng.randrange(8))
        out.append(spec)
    return b"".join(out)


def game_format(rng, args):
    fmt = rng.choice([b"%.2f", b"%.2fW", b"%5.0f", b"%10.0f %10.0f %9.2f%% %10.0f", b"%9.2f%%", b"%02d", b"%d",
                      b"%dW", b"%c", b"%s %d", b"%dx%d", b"%dx", b"LOAD%d.BMP", b"Track %d",
                      b"CHARANIM\\%s%04d.BMP", b"\\RollCage Dumps\\Dump %d.bmp", b"high\\lev%d"])
    for conv in fmt.replace(b"%%", b"").split(b"%")[1:]:
        c = conv.lstrip(b"0123456789.")[:1]
        if c == b"f":
            args += list(struct.unpack("<II", quiet(rand_double(rng))))
        elif c == b"s":
            args.append(STR_A)
        else:
            args.append(s32(pick(rng, rng.randrange(-100, 1000), rng.getrandbits(32), 0x41)))
    return fmt


def rand_path(rng):
    parts = []
    if rng.randrange(3) == 0:
        parts.append(rng.choice([b"C:", b"d:", b"::", b"C"]))
    for _ in range(rng.randrange(0, 4)):
        parts.append(rng.choice([b"\\", b"/", b""]) + rand_string(rng, rng.randrange(0, 6), b"ab.x\xe9\x81"))
    if rng.randrange(2):
        parts.append(rng.choice([b"\\", b"/"]))
    if rng.randrange(2):
        parts.append(rand_string(rng, rng.randrange(0, 5), b"ab.") + rng.choice([b"", b".txt", b".", b".a.b"]))
    p = b"".join(parts)
    if rng.randrange(20) == 0:
        p = rand_string(rng, rng.randrange(250, 300), b"ab\\.")
    return p


# ---- one case ----

def setup(env, rng, name):
    """State, arguments and stub script for one case."""
    m = env.m
    scr = bytearray(rng.getrandbits(8) for _ in range(0x9000)) + bytearray(SCR_SIZE - 0x9000)
    data = {a: bytearray(env.pristine[a]) for a, n in DATA_REGIONS}
    script = {}
    args = []
    kind = {}

    def put(addr, raw):
        if SCR <= addr < SCR + SCR_SIZE:
            scr[addr - SCR:addr - SCR + len(raw)] = raw
            return
        for a, n in DATA_REGIONS:
            if a <= addr < a + n:
                data[a][addr - a:addr - a + len(raw)] = raw
                return
        raise KeyError(hex(addr))

    def put32(addr, v):
        put(addr, struct.pack("<I", s32(v)))

    put32(ERRNO, 0)
    put32(DOSERRNO, 0)
    put32(0x4DB37C, 0)  # "C" locale
    put32(0x4DB28C, pick(rng, 0, 0, 0, 0, 0, 1))  # _fastflag
    put(0x6D1141, bytes(256))  # no lead bytes
    put32(0x6D103C, 0)
    for a, v in ((0x4B4D90, 0x499BD0), (0x4B4D94, 0x49987A), (0x4B4D98, 0x4998E0), (0x4B4D9C, 0x499820),
                 (0x4B4DA0, 0x4998C8), (0x4B4DA4, 0x499BD0)):  # _cfltcvt_init 0x4979b8
        put32(a, v)
    put32(0x4B5184, 0)  # no small-block heap: the original's HeapAlloc path
    env.filter_lock9 = name in ("msvc_malloc", "msvc_calloc", "msvc_free", "msvc_getcwd", "msvc_beginthread")
    if name in ("msvc_strcpy", "msvc_strcat"):
        a, b = rand_string(rng), rand_string(rng)
        off_a, off_b = rng.randrange(4), rng.randrange(4)
        put(STR_A + off_a, a + b"\0")
        put(STR_B + off_b, b + b"\0")
        args = [STR_A + off_a, STR_B + off_b]
    elif name == "msvc_strncpy":
        a, b = rand_string(rng), rand_string(rng)
        off_a, off_b = rng.randrange(4), rng.randrange(4)
        put(STR_A + off_a, a + b"\0")
        put(STR_B + off_b, b + b"\0")
        args = [STR_A + off_a, STR_B + off_b, pick(rng, 0, 1, 2, 3, 4, 5, len(b), len(b) + 1, rng.randrange(0, 300))]
    elif name in ("msvc_strncmp", "msvc_strstr"):
        a = rand_string(rng)
        if name == "msvc_strstr" and a and rng.randrange(3):
            i = rng.randrange(len(a))
            b = a[i:i + rng.randrange(0, 6)]
        else:
            b = a[:rng.randrange(len(a) + 1)] + rand_string(rng, rng.randrange(0, 3))
        if rng.randrange(3) == 0:
            b = rand_string(rng)
        put(STR_A + 1, a + b"\0")
        put(STR_B + 2, b + b"\0")
        args = [STR_A + 1, STR_B + 2]
        if name == "msvc_strncmp":
            args.append(pick(rng, 0, 1, 2, len(a), len(a) + 1, len(b) + 1, rng.randrange(0, 300), 0xFFFFFFFF))
        if name == "msvc_strstr" and rng.randrange(2):
            args.reverse()
    elif name == "msvc_isctype":
        put32(0x4B4D0C, 1)
        args = [s32(pick(rng, rng.randrange(-1, 256), rng.randrange(-300, 70000), rng.getrandbits(32))),
                pick(rng, 4, 8, 0x103, 0x8000, rng.getrandbits(16))]
        script = {"gst_out": [rng.getrandbits(16)], "__crtGetStringTypeA": [pick(rng, 0, 1, 1)]}
    elif name in ("msvc_toupper", "msvc_tolower"):
        if rng.randrange(4) == 0:
            put32(0x4DB37C, rng.getrandbits(32))
        put32(0x6D1248, pick(rng, 0, 1))
        args = [s32(pick(rng, rng.randrange(0x30, 0x80), rng.randrange(-200, 400), rng.getrandbits(32)))]
        script = {"_toupper_lk": [rng.getrandbits(32)], "_tolower_lk": [rng.getrandbits(32)]}
    elif name == "msvc_atoi":
        s = rng.choice([b"", b" ", b"\t\n "]) + rng.choice([b"", b"-", b"+", b"--", b"+-"]) + \
            rand_string(rng, rng.randrange(0, 14), b"0123456789") + rand_string(rng, rng.randrange(0, 3))
        if rng.randrange(5) == 0:
            s = rand_string(rng)
        put(STR_A, s + b"\0")
        put32(0x4B4D0C, pick(rng, 1, 1, 1, 2))
        args = [STR_A]
        script = {"gst_out": [rng.getrandbits(16) for _ in range(40)]}
    elif name == "msvc_rand":
        put32(PTD + 0x14, pick(rng, 1, 0, rng.getrandbits(32)))
    elif name in ("msvc_malloc", "msvc_calloc", "msvc_free"):
        put32(0x4DB360, pick(rng, 0, 0, 1))
        put32(0x4DB364, pick(rng, 0, CALLBACK + 0x10))
        put32(0x6D1268, rng.getrandbits(32))
        script = {"HeapAlloc": [pick(rng, HEAP, 0, 0), pick(rng, HEAP + 0x100, 0), HEAP + 0x200],
                  "callback": [1, 0]}
        if name == "msvc_malloc":
            args = [pick(rng, 1, 15, 16, 17, rng.randrange(1, 0x10000), 0xFFFFFFE0, 0xFFFFFFE1, 0xFFFFFFFF)]
        elif name == "msvc_calloc":
            args = [pick(rng, 1, 3, rng.randrange(1, 100)), pick(rng, 1, 7, rng.randrange(1, 100), 0x40000000)]
            if rng.randrange(8) == 0:
                args = [0x10000, 0x10001]  # wraps to 0x10000
            if args[0] * args[1] % 2 ** 32 == 0:  # the original's small-block heap (not used by ours)
                args[1] |= 1
        else:
            args = [pick(rng, 0, HEAP, rng.getrandbits(32))]
    elif name == "msvc_allmul":
        args = [rng.getrandbits(32) if rng.randrange(2) else rng.randrange(0, 1000) for _ in range(4)]
        if rng.randrange(3) == 0:
            args[1] = args[3] = 0
    elif name == "msvc_clock":
        start = rng.getrandbits(64) >> pick(rng, 0, 4, 8)
        put(0x4DB2E8, struct.pack("<Q", start))
        script = {"time": [(start + pick(rng, 0, rng.randrange(0, 10 ** 12), rng.getrandbits(64))) % 2 ** 64]}
    elif name in ("msvc_sprintf", "msvc_vsprintf", "msvc_vsnprintf", "sprintf_float"):
        put(STR_A, rand_string(rng, rng.randrange(0, 30)) + b"\0")
        put(STR_B, rand_string(rng, rng.randrange(0, 30)) + b"\0")
        w = [pick(rng, 0x41, 0x100, 0xE9, rng.getrandbits(16)) for _ in range(rng.randrange(0, 8))]
        put(WSTR, struct.pack("<%dH" % len(w), *w) + b"\0\0")
        put(TABLES + 0x800, struct.pack("<hhI", pick(rng, 0, 3, 8, -2), 0, pick(rng, 0, STR_A, WSTR)))
        if rng.randrange(8) == 0:
            put32(0x4DB37C, rng.getrandbits(32))
        put32(0x6D1248, pick(rng, 0, 0, 1))
        script = {"WideCharToMultiByte": [pick(rng, 0, 1, 2) for _ in range(20)],
                  "used": [pick(rng, 0, 0, 0, 1) for _ in range(20)]}
        fa = []
        if name == "sprintf_float":
            fmt = b""
            for _ in range(rng.randrange(1, 4)):
                fmt += b"%" + rng.choice([b"", b"#", b"+", b"-", b"0", b" "]) + \
                    rng.choice([b"", b"5", b"12", b"30"]) + rng.choice([b"", b".0", b".1", b".2", b".3", b".6",
                                                                         b".9", b".15", b".17", b".18", b".25"]) + \
                    rng.choice([b"f", b"e", b"g", b"E", b"G", b"f"])
                fa += list(struct.unpack("<II", quiet(rand_double(rng))))
        else:
            fmt = game_format(rng, fa) if rng.randrange(3) == 0 else rand_format(rng, fa)
        put(STR_B + 0x400, fmt + b"\0")
        while len(fa) < 24:
            fa.append(rng.getrandbits(32))
        if name in ("msvc_sprintf", "sprintf_float"):
            args = [OUT, STR_B + 0x400] + fa
        elif name == "msvc_vsnprintf":
            put(ARGS, struct.pack("<%dI" % len(fa), *fa))
            args = [OUT, s32(pick(rng, 0, 1, 5, 10, 20, 0x7FFFFFFF, -1, rng.randrange(0, 60))), STR_B + 0x400, ARGS]
        else:
            put(ARGS, struct.pack("<%dI" % len(fa), *fa))
            args = [OUT, STR_B + 0x400, ARGS]
    elif name in ("msvc_makepath", "msvc_splitpath"):
        if rng.randrange(6) == 0:
            put32(0x6D103C, 1)
            lead = bytearray(256)
            for c in (0x81, 0xE9, 0x82):
                if rng.randrange(2):
                    lead[c] = 4
            put(0x6D1141, bytes(lead))
        if name == "msvc_makepath":
            comps = []
            for i, (gen, base) in enumerate(((lambda: rng.choice([b"C", b"d", b""]), STR_A),
                                             (lambda: rand_path(rng), STR_A + 0x100),
                                             (lambda: rand_string(rng, rng.randrange(0, 8), b"ab.x"), STR_A + 0x300),
                                             (lambda: rng.choice([b"", b"txt", b".txt", b"."]), STR_A + 0x400))):
                if rng.randrange(5) == 0:
                    comps.append(0)
                else:
                    put(base, gen() + b"\0")
                    comps.append(base)
            args = [OUT] + comps
        else:
            put(STR_A, rand_path(rng) + b"\0")
            args = [STR_A] + [pick(rng, OUT + i * 0x200, OUT + i * 0x200, 0) for i in range(4)]
    elif name in ("msvc_getcwd", "msvc_chdir", "msvc_access"):
        text = pick(rng, b"C:\\GAMES\\ROLLCAGE", b"\\\\server\\share", b"//x", b"d:\\", rand_string(rng, 300, b"ab"))
        script = {"cwd": [text], "cwd_len": [pick(rng, len(text), 0, 0x104, 0x103, rng.randrange(0, 400))],
                  "HeapAlloc": [pick(rng, HEAP, 0)],
                  "SetCurrentDirectoryA": [pick(rng, 1, 1, 0)], "SetEnvironmentVariableA": [pick(rng, 1, 0)],
                  "GetLastError": [rng.randrange(0, 200)],
                  "GetFileAttributesA": [pick(rng, 0xFFFFFFFF, 0, 1, 0x20, 0x21)]}
        put(0x6D1141, bytes(rng.choice([0, 0x20, 4]) for _ in range(256)))
        put(0x6D1040, bytes(rng.getrandbits(8) for _ in range(256)))
        put32(0x6D1268, 0x5A5A0000)
        put(STR_A, b"C:\\X\0")
        if name == "msvc_getcwd":
            args = [pick(rng, 0, OUT), s32(pick(rng, 0, 1, 10, 0x104, 0x105, -1, rng.randrange(0, 400)))]
        elif name == "msvc_chdir":
            args = [STR_A]
        else:
            args = [STR_A, pick(rng, 0, 2, 4, 6)]
    elif name == "msvc_exit":
        put32(0x4DB2D4, pick(rng, 0, 0, 1))
        n = rng.randrange(0, 5)
        table = [pick(rng, 0, CALLBACK + 0x20 + 4 * i) for i in range(n)]
        put(TABLES, struct.pack("<%dI" % n, *table))
        put32(0x6D2294, pick(rng, 0, TABLES, TABLES))
        put32(0x6D2290, TABLES + 4 * n)
        for a in range(0x4A701C, 0x4A702C, 4):
            m.write(a, struct.pack("<I", pick(rng, 0, CALLBACK + 0x100 + (a & 0xFF))))
        args = [rng.getrandbits(32)]
    elif name == "msvc_beginthread":
        script = {"HeapAlloc": [pick(rng, HEAP, HEAP, 0)], "CreateThread": [pick(rng, 0x77, 0)],
                  "ResumeThread": [pick(rng, 0, 0xFFFFFFFF)], "GetLastError": [pick(rng, 0, 5)]}
        put32(0x6D1268, 0x5A5A0000)
        args = [rng.getrandbits(32), pick(rng, 0, 0x10000), rng.getrandbits(32)]
    elif name in ("cd_volume_get", "cd_volume_available"):
        put32(0x4DB250, pick(rng, 0, 1, 1))
        for a in (0x4DB284, 0x4DB280, 0x4DB288, 0x4DB1F0, 0x4DB204, 0x4DB268, 0x4DB26C, 0x4DB274):
            put32(a, pick(rng, 0, 1, rng.getrandbits(32)))
        put32(0x4B4AA0, pick(rng, 0, 1))
        put32(0x4B4A9C, pick(rng, 0, 1))
        script = {"aux_query": [pick(rng, 0, 0, 1, -1, 2) for _ in range(6)] + [-1],
                  "aux_caps": [pick(rng, 0, 2, rng.getrandbits(32)) for _ in range(7)],
                  "aux_volume": [rng.getrandbits(32) for _ in range(7)]}
    elif name == "msvc_ftol":
        kind["st0"] = struct.pack("<d", struct.unpack("<d", rand_double(rng))[0] * pick(rng, 1, 1, 1000, 1e10))
    elif name == "msvc_chkstk":
        kind["eax"] = pick(rng, 0, 4, 0xFFF, 0x1000, 0x1001, rng.randrange(0, 0x8000))
    elif name in ("msvc_cos", "msvc_sin", "msvc_atan", "msvc_sqrt", "msvc_pow"):
        x = rand_double(rng)
        if rng.randrange(6) == 0:
            x = struct.pack("<d", rng.uniform(-1, 1) * 2.0 ** rng.randrange(60, 70))
        args = list(struct.unpack("<II", x))
        if name == "msvc_pow":
            y = pick(rng, rand_double(rng), struct.pack("<d", float(rng.randrange(-5, 6))),
                     struct.pack("<d", rng.choice([0.5, 2.0, -1.0, 3.0, 0.0, 1.0, 1.5])))
            if rng.randrange(3) == 0:
                x = struct.pack("<d", rng.uniform(0, 100))
                args = list(struct.unpack("<II", x))
            args += list(struct.unpack("<II", y))
    cw = pick(rng, 0x27F, 0x7F, 0x7F, 0x7F, 0x37F, 0x47F, 0xC7F, 0x23F, 0x27E, 0x27F | 0x1000, rng.getrandbits(16) | 0x40)
    kind["cw"] = cw
    kind["pe"] = rng.randrange(2)
    return scr, data, args, script, kind


def write_code(m, code):
    m.write(CODE, code + b"\xf4")
    m.uc.emu_start(CODE, CODE + len(code))


def fpu_prepare(m, kind):
    m.write(FPIO, struct.pack("<H", kind["cw"]))
    m.write(FPIO + 0x20, struct.pack("<i", 3))
    code = b"\xdb\xe3"  # fninit
    code += b"\xd9\x2d" + struct.pack("<I", FPIO)  # fldcw [FPIO]
    if kind["pe"]:
        code += b"\xd9\xe8"  # fld1
        code += b"\xda\x35" + struct.pack("<I", FPIO + 0x20)  # fidiv dword [three]
        code += b"\xdd\xd8"  # fstp st0
    if "st0" in kind:
        m.write(FPIO + 0x10, kind["st0"])
        code += b"\xdd\x05" + struct.pack("<I", FPIO + 0x10)  # fld qword
    write_code(m, code)


def fpu_result(m):
    code = b"\xdd\x3d" + struct.pack("<I", FPIO + 0x40)  # fnstsw [sw]
    code += b"\xd9\x3d" + struct.pack("<I", FPIO + 0x44)  # fnstcw [cw]
    code += b"\xdb\x3d" + struct.pack("<I", FPIO + 0x30)  # fstp tbyte [out]
    write_code(m, code)
    sw, cw = struct.unpack("<HH", m.read(FPIO + 0x40, 4))
    value = m.read(FPIO + 0x30, 10)
    top = (sw >> 11) & 7
    return ("fpu", sw & 0x3F, top, cw, value if top != 0 else b"")


def run_case(env, name, address, call_kind, rng):
    m = env.m
    scr, data, args, script, kind = setup(env, rng, name)
    results = []
    for target in (address, m.symbols[SYMBOL.get(name, name)]):
        m.write(E.STACK, bytes(E.STACK_SIZE))
        m.write(SCR, bytes(scr))
        for a, raw in data.items():
            m.write(a, bytes(raw))
        env.reset(script)
        fpu = None
        if call_kind == "f":
            fpu_prepare(m, kind)
        if call_kind == "k":
            m.uc.reg_write(UC_X86_REG_ECX, 0x5A5A5A5A)
            m.uc.reg_write(UC_X86_REG_EAX, kind["eax"])
            esp0 = E.STACK + E.STACK_SIZE - 0x100
            m.write(esp0, struct.pack("<I", E.RETURN))
            m.uc.reg_write(UC_X86_REG_ESP, esp0)
            m.uc.emu_start(target, E.RETURN, count=100000)
            r = ("ok", m.uc.reg_read(UC_X86_REG_EAX), m.uc.reg_read(UC_X86_REG_ECX),
                 esp0 - m.uc.reg_read(UC_X86_REG_ESP))
        else:
            # the stack holds the arguments as the caller left them
            r = m.call(target, args)
            if r[0] == "ok":
                r = ("ok", 0 if name in VOID else r[1], m.uc.reg_read(UC_X86_REG_EDX) if name in ("msvc_allmul", "msvc_ftol") else 0)
            if call_kind == "f":
                fpu = fpu_result(m)
        mem = m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in DATA_REGIONS)
        results.append((r, fpu, mem, env.log))
    a, b = results
    if a == b:
        return None
    if name in ("msvc_malloc", "msvc_calloc", "msvc_free", "msvc_exit") and a[0][0] != "ok" and b[0][0] != "ok":
        return None
    msg = f"{name}{tuple(hex(x) for x in args)}: original {a[0]} {a[1]} ours {b[0]} {b[1]}"
    if a[2] != b[2]:
        first = next(i for i in range(len(a[2])) if a[2][i] != b[2][i])
        where = SCR + first if first < SCR_SIZE else first - SCR_SIZE
        msg += f" (memory differs at {where:#x}: {a[2][first:first + 16]} / {b[2][first:first + 16]})"
    if a[3] != b[3]:
        k = next((i for i in range(min(len(a[3]), len(b[3]))) if a[3][i] != b[3][i]), min(len(a[3]), len(b[3])))
        msg += f"\n  calls from #{k}: {a[3][k:k + 4]}\n  ours:          {b[3][k:k + 4]}"
    if "-v" in sys.argv and name in ("msvc_sprintf", "msvc_vsprintf", "msvc_vsnprintf", "sprintf_float"):
        msg += f"\n  format {bytes(m.read(STR_B + 0x400, 80)).split(b'\0')[0]}"
        msg += f"\n  out {a[2][OUT - SCR:OUT - SCR + 80]}\n  ours {b[2][OUT - SCR:OUT - SCR + 80]}"
    return msg


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    env = Env(m)
    rng = random.Random(7)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, call_kind) in TESTS.items():
        if only and name not in only:
            continue
        if SYMBOL.get(name, name) not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(env, name, address, call_kind, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
