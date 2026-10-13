#!/usr/bin/env python3
"""Differential test of orig_pv.c (group PV: the end of the DirectPlay
network file, the menu water, the snatch mesh drawing, the DirectX import
thunks and the start of the movie player, 0x48cdd0-0x4937e0) against the
original, on random state.

Callees outside the group are logging stubs in both runs (each logs the
arguments it reads; strings, structures and buffers by content, stack
buffers as "stack"): Windows calls through the import table (stdcall
stubs that pop their arguments), COM methods through fake DirectPlay /
DirectSound / DirectSoundBuffer objects, the C runtime's file and heap
calls (a scripted file stream, a bump allocator), the movie player's
other functions (fastcall stubs; 0x495220 pops its stack argument), the
network send, the allocators, the matrix / light / colour / ordering-table
helpers. The thread procedure and timer callback the original passes as
immediates (0x4932d0, 0x4937e0) are logged as those addresses for ours.
Pure game code runs for real in both: the GTE transform / projection
behind 0x6ccd78 / 0x6ccd80 (0x426a20 / 0x426b00, x87), 0x48cf20 /
0x48cfd0, the group's own functions called inside the group. The x87
control word is 0x27f, 0x37f or 0x07f.

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those runs (stack leftovers) are
masked before the comparison with ours. Compared: the whole data section
0x4a7000-0x6d3000, the scratch memory, the return value, esp after return
(stdcall), ebx / esi / edi / ebp, the x87 status top and tag word, and
the call log.

    tests/emu_orig_pv.py EXE build/emu_pv.elf [iterations] [names...] [-v] [-c]
"""
import os
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

DATA, DATA_SIZE = 0x4A7000, 0x6D3000 - 0x4A7000
SCR, SCR_SIZE = 0x01000000, 0x80000
CONFIG = SCR + 0x0000  # config table (0x100)
COMO = SCR + 0x0400  # fake COM objects (0x10 each)
ARGS = SCR + 0x1000  # strings, packets, argument blocks (0x1000)
TEXBASE = SCR + 0x2000  # *0x6c1eb0 block (0x100), texture records from +0x100 (0xf00)
OTS = SCR + 0x3000  # OT heads (0x1000)
TEXTBL = SCR + 0x4000  # *0x4be340 texture table (0x1000)
BODY = SCR + 0x8000  # the snatch body (0x3000)
WATERPTS = SCR + 0x10000  # water points (0xc4e0, room to 0x20000)
MOVIE = SCR + 0x20000  # the movie record (0x37c)
FILE = SCR + 0x20800  # the FILE (0x40)
HEAP = SCR + 0x21000  # bump allocator (to 0x60000)
HEAP_END = SCR + 0x60000
LOCKBUF = SCR + 0x60000  # the sound buffer's memory (0x10000)
STUBPAGE = 0x02200000

DP, DS, DSB_PRI, DSB_SND = COMO, COMO + 0x10, COMO + 0x20, COMO + 0x30

# stdcall imports: slot -> (name, argument count)
IAT = {
    0x4A3024: ("DirectDrawCreate", 3),
    0x4A3028: ("DirectDrawEnumerateA", 2),
    0x4A3030: ("DirectInputCreateA", 4),
    0x4A3038: ("DirectPlayLobbyCreateA", 5),
    0x4A3040: ("DirectSoundCreate", 3),
    0x4A3148: ("CreateThread", 6),
    0x4A314C: ("InterlockedExchange", 2),
    0x4A31AC: ("InterlockedIncrement", 1),
    0x4A31B0: ("ExitThread", 1),
    0x4A31B4: ("GetExitCodeThread", 2),
    0x4A3280: ("MessageBoxA", 4),
    0x4A32B0: ("timeGetDevCaps", 2),
    0x4A32BC: ("timeSetEvent", 5),
    0x4A32C0: ("timeBeginPeriod", 1),
}
# COM vtables: interface -> {offset: (name, argument count with this)}
VTABLES = {
    "dp": {0x58: ("GetSessionDesc", 3), 0x7C: ("SetSessionDesc", 3), 0xA0: ("SendChatMessage", 5),
           0xC8: ("GetMessageQueue", 6)},
    "ds": {0x08: ("DsRelease", 1), 0x0C: ("CreateSoundBuffer", 4), 0x10: ("DsGetCaps", 2),
           0x18: ("SetCooperativeLevel", 3)},
    "dsb": {0x08: ("BufRelease", 1), 0x0C: ("BufGetCaps", 2), 0x10: ("GetCurrentPosition", 3), 0x2C: ("Lock", 8),
            0x30: ("Play", 4), 0x38: ("SetFormat", 2), 0x44: ("SetFrequency", 2), 0x4C: ("Unlock", 5)},
}
VT_BASE = {"dp": STUBPAGE + 0x400, "ds": STUBPAGE + 0x600, "dsb": STUBPAGE + 0x800}
CBSTUB = STUBPAGE + 0xF00  # the movie's callback +0x40
# cdecl callees at their own addresses: name, stack argument count
STUBS = {
    0x48CC30: ("net_message_send", 4),
    0x476AE0: ("block_alloc", 2),
    0x476B40: ("block_free", 1),
    0x43692A: ("game_time", 0),
    0x48BED0: ("net_player_remove", 1),
    0x4445E0: ("heap_alloc", 1),
    0x4291F0: ("textures_reset", 0),
    0x426F80: ("rot_matrix", 2),
    0x4292B0: ("normal_colour", 3),
    0x428940: ("light_matrix_set", 1),
    0x427AB0: ("ot_add", 2),
    0x49908E: ("fread", 4),
    0x4982C2: ("malloc", 1),
    0x49827A: ("free", 1),
    0x499016: ("clock", 0),
    0x4991A5: ("fseek", 3),
    0x49946C: ("calloc", 2),
    0x499459: ("fopen", 2),
    0x4993F0: ("strncmp", 3),
    0x49925E: ("ftell", 1),
    0x4994F9: ("fclose", 1),
    0x498C89: ("exit", 1),
    0x495000: ("adpcm_mono", 6),
    0x4950A3: ("adpcm_stereo", 6),
    CBSTUB: ("movie_callback", 0),
}
# fastcall callees: name, stack argument count (popped by the callee)
FASTSTUBS = {
    0x494F10: ("frame_decode", 0),
    0x494040: ("blit16", 0),
    0x4943B0: ("blit24", 0),
    0x494740: ("blit_other", 0),
    0x494D30: ("frame_done", 0),
    0x494D40: ("video_init", 0),
    0x494ED0: ("video_free", 0),
    0x493A10: ("audio_stop", 0),
    0x495220: ("tables_init", 1),
}
CALLBACKS = {"movie_thread_proc": 0x4932D0, "movie_sound_timer": 0x4937E0}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def s32(v):
    return struct.unpack("<i", struct.pack("<I", v & 0xFFFFFFFF))[0]


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def ptr(a):
    return "stack" if on_stack(a) else a


