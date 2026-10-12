#!/usr/bin/env python3
"""Differential test of crt_cc.c (groups CC and CD: the rest of the Microsoft
C runtime) against the original, in the emulator.

Both runs see the same imports: every import slot points at a logging
stdcall stub; the file functions work on a small in-memory file system
(contents and positions are compared after each case), HeapAlloc is a
deterministic bump allocator, TlsGetValue / TlsSetValue keep one value. The
original's code runs unhooked; in our run, every original address we hook
in the game (the hook list below, as in src/main.c) is redirected to our
symbol, so original code that reaches a hooked function (exit's _lockexit,
the .CRT$XI table, _aexit_rtn...) gets ours as in the game. Other groups'
functions (malloc, calloc, free, strcpy, _vsnprintf, _isctype, exit) run
as the original in both.

Compared: return value (st(0) as 80 bits, the x87 control word, status
flags and stack top for the maths), the runtime's data (stream table, lock
table, handle table pointers, multibyte tables, globals), the scratch area
and heap, the import call log (pointer arguments by content where they
point into the stack), the files.

    tests/emu_crt_cc.py EXE build/emu_cc.elf [iterations] [names...] [-v]
"""
import math
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EFLAGS,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FS, UC_X86_REG_GDTR,
                               UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES)

SCR = 0x01000000
SCR_SIZE = 0x80000
HEAP = 0x02000000
HEAP_SIZE = 0x200000
CODE = 0x00310000
IATSTUB = 0x00320000
CALLBACK = 0x00330000
FNSTUB = 0x00340000
PTD = SCR + 0x7000
FPIO = SCR + 0x7800
CMDLINE = SCR + 0x8000
ENVW = SCR + 0x9000
ENVA = SCR + 0xA000
STRS = SCR + 0x10000
BUF = SCR + 0x20000
WINMAIN = 0x4351E0
TEB = 0x00350000  # fs: (the runtime's SEH frames)
GDT = 0x00360000

# compared data of the exe (not the float-conversion table 0x4b4d90-0x4b4da7: ours leaves it alone)
DATA_REGIONS = [(0x4B4A90, 0x40), (0x4B4D80, 0x10), (0x4B4DA8, 0x8), (0x4B4E40, 0x460), (0x4DB280, 0x2C0), (0x6D0F20, 0x400),
                (0x6D2280, 0x20)]

IAT = {
    0x4A3070: ("ReadFile", 5), 0x4A3074: ("WriteFile", 5), 0x4A3094: ("SetEndOfFile", 1),
    0x4A30A4: ("LoadLibraryA", 1), 0x4A30B0: ("FlushFileBuffers", 1), 0x4A30B4: ("GetFileType", 1),
    0x4A30BC: ("SetHandleCount", 1), 0x4A30C0: ("GetEnvironmentStringsW", 0),
    0x4A30C4: ("GetEnvironmentStrings", 0), 0x4A30C8: ("FreeEnvironmentStringsW", 1),
    0x4A30CC: ("FreeEnvironmentStringsA", 1), 0x4A30D0: ("GetModuleFileNameA", 3), 0x4A30DC: ("GetOEMCP", 0),
    0x4A30E0: ("GetACP", 0), 0x4A30E4: ("GetCPInfo", 2), 0x4A30F0: ("SetStdHandle", 2),
    0x4A30F4: ("RaiseException", 4), 0x4A30F8: ("GetStdHandle", 1), 0x4A3100: ("CloseHandle", 1),
    0x4A3104: ("SetFilePointer", 4), 0x4A3108: ("CreateFileA", 7), 0x4A310C: ("GetLastError", 0),
    0x4A311C: ("TlsGetValue", 1), 0x4A3120: ("SetLastError", 1), 0x4A3124: ("TlsAlloc", 0),
    0x4A3128: ("GetCurrentThreadId", 0), 0x4A312C: ("LCMapStringW", 6), 0x4A3130: ("LCMapStringA", 6),
    0x4A3134: ("WideCharToMultiByte", 8), 0x4A3150: ("DeleteCriticalSection", 1),
    0x4A3154: ("InitializeCriticalSection", 1), 0x4A315C: ("HeapCreate", 3), 0x4A3160: ("HeapDestroy", 1),
    0x4A3164: ("GetStringTypeW", 4), 0x4A3168: ("GetStringTypeA", 5), 0x4A316C: ("MultiByteToWideChar", 6),
    0x4A3170: ("LeaveCriticalSection", 1), 0x4A3174: ("EnterCriticalSection", 1),
    0x4A3178: ("GetProcAddress", 2), 0x4A317C: ("GetSystemTimeAsFileTime", 1), 0x4A3180: ("GetVersion", 0),
    0x4A3184: ("GetCommandLineA", 0), 0x4A3188: ("GetStartupInfoA", 1), 0x4A318C: ("GetModuleHandleA", 1),
    0x4A3190: ("TlsSetValue", 2), 0x4A3198: ("GetCurrentProcess", 0), 0x4A319C: ("TerminateProcess", 2),
    0x4A31A0: ("ExitProcess", 1), 0x4A31AC: ("InterlockedIncrement", 1), 0x4A31B0: ("ExitThread", 1),
    0x4A31C0: ("HeapFree", 3), 0x4A31C4: ("HeapAlloc", 3), 0x4A31C8: ("InterlockedDecrement", 1),
}
FNS = {"IsProcessorFeaturePresent": 1, "MessageBoxA": 4, "GetActiveWindow": 0, "GetLastActivePopup": 1}

# the hook table (src/main.c after integration): original address -> ours
HOOKS = {
    0x498EC5: "crtc_entry", 0x498D50: "crtc_lockexit", 0x498D59: "crtc_unlockexit", 0x498D62: "crtc_initterm",
    0x498C9A: "crtc_exit_quick", 0x498FCD: "crtc_amsg_exit", 0x49F925: "crtc_fptrap",
    0x498DF3: "crtc_threadstart", 0x49BCF1: "crtc_getptd", 0x49BCDE: "crtc_initptd", 0x49D48E: "crtc_errno",
    0x49D497: "crtc_doserrno", 0x49D41B: "crtc_dosmaperr", 0x49B161: "crtc_lock", 0x49B1C2: "crtc_unlock",
    0x499459: "crtc_fopen", 0x49908E: "crtc_fread", 0x4991A5: "crtc_fseek", 0x49925E: "crtc_ftell",
    0x4994F9: "crtc_fclose", 0x4996DC: "crtc_fputs", 0x499576: "crtc_fflush", 0x497F7A: "crtc_printf",
    0x49AD74: "crtc_endstdio", 0x49ACCC: "crtc_initstdio", 0x499059: "crtc_clock_init",
    0x49D2E7: "crtc_initmbctable", 0x4979A0: "crtc_fpmath", 0x4987E0: "crtc_fmod", 0x498820: "crtc_atan2",
    0x49A8E7: "crtc_one_arg_error", 0x49A8D0: "crtc_two_arg_error", 0x49AB37: "crtc_powhlp",
    0x4A0F1A: "crtc_except2", 0x4A0EBA: "crtc_handle_qnan2", 0x4985AF: "crtc_toupper_lk",
    0x4986EA: "crtc_tolower_lk", 0x49AEE3: "crtc_get_string_type", 0x49BA3B: "crtc_lcmap_string",
    0x49CEA2: "crtc_mbsdec", 0x49D303: "crtc_mbsnbcpy", 0x49D4A0: "crtc_mbctoupper",
}

TESTS = ["stdio", "startup", "fmod", "atan2", "one_arg_error", "two_arg_error", "powhlp", "except2",
         "handle_qnan2", "case_lk", "string_type", "lcmap", "mbsdec", "mbsnbcpy", "mbctoupper", "initmbctable",
         "ptd", "dosmaperr", "lock", "initterm", "exit_quick", "amsg_exit", "threadstart", "fpmath",
         "clock_init", "initstdio"]


def pick(rng, *choices):
    return rng.choice(choices)


def s32(v):
    return v & 0xFFFFFFFF


FAULTS = {}


class Stop(Exception):
    pass


