#!/usr/bin/env python3
"""Differential test of orig_pp.c (group PP: memory blocks and the arena,
file handles, the multimedia timer, the window class, the debug log,
bitmaps, the car flames and the track section drawing, 0x475030-0x4779f0)
against the original, on random state.

Callees outside the group are logging stubs in both runs (each logs the
arguments it reads; strings and buffers by content; stack addresses as
"stack"): every Windows call through the import table (a stub per slot that
pops its stdcall arguments), the unquote / _chdir / _access / vsprintf /
exit runtime entries, the debugger print 0x428310 and the archive file
calls 0x43b6c0 / 0x43b7f0 / 0x43b760 (reads fill the buffer from the
script). Pure game code runs for real in both: mem_copy / mem_fill, _ftol,
the GTE transform / projection behind 0x6ccd78 / 0x6ccd80 (0x426a20 /
0x426b00, x87), the float transform 0x426620, the vertex projection
0x4288c0, the facing value 0x4278f0, the matrix helpers 0x407e90 / 0x407220
/ 0x428be0 / 0x427fc0, and the group's own functions inside the group. The
x87 control word is 0x27f, 0x37f or 0x07f. Compared: the whole data
section 0x4a7000-0x6d3000, the scratch memory, the return value, esp after
return (stdcall), ebx / esi / edi / ebp, the x87 control word, status top
and tag word, and the call log.

Normalised: the timer callbacks our C stores or hands to timeSetEvent (ours
for the original's 0x477120 / 0x4770f0), the transform's record pointer
0x6ccd74 (a stack address), and 0x6ccbf2 after the car flame (an unwritten
stack word in the original).

    tests/emu_orig_pp.py EXE build/emu_pp.elf [iterations] [names...] [-v] [-c]

-c: branch coverage of the original (instructions reached / all).
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG)

DATA, DATA_SIZE = 0x4A7000, 0x6D3000 - 0x4A7000
SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
STR = SCRATCH + 0x0000  # strings (0x1000)
ARGS = SCRATCH + 0x1000  # argument blocks (0x1000)
CAR = SCRATCH + 0x2000  # car (0x2400)
RACE = SCRATCH + 0x4400  # race (0x800)
CONFIG = SCRATCH + 0x4C00  # options (0x400)
OTS = SCRATCH + 0x5000  # OT link words (0x1000)
HEAP = SCRATCH + 0x8000  # blocks (0x10000)
SECS = SCRATCH + 0x18000  # track sections, vertices, tables (0x8000)
RECS = SCRATCH + 0x20000  # render records (0x6000)
IMG = SCRATCH + 0x28000  # image record / palettes / pixels (0x8000)
IATSTUB = 0x02200000
USERCB = IATSTUB + 0x800  # the timer's user callback (stdcall, 5 arguments)
STACK_FILL = b"\xcc" * E.STACK_SIZE

# IAT slot: (name, stdcall argument kinds)
IAT = {
    0x4A3050: ("CreatePalette", "p"),
    0x4A3068: ("GlobalFree", "v"),
    0x4A3070: ("ReadFile", "vovov"),
    0x4A3074: ("WriteFile", "vbvov"),
    0x4A3078: ("GetFileSize", "vv"),
    0x4A307C: ("GetTempFileNameA", "ssvv"),
    0x4A3098: ("FindClose", "v"),
    0x4A30A0: ("FindFirstFileA", "so"),
    0x4A3100: ("CloseHandle", "v"),
    0x4A3104: ("SetFilePointer", "vvvv"),
    0x4A3108: ("CreateFileA", "svvvvvv"),
    0x4A310C: ("GetLastError", ""),
    0x4A3110: ("Sleep", "v"),
    0x4A3114: ("GlobalAlloc", "vv"),
    0x4A3140: ("CreateDirectoryA", "sv"),
    0x4A3270: ("LoadIconA", "vv"),
    0x4A3274: ("RegisterClassExA", "w"),
    0x4A3278: ("LoadCursorA", "vv"),
    0x4A32B0: ("timeGetDevCaps", "ov"),
    0x4A32B8: ("timeEndPeriod", "v"),
    0x4A32BC: ("timeSetEvent", "vvfvv"),
    0x4A32C0: ("timeBeginPeriod", "v"),
    0x4A32C4: ("timeKillEvent", "v"),
}

# address: (name, cdecl argument kinds: v value, s string, o out buffer, a va_list)
STUBS = {
    0x474ED0: ("unquote", "os"),
    0x498B3F: ("chdir", "s"),
    0x498BC5: ("access", "sv"),
    0x498C0B: ("vsprintf", "osa"),
    0x498C89: ("exit", "v"),
    0x428310: ("debug_line_print", "s"),
    0x43B6C0: ("file_open_any", "s"),
    0x43B7F0: ("file_read_any", "vov"),
    0x43B760: ("file_close_any", "v"),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def c_string(uc, a, limit=0x200):
    return bytes(uc.mem_read(a, limit)).split(b"\0")[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


class Stubs:
    def __init__(self, m):
        self.m = m
        self.ours = {}
        m.uc.mem_map(IATSTUB, 0x1000)
        m.uc.mem_write(IATSTUB, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, kinds)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, kinds, True)
        self.calls[USERCB] = ("user_callback", "vvvvv", True)
        for address, (name, kinds) in STUBS.items():
            self.calls[address] = (name, kinds, False)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
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
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            if k == "o":
                logged.append("stack" if on_stack(a) else a)
            elif k == "s":
                logged.append(c_string(uc, a) if a else 0)
            elif k == "b":  # WriteFile's buffer: its n bytes
                logged.append(bytes(uc.mem_read(a, min(args[2], 0x100))))
            elif k == "w":  # WNDCLASSEXA
                logged.append(bytes(uc.mem_read(a, 0x30)))
            elif k == "p":  # LOGPALETTE: version, count, entries
                n = struct.unpack("<H", bytes(uc.mem_read(a + 2, 2)))[0]
                logged.append(bytes(uc.mem_read(a, 4 + 4 * min(n, 0x200))))
            elif k == "a":  # a va_list: the first two words it points at
                logged.append(bytes(uc.mem_read(a, 8)))
            elif k == "f":  # a callback: ours named as the original's
                logged.append(self.ours.get(a, a))
            else:
                logged.append(a)
        self.log.append((name, tuple(logged)))
        eax = self._next(name, 0)
        if name == "exit":
            uc.emu_stop()
            return
        if name == "unquote":
            uc.mem_write(args[0], b"U" + c_string(uc, args[1]).replace(b'"', b"")[:0x100] + b"\0")
        elif name == "vsprintf":
            text = b"V" + c_string(uc, args[1])[:0x40] + b"%x" % struct.unpack("<I", bytes(uc.mem_read(args[2], 4)))
            uc.mem_write(args[0], text + b"\0")
            eax = len(text)
        elif name == "file_read_any":
            data = self._next("read_bytes", b"")
            if data:
                uc.mem_write(args[1], data[:args[2]])
        elif name == "ReadFile":
            uc.mem_write(args[3], struct.pack("<I", self._next("ReadFile_n", args[2])))
        elif name == "WriteFile":
            uc.mem_write(args[3], struct.pack("<I", self._next("WriteFile_n", args[2]) & 0xFFFFFFFF))
        elif name == "timeGetDevCaps":
            uc.mem_write(args[0], struct.pack("<2I", *self._next("caps", (1, 1000000))))
        elif name == "FindFirstFileA":
            uc.mem_write(args[1], b"F" * 0x140)
        elif name == "GetTempFileNameA":
            uc.mem_write(args[3], b"TMP1.TMP\0")
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * len(kinds) if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---- state builders: st = {address: bytes}, script = {stub name: [results]} ----

def w32(st, a, v):
    st[a] = struct.pack("<I", v & 0xFFFFFFFF)


def w16(st, a, v):
    st[a] = struct.pack("<H", v & 0xFFFF)


def w8(st, a, v):
    st[a] = bytes([v & 0xFF])


def base_state(rng):
    st = {}
    config = bytearray(rng.randbytes(0x400))
    config[0x30] = pick(rng, 0, 1, 1, 1, rng.getrandbits(8))
    st[CONFIG] = bytes(config)
    w32(st, 0x4B2198, CONFIG)
    race = bytearray(rng.randbytes(0x800))
    race[0x579] = pick(rng, 12, 12, 0, 3, rng.getrandbits(8))
    st[RACE] = bytes(race)
    w32(st, 0x4B1960, RACE)
    return st


def name_string(rng):
    parts = [pick(rng, b"DATA", b"a", b"x.bmp", b"\"q\"", b"", b"Rollcage", b"..")
             for _ in range(rng.randrange(0, 4))]
    return pick(rng, b"\\", b"/", b"\\\\").join(parts) + pick(rng, b"", b"\\", b"\\f.idx", b".txt")


def put_string(st, a, s):
    st[a] = s + b"\0"
    return a


def gte_state(rng, st):
    """The x87 transform / projection (0x426a20 / 0x426b00) and its state."""
    w32(st, 0x6CCD78, 0x426A20)
    w32(st, 0x6CCD80, 0x426B00)
    for a in (0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40, 0x6CCD44):
        w32(st, a, fbits(pick(rng, rng.uniform(-1, 1), rng.uniform(-1, 1), 0.0, 1.0, rng.uniform(-20, 20))))
    for a in (0x6CCD0C, 0x6CCD10, 0x6CCD14):
        w32(st, a, fbits(pick(rng, rng.uniform(-3000, 3000), 0.0, rng.uniform(-1e5, 1e5))))
    w32(st, 0x6CCD00, fbits(pick(rng, 256.0, 320.0, rng.uniform(1, 1000))))
    w32(st, 0x6CCD04, fbits(pick(rng, 10.0, 1.0, 50.0, rng.uniform(0, 100))))
    w32(st, 0x6CCD08, fbits(pick(rng, 20000.0, 65536.0, 1e9, rng.uniform(0, 30000))))
    w32(st, 0x6CCD5C, fbits(pick(rng, 320.0, 0.0, rng.uniform(0, 2000))))
    w32(st, 0x6CCD60, fbits(pick(rng, 240.0, 0.0, rng.uniform(0, 2000))))
    for a, v in ((0x6CCD64, 0.0), (0x6CCD68, 640.0), (0x6CCD6C, 0.0), (0x6CCD70, 480.0)):
        w32(st, a, fbits(pick(rng, v, v, rng.uniform(-100, 1000))))
    w32(st, 0x6CCE84, pick(rng, 0, 0, 1, r32(rng)))
    w32(st, 0x6CCE94, pick(rng, 0, 0, 1, r32(rng)))
    for table in (0x6CD510, 0x6CDD80):  # OT heads: pointers into the scratch with the flag bit sometimes set
        st[table] = b"".join(struct.pack("<I", OTS + rng.randrange(0, 0x400) * 4 | pick(rng, 0, 0, 0x80000000))
                             for _ in range(0x200))
    st[OTS] = bytes(rng.randbytes(0x1000))


# ---- the car flames ----

def car_state(rng, st, idx):
    car = bytearray(rng.randbytes(0x2400))
    for off in (0x34, 0x38, 0x3C):
        struct.pack_into("<i", car, off, rng.randrange(-1 << 26, 1 << 26))
    for off in (0x68, 0x6C, 0x70):
        struct.pack_into("<i", car, off, pick(rng, rng.randrange(-0x10000, 0x10001), 0, rng.randrange(-1 << 28, 1 << 28)))
    for off in (0xC6, 0xCC, 0xD2):
        struct.pack_into("<h", car, off, pick(rng, rng.randrange(-0x1000, 0x1001), 0, 0x1000))
    for off in (0x14BC, 0x14C0, 0x14C4):
        struct.pack_into("<i", car, off, rng.randrange(-0x40000, 0x40000))
    struct.pack_into("<i", car, 0x14B4, pick(rng, rng.randrange(-200, 0x5000), rng.randrange(0, 0x40000), -5))
    struct.pack_into("<I", car, 0x654, pick(rng, 0x3A0000, 0x400000, rng.randrange(0x380000, 0x500000),
                                           rng.randrange(0, 0x1000000), r32(rng)))
    st[CAR] = bytes(car)
    w32(st, 0x6BFB80 + idx * 0x274, CAR)


def case_car_flame_shape(rng):
    st = base_state(rng)
    idx = rng.randrange(0, 6)
    car_state(rng, st, idx)
    w32(st, 0x4B11D8, r32(rng) & pick(rng, 0x7FFFFFFF, 0xFFFFFFFF))
    st[0x5F72C0] = bytes(rng.randbytes(0x140))
    return [idx, ARGS], st, {}


def case_car_flame_draw(rng):
    st = base_state(rng)
    gte_state(rng, st)
    idx = rng.randrange(0, 6)
    car_state(rng, st, idx)
    w8(st, 0x6BFDE5 + idx * 0x274, pick(rng, 0xFF, 0, 1, 2, 3, 4, 5, rng.randrange(0, 8)))
    w8(st, 0x6CCD84, pick(rng, 0, 0, 4, rng.getrandbits(8)))
    w32(st, 0x4B11D8, r32(rng) & 0x7FFFFFFF)
    grid = b""
    for _ in range(40):
        grid += struct.pack("<4h", *[pick(rng, rng.randrange(-300, 300), rng.randrange(-0x8000, 0x8000))
                                     for _ in range(3)], rng.randrange(-0x8000, 0x8000))
    st[0x5F72C0] = grid
    m = [pick(rng, rng.randrange(-0x1000, 0x1001), 0x1000, 0) for _ in range(9)]
    st[0x6CE5D0] = struct.pack("<10h", *m, rng.randrange(-0x8000, 0x8000))
    st[0x6CE5E4] = struct.pack("<3i", *[rng.randrange(-0x10000, 0x10000) for _ in range(3)])
    st[0x5F7400] = bytes(rng.randbytes(0x1088 * 8))
    ot_b = OTS + rng.randrange(0, 0x400) * 4 | pick(rng, 0, 0x80000000)
    ot_f = OTS + rng.randrange(0, 0x400) * 4 | pick(rng, 0, 0x80000000)
    return [idx, ot_b, ot_f], st, {}


# ---- memory ----

def table_state(rng, st):
    st[0x4D4ACC] = b"".join(struct.pack("<I", pick(rng, 0, 0, HEAP + rng.randrange(0, 0x100) * 0x10, r32(rng)))
                            for _ in range(32))
    w32(st, 0x4D4B4C, r32(rng))


def case_arena_init(rng):
    st = base_state(rng)
    table_state(rng, st)
    return [], st, {"GlobalAlloc": [pick(rng, 0, HEAP, 0x7FFF0000)]}


def case_arena_shutdown(rng):
    st = base_state(rng)
    table_state(rng, st)
    w32(st, 0x4D4B50, pick(rng, 0, HEAP, r32(rng)))
    return [], st, {}


def case_global_block_alloc(rng):
    st = base_state(rng)
    table_state(rng, st)
    if rng.randrange(3) == 0:
        st[0x4D4ACC] = b"".join(struct.pack("<I", r32(rng) | 1) for _ in range(32))
    return [pick(rng, 0, 0x10, rng.randrange(0, 0x100000), r32(rng)), pick(rng, 0, 1, r32(rng))], st, \
        {"GlobalAlloc": [pick(rng, 0, HEAP + 0x40, r32(rng))]}


def case_global_block_free(rng):
    st = base_state(rng)
    table_state(rng, st)
    tab = st[0x4D4ACC]
    p = pick(rng, 0, struct.unpack_from("<I", tab, 4 * rng.randrange(32))[0], r32(rng))
    if p and rng.randrange(3) == 0:  # twice in the table
        t = bytearray(tab)
        struct.pack_into("<I", t, 4 * rng.randrange(32), p)
        struct.pack_into("<I", t, 4 * rng.randrange(32), p)
        st[0x4D4ACC] = bytes(t)
    return [p], st, {}


def arena_state(rng, st):
    base = HEAP + rng.randrange(0, 0x400) * 4
    low = base + rng.randrange(0, 0x4000)
    top = low + pick(rng, 0, 0x10, rng.randrange(0, 0x8000), 0x8000)
    w32(st, 0x4D4B50, pick(rng, base, base, top - 0x500000, r32(rng)))
    w32(st, 0x4D4B58, low)
    w32(st, 0x4D4B54, top)
    st[HEAP] = bytes(rng.randbytes(0x10000))


def alloc_size(rng):
    return pick(rng, 0, 1, 15, 16, 17, rng.randrange(0, 0x3000), rng.randrange(0, 0x9000), r32(rng))


def case_arena_alloc(rng):
    st = base_state(rng)
    arena_state(rng, st)
    return [alloc_size(rng)], st, {}


def case_arena_free_high(rng):
    st = base_state(rng)
    arena_state(rng, st)
    return [alloc_size(rng)], st, {}


# ---- files ----

def create_script(rng):
    n = rng.randrange(0, 6)
    return {"CreateFileA": [pick(rng, 0xFFFFFFFF, 0xFFFFFFFF, 0x1234, 0) for _ in range(n)] + [pick(rng, 0x88, 0xFFFFFFFF)],
            "GetLastError": [pick(rng, 0x21, 0x21, 2, 5) for _ in range(8)]}


def case_file_open(rng):
    st = base_state(rng)
    put_string(st, STR, name_string(rng))
    return [STR], st, create_script(rng)


def case_handle(rng):
    st = base_state(rng)
    return [pick(rng, 0, 0x44, r32(rng))], st, {
        "CloseHandle": [pick(rng, 0, 1)], "GetFileSize": [pick(rng, 0xFFFFFFFF, 0, 1234, r32(rng))],
        "SetFilePointer": [r32(rng)]}


def case_handle_read(rng):
    st = base_state(rng)
    n = pick(rng, 0, 1, 0x40, r32(rng))
    return [0x44, ARGS, n], st, {"ReadFile": [pick(rng, 0, 1, 7)], "ReadFile_n": [pick(rng, n, 0, 3)]}


def case_handle_write(rng):
    st = base_state(rng)
    st[ARGS] = bytes(rng.randbytes(0x100))
    n = pick(rng, 0, 1, 0x40, 0x100)
    return [0x44, ARGS, n], st, {"WriteFile": [pick(rng, 0, 1, 7)], "WriteFile_n": [pick(rng, n, n, 0, n - 1)]}


def case_dbg_vlog_printf(rng):
    st = base_state(rng)
    put_string(st, STR, pick(rng, b"fmt %d", b"", b"x" * 0x50))
    w32(st, 0x4D4B68, pick(rng, 0, 1, r32(rng)))
    script = create_script(rng)
    script["WriteFile"] = [pick(rng, 0, 1) for _ in range(4)]
    return [STR, r32(rng), r32(rng)], st, script


def case_dbg_log_line(rng):
    st = base_state(rng)
    put_string(st, STR, pick(rng, b"text", b"", b"y" * 0x60))
    tag = pick(rng, 0, STR + 0x100, STR + 0x100)
    put_string(st, STR + 0x100, pick(rng, b"", b"TAG", b"z" * 0x40))
    w32(st, 0x4D4B68, pick(rng, 0, 1, r32(rng)))
    script = create_script(rng)
    script["WriteFile"] = [pick(rng, 0, 1) for _ in range(4)]
    script["WriteFile_n"] = [pick(rng, 0, 3, 0x40) for _ in range(4)]
    return [STR, tag], st, script


def case_path(rng):
    st = base_state(rng)
    put_string(st, STR, name_string(rng))
    return [pick(rng, STR, STR, 0) if rng.randrange(2) else STR], st, {
        "FindFirstFileA": [pick(rng, 0xFFFFFFFF, 0x55)], "chdir": [pick(rng, 0, 0xFFFFFFFF)],
        "access": [pick(rng, 0, 0xFFFFFFFF, 5)], "CreateDirectoryA": [pick(rng, 0, 1)]}


def case_path_find(rng):
    st = base_state(rng)
    put_string(st, STR, name_string(rng))
    return [STR], st, {"FindFirstFileA": [pick(rng, 0xFFFFFFFF, 0x55, 0)]}


def case_temp_file_name(rng):
    st = base_state(rng)
    put_string(st, STR, name_string(rng))
    put_string(st, STR + 0x100, b"rc")
    return [STR, STR + 0x100, ARGS], st, {"GetTempFileNameA": [pick(rng, 0, 77)]}


def case_none(rng):
    return [], base_state(rng), {}


# ---- the timer ----

def timer_script(rng):
    lo = pick(rng, 0, 1, 5, 6, 10, r32(rng))
    hi = pick(rng, 0, 5, 6, 1000000, lo, lo + 1, r32(rng))
    return {"caps": [(lo, hi)], "timeGetDevCaps": [pick(rng, 0, 0, 0, 1)],
            "timeBeginPeriod": [pick(rng, 0, 0, 0, 97)], "timeSetEvent": [pick(rng, 0, 0x33, 0x33)]}


def case_timer_start(rng):
    st = base_state(rng)
    st[ARGS] = bytes(rng.randbytes(16))
    return [ARGS, pick(rng, 16, r32(rng)), pick(rng, 0, 0x4365ED, USERCB), ARGS + 8, pick(rng, 0, 1, r32(rng))], st, \
        timer_script(rng)


def case_timer_start_default(rng):
    st = base_state(rng)
    st[ARGS] = bytes(rng.randbytes(16))
    return [ARGS, pick(rng, 16, r32(rng)), pick(rng, 0, 0x4365ED, USERCB)], st, timer_script(rng)


def case_timer_stop(rng):
    st = base_state(rng)
    st[ARGS] = struct.pack("<2I", pick(rng, 0, r32(rng)), pick(rng, 0, r32(rng)))
    w32(st, 0x4D4B5C, pick(rng, 0, r32(rng)))
    return [ARGS, ARGS + 4], st, {"timeEndPeriod": [pick(rng, 0, 97)]}


def case_timer_tick(rng):
    st = base_state(rng)
    w32(st, 0x4D48C8, USERCB)
    w32(st, 0x5F72A8, r32(rng))
    return [r32(rng) for _ in range(5)], st, {}


# ---- the window class, the x87 control word ----

def case_window_class(rng):
    st = base_state(rng)
    w32(st, 0x6C7FFC, r32(rng))
    return [0x435ED1, pick(rng, 0x65, r32(rng)), pick(rng, 0, 0x17, 0x18, 0x19, r32(rng))], st, {
        "LoadIconA": [r32(rng)], "LoadCursorA": [r32(rng)], "RegisterClassExA": [r32(rng)]}


def case_fpu(rng):
    st = base_state(rng)
    w16(st, 0x4D48C4, pick(rng, 0x27F, 0x37F, 0x07F, rng.getrandbits(16) | 0x3F))
    return [], st, {}


# ---- bitmaps ----

def image_record(rng):
    img = bytearray(rng.randbytes(0x24))
    count = pick(rng, 0, 1, 16, 256, 257, rng.randrange(0, 300), rng.getrandbits(16))
    struct.pack_into("<h", img, 8, count - 0x10000 if count >= 0x8000 else count)
    img[0xA] = pick(rng, 8, 8, 16, 24)
    struct.pack_into("<I", img, 0x10, IMG + 0x1000)
    struct.pack_into("<I", img, 0x18, pick(rng, 0, 0, IMG + 0x4000))
    img[0x20] = rng.getrandbits(8)
    return img


def case_palette_to_16(rng):
    st = base_state(rng)
    arena_state(rng, st)
    st[IMG] = bytes(image_record(rng))
    st[IMG + 0x1000] = bytes(rng.randbytes(0x7000))
    for a in (0x4D5F50, 0x4D5F58, 0x4D5F60):
        st[a] = bytes([pick(rng, 3, 2, 0, rng.randrange(0, 40)), pick(rng, 11, 5, 0, 10, rng.randrange(0, 40))])
    return [IMG], st, {"GlobalAlloc": [pick(rng, 0, HEAP + 0x100)]}


def case_bmp_load(rng):
    st = base_state(rng)
    arena_state(rng, st)
    st[IMG] = bytes(rng.randbytes(0x24))
    put_string(st, STR, b"PIC.BMP")
    bits = pick(rng, 8, 8, 4, 16, 24, 1, 32, rng.getrandbits(16))
    width = pick(rng, 4, 13, 64, rng.randrange(0, 50), rng.randrange(-5, 5), 1 << 24 | 1, r32(rng))
    height = pick(rng, 1, 2, 3, 8, rng.randrange(-3, 20), r32(rng))
    used = pick(rng, 0, 0, 16, 256, 300, rng.getrandbits(16))
    info = struct.pack("<IIIHHIIiiII", 40, width & 0xFFFFFFFF, height & 0xFFFFFFFF, 1, bits, 0, 0, 0, 0, used, 0)
    reads = [b"BM" + bytes(rng.randbytes(12)), info, bytes(rng.randbytes(0x400)), bytes(rng.randbytes(0x2000))]
    flags = pick(rng, 0, 1, rng.getrandbits(8))
    script = {"file_open_any": [pick(rng, 0, 0x99, 0x99, 0x99)],
              "file_read_any": [pick(rng, 1, 1, 1, 1, 1, 0) for _ in range(4)],
              "read_bytes": reads,
              "GlobalAlloc": [pick(rng, IMG + 0x1000, IMG + 0x1000, 0), pick(rng, IMG + 0x6000, 0)],
              "CreatePalette": [r32(rng)]}
    return [STR, IMG, flags], st, script


def case_blit(rng):
    st = base_state(rng)
    st[IMG] = bytes(rng.randbytes(0x8000))
    w = pick(rng, 0, 1, 7, 16, rng.randrange(-3, 40))
    h = pick(rng, 0, 1, 5, rng.randrange(-3, 20))
    return [IMG, IMG + 0x4000, pick(rng, w, 64, 40), pick(rng, 2 * w, 128, 80), pick(rng, 8, 16, 16, 24, 15), w, h,
            IMG + 0x7000], st, {}


# ---- the track ----

def section(rng, st, at, vpr1, lods):
    """A track section at `at`: lod-1 rows of vpr1 vertices, its tables and
    quad lists, and coarser levels 2..lods. Returns the bytes used."""
    rows1 = 40
    sec = bytearray(rng.randbytes(0x60))
    struct.pack_into("<i", sec, 8, vpr1 - 1)
    p = at + 0x60
    blobs = []

    def blob(data):
        nonlocal p
        a = p
        blobs.append((a, data))
        p += (len(data) + 15) & ~15
        return a

    verts = b""
    for _ in range(rows1 * vpr1 + 8):
        x, y = rng.randrange(-400, 400), rng.randrange(-400, 400)
        z = pick(rng, rng.randrange(-50, 800), rng.randrange(-0x8000, 0x8000))
        fl = pick(rng, 0, 1, 4, 5, rng.getrandbits(16))
        verts += struct.pack("<4h", x, y, z, fl - 0x10000 if fl >= 0x8000 else fl)
    struct.pack_into("<I", sec, 0x20, blob(verts))

    def skip_table():
        n = rng.randrange(0, 8)
        return blob(struct.pack("<H", n) + b"".join(struct.pack("<H", pick(rng, 8, 16, 24, 32, 0, rng.getrandbits(16)))
                                                    for _ in range(n + 2)))

    def quad_list(n):
        return blob(b"".join(struct.pack("<HHI", rng.randrange(0, 0x28), rng.getrandbits(16), r32(rng))
                             for _ in range(n + 2)))

    struct.pack_into("<I", sec, 0x28, skip_table())
    struct.pack_into("<I", sec, 0x50, quad_list(rows1 * vpr1) + 8)
    for lod in range(2, lods + 1):
        s = 1 << (lod - 1)
        count = (vpr1 - 1) // s + 1
        deltas = [8] + [8 * s] * (count - 1)
        if rng.randrange(6) == 0:
            deltas = [pick(rng, 8, 16, 0, rng.getrandbits(8) & ~7) for _ in range(count)]
        tbl = struct.pack("<I", count | rng.getrandbits(16) << 16) + struct.pack("<%dH" % (count + 1), *deltas, 0)
        struct.pack_into("<I", sec, 0x20 + 4 * lod, blob(tbl))
        struct.pack_into("<I", sec, 0x24 + 4 * lod, skip_table())
        struct.pack_into("<I", sec, 0x34 + 4 * lod, blob(bytes(rng.randbytes(4 * (rows1 * vpr1 // 8 + 4)))))
        struct.pack_into("<I", sec, 0x4C + 4 * lod, quad_list(rows1 * vpr1) + 8)
    st[at] = bytes(sec)
    for a, data in blobs:
        st[a] = data
    return p - at


def track_state(rng, st):
    gte_state(rng, st)
    w32(st, 0x6CE60C, pick(rng, 0x6CD4A0, 0x6CD4A0, 0x6CDD10))
    w32(st, 0x6C1E30, ARGS)
    w32(st, ARGS + 4, RECS)
    recs = bytearray(rng.randbytes(0x30 * 0x120))
    for i in range(0x30):
        if rng.randrange(2):
            recs[i * 0x120 + 0x23] &= 0x7F
    st[RECS] = bytes(recs)


def case_track_section(rng):
    st = base_state(rng)
    track_state(rng, st)
    lods = 3
    vpr1 = pick(rng, 2, 3, 5, 9, 13, rng.randrange(1, 14))
    section(rng, st, SECS, vpr1, lods)
    lod = pick(rng, 1, 1, 2, 3)
    s = 1 << (lod - 1)
    rows = pick(rng, 0, 1, 2, 3, rng.randrange(1, 6))
    row = rng.randrange(0, max(1, 40 - (rows + 2) * s))
    return [SECS, row, rows, lod, pick(rng, 0, 1, 2, 3, rng.getrandbits(8))], st, {}


def case_track_sections(rng):
    st = base_state(rng)
    track_state(rng, st)
    secs = []
    at = SECS
    for _ in range(3):
        secs.append(at)
        at += section(rng, st, at, pick(rng, 3, 5, 9, rng.randrange(1, 10)), 3)
    w32(st, 0x6BFAC0, ARGS + 0x100)
    st[ARGS + 0x100] = struct.pack("<3I", *secs)
    n = pick(rng, 0, 1, 2, 4, 6)
    entries = b""
    for _ in range(n):
        lod = pick(rng, 1, 2, 3)
        rows = rng.randrange(0, 4)
        entries += struct.pack("<4H", rng.randrange(0, 3), rng.randrange(0, 40 - (rows + 2) * (1 << (lod - 1))), rows,
                               lod)
    st[ARGS + 0x200] = struct.pack("<I", n) + entries
    return [ARGS + 0x200], st, {}


# name: (original address, case builder, returns a value)
TESTS = {
    "dir_change_unquoted": (0x475030, case_path, True),
    "path_access_ok": (0x475070, case_path, True),
    "car_flame_shape": (0x475290, case_car_flame_shape, False),
    "car_flame_draw": (0x475580, case_car_flame_draw, False),
    "arena_init": (0x476A50, case_arena_init, True),
    "arena_shutdown": (0x476A90, case_arena_shutdown, True),
    "global_block_alloc": (0x476AE0, case_global_block_alloc, True),
    "global_block_free": (0x476B40, case_global_block_free, True),
    "arena_alloc_low": (0x476B80, case_arena_alloc, True),
    "arena_alloc_high": (0x476BC0, case_arena_alloc, True),
    "arena_free_high": (0x476C00, case_arena_free_high, True),
    "os_file_open_read": (0x476C30, case_file_open, True),
    "os_file_create": (0x476D10, case_file_open, True),
    "os_file_open_rw": (0x476D90, case_file_open, True),
    "os_handle_close": (0x476E10, case_handle, True),
    "os_handle_read": (0x476E20, case_handle_read, True),
    "os_handle_write": (0x476E50, case_handle_write, True),
    "os_handle_size": (0x476E80, case_handle, True),
    "os_handle_seek_end": (0x476EA0, case_handle, True),
    "dbg_vlog_printf": (0x476EC0, case_dbg_vlog_printf, True),
    "path_find_exists": (0x476F00, case_path_find, True),
    "exit_zero": (0x476F80, case_none, False),
    "mm_timer_start_default": (0x476F90, case_timer_start_default, True),
    "mm_timer_start": (0x476FB0, case_timer_start, True),
    "mm_timer_stop_default": (0x477090, case_timer_stop, True),
    "mm_timer_stop": (0x4770B0, case_timer_stop, True),
    "mm_timer_tick": (0x4770F0, case_timer_tick, False),
    "mm_timer_tick_none": (0x477120, case_timer_tick, False),
    "window_class_register": (0x477130, case_window_class, True),
    "dbg_log_line": (0x4772E0, case_dbg_log_line, True),
    "fpu_init_single": (0x477420, case_fpu, False),
    "fpu_cw_save": (0x477440, case_fpu, False),
    "fpu_cw_restore": (0x477450, case_fpu, False),
    "image_palette_to_16": (0x477460, case_palette_to_16, True),
    "image_bmp_load": (0x477530, case_bmp_load, True),
    "pixels_blit_convert": (0x477860, case_blit, False),
    "temp_file_name_get": (0x477910, case_temp_file_name, True),
    "directory_create": (0x477930, case_path, True),
    "track_sections_draw": (0x477940, case_track_sections, False),
    "track_section_draw": (0x4779F0, case_track_section, False),
}
REGS = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, base, name, address, builder, returns, rng):
    args, st, script = builder(rng)
    cw = pick(rng, 0x27F, 0x37F, 0x07F)
    data = bytearray(base)
    scratch = bytearray(SCRATCH_SIZE)
    for a, v in st.items():
        if DATA <= a < DATA + DATA_SIZE:
            data[a - DATA:a - DATA + len(v)] = v
        else:
            scratch[a - SCRATCH:a - SCRATCH + len(v)] = v
    results = []
    norm = {m.symbols["mm_timer_tick_none"]: 0x477120, m.symbols["mm_timer_tick"]: 0x4770F0}
    stubs.ours = norm
    for target in (address, m.symbols[name]):
        m.write(DATA, bytes(data))
        m.write(SCRATCH, bytes(scratch))
        m.write(E.STACK, STACK_FILL)
        for i, reg in enumerate(REGS):
            m.uc.reg_write(reg, 0x11111111 * (i + 1))
        m.uc.reg_write(UC_X86_REG_FPCW, cw)
        m.uc.reg_write(UC_X86_REG_FPSW, 0)
        m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        stubs.reset(script)
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        regs = tuple(m.uc.reg_read(reg) for reg in REGS) + (m.uc.reg_read(UC_X86_REG_ESP),
                                                              m.uc.reg_read(UC_X86_REG_FPCW),
                                                              (m.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7,
                                                              m.uc.reg_read(UC_X86_REG_FPTAG))
        if r[0] != "ok":
            regs = ()  # stopped or faulted: the registers there differ
        mem = bytearray(m.read(DATA, DATA_SIZE) + m.read(SCRATCH, SCRATCH_SIZE))
        if on_stack(struct.unpack_from("<I", mem, 0x6CCD74 - DATA)[0]):
            mem[0x6CCD74 - DATA:0x6CCD78 - DATA] = b"STAK"  # the transform's record pointer: a stack address
        cb = struct.unpack_from("<I", mem, 0x4D48C8 - DATA)[0]
        if cb in norm:
            mem[0x4D48C8 - DATA:0x4D48CC - DATA] = struct.pack("<I", norm[cb])
        if name == "car_flame_draw":
            mem[0x6CCBF2 - DATA:0x6CCBF4 - DATA] = b"\0\0"  # an unwritten stack word in the original
        results.append((r, regs, stubs.log, bytes(mem)))
    a, b = results
    if a[:3] == b[:3] and a[3] == b[3]:
        return None, a[0]
    msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {a[0]} ours {b[0]}"
    if a[1] != b[1]:
        msg += f"\n  registers / esp / x87 differ: {[hex(x) for x in a[1]]} vs {[hex(x) for x in b[1]]}"
    if a[3] != b[3]:
        x, y = a[3], b[3]
        diff = [i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]]
        where = [hex(DATA + i if i < DATA_SIZE else SCRATCH + i - DATA_SIZE) for i in diff[:8]]
        msg += f"\n  memory differs at {where} ({len(diff)} words)"
        if "-v" in sys.argv:
            msg += f"\n  orig {[x[i:i + 4].hex() for i in diff[:8]]}\n  ours {[y[i:i + 4].hex() for i in diff[:8]]}"
    if a[2] != b[2]:
        la, lb = a[2], b[2]
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        msg += f"\n  calls from #{k}: orig {la[k:k + 3]}\n                ours {lb[k:k + 3]}"
    return msg, a[0]


def func_sizes():
    sizes = {0x4770F0: 0x2D, 0x477120: 3}
    for line in open("build/groups/pp_owned.txt"):
        if line.startswith("0x"):
            parts = line.split()
            sizes[int(parts[0], 16)] = int(parts[2])
    return sizes


def instruction_addresses(address, size):
    out = set()
    for line in open("build/rollcage.asm"):
        head = line.split(":", 1)[0].strip()
        try:
            a = int(head, 16)
        except ValueError:
            continue
        if address <= a < address + size and "\tnop" not in line and "int3" not in line:
            out.add(a)
    return out


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    base = m.read(DATA, DATA_SIZE)
    rng = random.Random(0x5050)
    failed = 0
    for name, (address, builder, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        seen = set()
        size = func_sizes().get(address, 0) if "-c" in sys.argv else 0
        if size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg, r = run_case(m, stubs, base, name, address, builder, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs stopped or faulted the same way in both)" if errors else ""
        if size:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            note += f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:16]}" if missed else "") + "]"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