def rd(uc, a, n):
    try:
        return bytes(uc.mem_read(a, n))
    except UcError:
        return ("unmapped", a)


def c_string(uc, a, limit=0x100):
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
        m.uc.mem_map(STUBPAGE, 0x1000)
        m.uc.mem_write(STUBPAGE, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, n)) in enumerate(sorted(IAT.items())):
            address = STUBPAGE + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, n, "std")
        for iface, methods in VTABLES.items():
            table = VT_BASE[iface]
            for off, (name, n) in methods.items():
                address = table + 0x100 + off
                m.uc.mem_write(table + off, struct.pack("<I", address))
                self.calls[address] = (name, n, "std")
        for address, (name, n) in STUBS.items():
            self.calls[address] = (name, n, "cdecl")
        for address, (name, n) in FASTSTUBS.items():
            self.calls[address] = (name, n, "fast")
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.ours = {m.symbols[k]: v for k, v in CALLBACKS.items() if k in m.symbols}
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.stream = self.script.pop("stream", [b""])[0]
        self.pos = 0
        self.heap = HEAP
        self.clock = 1000
        self.extra = self.script.pop("wait_extra", [0])[0]

    def cb(self, v):
        return self.ours.get(v, v)

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def alloc(self, n):
        a = self.heap
        self.heap += (min(n & 0xFFFFFFFF, 0x400) + 0xF) & ~0xF
        if self.heap >= HEAP_END - 0x9000:
            self.heap = HEAP
        return a

    def _stub(self, uc, address):
        name, n, conv = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<11I", bytes(uc.mem_read(esp, 44)))
        ret, a = words[0], list(words[1:])
        ecx, edx = uc.reg_read(UC_X86_REG_ECX), uc.reg_read(UC_X86_REG_EDX)
        nx = self._next
        w = uc.mem_write
        p32 = lambda x: struct.pack("<I", x & 0xFFFFFFFF)  # noqa: E731
        eax = 0
        log = ()
        if name == "net_message_send":
            log = tuple(a[:4])
            eax = nx(name, 1)
        elif name == "block_alloc":
            log = (a[0], a[1])
            eax = nx(name, ARGS + 0x800)
        elif name in ("block_free", "net_player_remove", "free", "fclose", "exit", "timeBeginPeriod", "DsRelease",
                      "BufRelease"):
            log = (a[0],)
            eax = nx(name, 0)
        elif name in ("game_time", "textures_reset", "movie_callback"):
            eax = nx(name, 0x10000 if name == "game_time" else 0)
        elif name == "heap_alloc":
            log = (a[0],)
            eax = nx(name, WATERPTS)
        elif name == "rot_matrix":
            log = (ptr(a[0]), rd(uc, a[0], 6), ptr(a[1]))
            w(a[1], nx(name, bytes(range(1, 21))))
        elif name == "normal_colour":
            nrm = rd(uc, a[0], 6)
            log = (ptr(a[0]), nrm, a[1], ptr(a[2]))
            if isinstance(nrm, bytes):
                w(a[2], bytes([nrm[0] ^ nrm[3], nrm[1] ^ nrm[4], nrm[2] ^ nrm[5], nrm[0] + 7 & 0xFF]))
        elif name == "light_matrix_set":
            log = (ptr(a[0]), rd(uc, a[0], 20))
        elif name == "ot_add":
            log = (a[0], a[1])
        elif name == "fread":
            buf, size, cnt, f = a[0], a[1], a[2], a[3]
            log = (ptr(buf), size, cnt, f)
            total = (size * cnt) & 0xFFFFFFFF
            got = min(total, len(self.stream) - self.pos)
            if got > 0:
                w(buf, self.stream[self.pos:self.pos + got])
            self.pos += got
            if got < total:
                fl = rd(uc, f + 0xC, 4)
                if isinstance(fl, bytes):
                    w(f + 0xC, p32(struct.unpack("<I", fl)[0] | 0x10))
            eax = got // size if size else 0
        elif name in ("malloc", "calloc"):
            nbytes = a[0] if name == "malloc" else (a[0] * a[1]) & 0xFFFFFFFF
            log = (a[0],) if name == "malloc" else (a[0], a[1])
            eax = nx(name, None)
            if eax is None:
                eax = self.alloc(nbytes)
            if name == "calloc" and eax:
                w(eax, bytes(min(nbytes, 0x1000)))
        elif name == "clock":
            self.clock += 17
            eax = self.clock
        elif name == "fseek":
            log = (a[0], a[1], a[2])
            self.pos = min(a[1], len(self.stream))
            fl = rd(uc, a[0] + 0xC, 4)
            if isinstance(fl, bytes):
                w(a[0] + 0xC, p32(struct.unpack("<I", fl)[0] & ~0x10))
        elif name == "fopen":
            log = (c_string(uc, a[0]), c_string(uc, a[1]))
            eax = nx(name, FILE)
        elif name == "strncmp":
            x, y = c_string(uc, a[0], a[2]), c_string(uc, a[1], a[2])
            log = (ptr(a[0]), a[1], a[2])
            eax = (x > y) - (x < y)
        elif name == "ftell":
            log = (a[0],)
            eax = nx(name, self.pos)
        elif name in ("adpcm_mono", "adpcm_stereo"):
            log = (a[0], a[1], rd(uc, a[1], 16), a[2], a[3], a[4], a[5])
        elif name in ("frame_decode",):
            log = (ecx, edx, rd(uc, edx, 12))
        elif name in ("blit16", "blit24", "blit_other", "frame_done", "video_init", "video_free", "audio_stop"):
            log = (ecx,)
            if name == "frame_done" and nx("stop_after_frame", 0):
                w(ecx + 0x68, b"\1\0")
        elif name == "tables_init":
            log = (ecx, rd(uc, ecx, 16), edx, a[0])
        # stdcall imports
        elif name in ("DirectDrawCreate", "DirectDrawEnumerateA", "DirectInputCreateA", "DirectPlayLobbyCreateA"):
            log = tuple(a[:n])
            eax = nx(name, 0x1234)
        elif name == "DirectSoundCreate":
            hr, obj = nx(name, (0, DS))
            log = (a[0], a[1], a[2])
            w(a[1], p32(obj))
            eax = hr
        elif name == "CreateThread":
            log = (a[0], a[1], self.cb(a[2]), a[3], a[4], ptr(a[5]))
            h = nx(name, 0x77)
            if a[5]:
                w(a[5], p32(0x5150))
            eax = h
        elif name == "InterlockedExchange":
            old = rd(uc, a[0], 4)
            log = (a[0], a[1])
            w(a[0], p32(a[1]))
            eax = struct.unpack("<I", old)[0] if isinstance(old, bytes) else 0
            eax = nx(name, eax)
        elif name == "InterlockedIncrement":
            v = (struct.unpack("<I", rd(uc, a[0], 4))[0] + 1) & 0xFFFFFFFF
            log = (a[0],)
            w(a[0], p32(v))
            if a[0] == MOVIE + 0x348:  # the timer catching up at once (the wait in 0x492dc0 ends)
                w(MOVIE + 0x34C, p32(v + self.extra))
                if self.extra < 0:  # behind: no sound blocks left, so no wait
                    w(MOVIE + 0x370, p32(0))
            eax = v
        elif name == "ExitThread":
            self.log.append((name, (a[0],)))
            uc.emu_stop()
            return
        elif name == "GetExitCodeThread":
            log = (a[0], ptr(a[1]))
            ok, code = nx(name, (1, 0x103))
            w(a[1], p32(code))
            eax = ok
        elif name == "MessageBoxA":
            log = (a[0], c_string(uc, a[1]), c_string(uc, a[2]), a[3])
        elif name == "timeGetDevCaps":
            hr, lo, hi = nx(name, (0, 1, 1000000))
            log = (ptr(a[0]), a[1])
            w(a[0], p32(lo) + p32(hi))
            eax = hr
        elif name == "timeSetEvent":
            log = (a[0], a[1], self.cb(a[2]), a[3], a[4])
            w(a[3] + 0x374, p32(1))  # the first tick
            eax = nx(name, 0x42)
        # IDirectPlay4A
        elif name == "GetSessionDesc":
            hr, size = nx(name, (0, 0x50))
            log = (a[0], a[1], ptr(a[2]))
            w(a[2], p32(size))
            if a[1]:
                w(a[1], bytes((i * 37 + size) & 0xFF for i in range(0x50)))
            eax = hr
        elif name == "SetSessionDesc":
            log = (a[0], a[1], rd(uc, a[1], 0x50), a[2])
            eax = nx(name, 0)
        elif name == "SendChatMessage":
            chat = rd(uc, a[4], 12)
            text = c_string(uc, struct.unpack_from("<I", chat, 8)[0]) if isinstance(chat, bytes) else 0
            log = (a[0], a[1], a[2], a[3], ptr(a[4]), chat, text)
            eax = nx(name, 0)
        elif name == "GetMessageQueue":
            hr, cnt, nbytes = nx(name, (0, 1, 0x40))
            log = (a[0], a[1], a[2], a[3], ptr(a[4]), ptr(a[5]))
            if hr == 0 or nx("queue_write_on_fail", 0):
                w(a[4], p32(cnt))
                w(a[5], p32(nbytes))
            eax = hr
        # IDirectSound
        elif name == "CreateSoundBuffer":
            hr, obj = nx(name, (0, DSB_SND))
            desc = rd(uc, a[1], 0x14)
            wfx = 0
            if isinstance(desc, bytes):
                p = struct.unpack_from("<I", desc, 0x10)[0]
                wfx = (ptr(p), rd(uc, p, 0x12)) if p else 0
            if isinstance(desc, bytes):
                desc = desc[:0x10] + b"wfx*"  # the format pointer: logged by content
            log = (a[0], ptr(a[1]), desc, wfx, a[2], a[3])
            if hr == 0:
                w(a[2], p32(obj))
            eax = hr
        elif name == "DsGetCaps":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 0x60))
            w(a[1] + 4, p32(nx(name, 0xA)))
            eax = 0
        elif name == "SetCooperativeLevel":
            log = (a[0], a[1], a[2])
            eax = nx(name, 0)
        # IDirectSoundBuffer
        elif name == "BufGetCaps":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 0x14))
            w(a[1] + 8, p32(nx(name, 0x10000)))
        elif name == "SetFormat":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 0x12))
            eax = nx(name, 0)
        elif name == "SetFrequency":
            log = (a[0], a[1])
        elif name == "Play":
            log = (a[0], a[1], a[2], a[3])
        elif name == "GetCurrentPosition":
            play, write = nx(name, (0x100, 0x200))
            log = (a[0], ptr(a[1]), ptr(a[2]))
            w(a[1], p32(play))
            w(a[2], p32(write))
        elif name == "Lock":
            log = (a[0], a[1], a[2], ptr(a[3]), ptr(a[4]), ptr(a[5]), ptr(a[6]), a[7])
            split = nx(name, None)
            total = min(a[2], 0x800)
            if split is None:
                split = total
            n1 = min(split, total)
            w(a[3], p32(LOCKBUF + 0x100))
            w(a[4], p32(n1))
            w(a[5], p32(LOCKBUF + 0x1000))
            w(a[6], p32(total - n1))
        elif name == "Unlock":
            log = (a[0], a[1], a[2], a[3], a[4])
        else:
            raise KeyError(name)
        self.log.append((name, log))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        pop = 4 * n if conv in ("std", "fast") else 0
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
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
    config = bytearray(rng.randbytes(0x100))
    config[0x28] = pick(rng, 0, 1, 1, rng.getrandbits(8))
    st[CONFIG] = bytes(config)
    w32(st, 0x4B2198, CONFIG)
    w32(st, 0x5D2060, DP)
    st[COMO] = b"".join(struct.pack("<4I", VT_BASE[k], 0, 0, 0) for k in ("dp", "ds", "dsb", "dsb"))
    return st


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
    w32(st, 0x6CE60C, pick(rng, 0x6CDD10, 0x6CDD10, 0x6CD508, r32(rng)))
    w32(st, 0x6CCE84, pick(rng, 0, 1, r32(rng)))
    for table in (0x6CD510, 0x6CDD80):  # OT heads: pointers into the scratch with the flag bit sometimes set
        st[table] = b"".join(struct.pack("<I", OTS + rng.randrange(0, 0x400) * 4 | pick(rng, 0, 0, 0x80000000))
                             for _ in range(0x200))
    st[OTS] = bytes(rng.randbytes(0x1000))