class Env:
    def __init__(self, m):
        self.m = m
        uc = m.uc
        uc.mem_map(SCR, SCR_SIZE)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_map(CODE, 0x1000)
        for region in (IATSTUB, CALLBACK, FNSTUB):
            uc.mem_map(region, 0x1000)
            uc.mem_write(region, b"\xf4" * 0x1000)
        uc.mem_map(TEB, 0x1000)
        uc.mem_map(GDT, 0x1000)
        uc.mem_write(GDT + 0x18, struct.pack("<HHBBBB", 0xFFF, TEB & 0xFFFF, (TEB >> 16) & 0xFF, 0x92, 0x40,
                                             TEB >> 24))
        uc.mem_write(GDT + 0x10, struct.pack("<HHBBBB", 0xFFFF, 0, 0, 0x92, 0xCF, 0))  # flat data
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 0x1F, 0))
        uc.reg_write(UC_X86_REG_FS, 0x18)
        for reg in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES):
            uc.reg_write(reg, 0x10)
        self.iat_at = {}
        for i, (slot, (name, nargs)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            uc.mem_write(slot, struct.pack("<I", address))
            self.iat_at[address] = (name, nargs)
        uc.hook_add(UC_HOOK_CODE, self._iat, begin=IATSTUB, end=IATSTUB + 0xFFF)
        self.fn_at = {}
        for i, (name, nargs) in enumerate(sorted(FNS.items())):
            self.fn_at[FNSTUB + 0x10 * i] = (name, nargs)
        self.fn_addr = {v[0]: k for k, v in self.fn_at.items()}
        uc.hook_add(UC_HOOK_CODE, self._fn, begin=FNSTUB, end=FNSTUB + 0xFFF)
        uc.hook_add(UC_HOOK_CODE, self._callback, begin=CALLBACK, end=CALLBACK + 0xFFF)
        self.redirect = False
        for address, name in HOOKS.items():
            if name in m.symbols:
                uc.hook_add(UC_HOOK_CODE, self._redirect, begin=address, end=address, user_data=m.symbols[name])
        uc.hook_add(UC_HOOK_CODE, self._winmain, begin=WINMAIN, end=WINMAIN)
        self.pristine = {a: bytes(m.read(a, n)) for a, n in DATA_REGIONS}
        self.pristine_all = bytes(m.read(0x4A7000, 0x6D3000 - 0x4A7000))

    # ---- per-run state ----
    def reset(self, script, files, std):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.files = {k: bytearray(v) for k, v in files.items()}
        self.handles = {}
        self.next_handle = 0x100
        self.std = dict(std)  # std handle number -> handle value
        self.last_error = 0
        self.tls = self.script.pop("tls", [PTD])[0]
        self.heap_next = HEAP + 0x100
        self.stopped = None

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

    def _ptr(self, a):
        if E.STACK <= a < E.STACK + E.STACK_SIZE:
            return "stack"
        return a

    def cstr(self, a, limit=0x400):
        out = b""
        while len(out) < limit:
            chunk = bytes(self.m.uc.mem_read(a + len(out), 0x10 - (a + len(out)) % 0x10))
            out += chunk
            if b"\0" in chunk:
                break
        return out.split(b"\0")[0]

    def wstr(self, a, n):
        if n == 0xFFFFFFFF:
            out = []
            while True:
                c = struct.unpack("<H", bytes(self.m.uc.mem_read(a + 2 * len(out), 2)))[0]
                out.append(c)
                if c == 0:
                    return out
        return list(struct.unpack("<%dH" % n, bytes(self.m.uc.mem_read(a, 2 * n)))) if n else []

    def _redirect(self, uc, address, size, target):
        if self.redirect:
            uc.reg_write(UC_X86_REG_EIP, target)

    def _winmain(self, uc, address, size, _):
        if self.redirect or True:
            esp, ret, args = self._args(uc, 4)
            self.log.append(("WinMain", args[0], args[1], self.cstr(args[2]), args[3]))
            self.stopped = ("winmain",)
            uc.emu_stop()

    def _callback(self, uc, address, size, _):
        esp, ret, args = self._args(uc, 1)
        index = (address - CALLBACK) // 0x10
        self.log.append(("callback", index, args[0] if index >= 0x40 else None))
        self._ret(uc, esp, ret, 0, self.next("callback", 0))

    def _fn(self, uc, address, size, _):
        name, nargs = self.fn_at[address]
        esp, ret, args = self._args(uc, nargs)
        shown = list(args)
        if name == "MessageBoxA":
            shown = [args[0], self.cstr(args[1]), self.cstr(args[2]), args[3]]
        self.log.append((name, tuple(shown)))
        self._ret(uc, esp, ret, 4 * nargs, self.next(name, 0))

    # ---- the file system ----
    def _iat(self, uc, address, size, _):
        name, nargs = self.iat_at[address]
        esp, ret, args = self._args(uc, nargs)
        shown = list(args)
        eax = 0
        w = lambda a, data: uc.mem_write(a, data)  # noqa: E731
        if name == "HeapAlloc":
            n = args[2]
            if self.next("alloc_fail", 0):
                eax = 0
            else:
                eax = self.heap_next
                self.heap_next += (n + 15 & ~15) + 16
                if self.heap_next > HEAP + HEAP_SIZE:
                    raise SystemExit("test heap exhausted")
                if args[1] & 8:
                    w(eax, bytes(n))
        elif name in ("HeapFree", "HeapDestroy", "FreeEnvironmentStringsW", "FreeEnvironmentStringsA",
                      "SetHandleCount"):
            eax = 1
        elif name == "HeapCreate":
            eax = self.next(name, 0x5A5A0000)
        elif name in ("InitializeCriticalSection", "DeleteCriticalSection", "EnterCriticalSection",
                      "LeaveCriticalSection"):
            pass
        elif name == "InterlockedIncrement":
            v = (struct.unpack("<I", bytes(uc.mem_read(args[0], 4)))[0] + 1) & 0xFFFFFFFF
            w(args[0], struct.pack("<I", v))
            eax = v
        elif name == "InterlockedDecrement":
            v = (struct.unpack("<I", bytes(uc.mem_read(args[0], 4)))[0] - 1) & 0xFFFFFFFF
            w(args[0], struct.pack("<I", v))
            eax = v
        elif name == "GetLastError":
            eax = self.last_error
        elif name == "SetLastError":
            self.last_error = args[0]
        elif name == "TlsAlloc":
            eax = self.next(name, 3)
        elif name == "TlsGetValue":
            eax = self.tls
        elif name == "TlsSetValue":
            eax = self.next(name, 1)
            if eax:
                self.tls = args[1]
        elif name == "GetCurrentThreadId":
            eax = 0x77
        elif name == "GetCurrentProcess":
            eax = 0xFFFFFFFF
        elif name in ("ExitProcess", "ExitThread", "TerminateProcess"):
            self.log.append((name, tuple(args)))
            self.stopped = (name, args[-1])
            uc.emu_stop()
            return
        elif name == "GetVersion":
            eax = self.next(name, 0x0A280105)
        elif name == "GetCommandLineA":
            eax = CMDLINE
        elif name == "GetEnvironmentStringsW":
            eax = self.next(name, ENVW)
        elif name == "GetEnvironmentStrings":
            eax = self.next(name, ENVA)
        elif name == "GetStartupInfoA":
            w(args[0], self.next("startup", bytes([0x44]) + bytes(0x43)))
            shown = ["buf"]
        elif name == "GetStdHandle":
            eax = self.std.get(args[0], 0xFFFFFFFF)
        elif name == "SetStdHandle":
            pass
        elif name == "GetModuleFileNameA":
            text = self.next("modname", b"C:\\GAMES\\ROLLCAGE\\ROLLCAGE.EXE")
            if text is None:
                eax = 0
            else:
                n = min(len(text), args[2] - 1)
                w(args[1], text[:n] + b"\0")
                eax = n
            shown[1] = self._ptr(args[1])
        elif name == "GetModuleHandleA":
            shown = [self.cstr(args[0]) if args[0] else 0]
            eax = 0x400000 if args[0] == 0 else self.next(name, 0x7C800000)
        elif name == "LoadLibraryA":
            shown = [self.cstr(args[0])]
            eax = self.next(name, 0x77D40000)
        elif name == "GetProcAddress":
            fn = self.cstr(args[1]).decode()
            shown = [args[0], fn]
            eax = self.fn_addr.get(fn, 0) if self.next("proc_ok", 1) else 0
        elif name == "GetSystemTimeAsFileTime":
            w(args[0], struct.pack("<Q", self.next("time", 0x01C0FFEE12345678)))
            shown = [self._ptr(args[0])]
        elif name == "GetACP":
            eax = self.next(name, 1252)
        elif name == "GetOEMCP":
            eax = self.next(name, 437)
        elif name == "GetCPInfo":
            info = self.next("cpinfo", (1, 1, b""))
            ok, maxsize, lead = info
            if ok:
                w(args[1], struct.pack("<I", maxsize) + b"?\0" + (lead + bytes(12))[:12])
            eax = ok
            shown = [args[0], self._ptr(args[1])]
        elif name == "GetStringTypeW":
            chars = self.wstr(args[1], args[2])
            if self.next("gst_fail", 0):
                eax = 0
            else:
                w(args[3], b"".join(struct.pack("<H", ctype(c)) for c in chars))
                eax = 1
            shown = [args[0], tuple(chars), args[2], "out"]
        elif name == "GetStringTypeA":
            n = args[3] if args[3] != 0xFFFFFFFF else len(self.cstr(args[2])) + 1
            chars = bytes(uc.mem_read(args[2], n))
            if self.next("gsta_fail", 0):
                eax = 0
            else:
                w(args[4], b"".join(struct.pack("<H", ctype(c)) for c in chars))
                eax = 1
            shown = [args[0], args[1], chars, args[3], "out"]
        elif name in ("LCMapStringW", "LCMapStringA"):
            wide = name == "LCMapStringW"
            n = args[3]
            if wide:
                chars = self.wstr(args[2], n)
            else:
                chars = list(bytes(uc.mem_read(args[2], n if n != 0xFFFFFFFF else len(self.cstr(args[2])) + 1)))
            mapped = [casemap(c, args[1]) for c in chars]
            if self.next("lcmap_fail", 0):
                eax = 0
            elif args[5] == 0:
                eax = len(mapped)
            elif args[5] < len(mapped):
                eax = 0
            else:
                w(args[4], b"".join(struct.pack("<H", c) for c in mapped) if wide else bytes(c & 0xFF for c in mapped))
                eax = len(mapped)
            shown = [args[0], args[1], tuple(chars), args[3], "out" if args[4] else 0, args[5]]
        elif name == "MultiByteToWideChar":
            n = args[3]
            data = bytes(uc.mem_read(args[2], n)) if n != 0xFFFFFFFF else self.cstr(args[2]) + b"\0"
            if self.next("mb2wc_fail", 0):
                eax = 0
            elif args[5] == 0:
                eax = len(data)
            elif args[5] < len(data):
                eax = 0
            else:
                w(args[4], b"".join(struct.pack("<H", c) for c in data))
                eax = len(data)
            shown = [args[0], args[1], data, n, "out" if args[4] else 0, args[5]]
        elif name == "WideCharToMultiByte":
            chars = self.wstr(args[2], args[3])
            data = bytes(c if c < 0x100 else 0x3F for c in chars)
            if self.next("wc2mb_fail", 0):
                eax = 0
            elif args[5] == 0:
                eax = len(data)
            elif args[5] < len(data):
                eax = 0
            else:
                w(args[4], data)
                eax = len(data)
            shown = [args[0], args[1], tuple(chars), args[3], "out" if args[4] else 0, args[5], args[6], args[7]]
        elif name == "RaiseException":
            shown = [args[0], args[1], args[2], "args"]
        elif name == "CreateFileA":
            fname = self.cstr(args[0])
            sa = struct.unpack("<III", bytes(uc.mem_read(args[3], 12))) if args[3] else None
            shown = [fname, args[1], args[2], sa, args[4], args[5], args[6]]
            eax = self._create(fname, args[1], args[4])
        elif name == "GetFileType":
            h = args[0]
            if h in self.handles:
                eax = self.handles[h][2]
            elif h in self.std.values():
                eax = self.next("stdtype", 2)
            else:
                self.last_error = 6
                eax = 0
        elif name == "CloseHandle":
            if args[0] in self.handles:
                del self.handles[args[0]]
                eax = self.next("close_ok", 1)
                if not eax:
                    self.last_error = 5
            else:
                self.last_error = 6
                eax = 0
        elif name == "ReadFile":
            eax = self._read(uc, args)
            shown = [args[0], self._ptr(args[1]), args[2], self._ptr(args[3]), args[4]]
        elif name == "WriteFile":
            data = bytes(uc.mem_read(args[1], args[2]))
            eax = self._write(uc, args, data)
            shown = [args[0], data, args[2], self._ptr(args[3]), args[4]]
        elif name == "SetFilePointer":
            eax = self._seek(args)
        elif name == "SetEndOfFile":
            h = args[0]
            if h in self.handles:
                fname, pos, _ = self.handles[h]
                f = self.files[fname]
                del f[pos:]
                f.extend(bytes(max(0, pos - len(f))))
                eax = 1
            else:
                self.last_error = 6
        elif name == "FlushFileBuffers":
            eax = self.next(name, 1)
            if not eax:
                self.last_error = 5
        else:
            eax = self.next(name, 0)
        self.log.append((name, tuple(shown)))
        self._ret(uc, esp, ret, 4 * nargs, eax)

    def _create(self, fname, access, disposition):
        exists = fname in self.files
        if self.next("create_fail", 0):
            self.last_error = 5
            return 0xFFFFFFFF
        if disposition == 3 and not exists:  # OPEN_EXISTING
            self.last_error = 2
            return 0xFFFFFFFF
        if disposition == 1 and exists:  # CREATE_NEW
            self.last_error = 80
            return 0xFFFFFFFF
        if disposition == 5:  # TRUNCATE_EXISTING
            if not exists:
                self.last_error = 2
                return 0xFFFFFFFF
            self.files[fname] = bytearray()
        if disposition in (1, 2) or (disposition == 4 and not exists):
            self.files[fname] = bytearray()
        h = self.next_handle
        self.next_handle += 4
        self.handles[h] = [fname, 0, self.next("filetype", 1)]
        return h

    def _read(self, uc, args):
        h, buf, n, pread = args[0], args[1], args[2], args[3]
        if h not in self.handles:
            self.last_error = 6
            return 0
        err = self.next("read_fail", 0)
        if err:
            self.last_error = err
            return 0
        fname, pos, _ = self.handles[h]
        data = bytes(self.files[fname][pos:pos + n])
        uc.mem_write(buf, data)
        uc.mem_write(pread, struct.pack("<I", len(data)))
        self.handles[h][1] = pos + len(data)
        return 1

    def _write(self, uc, args, data):
        h, pwritten = args[0], args[3]
        if h not in self.handles:
            if h in self.std.values():
                uc.mem_write(pwritten, struct.pack("<I", len(data)))
                return 1
            self.last_error = 6
            return 0
        err = self.next("write_fail", 0)
        if err:
            self.last_error = err
            return 0
        short = self.next("write_short", -1)
        if short >= 0:
            data = data[:short]
        fname, pos, _ = self.handles[h]
        f = self.files[fname]
        if pos > len(f):
            f.extend(bytes(pos - len(f)))
        f[pos:pos + len(data)] = data
        self.handles[h][1] = pos + len(data)
        uc.mem_write(pwritten, struct.pack("<I", len(data)))
        return 1

    def _seek(self, args):
        h, dist, high, method = args
        if h not in self.handles:
            self.last_error = 6
            return 0xFFFFFFFF
        fname, pos, _ = self.handles[h]
        base = {0: 0, 1: pos, 2: len(self.files[fname])}.get(method)
        if base is None:
            self.last_error = 87
            return 0xFFFFFFFF
        d = dist - (1 << 32) if dist & 0x80000000 else dist
        new = base + d
        if new < 0:
            self.last_error = 131
            return 0xFFFFFFFF
        self.handles[h][1] = new
        return new & 0xFFFFFFFF

    def state(self):
        files = {k: bytes(v) for k, v in self.files.items()}
        handles = {k: (v[0], v[1]) for k, v in self.handles.items()}
        return files, handles, self.last_error, self.tls, self.stopped


def ctype(c):
    t = 0
    if 0x41 <= c <= 0x5A or (0xC0 <= c <= 0xDE and c != 0xD7):
        t |= 0x101
    if 0x61 <= c <= 0x7A or (0xDF <= c <= 0xFF and c != 0xF7):
        t |= 0x102
    if 0x30 <= c <= 0x39:
        t |= 4
    if c == 0x20 or 9 <= c <= 13:
        t |= 8
    if c < 0x20 or c == 0x7F:
        t |= 0x20
    if c == 0x20:
        t |= 0x40
    if 0x21 <= c <= 0x2F or 0x3A <= c <= 0x40:
        t |= 0x10
    return t


def casemap(c, flags):
    if flags & 0x200:  # upper
        if 0x61 <= c <= 0x7A or (0xE0 <= c <= 0xFE and c != 0xF7):
            return c - 0x20
    elif flags & 0x100:
        if 0x41 <= c <= 0x5A or (0xC0 <= c <= 0xDE and c != 0xD7):
            return c + 0x20
    return c


# ---- machine helpers ----

def fpu_prepare(m, cw, pe, st=()):
    m.write(FPIO, struct.pack("<H", cw))
    m.write(FPIO + 0x20, struct.pack("<i", 3))
    code = b"\xdb\xe3" + b"\xd9\x2d" + struct.pack("<I", FPIO)  # fninit; fldcw
    if pe:
        code += b"\xd9\xe8" + b"\xda\x35" + struct.pack("<I", FPIO + 0x20) + b"\xdd\xd8"
    for i, raw in enumerate(st):  # tbyte values, pushed in order
        m.write(FPIO + 0x60 + 0x10 * i, raw)
        code += b"\xdb\x2d" + struct.pack("<I", FPIO + 0x60 + 0x10 * i)
    m.write(CODE, code + b"\xf4")
    m.uc.emu_start(CODE, CODE + len(code))


def fpu_result(m, pops=1):
    code = b"\xdd\x3d" + struct.pack("<I", FPIO + 0x40) + b"\xd9\x3d" + struct.pack("<I", FPIO + 0x44)
    m.write(CODE, code + b"\xf4")
    m.uc.emu_start(CODE, CODE + len(code))
    sw, cw = struct.unpack("<HH", m.read(FPIO + 0x40, 4))
    top = (sw >> 11) & 7
    values = []
    for i in range(pops if top != 0 else 0):
        code = b"\xdb\x3d" + struct.pack("<I", FPIO + 0x30)
        m.write(CODE, code + b"\xf4")
        m.uc.emu_start(CODE, CODE + len(code))
        values.append(m.read(FPIO + 0x30, 10))
    return ("fpu", sw & 0x3F, top, cw, tuple(values))


def run_call(m, env, target, args, regs=None, limit=20000000):
    esp = E.STACK + E.STACK_SIZE - 0x200
    frame = struct.pack("<I", E.RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args)
    m.write(esp, frame)
    m.uc.reg_write(UC_X86_REG_ESP, esp)
    m.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
    for reg, value in (regs or {}).items():
        m.uc.reg_write(reg, value)
    try:
        m.uc.emu_start(target, E.RETURN, count=limit)
    except UcError as e:
        if "-v" in sys.argv:
            return ("error", str(e), hex(m.uc.reg_read(UC_X86_REG_EIP)), hex(m.uc.reg_read(UC_X86_REG_ESP)))
        return ("error", str(e))
    if env.stopped:
        return ("stopped",) + env.stopped
    if m.uc.reg_read(UC_X86_REG_EIP) != E.RETURN:
        return ("cut off", hex(m.uc.reg_read(UC_X86_REG_EIP)))
    return ("ok", m.uc.reg_read(UC_X86_REG_EAX), esp + 4 - m.uc.reg_read(UC_X86_REG_ESP))


# ---- random data ----

def rand_bytes(rng, n, alphabet=None):
    if alphabet is None:
        alphabet = pick(rng, b"ab\r\n", b"abc\n", b"x\r\n\x1a", bytes(range(256)), b"line\r\n")
    return bytes(rng.choice(alphabet) for _ in range(n))


def rand_double(rng):
    k = rng.randrange(12)
    if k == 0:
        return struct.pack("<d", rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 360.0, -360.0, 720.0, 1e-300, 1e300,
                                             3.14159, 1e-310, 5e-324]))
    if k == 1:
        return struct.pack("<d", rng.uniform(-1000, 1000))
    if k == 2:
        return struct.pack("<d", rng.uniform(-1, 1))
    if k == 3:
        return struct.pack("<d", float(rng.randrange(-100000, 100000)) / 8)
    if k == 4:
        return struct.pack("<d", rng.uniform(-1, 1) * 10 ** rng.randrange(-30, 30))
    if k == 5:
        return struct.pack("<Q", rng.getrandbits(64))
    if k == 6:
        hi = rng.choice([0x7FF00000, 0xFFF00000, 0x7FF80000, 0xFFF80000, 0x00000000, 0x80000000, 0x000FFFFF,
                         0x7FEFFFFF, 0x00100000])
        lo = rng.choice([0, 0, 1, 0xFFFFFFFF])
        return struct.pack("<II", lo, hi)
    if k == 7:
        return struct.pack("<d", math.ldexp(rng.uniform(-10, 10), rng.randrange(-1074, 1020)))
    if k == 8:
        return struct.pack("<d", float(struct.unpack("<f", struct.pack("<f", rng.uniform(-4000, 4000)))[0]))
    return struct.pack("<d", rng.uniform(-1e6, 1e6))


SPECIAL = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 1.5, 1e300, -1e300, 1e-310, -1e-310]


def special_double(rng):
    k = rng.randrange(4)
    if k == 0:
        return struct.pack("<d", rng.choice(SPECIAL))
    if k == 1:
        hi = rng.choice([0x7FF00000, 0xFFF00000, 0x7FF80000, 0xFFF80000, 0x7FF40000, 0xFFF00001])
        return struct.pack("<II", rng.choice([0, 0, 1, 0xFFFFFFFF]) if hi & 0xFFFFF else 0, hi)
    return rand_double(rng)


def quiet(raw):
    lo, hi = struct.unpack("<II", raw)
    if hi & 0x7FF00000 == 0x7FF00000 and (hi & 0xFFFFF or lo):
        hi |= 0x80000
    return struct.pack("<II", lo, hi)


def rand_cw(rng):
    return pick(rng, 0x27F, 0x7F, 0x7F, 0x7F, 0x37F, 0x47F, 0xC7F, 0x87F, 0x8FF, 0x23F, 0x27E, 0x27B, 0x277, 0x26F, 0x25F,
                0x27F | 0x1000, rng.getrandbits(16) | 0x40, 0x133F)


# ---- cases ----

class Case:
    """A test case: shared state for both runs and a runner."""

    def __init__(self):
        self.scr = bytearray(SCR_SIZE)
        self.data = {}
        self.script = {}
        self.files = {}
        self.std = {}
        self.fpu = None