def players(rng, st):
    for i in range(6):
        rec = 0x5D2CD0 + 0x66C * i
        w32(st, rec, pick(rng, 0, 1, 1, r32(rng)))
        w32(st, rec + 4, pick(rng, 0x100 + i, 0x100 + i, 1, r32(rng)))
        w32(st, rec + 0x664, r32(rng))
    w32(st, 0x5D535C, pick(rng, -1, 0, 1, 2, 3, 4, 5, rng.randrange(0, 6)))
    w32(st, 0x5D5360, pick(rng, 0x100, 0x101, 0x102, 1, r32(rng)))


# ---- network ----

def case_send_to(rng):
    st = base_state(rng)
    players(rng, st)
    mode = pick(rng, 0, 1, 2, 2, 3, r32(rng))
    script = {"net_message_send": [pick(rng, 1, 1, 1, 0, r32(rng)) for _ in range(7)]}
    return [mode, r32(rng), ARGS, rng.randrange(0, 0x100), r32(rng)], None, st, script


def case_packet_filter(rng):
    st = base_state(rng)
    table = bytearray(rng.randbytes(64 * 12))
    pkt = rng.randbytes(8)
    frm = r32(rng)
    for _ in range(rng.randrange(0, 4)):
        k = rng.randrange(64)
        table[12 * k:12 * k + 12] = pkt + struct.pack("<I", pick(rng, frm, frm, r32(rng)))
        if rng.randrange(3) == 0:
            table[12 * k + pick(rng, 0, 4)] ^= 1
    st[0x5D59B4] = bytes(table)
    w32(st, 0x5D5CB4, pick(rng, 0, 63, rng.randrange(64), rng.randrange(64), rng.randrange(64, 200)))
    st[ARGS] = pkt
    return [ARGS, frm], None, st, {}