def base_case(env, rng):
    c = Case()
    c.scr[:0x7000] = bytes(rng.getrandbits(8) for _ in range(0x7000))
    c.data = {a: bytearray(env.pristine[a]) for a, n in DATA_REGIONS}
    c.puts = {}
    return c


def put(c, addr, raw):
    if SCR <= addr < SCR + SCR_SIZE:
        c.scr[addr - SCR:addr - SCR + len(raw)] = raw
        return
    for a, n in DATA_REGIONS:
        if a <= addr < a + n:
            c.data[a][addr - a:addr - a + len(raw)] = raw
            return
    c.puts[addr] = raw


def put32(c, addr, v):
    put(c, addr, struct.pack("<I", s32(v)))


def ptd_init(c, rng):
    ptd = bytes(rng.getrandbits(8) for _ in range(0x74))
    put(c, PTD, ptd)
    put32(c, PTD + 8, 0)
    put32(c, PTD + 0xC, 0)


def apply(env, c):
    m = env.m
    m.write(0x4A7000, env.pristine_all)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    m.write(SCR, bytes(c.scr))
    m.write(HEAP, b"\xcd" * HEAP_SIZE)
    m.write(TEB, struct.pack("<I", 0xFFFFFFFF) + bytes(0xFFC))
    for a, raw in c.data.items():
        m.write(a, bytes(raw))
    for a, raw in c.puts.items():
        m.write(a, raw)
    env.reset(c.script, c.files, c.std)


def snapshot(env):
    m = env.m
    return (m.read(SCR, SCR_SIZE), m.read(HEAP, HEAP_SIZE), tuple(m.read(a, n) for a, n in DATA_REGIONS))


def first_diff(a, b, names):
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            if isinstance(x, (bytes, bytearray)):
                k = next(j for j in range(min(len(x), len(y))) if x[j] != y[j])
                return f"{names[i]} at +{k:#x}: {bytes(x[k:k + 16]).hex()} / {bytes(y[k:k + 16]).hex()}"
            if isinstance(x, tuple) and all(isinstance(e, (bytes, bytearray)) for e in x):
                for j, (p, q) in enumerate(zip(x, y)):
                    if p != q:
                        k = next(t for t in range(len(p)) if p[t] != q[t])
                        return f"data {DATA_REGIONS[j][0] + k:#x}: {bytes(p[k:k + 16]).hex()} / {bytes(q[k:k + 16]).hex()}"
            return f"{names[i]}: {x} / {y}"
    return None


def compare(name, runs, info):
    a, b = runs
    if a == b:
        return None
    msg = f"{name} {info}"
    names = ["result", "scr", "heap", "data", "fs", "log"]
    msg += "\n  " + str(first_diff(a, b, names))
    la, lb = a[5], b[5]
    if la != lb:
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        msg += f"\n  calls from #{k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
    return msg


def run_both(env, name, c, body):
    """body(m, env, ours) -> result; returns the difference message."""
    m = env.m
    runs = []
    for ours in (False, True):
        apply(env, c)
        env.redirect = ours
        if c.fpu is not None:
            fpu_prepare(m, *c.fpu)
        r = body(m, env, ours)
        runs.append((r,) + snapshot(env) + (env.state(), list(env.log)))
    env.redirect = False
    if "'error'" in repr(runs[0][0]) or "'cut off'" in repr(runs[0][0]):
        FAULTS[name] = FAULTS.get(name, 0) + 1
    return compare(name, runs, getattr(c, "info", ""))


def sym(m, ours, address):
    return m.symbols[HOOKS[address]] if ours else address


# ---- stdio ----

NAMES = [b"A.TXT", b"B.BIN", b"C", b"NOFILE", b"D.DAT"]
MODES = [b"r", b"rb", b"rt", b"w", b"wb", b"wt", b"a", b"at", b"ab", b"r+", b"r+b", b"rb+", b"w+", b"w+t", b"a+",
         b"a+t", b"a+b", b"rc", b"wbc", b"rn", b"x", b"", b"rS", b"rR", b"rT", b"rD", b"rbt", b"r++", b"wDT", b"rSR",
         b"wcn", b"rbb"]


def stdio_case(env, rng):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)  # the original malloc's small-block threshold: HeapAlloc for everything
    put32(c, 0x4DB540, pick(rng, 0, 0, 0x8000, 0x4000))  # _fmode
    put32(c, 0x4DB518, pick(rng, 0, 0, 0x4000))  # _commode
    put32(c, 0x4B4D84, pick(rng, 2, 2, 1))  # __app_type
    for n in NAMES[:3] + NAMES[4:]:
        if rng.randrange(4):
            c.files[n] = rand_bytes(rng, pick(rng, 0, 1, 5, rng.randrange(0, 600), rng.randrange(0, 9000)))
            if rng.randrange(4) == 0:
                c.files[n] += pick(rng, b"\x1a", b"\r", b"\r\n", b"\x1a\x1a")
    c.std = {0xFFFFFFF6: 0x10, 0xFFFFFFF5: pick(rng, 0x14, 0x14, 0xFFFFFFFF, 0), 0xFFFFFFF4: pick(rng, 0x18, 0x14)}
    c.script = {"stdtype": [pick(rng, 2, 2, 1, 3, 0) for _ in range(3)]}
    ops = []
    textish = rng.randrange(2)
    if rng.randrange(10) == 0:  # many streams (beyond the 20 in _iob)
        ops += [("fopen", rng.choice(NAMES[:3]), b"r") for _ in range(rng.randrange(15, 30))]
    for _ in range(rng.randrange(1, 24)):
        k = rng.randrange(25)
        last = pick(rng, 7, 7, 7, rng.randrange(8))  # 7: the newest open stream
        if k < 4:
            mode = rng.choice(MODES) if not textish or rng.randrange(3) == 0 else \
                rng.choice([b"r", b"rt", b"a+t", b"r+", b"w+t", b"at", b"a+", b"r+t"])
            ops.append(("fopen", rng.choice(NAMES), mode))
        elif k < 9:
            ops.append(("fread", pick(rng, 1, 1, 2, 4, 16, 3), pick(rng, 0, 1, 5, 100, 0x200, 0x1000, 0x1001,
                                                                        rng.randrange(0, 0x3000), rng.randrange(0, 20)),
                        last))
        elif k < 12:
            s = rand_bytes(rng, pick(rng, 0, 1, 3, 20, rng.randrange(0, 600), rng.randrange(0x3F0, 0x1200)),
                           pick(rng, b"ab\n", b"x\r\n\x1a", b"hello world\n", b"\n\n\n\na"))
            ops.append(("fputs", s.replace(b"\0", b"."), last))
        elif k < 15:
            ops.append(("fseek", s32(pick(rng, 0, 1, -1, 10, -10, 100, 0x1000, rng.randrange(-50, 9000))),
                        pick(rng, 0, 1, 2, 0, 1, 2, 0, 1, 3, -1), last))
        elif k < 18:
            ops.append(("ftell", last))
        elif k < 19:
            ops.append(("fflush", pick(rng, last, 8)))
        elif k < 21:
            ops.append(("fclose", last))
        elif k < 23:
            ops.append(("printf", rng.choice([b"Fatal %c\n", b"%s", b"abc", b"%d lines\n", b"x\ny\n"]),
                        rng.getrandbits(8) | 0x40))
        else:
            s = rand_bytes(rng, pick(rng, 0, 3, 20, rng.randrange(0, 0x1200)), b"ab\n")
            ops.append(("fputs_std", s, pick(rng, 1, 2)))
    if rng.randrange(3) == 0:  # one text file read, written and positioned
        c.files[b"A.TXT"] = rand_bytes(rng, rng.randrange(100, 12000), pick(rng, b"ab\r\n", b"a\n", b"\r\r\n\n\x1a"))
        ops = [("fopen", b"A.TXT", rng.choice([b"r", b"rt", b"r+", b"r+t", b"a+t", b"rb"]))]
        for _ in range(rng.randrange(1, 10)):
            k = rng.randrange(4)
            if k == 0:
                ops.append(("fread", 1, rng.choice([1, 10, 100, 600, 4000, 5000]), 7))
            elif k == 1:
                ops.append(("ftell", 7))
            elif k == 2:
                ops.append(("fseek", s32(rng.choice([0, 1, 5, -3, 700, 5000])), rng.choice([0, 1, 2]), 7))
            else:
                ops.append(("fputs", rng.choice([b"x\ny\r\n", b"\n", b"abc"]), 7))
    if rng.randrange(3) == 0:
        ops.append(("endstdio", pick(rng, 0, 1)))
    c.ops = ops
    c.script.update({
        "read_fail": [pick(rng, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0x6D, 0x26) for _ in range(60)],
        "write_fail": [pick(rng, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0x70) for _ in range(60)],
        "write_short": [pick(rng, -1, -1, -1, -1, -1, -1, -1, -1, 0, 3) for _ in range(60)],
        "create_fail": [pick(rng, 0, 0, 0, 0, 0, 0, 1) for _ in range(20)],
        "filetype": [pick(rng, 1, 1, 1, 1, 1, 1, 1, 2, 3, 0) for _ in range(20)],
        "FlushFileBuffers": [pick(rng, 1, 1, 0) for _ in range(20)],
        "close_ok": [pick(rng, 1, 1, 1, 1, 0) for _ in range(20)],
        "alloc_fail": [0] * 12 + [pick(rng, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1) for _ in range(60)],
    })
    c.info = str([(o[0],) + tuple(x if not isinstance(x, bytes) or len(x) < 20 else len(x) for x in o[1:])
                  for o in ops])
    return c