def case_session(rng):
    st = base_state(rng)
    script = {"GetSessionDesc": [(pick(rng, 0x8877001E, 0x8877001E, 0, 0x80004005), rng.randrange(0x10, 0x50)),
                                 (pick(rng, 0, 0, 0, 1), rng.randrange(0x10, 0x50))],
              "block_alloc": [pick(rng, ARGS + 0x800, ARGS + 0x800, 0)],
              "SetSessionDesc": [pick(rng, 0, 0, 0, 0x80004005)]}
    return [], None, st, script


def case_chat_send(rng):
    st = base_state(rng)
    w32(st, 0x5D622C, r32(rng) & pick(rng, 0, 1, 2, 3, 3, 0xFF))
    lines = bytearray(rng.randbytes(4 * 0x41))
    for k in range(4):
        if rng.randrange(5):
            n = rng.randrange(0, 0x41)
            lines[k * 0x41 + n] = 0
    st[0x5D6128] = bytes(lines)
    st[0x5D622C + 4] = b"\0" * 4 if rng.randrange(2) else rng.randbytes(4)
    n = pick(rng, 0, 1, 0x3F, 0x40, 0x41, 0x50, rng.randrange(0, 0x60))
    st[ARGS] = bytes(rng.randrange(1, 256) for _ in range(n)) + b"\0"
    w32(st, 0x5D5360, r32(rng))
    return [ARGS], None, st, {"SendChatMessage": [r32(rng)]}


def case_void(rng):
    return [], None, base_state(rng), {}


def case_queue(rng):
    st = base_state(rng)
    script = {"GetMessageQueue": [(pick(rng, 0, 0, 0, 0x80004005), pick(rng, 0, 1, 2, 3, r32(rng)), r32(rng))
                                  for _ in range(2)],
              # the first count is a stack leftover when that call fails without writing it: always written
              "queue_write_on_fail": [1, rng.randrange(2)]}
    return [], None, st, script


def case_conn(rng):
    st = base_state(rng)
    w32(st, 0x5D26A4, pick(rng, -1, 0, 1, 3, 10, rng.randrange(0, 10)))
    for i in range(10):
        rec = 0x5D2064 + 0xA0 * i
        w32(st, rec + 0x18, pick(rng, 0, 1, 2, 3, r32(rng)))
        w32(st, rec + 0x1C, pick(rng, 0, 1, r32(rng)))
    return [pick(rng, 0, 1, 2, 3, r32(rng))], None, st, {}


def case_timeout(rng):
    st = base_state(rng)
    players(rng, st)
    now = r32(rng)
    for i in range(6):
        w32(st, 0x5D2CD0 + 0x66C * i + 0x664, pick(rng, now - 3000, now - 3001, now - 2999, now, r32(rng)))
    return [], None, st, {"game_time": [now]}


# ---- menu water ----