def stdio_body(c):
    def body(m, env, ours):
        results = []
        # the start-up's _ioinit and _initstdio, the original's in both runs
        for address in (0x49DCF4, 0x49ACCC):
            r = run_call(m, env, address, [])
            if r[0] != "ok":
                return ("init", r)
        files = []
        strs = STRS
        for op in c.ops:
            kind = op[0]
            live = [f for f in files if f]

            def fp(i):
                if not live:
                    return None
                return live[-1] if i == 7 else live[i % len(live)]
            if kind == "fopen":
                m.write(strs, op[1] + b"\0")
                m.write(strs + 0x100, op[2] + b"\0")
                r = run_call(m, env, sym(m, ours, 0x499459), [strs, strs + 0x100])
                if r[0] == "ok" and r[1]:
                    files.append(r[1])
            elif kind == "fread":
                f = fp(op[3])
                if f is None:
                    continue
                r = run_call(m, env, sym(m, ours, 0x49908E), [BUF, op[1], op[2], f])
            elif kind == "fputs":
                f = fp(op[2])
                if f is None:
                    continue
                m.write(strs + 0x200, op[1] + b"\0")
                r = run_call(m, env, sym(m, ours, 0x4996DC), [strs + 0x200, f])
            elif kind == "fseek":
                f = fp(op[3])
                if f is None:
                    continue
                r = run_call(m, env, sym(m, ours, 0x4991A5), [f, op[1], op[2]])
            elif kind == "ftell":
                f = fp(op[1])
                if f is None:
                    continue
                r = run_call(m, env, sym(m, ours, 0x49925E), [f])
            elif kind == "fflush":
                f = 0 if op[1] == 8 else fp(op[1])
                if f is None:
                    continue
                r = run_call(m, env, sym(m, ours, 0x499576), [f])
            elif kind == "fclose":
                f = fp(op[1])
                if f is None:
                    continue
                r = run_call(m, env, sym(m, ours, 0x4994F9), [f])
                files[files.index(f)] = 0
            elif kind == "fputs_std":
                m.write(strs + 0x200, op[1] + b"\0")
                r = run_call(m, env, sym(m, ours, 0x4996DC), [strs + 0x200, 0x4B4E40 + 0x20 * op[2]])
            elif kind == "printf":
                m.write(strs + 0x300, op[1] + b"\0")
                m.write(strs + 0x380, b"text\0")
                r = run_call(m, env, sym(m, ours, 0x497F7A), [strs + 0x300, op[2] if b"%s" not in op[1] else strs + 0x380])
            elif kind == "endstdio":
                m.write(0x4DB2CC, bytes([op[1]]))
                r = run_call(m, env, sym(m, ours, 0x49AD74), [])
            results.append((kind, r))
            if r[0] != "ok":
                break
        return results
    return body


# ---- the start-up ----

def startup_case(env, rng):
    c = base_case(env, rng)
    put32(c, 0x4B5184, 0)
    cmd = pick(rng, b'"C:\\GAMES\\ROLLCAGE\\ROLLCAGE.EXE"', b'"C:\\GAMES\\ROLLCAGE\\ROLLCAGE.EXE" /FRONTEND',
               b"ROLLCAGE.EXE  /x \"a b\" c\\\"d e\\\\\"f g\"", b"", b'"unterminated', b"x\ty",
               b'"C:\\A B\\R.EXE"  ' + rand_bytes(rng, rng.randrange(0, 30), b' ab"\\\t\xe9\x81'))
    put(c, CMDLINE, cmd + b"\0")
    env_strings = [b"PATH=C:\\WINDOWS", b"=C:=C:\\", b"TEMP=C:\\TEMP", b"A=" + b"x" * rng.randrange(0, 50)]
    rng.shuffle(env_strings)
    env_strings = env_strings[:rng.randrange(0, 5)]
    block = b"".join(s + b"\0" for s in env_strings) + b"\0"
    put(c, ENVA, block)
    put(c, ENVW, b"".join(struct.pack("<H", b) for b in block))
    startup = bytearray(0x44)
    startup[0] = 0x44
    if rng.randrange(2):
        struct.pack_into("<IH", startup, 0x2C, pick(rng, 1, 0, 0x101), pick(rng, 1, 3, 7))
    if rng.randrange(4) == 0:  # inherited handles
        n = pick(rng, 2, 3, 5, 40)
        flags = bytes(pick(rng, 0x81, 0x01, 0x09, 0x00, 0x41) for _ in range(n))
        handles = b"".join(struct.pack("<I", pick(rng, 0x30 + 4 * i, 0xFFFFFFFF)) for i in range(n))
        put(c, SCR + 0xB800, struct.pack("<I", n) + flags + handles)
        struct.pack_into("<HI", startup, 0x32, 4 + 5 * n, SCR + 0xB800)
    acp = pick(rng, 1252, 1252, 932, 936, 1250, 0)
    c.script = {
        "startup": [bytes(startup), bytes(startup)],
        "GetVersion": [pick(rng, 0x0A280105, 0xC0000A04, 0x23F00206)],
        "GetEnvironmentStringsW": [pick(rng, ENVW, ENVW, 0)],
        "GetEnvironmentStrings": [pick(rng, ENVA, 0)],
        "GetACP": [acp],
        "cpinfo": [pick(rng, (1, 1, b""), (1, 2, b"\x81\x9f\xe0\xfc"), (0, 1, b"")) for _ in range(3)],
        "modname": [pick(rng, b"C:\\GAMES\\ROLLCAGE\\ROLLCAGE.EXE", b"R.EXE")],
        "IsProcessorFeaturePresent": [pick(rng, 0, 0, 1)],
        "proc_ok": [pick(rng, 1, 1, 0)],
        "GetModuleHandleA": [pick(rng, 0x7C800000, 0)],
        "time": [rng.getrandbits(64)],
        "TlsAlloc": [pick(rng, 3, 3, 0xFFFFFFFF)],
        "HeapCreate": [pick(rng, 0x5A5A0000, 0x5A5A0000, 0)],
        "alloc_fail": [0] * rng.randrange(0, 40) + [pick(rng, 0, 0, 0, 1)],
        "stdtype": [pick(rng, 2, 1, 3, 0) for _ in range(3)],
        "gst_fail": [pick(rng, 0, 0, 0, 1)],
        "lcmap_fail": [pick(rng, 0, 0, 0, 1)],
        "wc2mb_fail": [0, pick(rng, 0, 0, 0, 1)],
    }
    if rng.randrange(3) == 0:  # a double-byte code page and lead bytes in the command line
        c.script["GetACP"] = [932]
        c.script["cpinfo"] = [(1, 2, b"\x81\x9f\xe0\xfc")] * 3
        put(c, CMDLINE, rand_bytes(rng, rng.randrange(0, 40), b' a"\\\t\x81\xe0@\x9f') + b"\0")
    c.std = {0xFFFFFFF6: pick(rng, 0x10, 0xFFFFFFFF), 0xFFFFFFF5: 0x14, 0xFFFFFFF4: pick(rng, 0x18, 0x14)}
    c.fpu = (0x37F, 0)
    c.info = repr(cmd)
    return c


def startup_body(c):
    def body(m, env, ours):
        r = run_call(m, env, sym(m, ours, 0x498EC5), [])
        return (r, fpu_result(m, 0))
    return body


# ---- maths ----

def fmod_case(env, rng, name):
    c = base_case(env, rng)
    ptd_init(c, rng)
    # NaNs reach the dispatcher through __fload's 80-bit load, so signalling ones are fine here
    x, y = special_double(rng), special_double(rng)
    if rng.randrange(3) == 0:
        y = struct.pack("<d", pick(rng, 360.0, 1.0, 2.0, 0.5, 1e-300))
    c.args = list(struct.unpack("<II", x)) + list(struct.unpack("<II", y))
    put32(c, 0x4DB28C, pick(rng, 0, 0, 0, 1))  # _fastflag
    put32(c, 0x4B5948, pick(rng, 0, 0, 1))
    put32(c, 0x4DB290, 0)
    c.fpu = (rand_cw(rng), rng.randrange(2))
    c.info = f"{struct.unpack('<d', x)[0]!r}, {struct.unpack('<d', y)[0]!r} cw {c.fpu[0]:#x}"
    c.address = 0x4987E0 if name == "fmod" else 0x498820
    return c


def fpcall_body(c, regs=None, pops=1):
    def body(m, env, ours):
        r = run_call(m, env, sym(m, ours, c.address), c.args, regs)
        if r[0] == "ok" and not getattr(c, "keep_eax", False):
            r = ("ok", None, r[2])
        return (r, fpu_result(m, pops))
    return body


def error_case(env, rng, name):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5948, pick(rng, 0, 0, 1))
    cw = rand_cw(rng)
    x, y = quiet(rand_double(rng)), quiet(rand_double(rng))
    c.fpu = (pick(rng, 0x133F, 0x7F, cw), rng.randrange(2), (rand_ext(rng),))
    if name in ("one_arg_error", "two_arg_error"):
        # [ret][cw][slot][arg1][arg2]: the frame crt_cb.c's maths leave
        c.args = [cw, rng.getrandbits(32)] + list(struct.unpack("<II", x)) + list(struct.unpack("<II", y))
        c.regs = {UC_X86_REG_EAX: pick(rng, 1, 2, 3, 4, 5, 7, 8, 6, 0),
                  UC_X86_REG_EDX: pick(rng, 0x18, 0x1D, 0xE, 0x10, 0xC, 0x16),
                  UC_X86_REG_ECX: rng.choice([0x4B4AC0, 0x4B4D31, 0x4B5401])}
    elif name == "powhlp":
        x, y = special_double(rng), special_double(rng)
        if rng.randrange(2):
            y = struct.pack("<d", pick(rng, math.inf, -math.inf))
        c.args = list(struct.unpack("<II", x)) + list(struct.unpack("<II", y)) + [SCR + 0x7A00]
        c.keep_eax = True
        c.fpu = (cw, rng.randrange(2))
    elif name == "except2":
        r = quiet(rand_double(rng))
        c.args = [pick(rng, 8, 0x10, 0x11, 0x12, 4, 1, 2, 0x18, rng.getrandbits(6)),
                  pick(rng, 0x18, 0x10, 0x16, rng.randrange(0, 0x40)),
                  ] + list(struct.unpack("<II", x)) + list(struct.unpack("<II", y)) + \
            list(struct.unpack("<II", r)) + [cw]
        c.fpu = (pick(rng, 0x133F, cw), rng.randrange(2))
    else:  # handle_qnan2
        c.args = [pick(rng, 0x18, 0x10, rng.randrange(0, 0x40))] + list(struct.unpack("<II", x)) + list(struct.unpack("<II", y)) + [cw]
        c.fpu = (pick(rng, 0x133F, cw), rng.randrange(2))
    c.address = {"one_arg_error": 0x49A8E7, "two_arg_error": 0x49A8D0, "powhlp": 0x49AB37,
                 "except2": 0x4A0F1A, "handle_qnan2": 0x4A0EBA}[name]
    c.info = f"args {[hex(a) for a in c.args]} fpu {c.fpu[0]:#x}"
    return c


def rand_ext(rng):
    d = struct.unpack("<d", quiet(rand_double(rng)))[0]
    lo, hi = struct.unpack("<II", struct.pack("<d", d))
    # to 80 bits by hand (normal numbers; others as an x87 load would)
    sign = hi >> 31
    exp = (hi >> 20) & 0x7FF
    mant = ((hi & 0xFFFFF) << 32) | lo
    if exp == 0:
        if mant == 0:
            return struct.pack("<QH", 0, sign << 15)
        e = -1022
        while not mant & (1 << 52):
            mant <<= 1
            e -= 1
        return struct.pack("<QH", mant << 11, (sign << 15) | (e + 16383))
    if exp == 0x7FF:
        return struct.pack("<QH", (1 << 63) | (mant << 11), (sign << 15) | 0x7FFF)
    return struct.pack("<QH", (1 << 63) | (mant << 11), (sign << 15) | (exp - 1023 + 16383))


# ---- locale ----

def locale_case(env, rng, name):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)
    put32(c, 0x4DB37C, pick(rng, 0, 0x409, 0x409, 0x411))  # __lc_handle[LC_CTYPE]
    put32(c, 0x4DB38C, pick(rng, 0, 1252, 932))
    put32(c, 0x4B4D0C, pick(rng, 1, 1, 2))
    put32(c, 0x4DB2FC, pick(rng, 0, 0, 1, 2))
    put32(c, 0x4DB394, pick(rng, 0, 0, 1, 2))
    put32(c, 0x6D103C, pick(rng, 0, 1, 1))
    lead = bytearray(0x101)
    for ch in range(0x101):
        lead[ch] = pick(rng, 0, 0, 0, 0x10, 0x20) | (4 if rng.randrange(6) == 0 and ch > 0x80 else 0)
    put(c, 0x6D1140, bytes(lead))
    put(c, 0x6D1040, bytes(rng.getrandbits(8) for _ in range(0x100)))
    put32(c, 0x6D1024, pick(rng, 932, 1252))
    put32(c, 0x6D1244, pick(rng, 0, 0x411))
    c.script = {"gst_fail": [pick(rng, 0, 0, 1) for _ in range(4)],
                "gsta_fail": [pick(rng, 0, 0, 1) for _ in range(4)],
                "lcmap_fail": [pick(rng, 0, 0, 0, 1) for _ in range(6)],
                "mb2wc_fail": [pick(rng, 0, 0, 0, 0, 1) for _ in range(6)],
                "wc2mb_fail": [pick(rng, 0, 0, 0, 1) for _ in range(6)],
                "alloc_fail": [0] * 20}
    s = rand_bytes(rng, pick(rng, 0, 1, 2, 5, rng.randrange(0, 40)), pick(rng, b"aBz\xe9\x81", bytes(range(1, 256))))
    put(c, STRS, s + b"\0")
    c.keep_eax = True
    if name == "case_lk":
        c.address = rng.choice([0x4985AF, 0x4986EA])
        c.args = [s32(pick(rng, rng.randrange(0x30, 0x80), rng.randrange(-1, 0x100), rng.randrange(0, 0x10000),
                           0xE9, 0x8140, 0xC9))]
    elif name == "string_type":
        c.address = 0x49AEE3
        c.args = [pick(rng, 1, 2, 4), STRS, s32(pick(rng, len(s), len(s) + 1, -1, 0, 3)), STRS + 0x400,
                  pick(rng, 0, 1252, 932), pick(rng, 0, 0x409), pick(rng, 0, 1)]
    elif name == "lcmap":
        c.address = 0x49BA3B
        c.args = [pick(rng, 0, 0x409), pick(rng, 0x100, 0x200, 0x400, 0x300), STRS, s32(pick(rng, len(s), -1, 0, 3)),
                  STRS + 0x400, pick(rng, 0, 0x100, 3, 1, len(s)), pick(rng, 0, 1252), pick(rng, 0, 1)]
    elif name == "mbsdec":
        c.address = 0x49CEA2
        c.args = [STRS + pick(rng, 0, 1, 3), STRS + rng.randrange(0, len(s) + 2)]
    elif name == "mbsnbcpy":
        c.address = 0x49D303
        c.args = [STRS + 0x400, STRS + pick(rng, 0, 1), pick(rng, 0, 1, 2, 3, len(s), len(s) + 5, rng.randrange(0, 60))]
    else:  # mbctoupper
        c.address = 0x49D4A0
        c.args = [pick(rng, rng.randrange(0, 0x100), rng.randrange(0x8100, 0xFFFF), 0x41, 0x61)]
    c.info = f"{c.address:#x} {[hex(a) for a in c.args]} s={s!r}"
    return c


def call_body(c, regs=None):
    def body(m, env, ours):
        r = run_call(m, env, sym(m, ours, c.address), c.args, regs)
        if r[0] == "ok" and not getattr(c, "keep_eax", False):
            r = ("ok", None, r[2])
        return r
    return body