def water_record(rng, st, a, small=True):
    rec = bytearray(rng.randbytes(0x70))
    if small:
        size = lambda: pick(rng, 50, 100, 150, 200, 250, 300, rng.randrange(50, 400), -100, -50)  # noqa: E731
    else:
        size = lambda: pick(rng, 1500, 50, rng.randrange(50, 1600))  # noqa: E731
    # no zero-sized grids: a #DE leaves unicorn's exception state behind (the next one is a double fault)
    struct.pack_into("<3iI", rec, 0, WATERPTS, size(), size(), r32(rng))
    struct.pack_into("<ii", rec, 0x2C, pick(rng, 0, 1, 2, rng.randrange(0, 10)), pick(rng, 0, 1, 2, rng.randrange(10)))
    st[a] = bytes(rec)
    pts = bytearray(rng.randbytes(0x8000))
    for i in range(0x8000 // 0x38):
        o = i * 0x38
        struct.pack_into("<i", pts, o + 4, rng.randrange(-1 << 22, 1 << 22))
        struct.pack_into("<i", pts, o + 0x24, pick(rng, rng.randrange(-1 << 19, 1 << 19), s32(r32(rng))))
        struct.pack_into("<i", pts, o + 0x28, pick(rng, rng.randrange(-1 << 19, 1 << 19), s32(r32(rng))))
    st[WATERPTS] = bytes(pts)


def lights(rng, st):
    st[0x4B2F78] = rng.randbytes(0x30)


def case_water_init(rng):
    st = base_state(rng)
    st[TEXTBL] = rng.randbytes(0x400)
    w32(st, 0x4BE340, TEXTBL)
    st[WATERPTS] = rng.randbytes(0xC4E0)
    return [], None, st, {}


def case_water_rec(rng):
    st = base_state(rng)
    st[TEXTBL] = rng.randbytes(0x400)
    w32(st, 0x4BE340, TEXTBL)
    st[ARGS] = rng.randbytes(0x80)
    st[WATERPTS] = rng.randbytes(0xC4E0)
    return [ARGS, r32(rng), r32(rng)], None, st, {}


def case_camera(rng):
    st = base_state(rng)
    st[ARGS] = struct.pack("<3i", *[pick(rng, rng.randrange(-5000, 5000), s32(r32(rng))) for _ in range(3)])
    st[ARGS + 0x20] = rng.randbytes(20)
    return [ARGS, ARGS + 0x20], None, st, {}


def case_water_transform(rng):
    st = base_state(rng)
    gte_state(rng, st)
    lights(rng, st)
    water_record(rng, st, ARGS + 0x200)
    return [ARGS + 0x200], None, st, {"rot_matrix": [rng.randbytes(20)]}


def case_slope(rng):
    st = base_state(rng)
    v = pick(rng, 0, 1, -1, 150, -150, 149, 0x7FFFFF, -0x800000, rng.randrange(-400, 400), s32(r32(rng)),
             -0x80000000)
    return [v], None, st, {}


def case_lights(rng):
    st = base_state(rng)
    lights(rng, st)
    return [], None, st, {}


def case_water_draw(rng):
    st = base_state(rng)
    gte_state(rng, st)
    lights(rng, st)
    water_record(rng, st, 0x5D1FE0)
    w32(st, 0x4DA214, pick(rng, 0, 0x1FE0000, r32(rng)))
    w32(st, 0x4B2FB0, pick(rng, 0, 1, 1, 1))
    st[0x4DB620] = rng.randbytes(0x84 * 8)
    return [], None, st, {"rot_matrix": [rng.randbytes(20)]}


# ---- snatch ----

def snatch_body(rng, st):
    body = bytearray(rng.randbytes(0x2A40))
    spread = pick(rng, 0x100, 0x1000, 0x4000)
    cx, cy, cz = [rng.randrange(-0x2000, 0x2000) for _ in range(3)]
    clamp = lambda v: max(-0x8000, min(0x7FFF, v))  # noqa: E731
    for i in range(0x48):
        struct.pack_into("<4h", body, 0x344 + 8 * i, clamp(cx + rng.randrange(-spread, spread)),
                         clamp(cy + rng.randrange(-spread, spread)), clamp(cz + rng.randrange(-spread, spread)),
                         rng.getrandbits(15))
    struct.pack_into("<4h", body, 0x6A4, cx, cy, cz, 0)
    body[0x340] = pick(rng, 0, 1, 2, 3, 4, 5, 0xFF, 0x80, rng.getrandbits(8))
    body[0x2A37] = pick(rng, 0, 0, 1, 0x30, 0x34, 0x40, rng.getrandbits(8))
    struct.pack_into("<i", body, 0x180, pick(rng, 0, 1, 0x27, 0x28, 0x100, -5, rng.randrange(-100, 100),
                                             s32(r32(rng))))
    struct.pack_into("<i", body, 0x188, pick(rng, 0, 1, 0x10, 0x40, s32(r32(rng))))
    st[BODY] = bytes(body)
    w32(st, 0x5D1FC0, BODY)
    st[TEXBASE] = rng.randbytes(0x1000)
    w16(st, TEXBASE + 0x2E, rng.randrange(0, 0x40))
    w32(st, 0x6C1EB0, TEXBASE)
    w32(st, 0x6C1E14, TEXBASE + 0x900 - 0x5C * 16)
    w32(st, 0x4B24A0, pick(rng, -1, 0, 1, r32(rng)))
    w8(st, 0x6CCD84, rng.getrandbits(8))


def case_snatch(rng):
    st = base_state(rng)
    gte_state(rng, st)
    snatch_body(rng, st)
    return [BODY], None, st, {}


def case_snatch_void(rng):
    st = base_state(rng)
    gte_state(rng, st)
    snatch_body(rng, st)
    return [], None, st, {}


# ---- thunks ----

def case_thunk(n):
    def gen(rng):
        st = base_state(rng)
        return [r32(rng) for _ in range(n)], None, st, {}
    return gen


# ---- the movie player ----

VIDEO, SOUND, END = 0x0B85120F, 0x90FC0302, 0xD6BD0106


def chunk_stream(rng, count, sound_bias=0.4):
    out = b""
    for _ in range(count):
        kind = pick(rng, VIDEO, SOUND, VIDEO, SOUND, END, r32(rng)) if rng.random() > sound_bias else SOUND
        n = pick(rng, 0, 4, 0x10, rng.randrange(0, 0x60))
        size = n + 8 if rng.randrange(12) else rng.randrange(0, 8)
        out += struct.pack("<II", kind, size) + rng.randbytes(n)
    if rng.randrange(3) == 0:
        out += rng.randbytes(rng.randrange(1, 8))  # a short header at the end
    return out


def movie_record(rng, st, sound=None):
    m = bytearray(rng.randbytes(0x380))
    flags = r32(rng) & 0xFFFF
    flags = flags & ~0x200 | pick(rng, 0, 0x200, 0)
    struct.pack_into("<I", m, 0x1C, flags)
    struct.pack_into("<I", m, 0x40, CBSTUB)
    struct.pack_into("<I", m, 0x48, FILE)
    struct.pack_into("<H", m, 0x12, pick(rng, 0, 1, 1) if sound is None else sound)
    struct.pack_into("<H", m, 0x18, pick(rng, 1, 2, 2, rng.getrandbits(16)))
    struct.pack_into("<H", m, 0x14, pick(rng, 22050, 11025, 44100, r32(rng) >> 16))
    struct.pack_into("<H", m, 0x16, pick(rng, 8, 16))
    struct.pack_into("<H", m, 0x68, pick(rng, 0, 0, 0, 0, 1))
    for off in (0x360, 0x364, 0x368, 0x35C):
        struct.pack_into("<I", m, off, rng.randrange(0, 32))
    struct.pack_into("<I", m, 0x88, pick(rng, 0xF, 0x10, 0x18, 0x20, 8))
    struct.pack_into("<i", m, 0x5C, pick(rng, 0, 1, 2, 3, 5, -1))
    struct.pack_into("<I", m, 0x350, pick(rng, 0, 1, 1))
    struct.pack_into("<I", m, 0x370, pick(rng, 0, 1, 5))
    struct.pack_into("<I", m, 0x94, pick(rng, DSB_SND, DSB_SND, 0))
    for k in range(32):  # rings: some empty, the rest point into the heap's upper part
        struct.pack_into("<I", m, 0x198 + 12 * k, pick(rng, 0, 0, HEAP_END - 0x8000 + 0x100 * k))
        struct.pack_into("<I", m, 0x98 + 8 * k, pick(rng, 0, HEAP_END - 0x4000 + 0x100 * k))
        struct.pack_into("<I", m, 0x9C + 8 * k, pick(rng, 0, 0x10, 0x80, 0x100, rng.randrange(0, 0x200)))
    st[MOVIE] = bytes(m[:0x37C])
    st[FILE] = rng.randbytes(0xC) + struct.pack("<I", pick(rng, 0, 0, 0, 0x10, r32(rng) & 0xFF)) + rng.randbytes(0x30)
    st[HEAP_END - 0x8000] = rng.randbytes(0x8000)
    return m


def sound_globals(rng, st):
    w32(st, 0x4DA28C, pick(rng, DS, DS, 0))
    w32(st, 0x4DA290, pick(rng, DSB_PRI, DSB_PRI, 0))
    w32(st, 0x4DA294, pick(rng, 0, 1))
    w32(st, 0x4DA298, pick(rng, 0, 1))
    w32(st, 0x4DA288, pick(rng, 0x10000, 0x8000, r32(rng)))


def case_chunk_read(rng):
    st = base_state(rng)
    movie_record(rng, st)
    return [], MOVIE, st, {"stream": [chunk_stream(rng, 2)]}


def case_play_loop(rng):
    st = base_state(rng)
    m = movie_record(rng, st)
    sound_globals(rng, st)
    flags = struct.unpack_from("<I", m, 0x1C)[0]
    if flags & 0x100:
        flags |= 0x400
    struct.pack_into("<I", m, 0x1C, flags)
    st[MOVIE] = bytes(m[:0x37C])
    script = {"stream": [chunk_stream(rng, rng.randrange(0, 12))],
              "movie_callback": [pick(rng, 0, 0, 1) for _ in range(rng.randrange(0, 6))] + [1],
              "wait_extra": [pick(rng, 0, 1, 2, 3, 5, -1)],
              "stop_after_frame": [pick(rng, 0, 0, 0, 1) for _ in range(6)],
              "timeGetDevCaps": [(0, pick(rng, 0, 1, 2, 5), pick(rng, 0, 1, 10, 1000000))],
              "GetCurrentPosition": [], "BufGetCaps": [pick(rng, 0x10000, 0x8000)]}
    return [MOVIE], None, st, script


def open_stream(rng, sound):
    n = rng.randrange(0, 0x40)
    hdr = bytearray(rng.randbytes(0x1C))
    hdr[0:5] = pick(rng, b"!Reap", b"!Reap", b"!Reap", b"!Reaq", b"Reap!")
    struct.pack_into("<I", hdr, 8, 0x1C + n)
    struct.pack_into("<h", hdr, 0x10, pick(rng, 0x100, 0x20, 0, rng.randrange(0, 0x400)))
    struct.pack_into("<H", hdr, 0x12, sound)
    out = bytes(hdr) + rng.randbytes(n)
    if sound:
        for _ in range(16 + rng.randrange(0, 3)):
            if rng.randrange(4) == 0:
                out += struct.pack("<II", VIDEO, 8 + 4) + rng.randbytes(4)
            k = rng.randrange(0, 0x20)
            out += struct.pack("<II", SOUND, 8 + k) + rng.randbytes(k)
    return out


def open_config(rng):
    cfg = [r32(rng) for _ in range(10)]
    cfg[0] = r32(rng) & 0xFFFF & ~0x200
    cfg[5] = pick(rng, 0, DS)  # +0x30: the caller's DirectSound
    cfg[6] = pick(rng, 0, DSB_PRI)  # +0x34: the primary buffer
    cfg[9] = CBSTUB
    return cfg


def case_open(rng):
    st = base_state(rng)
    sound_globals(rng, st)
    st[MOVIE] = rng.randbytes(0x37C)
    st[FILE] = bytes(0x40)
    st[ARGS] = b"movies\\intro.fmv\0"
    cfg = open_config(rng)
    st[ARGS + 0x40] = struct.pack("<10I", *cfg)
    sound = pick(rng, 0, 1)
    script = {"stream": [open_stream(rng, sound)], "calloc": [pick(rng, MOVIE, MOVIE, MOVIE, 0)],
              "DirectSoundCreate": [(pick(rng, 0, 0, 1), DS)], "SetCooperativeLevel": [pick(rng, 0, 0, 1)],
              "CreateSoundBuffer": [(pick(rng, 0, 0, 1), DSB_PRI)], "SetFormat": [pick(rng, 0, 0, 1)],
              "DsGetCaps": [pick(rng, 0, 2, 8, 0xA, r32(rng))]}
    return [ARGS, r32(rng), ARGS + 0x40, pick(rng, 0, 0, 0x1234)], None, st, script


def case_close(rng):
    st = base_state(rng)
    m = movie_record(rng, st)
    sound_globals(rng, st)
    struct.pack_into("<I", m, 0x48, pick(rng, FILE, FILE, 0))
    struct.pack_into("<I", m, 0x64, pick(rng, 0, HEAP + 0x40))
    st[MOVIE] = bytes(m[:0x37C])
    return [MOVIE], None, st, {}


def case_run(rng):
    st = base_state(rng)
    sound_globals(rng, st)
    st[MOVIE] = rng.randbytes(0x37C)
    st[FILE] = bytes(0x40)
    st[ARGS] = b"movies\\atd.fmv\0"
    cfg = open_config(rng)
    cfg[0] |= pick(rng, 0, 0x200)
    cfg[0] = cfg[0] & ~0x100 if rng.randrange(2) else cfg[0] | 0x500
    st[ARGS + 0x40] = struct.pack("<10I", *cfg)
    sound = pick(rng, 0, 1)
    stream = open_stream(rng, sound) + chunk_stream(rng, rng.randrange(0, 8))
    script = {"stream": [stream], "calloc": [pick(rng, MOVIE, MOVIE, MOVIE, 0)],
              "CreateThread": [pick(rng, 0x77, 0x77, 0)],
              "movie_callback": [pick(rng, 0, 0, 1) for _ in range(rng.randrange(0, 6))] + [1],
              "timeGetDevCaps": [(0, 1, 10)], "wait_extra": [pick(rng, 0, 1)]}
    return [ARGS, r32(rng), ARGS + 0x40], None, st, script


def case_thread(rng):
    st = base_state(rng)
    m = movie_record(rng, st)
    sound_globals(rng, st)
    flags = struct.unpack_from("<I", m, 0x1C)[0] | 0x400
    struct.pack_into("<I", m, 0x1C, flags)
    struct.pack_into("<I", m, 0x3C, pick(rng, 0, 0x77))
    st[MOVIE] = bytes(m[:0x37C])
    script = {"stream": [chunk_stream(rng, rng.randrange(0, 6))],
              "movie_callback": [pick(rng, 0, 1) for _ in range(rng.randrange(0, 4))] + [1],
              "GetExitCodeThread": [(pick(rng, 0, 1), 0x103)], "timeGetDevCaps": [(0, 1, 10)]}
    return [MOVIE], None, st, script


def case_fatal(rng):
    st = base_state(rng)
    st[ARGS] = b"something failed\0"
    return [], pick(rng, ARGS, 0x4B3080, 0x4B3098), st, {}


def case_sound_init(rng):
    st = base_state(rng)
    m = movie_record(rng, st)
    sound_globals(rng, st)
    flags = r32(rng) & 0xFFFF | pick(rng, 0x800, 0x800, 0)
    struct.pack_into("<I", m, 0x1C, flags)
    struct.pack_into("<I", m, 0x30, pick(rng, 0, DS))
    struct.pack_into("<I", m, 0x34, pick(rng, 0, DSB_PRI))
    st[MOVIE] = bytes(m[:0x37C])
    script = {"DirectSoundCreate": [(pick(rng, 0, 0, 1), pick(rng, DS, DS, 0))],
              "SetCooperativeLevel": [pick(rng, 0, 0, 1)],
              "CreateSoundBuffer": [(pick(rng, 0, 0, 1), DSB_PRI)], "SetFormat": [pick(rng, 0, 0, 1)],
              "DsGetCaps": [pick(rng, 0, 2, 8, 0xA, 0x20, r32(rng))],
              "BufGetCaps": [r32(rng), r32(rng)]}
    return [], MOVIE, st, script


def case_sound_release(rng):
    st = base_state(rng)
    m = movie_record(rng, st)
    sound_globals(rng, st)
    struct.pack_into("<I", m, 0x1C, r32(rng) & 0xFFFF | pick(rng, 0x800, 0x800, 0) | pick(rng, 0x2000, 0))
    st[MOVIE] = bytes(m[:0x37C])
    return [], MOVIE, st, {}


def case_sound_start(rng):
    st = base_state(rng)
    movie_record(rng, st)
    sound_globals(rng, st)
    lo = pick(rng, 0, 1, 2, 5, 16)
    hi = pick(rng, 0, 1, 2, 10, 1000000)
    script = {"CreateSoundBuffer": [(pick(rng, 0, 0, 1), DSB_SND)], "BufGetCaps": [pick(rng, 0x10000, r32(rng))],
              "timeGetDevCaps": [(pick(rng, 0, 0, 0, 1), lo, hi)], "timeSetEvent": [pick(rng, 0x42, 0x42, 0)]}
    return [], MOVIE, st, script


def case_timer(rng):
    st = base_state(rng)
    m = movie_record(rng, st)
    struct.pack_into("<I", m, 0x32C, pick(rng, 0, 0, 0, 1))
    struct.pack_into("<I", m, 0x374, pick(rng, 0, 1, 7))
    struct.pack_into("<I", m, 0x350, pick(rng, 0, 1, 1))
    struct.pack_into("<I", m, 0x318, pick(rng, 0, 1))
    size = pick(rng, 0x8000, 0x1000, 0x800, rng.randrange(1, 0x10000))
    struct.pack_into("<I", m, 0x354, size)
    struct.pack_into("<I", m, 0x378, rng.randrange(0, size + 0x400))
    struct.pack_into("<I", m, 0x358, pick(rng, 0, rng.randrange(0, size), size - 1))
    st[MOVIE] = bytes(m[:0x37C])
    st[LOCKBUF] = rng.randbytes(0x2000)
    play = rng.randrange(0, size)
    lock = rng.randrange(0, 0x200)
    script = {"GetCurrentPosition": [(play, rng.randrange(0, size))], "Lock": [pick(rng, None, lock, 0)]}
    return [r32(rng), r32(rng), MOVIE, r32(rng), r32(rng)], None, st, script


# name: (original address, case builder, returns a value)
TESTS = {
    "net_message_send_to": (0x48CDD0, case_send_to, True),
    "net_packet_filter": (0x48CF40, case_packet_filter, True),
    "net_session_lock": (0x48D110, case_session, True),
    "net_session_unlock": (0x48D1C0, case_session, True),
    "net_chat_send": (0x48D270, case_chat_send, True),
    "net_chat_clear": (0x48D350, case_void, False),
    "net_send_queue_low": (0x48D390, case_queue, True),
    "net_message_queue_count": (0x48D3E0, case_queue, True),
    "net_connection_available": (0x48D450, case_conn, True),
    "net_players_timeout": (0x48D4A0, case_timeout, False),
    "menu_water_init": (0x48D4F0, case_water_init, False),
    "water_mesh_alloc": (0x48D530, case_water_rec, False),
    "water_mesh_reset": (0x48D580, case_water_rec, False),
    "water_textures_set": (0x48D5A0, case_water_rec, False),
    "water_texture_get": (0x48D600, case_water_rec, False),
    "snatch_draw": (0x48FDD0, case_snatch_void, False),
    "snatch_mesh_draw": (0x48FDF0, case_snatch, False),
    "snatch_state_check": (0x4906F0, case_snatch_void, False),
    "menu_camera_set": (0x490740, case_camera, False),
    "water_mesh_transform": (0x4908A0, case_water_transform, False),
    "water_slope_lookup": (0x490B90, case_slope, True),
    "water_lights_set": (0x490BE0, case_lights, False),
    "water_light_matrix_set": (0x490BF0, case_lights, False),
    "water_colour_matrix_set": (0x490CA0, case_lights, False),
    "water_back_colour_set": (0x490E40, case_lights, False),
    "water_mesh_draw": (0x490ED0, case_water_draw, False),
    "ddraw_create_thunk": (0x4910F0, case_thunk(3), True),
    "ddraw_enumerate_thunk": (0x4910F6, case_thunk(2), True),
    "dinput_create_thunk": (0x4910FC, case_thunk(4), True),
    "dplay_lobby_create_thunk": (0x492B88, case_thunk(5), True),
    "movie_chunk_read": (0x492B90, case_chunk_read, True),
    "movie_play_loop": (0x492DC0, case_play_loop, True),
    "movie_open": (0x492F90, case_open, True),
    "movie_close": (0x493140, case_close, True),
    "movie_player_run": (0x493210, case_run, True),
    "movie_thread_proc": (0x4932D0, case_thread, True),
    "movie_fatal_error": (0x493340, case_fatal, False),
    "movie_sound_init": (0x493360, case_sound_init, False),
    "movie_sound_release": (0x493590, case_sound_release, False),
    "movie_sound_start": (0x4935E0, case_sound_start, False),
    "movie_sound_timer": (0x4937E0, case_timer, False),
}
SIZES = {0x4937E0: 0x22E}
REGS = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def mask_logs(la, lb, lo):
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
        xo = lo[i] if i < len(lo) else None
        na, no = fix(ea, eb, xo)
        la[i] = na
        if i < len(lo):
            lo[i] = no


def run_once(m, stubs, data, scratch, target, args, ecx, cw, fill, script, returns):
    m.write(DATA, bytes(data))
    m.write(SCR, bytes(scratch))
    m.write(E.STACK, bytes([fill]) * E.STACK_SIZE)
    for i, reg in enumerate(REGS):
        m.uc.reg_write(reg, 0x11111111 * (i + 1))
    m.uc.reg_write(UC_X86_REG_ECX, ecx if ecx is not None else 0x55555555)
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    stubs.reset(script)
    r = m.call(target, [a & 0xFFFFFFFF for a in args])
    if not returns and r[0] == "ok":
        r = ("ok",)
    regs = tuple(m.uc.reg_read(reg) for reg in REGS) + (m.uc.reg_read(UC_X86_REG_ESP),
                                                          (m.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7,
                                                          m.uc.reg_read(UC_X86_REG_FPTAG))
    if r[0] != "ok":
        regs = ()  # faulted / stopped: the registers there differ
    mem = bytearray(m.read(DATA, DATA_SIZE) + m.read(SCR, SCR_SIZE))
    if on_stack(struct.unpack_from("<I", mem, 0x6CCD74 - DATA)[0]):
        mem[0x6CCD74 - DATA:0x6CCD78 - DATA] = b"STAK"  # the transform's record pointer: a stack address
    return r, regs, list(stubs.log), mem


def run_case(m, stubs, base, name, address, builder, returns, rng):
    args, ecx, st, script = builder(rng)
    cw = pick(rng, 0x27F, 0x37F, 0x07F)
    data = bytearray(base)
    scratch = bytearray(SCR_SIZE)
    for a, v in st.items():
        if DATA <= a < DATA + DATA_SIZE:
            data[a - DATA:a - DATA + len(v)] = v
        else:
            scratch[a - SCR:a - SCR + len(v)] = v
    a = run_once(m, stubs, data, scratch, address, args, ecx, cw, 0xCC, script, returns)
    b = run_once(m, stubs, data, scratch, address, args, ecx, cw, 0x5A, script, returns)
    o = run_once(m, stubs, data, scratch, m.symbols[name], args, ecx, cw, 0xCC, script, returns)
    am, om = a[3], o[3]
    for k in range(len(am)):
        if am[k] != b[3][k]:
            am[k] = 0
            om[k] = 0
    la, lo = a[2], o[2]
    mask_logs(la, b[2], lo)
    if a[0] == o[0] and a[1] == o[1] and la == lo and am == om:
        return None, a[0]
    msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} ecx {ecx} cw {cw:#x}: original {a[0]} ours {o[0]}"
    if a[1] != o[1]:
        msg += f"\n  registers / esp / x87 differ: {[hex(x) for x in a[1]]} vs {[hex(x) for x in o[1]]}"
    if am != om:
        diff = [i for i in range(0, len(am), 4) if am[i:i + 4] != om[i:i + 4]]
        where = [hex(DATA + i if i < DATA_SIZE else SCR + i - DATA_SIZE) for i in diff[:8]]
        msg += f"\n  memory differs at {where} ({len(diff)} words)"
        if "-v" in sys.argv:
            msg += f"\n  orig {[am[i:i + 4].hex() for i in diff[:8]]}\n  ours {[om[i:i + 4].hex() for i in diff[:8]]}"
    if la != lo:
        k = next((i for i in range(min(len(la), len(lo))) if la[i] != lo[i]), min(len(la), len(lo)))
        msg += f"\n  calls from #{k}: orig {la[k:k + 2]}\n                ours {lo[k:k + 2]}"
    return msg, a[0]


def func_sizes():
    sizes = dict(SIZES)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "groups", "pv_owned.txt")
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
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    base = m.read(DATA, DATA_SIZE)
    rng = random.Random(0x5E)
    sizes = func_sizes()
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
        hook = None
        size = sizes.get(address, 0)
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address,
                                 end=address + size - 1)
        for _ in range(iterations):
            msg, r = run_case(m, stubs, base, name, address, builder, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if hook is not None:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:12]}" if missed else "") + "]"
        note = f" ({errors} runs faulted / stopped the same way in both)" if errors else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