def mbctable_case(env, rng):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)
    put32(c, 0x6D228C, pick(rng, 0, 0, 1))
    put32(c, 0x6D1024, pick(rng, 0, 1252, 932))
    put32(c, 0x4DB2FC, pick(rng, 0, 1, 2))
    put32(c, 0x4DB394, pick(rng, 0, 1, 2))
    put32(c, 0x4DB38C, pick(rng, 1252, 437))
    c.script = {"GetACP": [pick(rng, 1252, 932, 936, 949, 950, 1361, 1250, 0, 437)],
                "cpinfo": [pick(rng, (1, 1, b""), (1, 2, b"\x81\x9f\xe0\xfc"), (1, 2, b"\xa1\xfe"),
                                (1, 2, b""), (0, 1, b"")) for _ in range(3)],
                "gst_fail": [pick(rng, 0, 0, 0, 1)], "gsta_fail": [pick(rng, 0, 1)],
                "lcmap_fail": [pick(rng, 0, 0, 0, 1) for _ in range(4)], "alloc_fail": [0] * 20}
    c.address = 0x49D2E7
    c.args = []
    c.info = str(c.script["GetACP"])
    return c


# ---- per-thread data, errno, locks, exit ----

def ptd_case(env, rng, name):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)
    c.script = {"tls": [pick(rng, PTD, PTD, 0)], "TlsSetValue": [pick(rng, 1, 1, 0)],
                "alloc_fail": [pick(rng, 0, 0, 0, 1)] * 3}
    put32(c, 0x4B4D80, 0x498C9A)
    c.keep_eax = True
    if name == "ptd":
        c.address = rng.choice([0x49BCF1, 0x49D48E, 0x49D497, 0x49BCDE])
        c.args = [SCR + 0x7C00] if c.address == 0x49BCDE else []
    elif name == "dosmaperr":
        c.address = 0x49D41B
        c.args = [pick(rng, rng.randrange(0, 0x120), rng.randrange(0x13, 0x25), rng.randrange(0xBC, 0xCB),
                       rng.getrandbits(32), 2, 5, 0x6D)]
        c.keep_eax = False
    else:  # lock
        c.address = rng.choice([0x49B161, 0x49B1C2])
        n = rng.randrange(0, 0x30)
        c.args = [n]
        for i in range(0x30):
            if rng.randrange(3) == 0 and i not in (1, 9, 0xD, 0x11):  # the four static locks
                put32(c, 0x4B50C4 + 4 * i, 0)
        c.keep_eax = False
    c.info = f"{c.address:#x} {[hex(a) for a in c.args]} {c.script}"
    return c


def exit_case(env, rng, name):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)
    put32(c, 0x4B4D80, 0x498C9A)
    c.keep_eax = False
    if name == "initterm":
        n = rng.randrange(0, 6)
        table = [pick(rng, 0, CALLBACK + 0x10 * i) for i in range(n)]
        put(c, SCR + 0x7E00, struct.pack("<%dI" % n, *table))
        c.address = 0x498D62
        c.args = [SCR + 0x7E00, SCR + 0x7E00 + 4 * pick(rng, n, n, 0, max(n - 1, 0))]
    elif name == "exit_quick":
        put32(c, 0x4DB2D4, pick(rng, 0, 0, 1))
        c.address = 0x498C9A
        c.args = [rng.getrandbits(32)]
    else:  # amsg_exit
        put32(c, 0x4DB2E0, pick(rng, 0, 0, 1, 2))  # __error_mode
        put32(c, 0x4B4D84, pick(rng, 2, 2, 1))  # __app_type
        put32(c, 0x4DB50C, 0)
        put32(c, 0x4DB4A4, pick(rng, 0, CALLBACK + 0x100))
        c.std = {0xFFFFFFF4: 0x18}
        c.script = {"modname": [pick(rng, b"C:\\GAMES\\ROLLCAGE\\ROLLCAGE.EXE", None,
                                     b"C:\\" + b"x" * rng.randrange(50, 120) + b".EXE")],
                    "LoadLibraryA": [pick(rng, 0x77D40000, 0)], "proc_ok": [pick(rng, 1, 1, 0) for _ in range(3)],
                    "GetActiveWindow": [pick(rng, 0, 0x55)], "GetLastActivePopup": [pick(rng, 0, 0x66)]}
        c.address = 0x498FCD
        c.args = [pick(rng, 2, 8, 9, 0x10, 0x11, 0x12, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x78, 0x79, 0x7a, 0xfc, 0xff, 3,
                       rng.randrange(0, 0x100))]
    c.info = f"{[hex(a) for a in c.args]}"
    return c


def thread_case(env, rng):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)
    ptd2 = SCR + 0x7C00
    put(c, ptd2, bytes(rng.getrandbits(8) for _ in range(0x74)))
    put32(c, ptd2 + 0x48, CALLBACK + 0x400)
    put32(c, ptd2 + 0x4C, rng.getrandbits(32))
    put32(c, ptd2 + 4, pick(rng, 0xFFFFFFFF, 0x99))
    put32(c, ptd2 + 0x50, pick(rng, 0x4B56B8, HEAP + 0x10))
    for off in (0x24, 0x28, 0x30, 0x38, 0x40, 0x44):
        put32(c, ptd2 + off, pick(rng, 0, HEAP + off))
    c.script = {"TlsSetValue": [pick(rng, 1, 1, 1, 0), 1, 1]}
    put32(c, 0x4B4D80, 0x498C9A)
    c.address = 0x498DF3
    c.args = [ptd2]
    c.info = ""
    return c


def init_case(env, rng, name):
    c = base_case(env, rng)
    ptd_init(c, rng)
    put32(c, 0x4B5184, 0)
    c.script = {"IsProcessorFeaturePresent": [pick(rng, 0, 1)], "proc_ok": [pick(rng, 1, 0)],
                "GetModuleHandleA": [pick(rng, 0x7C800000, 0)], "time": [rng.getrandbits(64)],
                "alloc_fail": [pick(rng, 0, 0, 0, 1), pick(rng, 0, 1)]}
    c.fpu = (rand_cw(rng), rng.randrange(2))
    if name == "initstdio":
        put32(c, 0x6D2280, pick(rng, 0, 0, 3, 20, 25, 0x200))
        put32(c, 0x4B4D80, 0x498C9A)
        # a handle table for the standard handles
        put32(c, 0x6D0F20, SCR + 0x7400)
        for i in range(3):
            put32(c, SCR + 0x7400 + 0x24 * i, pick(rng, 0xFFFFFFFF, 0, 0x14))
    c.address = {"fpmath": 0x4979A0, "clock_init": 0x499059, "initstdio": 0x49ACCC}[name]
    c.args = []
    c.info = name
    return c


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    only = [x for x in sys.argv[4:] if x != "-v"]
    m = E.Machine(exe, elf)
    env = Env(m)
    rng = random.Random(11)
    failed = 0
    for name in TESTS:
        if only and name not in only:
            continue
        bad = 0
        count = iterations if name not in ("stdio", "startup") else max(iterations // 2, 1)
        for _ in range(count):
            if name == "stdio":
                c = stdio_case(env, rng)
                body = stdio_body(c)
            elif name == "startup":
                c = startup_case(env, rng)
                body = startup_body(c)
            elif name in ("fmod", "atan2"):
                c = fmod_case(env, rng, name)
                body = fpcall_body(c)
            elif name in ("one_arg_error", "two_arg_error", "powhlp", "except2", "handle_qnan2"):
                c = error_case(env, rng, name)
                body = fpcall_body(c, getattr(c, "regs", None), 2 if name.endswith("_error") else 1)
            elif name in ("case_lk", "string_type", "lcmap", "mbsdec", "mbsnbcpy", "mbctoupper"):
                c = locale_case(env, rng, name)
                body = call_body(c)
            elif name == "initmbctable":
                c = mbctable_case(env, rng)
                body = call_body(c)
            elif name in ("ptd", "dosmaperr", "lock"):
                c = ptd_case(env, rng, name)
                body = call_body(c)
            elif name in ("initterm", "exit_quick", "amsg_exit"):
                c = exit_case(env, rng, name)
                body = call_body(c)
            elif name == "threadstart":
                c = thread_case(env, rng)
                body = call_body(c)
            else:
                c = init_case(env, rng, name)
                body = fpcall_body(c, None, 0)
            msg = run_both(env, name, c, body)
            if msg:
                if bad < 3:
                    print("\n".join(line[:400] for line in msg.split("\n")))
                bad += 1
        faults = f" ({FAULTS[name]} faulted in the original)" if name in FAULTS else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {count} differ'}{faults}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
