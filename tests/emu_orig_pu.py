#!/usr/bin/env python3
"""Differential test of orig_pu.c (group PU: the DirectInput force-feedback
helpers and the DirectPlay network file, 0x489de0-0x48cdc7) against the
original, on random state.

Windows imports (stdcall, through their import slots), COM methods (fake
DirectPlay / DirectPlayLobby / DirectInputEffect objects whose vtables
point at stubs) and the callees outside the group that allocate, touch
threads, text, the front end or other network code are logging stubs in
both runs, with scripted results; the log records as many arguments as
each callee reads, with buffers and structures by content and stack
pointers as "stack". The callbacks the original passes as immediates
(0x48b3e0, 0x48b320, 0x48ce90) are logged as those addresses for ours too.
EnumConnections' stub fills the connection table itself (as the callback
would). Pure game code runs for real in both: mem_copy 0x476f30, mem_fill
0x476f50, the serial set-up tables 0x48cff0 / 0x48d070 / 0x48d090 /
0x48d0e0, the sequence 0x48c920, the checksum 0x48cab0. The group's own
functions run for real (the original's call the original's, ours ours).

Each case runs the original twice with different stack fills: memory and
logged-buffer bytes that differ between those runs (stack leftovers) are
masked before the comparison with ours. Compared: the return value, ebx /
esi / edi / ebp / esp after the return, the globals and scratch memory,
the call log.

    tests/emu_orig_pu.py EXE build/emu_pu.elf [iterations] [names...] [-v] [-c]
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
SCR_SIZE = 0x40000
CFG = SCR + 0x0000  # the config table (0x200)
COMO = SCR + 0x0400  # fake COM objects (0x10 each)
CONNS = SCR + 0x0800  # connection blobs (0x400)
ARG = SCR + 0x1000  # strings, messages, argument buffers (0x3000)
RECV = SCR + 0x4000  # receive buffer (0x1000)
SEND = SCR + 0x5000  # send ring (0x8000)
EFF = SCR + 0xD000  # 0x489250's DIEFFECT pattern source (unused)
INBOX = SCR + 0x10000  # inbox (0x20010)
STUBPAGE = 0x02200000

REGIONS = [(0x4B0000, 0x3000), (0x4C2000, 0x1000), (0x4D6000, 0x4000), (0x5D2000, 0x5000), (0x6C7000, 0x2000)]

DP_OBJ, LOBBY1, LOBBY2, DP_TMP = COMO, COMO + 0x10, COMO + 0x20, COMO + 0x30
FX = [COMO + 0x40 + 0x10 * i for i in range(8)]

# stdcall imports: slot -> (name, argument count)
IAT = {
    0x4A3038: ("DirectPlayLobbyCreateA", 5),
    0x4A3064: ("WaitForSingleObject", 2),
    0x4A3080: ("ReleaseMutex", 1),
    0x4A3084: ("OpenMutexA", 3),
    0x4A3088: ("WaitForMultipleObjects", 4),
    0x4A30A8: ("SetEvent", 1),
    0x4A30AC: ("CreateEventA", 4),
    0x4A30FC: ("CreateMutexA", 3),
    0x4A3100: ("CloseHandle", 1),
    0x4A3288: ("EndDialog", 2),
    0x4A328C: ("DialogBoxParamA", 5),
    0x4A3290: ("SendDlgItemMessageA", 5),
    0x4A32B4: ("timeGetTime", 0),
    0x4A3304: ("CoCreateInstance", 5),
}
# COM vtables: interface -> {offset: (name, argument count with this)}
VTABLES = {
    "dp": {0x08: ("Release", 1), 0x10: ("Close", 1), 0x18: ("CreatePlayer", 7), 0x24: ("DestroyPlayer", 2),
           0x38: ("GetCaps", 3), 0x48: ("GetPlayerAddress", 4), 0x60: ("Open", 3), 0x64: ("Receive", 6),
           0x8C: ("EnumConnections", 5), 0x98: ("InitializeConnection", 3), 0xB8: ("GetPlayerFlags", 3),
           0xC4: ("SendEx", 10), 0xCC: ("CancelMessage", 3)},
    "lobby": {0x00: ("QueryInterface", 3), 0x08: ("LbRelease", 1), 0x14: ("EnumAddress", 5),
              0x38: ("CreateCompoundAddress", 5)},
    "fx": {0x18: ("SetParameters", 3), 0x1C: ("Start", 3), 0x20: ("Stop", 1), 0x24: ("GetEffectStatus", 2)},
}
VT_BASE = {"dp": STUBPAGE + 0x800, "lobby": STUBPAGE + 0xA00, "fx": STUBPAGE + 0xC00}
# cdecl callees at their own addresses
STUBS = {
    0x489250: "dieffect_fill",
    0x489A80: "dinput_stop_a",
    0x489B90: "dinput_stop_b",
    0x489CA0: "dinput_stop_c",
    0x476AE0: "block_alloc",
    0x476B40: "block_free",
    0x476B80: "arena_alloc",
    0x498D7C: "beginthread",
    0x45F790: "net_flag9_set",
    0x4609B0: "net_players_compact",
    0x45EF30: "net_state_bit5",
    0x45CB90: "net_player_left",
    0x45D170: "net_race_abort",
    0x45DDE0: "net_players_send",
    0x45F310: "net_states_send",
    0x42E6D0: "text_line",
    0x4979F0: "sprintf",
    0x48D270: "message_box",
    0x452D20: "menu_dirty",
    0x45D230: "game_message_handle",
    0x43692A: "game_time",
    0x48CF40: "packet_filter",
    0x45EBA0: "race_over",
    0x48BF70: "drop_player",
    0x48BE20: "player_set",
    0x48C990: "buffer_reset",
    0x48AC40: "enum_address_cb",  # never reached (EnumAddress is a stub); listed to catch it
}
CALLBACKS = {"dplay_connection_enum_cb": 0x48B3E0, "dplay_provider_dialog_proc": 0x48B320,
             "dplay_receive_thread": 0x48CE90}

GUIDS = [0x4A4100, 0x4A40F0, 0x4A4110, 0x4A4120]  # TCP/IP, IPX, serial, modem

DPERR_PENDING = 0x8877010E
DPERR_NOMESSAGES = 0x887700BE
DPERR_CONNECTING = 0x8877015E


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


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
        self.vtable = {}
        for iface, methods in VTABLES.items():
            table = VT_BASE[iface]
            for off, (name, n) in methods.items():
                address = table + 0x100 + off
                m.uc.mem_write(table + off, struct.pack("<I", address))
                self.calls[address] = (name, n, True)
        for address, name in STUBS.items():
            self.calls[address] = (name, 0, False)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.ours = {m.symbols[k]: v for k, v in CALLBACKS.items() if k in m.symbols}
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.time = self.script.pop("time0", [0x10000])[0]

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

    def _stub(self, uc, address):
        name, n, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<11I", bytes(uc.mem_read(esp, 44)))
        ret, a = words[0], list(words[1:])
        nx = self._next
        w = uc.mem_write
        eax = 0
        log = ()
        if name == "dieffect_fill":
            log = (ptr(a[0]),)
            w(a[0], bytes(range(0x40, 0x74)))
        elif name in ("dinput_stop_a", "dinput_stop_b", "dinput_stop_c", "block_free", "net_flag9_set",
                      "net_player_left", "drop_player"):
            log = (a[0],)
        elif name in ("net_players_compact", "net_race_abort", "net_players_send", "net_states_send", "menu_dirty",
                      "race_over", "buffer_reset"):
            eax = nx(name)
        elif name == "block_alloc":
            log = (a[0], a[1])
            eax = nx(name)
        elif name == "arena_alloc":
            log = (a[0],)
            eax = nx(name, RECV)
        elif name == "beginthread":
            log = (self.cb(a[0]), a[1], a[2])
            eax = nx(name)
        elif name in ("net_state_bit5", "game_time"):
            eax = nx(name)
        elif name == "text_line":
            res, text = nx(name, (0, b"x"))
            log = (ptr(a[0]), a[1], a[2], a[3])
            if res != 0xFFFFFFFF:
                w(a[0], text + b"\0")
            eax = res
        elif name == "sprintf":
            s = c_string(uc, a[1], 0x7F)
            log = (ptr(a[0]), ptr(a[1]), s)
            w(a[0], s + b"\0")
            eax = len(s)
        elif name == "message_box":
            log = (ptr(a[0]), rd(uc, a[0], 0x80))
        elif name == "game_message_handle":
            log = (ptr(a[0]), rd(uc, a[0], 0x10), a[1], a[2], a[3])
        elif name == "packet_filter":
            log = (ptr(a[0]), rd(uc, a[0], 0x10), a[1])
            eax = nx(name)
        elif name == "player_set":
            log = (a[0], c_string(uc, a[1], 0x20) if a[1] else 0, a[2])
            eax = nx(name, 1)
        elif name == "enum_address_cb":
            log = tuple(a[:4])
        # stdcall imports
        elif name == "DirectPlayLobbyCreateA":
            hr, obj = nx(name, (0, LOBBY1))
            log = (a[0], ptr(a[1]), a[2], a[3], a[4])
            w(a[1], struct.pack("<I", obj))
            eax = hr
        elif name == "CoCreateInstance":
            hr, obj = nx(name, (0, DP_TMP))
            log = (a[0], a[1], a[2], a[3], ptr(a[4]))
            if not hr:
                w(a[4], struct.pack("<I", obj))
            eax = hr
        elif name in ("WaitForSingleObject", "EndDialog"):
            log = (a[0], a[1])
            eax = nx(name)
        elif name in ("ReleaseMutex", "CloseHandle", "SetEvent"):
            log = (a[0],)
            eax = nx(name, 1)
        elif name == "OpenMutexA":
            log = (a[0], a[1], c_string(uc, a[2]))
            eax = nx(name)
        elif name == "CreateMutexA":
            log = (a[0], a[1], c_string(uc, a[2]))
            eax = nx(name)
        elif name == "CreateEventA":
            log = (a[0], a[1], a[2], c_string(uc, a[3]))
            eax = nx(name)
        elif name == "WaitForMultipleObjects":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 8), a[2], a[3])
            eax = nx(name, 1)
        elif name == "DialogBoxParamA":
            log = (a[0], a[1], a[2], self.cb(a[3]), a[4])
            eax = nx(name)
        elif name == "SendDlgItemMessageA":
            log = (a[0], a[1], a[2], a[3], c_string(uc, a[4], 0x80) if a[2] == 0x143 else a[4])
            eax = nx(name)
        elif name == "timeGetTime":
            t = nx(name, None)
            if t is None:
                self.time = (self.time + 300) & 0xFFFFFFFF
                t = self.time
            eax = t
        # IDirectPlay4A
        elif name in ("Release", "Close", "LbRelease", "Stop"):
            log = (a[0],)
            eax = nx(name)
        elif name == "CreatePlayer":
            hr, pid = nx(name, (0, 0x77))
            log = (a[0], ptr(a[1]), rd(uc, a[2], 16), a[3], a[4], a[5], a[6])
            w(a[1], struct.pack("<I", pid))
            eax = hr
        elif name == "DestroyPlayer":
            log = (a[0], a[1])
        elif name == "GetCaps":
            hr, caps = nx(name, (0, bytes(0x28)))
            log = (a[0], rd(uc, a[1], 4), a[2])
            w(a[1], caps)
            eax = hr
        elif name == "GetPlayerAddress":
            hr, size, data = nx(name, (0, 0x20, b""))
            log = (a[0], a[1], ptr(a[2]) if a[2] else 0, rd(uc, a[3], 4))
            w(a[3], struct.pack("<I", size))
            if a[2]:
                w(a[2], data)
            eax = hr
        elif name == "Open":
            desc = rd(uc, a[1], 0x50)
            log = (a[0], desc, c_string(uc, struct.unpack_from("<I", desc, 0x30)[0], 0x80), a[2])
            eax = nx(name)
        elif name == "Receive":
            hr, frm, to, data = nx(name, (DPERR_NOMESSAGES, 0, 0, b""))
            log = (a[0], rd(uc, a[1], 4), rd(uc, a[2], 4), a[3], a[4], rd(uc, a[5], 4))
            if hr == 0:
                w(a[1], struct.pack("<I", frm))
                w(a[2], struct.pack("<I", to))
                w(a[5], struct.pack("<I", len(data)))
                w(a[4], data)
            eax = hr
        elif name == "EnumConnections":
            hr, conns = nx(name, (0, []))
            log = (a[0], a[1], self.cb(a[2]), a[3], a[4])
            for i, (guid, cp, nm) in enumerate(conns):
                rec = 0x5D2064 + 0xA0 * i
                w(rec, guid)
                w(rec + 0x10, struct.pack("<I", cp))
                w(rec + 0x20, nm + b"\0")
            w(0x5D26A4, struct.pack("<I", len(conns)))
            eax = hr
        elif name == "InitializeConnection":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 0x20), a[2])
            eax = nx(name)
        elif name == "GetPlayerFlags":
            hr, flags = nx(name, (0, 0))
            log = (a[0], a[1])
            w(a[2], struct.pack("<I", flags))
            eax = hr
        elif name == "SendEx":
            hr, msgid = nx(name, (0, 0x99))
            log = (a[0], a[1], a[2], a[3], ptr(a[4]), rd(uc, a[4], min(a[5], 0x40)), a[5], a[6], a[7], a[8])
            w(a[9], struct.pack("<I", msgid))
            eax = hr
        elif name == "CancelMessage":
            log = (a[0], a[1], a[2])
        # IDirectPlayLobby2A
        elif name == "QueryInterface":
            hr, obj = nx(name, (0, LOBBY2))
            log = (a[0], a[1], ptr(a[2]))
            w(a[2], struct.pack("<I", obj))
            eax = hr
        elif name == "EnumAddress":
            log = (a[0], self.cb(a[1]), ptr(a[2]), rd(uc, a[2], min(a[3], 0x40)), a[3], a[4])
            eax = nx(name)
        elif name == "CreateCompoundAddress":
            hr, size, data = nx(name, (0, 0x40, bytes(range(0x20))))
            elems = []
            for k in range(min(a[2], 4)):
                e = rd(uc, a[1] + 0x18 * k, 0x18)
                dsize, dptr = struct.unpack_from("<II", e, 0x10)
                elems.append((e[:0x14], ptr(dptr), rd(uc, dptr, min(dsize, 0x40))))
            log = (a[0], tuple(elems), a[2], ptr(a[3]), rd(uc, a[4], 4))
            w(a[4], struct.pack("<I", size))
            if size <= 0x100:
                w(a[3], data)
            eax = hr
        # IDirectInputEffect
        elif name == "SetParameters":
            e = rd(uc, a[1], 0x34)
            p = struct.unpack_from("<I", e, 0x30)[0]
            log = (a[0], e[:0x30], ptr(p), rd(uc, p, 16), a[2])
            eax = nx(name)
        elif name == "Start":
            log = (a[0], a[1], a[2])
            eax = nx(name)
        elif name == "GetEffectStatus":
            log = (a[0], ptr(a[1]))
            w(a[1], struct.pack("<I", nx(name)))
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

    def putb(self, address, data):
        buf, off = self._where(address)
        buf[off:off + len(data)] = data

    def get(self, address, fmt):
        buf, off = self._where(address)
        return struct.unpack_from(fmt, buf, off)[0]


def name_bytes(rng, lo=0, hi=12):
    return bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij0123456789 .") for _ in range(rng.randrange(lo, hi)))


IDS = [0x4A4100, 0x4A40F0, 0x4A4110, 0x4A4120]  # TCP/IP, IPX, serial, modem

DPERR_PENDING = 0x8877010E
DPERR_NOMESSAGES = 0x887700BE
DPERR_CONNECTING = 0x8877015E


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


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
        self.vtable = {}
        for iface, methods in VTABLES.items():
            table = VT_BASE[iface]
            for off, (name, n) in methods.items():
                address = table + 0x100 + off
                m.uc.mem_write(table + off, struct.pack("<I", address))
                self.calls[address] = (name, n, True)
        for address, name in STUBS.items():
            self.calls[address] = (name, 0, False)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.ours = {m.symbols[k]: v for k, v in CALLBACKS.items() if k in m.symbols}
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.time = self.script.pop("time0", [0x10000])[0]

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

    def _stub(self, uc, address):
        name, n, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<11I", bytes(uc.mem_read(esp, 44)))
        ret, a = words[0], list(words[1:])
        nx = self._next
        w = uc.mem_write
        eax = 0
        log = ()
        if name == "dieffect_fill":
            log = (ptr(a[0]),)
            w(a[0], bytes(range(0x40, 0x74)))
        elif name in ("dinput_stop_a", "dinput_stop_b", "dinput_stop_c", "block_free", "net_flag9_set",
                      "net_player_left", "drop_player"):
            log = (a[0],)
        elif name in ("net_players_compact", "net_race_abort", "net_players_send", "net_states_send", "menu_dirty",
                      "race_over", "buffer_reset"):
            eax = nx(name)
        elif name == "block_alloc":
            log = (a[0], a[1])
            eax = nx(name)
        elif name == "arena_alloc":
            log = (a[0],)
            eax = nx(name, RECV)
        elif name == "beginthread":
            log = (self.cb(a[0]), a[1], a[2])
            eax = nx(name)
        elif name in ("net_state_bit5", "game_time"):
            eax = nx(name)
        elif name == "text_line":
            res, text = nx(name, (0, b"x"))
            log = (ptr(a[0]), a[1], a[2], a[3])
            if res != 0xFFFFFFFF:
                w(a[0], text + b"\0")
            eax = res
        elif name == "sprintf":
            s = c_string(uc, a[1], 0x7F)
            log = (ptr(a[0]), ptr(a[1]), s)
            w(a[0], s + b"\0")
            eax = len(s)
        elif name == "message_box":
            log = (ptr(a[0]), rd(uc, a[0], 0x80))
        elif name == "game_message_handle":
            log = (ptr(a[0]), rd(uc, a[0], 0x10), a[1], a[2], a[3])
        elif name == "packet_filter":
            log = (ptr(a[0]), rd(uc, a[0], 0x10), a[1])
            eax = nx(name)
        elif name == "player_set":
            log = (a[0], c_string(uc, a[1], 0x20) if a[1] else 0, a[2])
            eax = nx(name, 1)
        elif name == "enum_address_cb":
            log = tuple(a[:4])
        # stdcall imports
        elif name == "DirectPlayLobbyCreateA":
            hr, obj = nx(name, (0, LOBBY1))
            log = (a[0], ptr(a[1]), a[2], a[3], a[4])
            w(a[1], struct.pack("<I", obj))
            eax = hr
        elif name == "CoCreateInstance":
            hr, obj = nx(name, (0, DP_TMP))
            log = (a[0], a[1], a[2], a[3], ptr(a[4]))
            if not hr:
                w(a[4], struct.pack("<I", obj))
            eax = hr
        elif name in ("WaitForSingleObject", "EndDialog"):
            log = (a[0], a[1])
            eax = nx(name)
        elif name in ("ReleaseMutex", "CloseHandle", "SetEvent"):
            log = (a[0],)
            eax = nx(name, 1)
        elif name == "OpenMutexA":
            log = (a[0], a[1], c_string(uc, a[2]))
            eax = nx(name)
        elif name == "CreateMutexA":
            log = (a[0], a[1], c_string(uc, a[2]))
            eax = nx(name)
        elif name == "CreateEventA":
            log = (a[0], a[1], a[2], c_string(uc, a[3]))
            eax = nx(name)
        elif name == "WaitForMultipleObjects":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 8), a[2], a[3])
            eax = nx(name, 1)
        elif name == "DialogBoxParamA":
            log = (a[0], a[1], a[2], self.cb(a[3]), a[4])
            eax = nx(name)
        elif name == "SendDlgItemMessageA":
            log = (a[0], a[1], a[2], a[3], c_string(uc, a[4], 0x80) if a[2] == 0x143 else a[4])
            eax = nx(name)
        elif name == "timeGetTime":
            t = nx(name, None)
            if t is None:
                self.time = (self.time + 300) & 0xFFFFFFFF
                t = self.time
            eax = t
        # IDirectPlay4A
        elif name in ("Release", "Close", "LbRelease", "Stop"):
            log = (a[0],)
            eax = nx(name)
        elif name == "CreatePlayer":
            hr, pid = nx(name, (0, 0x77))
            log = (a[0], ptr(a[1]), rd(uc, a[2], 16), a[3], a[4], a[5], a[6])
            w(a[1], struct.pack("<I", pid))
            eax = hr
        elif name == "DestroyPlayer":
            log = (a[0], a[1])
        elif name == "GetCaps":
            hr, caps = nx(name, (0, bytes(0x28)))
            log = (a[0], rd(uc, a[1], 4), a[2])
            w(a[1], caps)
            eax = hr
        elif name == "GetPlayerAddress":
            hr, size, data = nx(name, (0, 0x20, b""))
            log = (a[0], a[1], ptr(a[2]) if a[2] else 0, rd(uc, a[3], 4))
            w(a[3], struct.pack("<I", size))
            if a[2]:
                w(a[2], data)
            eax = hr
        elif name == "Open":
            desc = rd(uc, a[1], 0x50)
            log = (a[0], desc, c_string(uc, struct.unpack_from("<I", desc, 0x30)[0], 0x80), a[2])
            eax = nx(name)
        elif name == "Receive":
            hr, frm, to, data = nx(name, (DPERR_NOMESSAGES, 0, 0, b""))
            log = (a[0], rd(uc, a[1], 4), rd(uc, a[2], 4), a[3], a[4], rd(uc, a[5], 4))
            if hr == 0:
                w(a[1], struct.pack("<I", frm))
                w(a[2], struct.pack("<I", to))
                w(a[5], struct.pack("<I", len(data)))
                w(a[4], data)
            eax = hr
        elif name == "EnumConnections":
            hr, conns = nx(name, (0, []))
            log = (a[0], a[1], self.cb(a[2]), a[3], a[4])
            for i, (guid, cp, nm) in enumerate(conns):
                rec = 0x5D2064 + 0xA0 * i
                w(rec, guid)
                w(rec + 0x10, struct.pack("<I", cp))
                w(rec + 0x20, nm + b"\0")
            w(0x5D26A4, struct.pack("<I", len(conns)))
            eax = hr
        elif name == "InitializeConnection":
            log = (a[0], ptr(a[1]), rd(uc, a[1], 0x20), a[2])
            eax = nx(name)
        elif name == "GetPlayerFlags":
            hr, flags = nx(name, (0, 0))
            log = (a[0], a[1])
            w(a[2], struct.pack("<I", flags))
            eax = hr
        elif name == "SendEx":
            hr, msgid = nx(name, (0, 0x99))
            log = (a[0], a[1], a[2], a[3], ptr(a[4]), rd(uc, a[4], min(a[5], 0x40)), a[5], a[6], a[7], a[8])
            w(a[9], struct.pack("<I", msgid))
            eax = hr
        elif name == "CancelMessage":
            log = (a[0], a[1], a[2])
        # IDirectPlayLobby2A
        elif name == "QueryInterface":
            hr, obj = nx(name, (0, LOBBY2))
            log = (a[0], a[1], ptr(a[2]))
            w(a[2], struct.pack("<I", obj))
            eax = hr
        elif name == "EnumAddress":
            log = (a[0], self.cb(a[1]), ptr(a[2]), rd(uc, a[2], min(a[3], 0x40)), a[3], a[4])
            eax = nx(name)
        elif name == "CreateCompoundAddress":
            hr, size, data = nx(name, (0, 0x40, bytes(range(0x20))))
            elems = []
            for k in range(min(a[2], 4)):
                e = rd(uc, a[1] + 0x18 * k, 0x18)
                dsize, dptr = struct.unpack_from("<II", e, 0x10)
                elems.append((e[:0x14], ptr(dptr), rd(uc, dptr, min(dsize, 0x40))))
            log = (a[0], tuple(elems), a[2], ptr(a[3]), rd(uc, a[4], 4))
            w(a[4], struct.pack("<I", size))
            if size <= 0x100:
                w(a[3], data)
            eax = hr
        # IDirectInputEffect
        elif name == "SetParameters":
            e = rd(uc, a[1], 0x34)
            p = struct.unpack_from("<I", e, 0x30)[0]
            log = (a[0], e[:0x30], ptr(p), rd(uc, p, 16), a[2])
            eax = nx(name)
        elif name == "Start":
            log = (a[0], a[1], a[2])
            eax = nx(name)
        elif name == "GetEffectStatus":
            log = (a[0], ptr(a[1]))
            w(a[1], struct.pack("<I", nx(name)))
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

    def putb(self, address, data):
        buf, off = self._where(address)
        buf[off:off + len(data)] = data

    def get(self, address, fmt):
        buf, off = self._where(address)
        return struct.unpack_from(fmt, buf, off)[0]


def name_bytes(rng, lo=0, hi=12):
    return bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij0123456789 .") for _ in range(rng.randrange(lo, hi)))


def guid_bytes(st, which):
    return bytes(st.mem[0x4B0000][0:0]) if which is None else None


EXE_GUIDS = {}


def guid_of(st, address):
    return bytes(st.mem_exe[address])


IDS = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77]


def world(st, rng):
    st.put(0x4B2198, "<I", CFG)
    for off, n in ((0xA0, 0x18), (0xC0, 0x18), (0xE0, 0x18)):
        st.putb(CFG + off, name_bytes(rng, 0, n) + b"\0")
    st.put(CFG + 0x80, "<B", pick(rng, 0, 1, 2, 3, 4, 0xFF))
    for off in (0x3A, 0x3C, 0x3D, 0x3E, 0x3F):
        st.put(CFG + off, "<B", rng.randrange(0, 12))
    st.put(CFG + 0x34, "<B", pick(rng, 0, 0, 1))
    st.put(CFG + 0x37, "<B", pick(rng, 0, 0, 1))
    st.put(0x4C2458, "<B", rng.randrange(0, 4))
    st.put(0x5D2060, "<I", DP_OBJ if rng.random() < 0.92 else 0)
    st.put(0x5D622C, "<I", rng.getrandbits(12) | pick(rng, 0, 7, 7, 0x27, 0x207, 0x227))
    st.put(0x6C8000, "<I", r32(rng))
    st.put(0x6C7FFC, "<I", r32(rng))
    # fake COM objects
    st.put(DP_OBJ, "<I", VT_BASE["dp"])
    st.put(DP_TMP, "<I", VT_BASE["dp"])
    st.put(LOBBY1, "<I", VT_BASE["lobby"])
    st.put(LOBBY2, "<I", VT_BASE["lobby"])
    for f in FX:
        st.put(f, "<I", VT_BASE["fx"])
    # players
    for i in range(6):
        p = 0x5D2CD0 + i * 0x66C
        st.put(p, "<I", pick(rng, 0, 1, 1))
        st.put(p + 4, "<I", pick(rng, *IDS, 0))
        st.putb(p + 8, name_bytes(rng) + b"\0")
    st.put(0x5D5358, "<i", rng.randrange(-1, 8))
    st.put(0x5D535C, "<i", pick(rng, 0, 0, 1, 2, -1))
    st.put(0x5D5360, "<I", pick(rng, *IDS))
    # acknowledgement records
    for i in range(0x40):
        r = 0x5D5368 + i * 0x18
        st.put(r, "<I", pick(rng, 0, 0, 1, 1, 2))
        st.put(r + 4, "<I", pick(rng, 0x99, r32(rng)))
        st.put(r + 8, "<I", rng.randrange(0x8000, 0x12000))
        st.put(r + 0xC, "<I", pick(rng, *IDS))
        st.put(r + 0x10, "<H", rng.randrange(0, 8))
        st.put(r + 0x12, "<H", rng.getrandbits(4))
        st.put(r + 0x14, "<I", rng.randrange(0, 4))
    # sent log
    for i in range(0x20):
        r = 0x5D5E18 + i * 0x18
        data = pick(rng, 0, SEND + rng.randrange(0, 0x7F00))
        size = rng.randrange(4, 0x40)
        st.put(r, "<I", data)
        st.put(r + 4, "<I", size)
        st.put(r + 8, "<I", pick(rng, *IDS))
        st.put(r + 0xC, "<I", pick(rng, *IDS))
        st.put(r + 0x10, "<I", pick(rng, 0, 1, 0x200, 0x600, 0x401, r32(rng)))
        st.put(r + 0x14, "<I", rng.randrange(0, 4))
        if data:
            st.put(data + 2, "<H", rng.randrange(0, 8))
    st.put(0x5D6118, "<b", pick(rng, 0, 5, 0x1F, rng.randrange(-128, 128)))
    st.put(0x5D611C, "<I", SEND)
    st.put(0x5D6120, "<I", SEND + 0x8000)
    st.put(0x5D6124, "<I", SEND + pick(rng, 0, rng.randrange(0, 0x8000), 0x7FF0))
    st.put(0x5D5364, "<I", RECV)
    st.put(0x5D5968, "<I", INBOX)
    # connections
    n = rng.randrange(0, 6)
    st.put(0x5D26A4, "<i", n)
    st.put(0x5D26A8, "<i", rng.randrange(0, max(n, 1)))
    for i in range(6):
        rec = 0x5D2064 + 0xA0 * i
        g = pick(rng, *GUIDS, None)
        st.putb(rec, st.exe_bytes(g, 16) if g else rng.randbytes(16))
        st.put(rec + 0x10, "<I", pick(rng, 0, CONNS + 0x40 * i))
        st.putb(rec + 0x20, name_bytes(rng) + b"\0")
    # sessions
    st.put(0x5D2CC4, "<i", rng.randrange(0, 4))
    st.put(0x5D2CC8, "<i", rng.randrange(0, 4))
    # handles
    for a in (0x5D5988, 0x5D598C, 0x5D5990, 0x5D5994):
        st.put(a, "<I", pick(rng, 0, 0, r32(rng)))
    # devices
    st.put(0x4D9554, "<i", rng.randrange(0, 5))
    for d in range(4):
        rec = 0x4D75C0 + d * 0x43C
        st.put(rec + 0xD8, "<I", pick(rng, 0, FX[2 * d]))
        st.put(rec + 0xDC, "<I", pick(rng, 0, FX[2 * d + 1]))
    st.put(0x4B2CBC, "<B", pick(rng, 0x0B, 0x0B, rng.randrange(256)))
    st.put(0x4D9AEC, "<B", rng.randrange(256))
    st.put(0x4D9AE4, "<H", rng.randrange(0x10000))
    for i in range(4):
        st.putb(0x5D6128 + 0x41 * i, name_bytes(rng, 0, pick(rng, 0x40, 0x40, 0x40, 0x48)) + b"\0")
    st.script["time0"] = [rng.randrange(0x8000, 0x14000)]


# ---------------------------------------------------------------- generators: (args, returns value?)

def dev_arg(rng):
    return rng.randrange(0, 4)


def g_dev(st, rng):
    return [dev_arg(rng)], False


def g_kick(st, rng):
    st.put(0x4B2CB8, "<i", pick(rng, -1, 100000, 100000, rng.randrange(-200000, 200000)))
    st.put(0x4D6F24, "<i", pick(rng, 2000, 2500, rng.randrange(-5000, 5000)))
    st.put(0x4D9AD8, "<i", pick(rng, 100000, 95000, rng.randrange(0, 200000)))
    st.script["GetEffectStatus"] = [pick(rng, 0, 1, 3, 2), pick(rng, 0, 1)]
    st.script["SetParameters"] = [r32(rng)]
    st.script["Start"] = [r32(rng)]
    period = pick(rng, 100000, -1, rng.randrange(-200000, 200000), -0x80000000)
    return [dev_arg(rng), period, pick(rng, 2000, rng.randrange(-5000, 5000)),
            pick(rng, 100000, rng.randrange(0, 200000))], False


def g_none(st, rng):
    return [], True


def g_void(st, rng):
    return [], False


def g_init(st, rng):
    st.put(0x5D2060, "<I", 0)  # an object still there is shut down first on purpose (orig_pu.c)
    st.script["CoCreateInstance"] = [(pick(rng, 0, 0, 0, 0x80004005), DP_TMP), (pick(rng, 0, 0, 0, 1), LOBBY2),
                                     (pick(rng, 0, 0, 1), DP_OBJ)]
    st.script["EnumConnections"] = [(pick(rng, 0, 0, 5), [])]
    st.script["arena_alloc"] = [RECV, INBOX]
    st.script["GetPlayerAddress"] = [(pick(rng, 0x8877001E, 0, 7), 0x20, b""), (pick(rng, 0, 0, 3), 0x20, b"Q" * 0x20)]
    st.script["block_alloc"] = [pick(rng, ARG + 0x2000, 0)]
    return [], True


def g_close(st, rng):
    st.script["CoCreateInstance"] = [(pick(rng, 0, 0, 0x80004005), DP_OBJ)]
    t = st.script["time0"][0]
    st.script["timeGetTime"] = [t] + [t + rng.randrange(0, 7000) for _ in range(rng.randrange(0, 5))]
    return [], False


def g_probe(st, rng):
    st.script["CoCreateInstance"] = [(pick(rng, 0, 0, 0, 0x80004005), DP_TMP), (pick(rng, 0, 0, 0, 1), LOBBY2)]
    st.script["EnumConnections"] = [(pick(rng, 0, 0, 0, 5), [])]
    st.script["CreateCompoundAddress"] = [(pick(rng, 0, 0, 0, 9), 0x30, rng.randbytes(0x30))]
    st.script["InitializeConnection"] = [pick(rng, 0, 0, 0, 4)]
    st.script["GetPlayerAddress"] = [(pick(rng, 0x8877001E, 0x8877001E, 0, 7), rng.randrange(0, 0x40), b""),
                                     (pick(rng, 0, 0, 0, 3), 0x20, rng.randbytes(0x20))]
    st.script["block_alloc"] = [pick(rng, ARG + 0x2000, ARG + 0x2000, 0)]
    st.script["EnumAddress"] = [pick(rng, 0, 0, 0, 6)]
    return [], True


def conns_script(st, rng):
    out = []
    for i in range(rng.randrange(0, 6)):
        g = pick(rng, *GUIDS, None)
        out.append((st.exe_bytes(g, 16) if g else rng.randbytes(16), pick(rng, CONNS + 0x40 * i, 0),
                    name_bytes(rng)))
    return out


def g_conn_init(st, rng):
    st.script["EnumConnections"] = [(pick(rng, 0, 0, 0, 0, 0, 5), conns_script(st, rng))]
    st.script["DirectPlayLobbyCreateA"] = [(pick(rng, 0, 0, 0, 0, 3), LOBBY1)]
    st.script["QueryInterface"] = [(pick(rng, 0, 0, 0, 0, 3), LOBBY2)]
    st.script["CreateCompoundAddress"] = [(pick(rng, 0, 0, 0, 0, 9), pick(rng, 0x40, 0x100, 0x101), rng.randbytes(0x20))]
    st.script["InitializeConnection"] = [pick(rng, 0, 0, 0x88770005, 4)]
    st.script["DialogBoxParamA"] = [pick(rng, 0, 1, 2, -1)]
    for a in (ARG, ARG + 0x40, ARG + 0x80):
        st.putb(a, name_bytes(rng, 0, 0x30) + b"\0")
    return [pick(rng, 0, ARG), pick(rng, 0, ARG + 0x40, ARG + 0x40), pick(rng, 0, ARG + 0x80, ARG + 0x80)], True


def g_dialog(st, rng):
    st.put(0x5D26A4, "<i", rng.randrange(-1, 6))
    st.script["SendDlgItemMessageA"] = [r32(rng)]
    msg = pick(rng, 0x110, 0x111, 0x111, 0x111, 0x112, 0x10F)
    wp = pick(rng, 1, 2, 3, 0x10001, 0x10002, r32(rng))
    return [r32(rng), msg, wp, r32(rng)], True


def g_enum_cb(st, rng):
    n = rng.randrange(0, 6)
    st.put(0x5D26A4, "<i", n)
    g = pick(rng, *GUIDS, None)
    st.putb(ARG, st.exe_bytes(g, 16) if g else rng.randbytes(16))
    size = rng.randrange(1, 0x40)
    st.putb(ARG + 0x100, rng.randbytes(size))
    st.putb(ARG + 0x200, name_bytes(rng, 0, 0x30) + b"\0")
    st.put(ARG + 0x300 + 8, "<I", ARG + 0x200)
    st.script["block_alloc"] = [pick(rng, ARG + 0x1000, ARG + 0x1000, 0)]
    st.script["CoCreateInstance"] = [(pick(rng, 0, 0, 4), DP_TMP)]
    st.script["InitializeConnection"] = [pick(rng, 0, 0, 4)]
    return [ARG, ARG + 0x100, size, ARG + 0x300, r32(rng), r32(rng)], True


def caps_script(st, rng):
    st.script["GetCaps"] = [(pick(rng, 0, 0, 0, 5), rng.randbytes(0x24) + struct.pack("<I", pick(
        rng, 0, 4999, 5000, 20000, r32(rng))))]


def g_join(st, rng):
    st.put(0x5D2CC4, "<i", rng.randrange(-1, 4))
    for i in range(4):
        st.putb(0x5D26AC + i * 0x9C + 8, rng.randbytes(16))
    k = rng.randrange(0, 3)
    st.script["Open"] = [DPERR_CONNECTING] * k + [pick(rng, 0, 0, 0, 7)]
    st.script["player_set"] = [pick(rng, 1, 1, 0)]
    st.script["CreatePlayer"] = [(pick(rng, 0, 0, 3), pick(rng, *IDS))]
    caps_script(st, rng)
    st.putb(ARG, name_bytes(rng) + b"\0")
    return [rng.randrange(-1, 4), ARG], True


def g_host(st, rng):
    k = rng.randrange(0, 3)
    st.script["Open"] = [DPERR_CONNECTING] * k + [pick(rng, 0, 0, 0, 7)]
    st.script["player_set"] = [pick(rng, 1, 1, 0)]
    st.script["CreatePlayer"] = [(pick(rng, 0, 0, 3), pick(rng, *IDS))]
    caps_script(st, rng)
    st.putb(ARG, name_bytes(rng) + b"\0")
    st.putb(ARG + 0x40, name_bytes(rng, 0, 0x30) + b"\0")
    return [ARG, ARG + 0x40], True


def g_thread_start(st, rng):
    st.script["CreateMutexA"] = [pick(rng, 0, 0x123, 0x123)]
    st.script["CreateEventA"] = [pick(rng, 0, 0x234, 0x234), pick(rng, 0, 0x345, 0x345)]
    st.script["beginthread"] = [pick(rng, 0x456, 0xFFFFFFFF, 0)]
    return [], True


def g_slot_of(st, rng):
    return [pick(rng, *IDS, 0, r32(rng))], True


def g_player_create(st, rng):
    st.script["player_set"] = [pick(rng, 1, 1, 0)]
    st.script["CreatePlayer"] = [(pick(rng, 0, 0, 3), pick(rng, *IDS))]
    st.putb(ARG, name_bytes(rng) + b"\0")
    return [ARG, pick(rng, 0, 1, 7)], True


def g_remove(st, rng):
    st.script["net_state_bit5"] = [pick(rng, 0, 1)]
    return [rng.randrange(-2, 8)], True


def g_checksum(st, rng):
    n = rng.randrange(8, 0x60)
    data = bytearray(rng.randbytes(n))
    if rng.random() < 0.5:
        struct.pack_into("<I", data, 4, 0)
        struct.pack_into("<I", data, 4, sum(data) & 0xFFFFFFFF)
    st.putb(ARG, bytes(data))
    return [ARG, n], True


def g_system(st, rng):
    t = pick(rng, 3, 5, 0x31, 0x101, 0x103, 0x104, 0x107, 0x109, 0x10D, 0x10E, 0x200, 1, 0x40)
    msg = ARG
    st.put(msg, "<I", t)
    st.put(msg + 4, "<I", pick(rng, 1, 1, 2))
    st.put(msg + 8, "<I", pick(rng, *IDS, r32(rng)))
    st.put(msg + 0xC, "<I", r32(rng))
    st.put(msg + 0x10, "<I", r32(rng))
    st.put(msg + 0x14, "<I", ARG + 0x100)
    st.put(msg + 0x1C, "<I", pick(rng, *IDS, r32(rng)))
    st.put(msg + 0x20, "<I", ARG + 0x200)
    st.put(ARG + 0x108, "<I", ARG + 0x300)
    st.putb(ARG + 0x300, name_bytes(rng, 0, pick(rng, 0x40, 0x40, 0x60)) + b"\0")
    st.putb(ARG + 0x200, name_bytes(rng) + b"\0")
    st.script["text_line"] = [(pick(rng, 0, 0, 0xFFFFFFFF), name_bytes(rng, 0, 0x40))]
    st.script["net_state_bit5"] = [pick(rng, 0, 1, 1), pick(rng, 0, 1)]
    st.script["player_set"] = [pick(rng, 1, 1, 0)]
    return [msg, rng.randrange(0, 0x100), r32(rng), r32(rng)], False


def inbox_fill(st, rng):
    used = 0
    n = rng.randrange(0, 5)
    for _ in range(n):
        size = rng.randrange(0, 0x40)
        st.put(INBOX + used, "<I", pick(rng, 0, *IDS))
        st.put(INBOX + used + 4, "<I", pick(rng, *IDS))
        st.put(INBOX + used + 8, "<I", size)
        data = bytearray(rng.randbytes(size + 0x20))
        struct.pack_into("<I", data, 0, pick(rng, 0x10D, 0x103, 0x107, r32(rng)))
        struct.pack_into("<I", data, 0x14, ARG + 0x100)
        st.putb(INBOX + used + 0xC, bytes(data))
        used += size + 0xC
    st.put(ARG + 0x108, "<I", ARG + 0x300)
    st.putb(ARG + 0x300, name_bytes(rng) + b"\0")
    st.put(0x5D5970, "<I", pick(rng, used, rng.randrange(0, 0x20000), 0x1FFF0))
    st.put(0x5D5974, "<i", pick(rng, n, n, n - 1, -1))
    st.put(0x5D596C, "<I", rng.randrange(0, 0x40))


def g_push(st, rng):
    inbox_fill(st, rng)
    used = st.get(0x5D5970, "<I")
    size = pick(rng, rng.randrange(0, 0x100), 0x20000, 0xFFFFFFF8, (0x20000 - 0xC - used) & 0xFFFFFFFF,
                (0x20000 - 0xB - used) & 0xFFFFFFFF)
    if size > 0x100 and size < 0x20000:
        st.put(0x5D5970, "<I", 0x20000 - 0xC - pick(rng, 0x40, 0x80))
        size = pick(rng, 0x40, 0x80)
    st.putb(ARG, rng.randbytes(0x100))
    return [pick(rng, *IDS), pick(rng, *IDS), ARG, size], True


def g_pop(st, rng):
    inbox_fill(st, rng)
    st.put(0x5D596C, "<I", 0)
    return [ARG + 0x800, ARG + 0x804, ARG, ARG + 0x808], True


def g_lock(st, rng):
    st.script["OpenMutexA"] = [pick(rng, 0, 0x55, 0x55)]
    st.script["WaitForSingleObject"] = [pick(rng, 0, 0, 0x102, 0x80)]
    return [pick(rng, 0, 1, 2)], True


def g_unlock(st, rng):
    st.script["CloseHandle"] = [r32(rng)]
    return [r32(rng)], True


def g_dispatch(st, rng):
    inbox_fill(st, rng)
    st.script["OpenMutexA"] = [pick(rng, 0, 0x55, 0x55, 0x55)]
    st.script["WaitForSingleObject"] = [pick(rng, 0, 0, 0x102)]
    st.script["text_line"] = [(0, b"a"), (0, b"b")]
    return [], True


def g_ack_track(st, rng):
    if rng.random() < 0.2:
        for i in range(0x40):
            st.put(0x5D5368 + i * 0x18, "<I", 1)
    return [r32(rng), r32(rng), r32(rng), r32(rng), r32(rng)], True


def g_player_check(st, rng):
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 0, 5), pick(rng, 0, 0x100, 0x1FF, r32(rng)))]
    return [pick(rng, *IDS, r32(rng))], True


def g_game_message(st, rng):
    st.putb(ARG, rng.randbytes(0x20))
    st.put(ARG, "<B", pick(rng, 0x0B, rng.randrange(256)))
    return [ARG, rng.randrange(0, 0x20), pick(rng, *IDS), pick(rng, *IDS)], True


def recv_script(st, rng):
    out = []
    for _ in range(rng.randrange(0, 5)):
        hr = pick(rng, 0, 0, 0, 0, 7)
        data = bytearray(rng.randbytes(rng.randrange(1, 0x40)))
        data[0] = pick(rng, 0x0B, 0x0B, rng.randrange(256))
        out.append((hr, pick(rng, 0, 0, *IDS), pick(rng, *IDS), bytes(data)))
    out.append((DPERR_NOMESSAGES, 0, 0, b""))
    return out


def g_receive(st, rng):
    st.script["game_time"] = [r32(rng)]
    st.script["OpenMutexA"] = [pick(rng, 0, 0x55, 0x55, 0x55)]
    st.script["WaitForSingleObject"] = [pick(rng, 0, 0, 0, 0x102)]
    st.script["Receive"] = recv_script(st, rng)
    st.script["packet_filter"] = [pick(rng, 0, 0, 1) for _ in range(6)]
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 5), 0x100) for _ in range(6)]
    st.put(0x5D5970, "<I", pick(rng, 0, rng.randrange(0, 0x20000), 0x1FFF0))
    st.put(0x5D5974, "<i", rng.randrange(0, 4))
    st.put(0x5D622C, "<I", pick(rng, 7, 7, 7, 3, 5, 6))
    return [], True


def times(st, rng, n=8):
    t = st.script["time0"][0]
    st.script["timeGetTime"] = sorted(t + rng.randrange(-100, 2600) for _ in range(n))


def g_ack_wait(st, rng):
    times(st, rng)
    st.script["SendEx"] = [(pick(rng, 0, 0, 0x8000000A, 5), 0x99)]
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 5), 0)]
    return [pick(rng, 0, 1, 2, 3, 4, 5, 6, 7, r32(rng))], True


def g_acks_first(st, rng):
    times(st, rng)
    st.script["SendEx"] = [(pick(rng, 0, 0, 0x8000000A, 5), 0x99)]
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 5), 0)]
    st.script["game_time"] = [r32(rng), r32(rng)]
    if rng.random() < 0.3:
        for i in range(0x40):
            st.put(0x5D5368 + i * 0x18, "<I", pick(rng, 0, 2))
    return [], True


def g_lost(st, rng):
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 5), 0)]
    return [pick(rng, *IDS, r32(rng))], True


def g_send_log(st, rng):
    size = pick(rng, rng.randrange(0, 0x80), 0x8000, 0x8001, 0xFFFFFFF0)
    st.putb(ARG, rng.randbytes(0x80))
    return [r32(rng), r32(rng), r32(rng), ARG, size, r32(rng)], False


def sendex_script(st, rng):
    k = rng.randrange(0, 4)
    st.script["SendEx"] = [(DPERR_PENDING, 0x98)] * k + [(pick(rng, 0, 0, 0x8000000A, 5), pick(rng, 0x99, r32(rng)))]
    t = st.script["time0"][0]
    st.script["timeGetTime"] = [t] + sorted(t + rng.randrange(0, 1500) for _ in range(6))


def g_resend(st, rng):
    sendex_script(st, rng)
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 5), 0)]
    return [pick(rng, *IDS), pick(rng, 0, 1, 2, 3, 0x10001, r32(rng))], True


def g_send(st, rng):
    sendex_script(st, rng)
    st.script["GetPlayerFlags"] = [(pick(rng, 0, 5), 0)]
    size = rng.randrange(8, 0x40)
    st.putb(ARG, rng.randbytes(size))
    return [pick(rng, *IDS), ARG, size, rng.getrandbits(4)], True


def g_thread(st, rng):
    st.script["WaitForMultipleObjects"] = [0] * rng.randrange(0, 3) + [pick(rng, 1, 0xFFFFFFFF)]
    st.script["OpenMutexA"] = [0, 0, 0]
    st.put(0x5D622C, "<I", pick(rng, 7, 3))
    return [r32(rng)], False


TESTS = {
    "ff_effect_a_stop": (0x489DE0, g_dev),
    "ff_periodic_play": (0x489E10, g_kick),
    "ff_effect_b_stop": (0x489F40, g_dev),
    "ff_device_stop_all": (0x489F70, g_dev),
    "joy_device_count": (0x489FE0, g_none),
    "joy_device_count_thunk": (0x48A010, g_none),
    "dplay_init": (0x48A600, g_init),
    "dplay_pending_cancel": (0x48A670, g_void),
    "dplay_close": (0x48A6B0, g_close),
    "dplay_shutdown": (0x48A8B0, g_close),
    "dplay_connections_probe": (0x48A9F0, g_probe),
    "dplay_connection_init": (0x48ACE0, g_conn_init),
    "dplay_provider_dialog_proc": (0x48B320, g_dialog),
    "dplay_connection_enum_cb": (0x48B3E0, g_enum_cb),
    "dplay_session_join": (0x48B7E0, g_join),
    "dplay_session_host": (0x48B940, g_host),
    "dplay_thread_start": (0x48BC40, g_thread_start),
    "net_player_slot_free": (0x48BD10, g_none),
    "net_player_slot_of": (0x48BD40, g_slot_of),
    "dplay_player_create": (0x48BD70, g_player_create),
    "net_player_remove": (0x48BED0, g_remove),
    "net_message_checksum_ok": (0x48BFB0, g_checksum),
    "dplay_system_message": (0x48BFF0, g_system),
    "net_inbox_push": (0x48C310, g_push),
    "net_inbox_pop": (0x48C380, g_pop),
    "net_lock": (0x48C3F0, g_lock),
    "net_unlock": (0x48C440, g_unlock),
    "net_inbox_dispatch": (0x48C460, g_dispatch),
    "net_ack_track": (0x48C530, g_ack_track),
    "net_player_check": (0x48C5A0, g_player_check),
    "net_game_message": (0x48C5F0, g_game_message),
    "dplay_receive_all": (0x48C630, g_receive),
    "net_ack_wait": (0x48C790, g_ack_wait),
    "net_acks_wait_first": (0x48C830, g_acks_first),
    "net_acks_clear": (0x48C900, g_void),
    "net_player_lost_check": (0x48C930, g_lost),
    "net_send_buffer_forget": (0x48C9F0, g_void),
    "net_send_log": (0x48CA00, g_send_log),
    "net_resend": (0x48CAE0, g_resend),
    "net_message_send": (0x48CC30, g_send),
    "dplay_receive_thread": (0x48CE90, g_thread),
}
EXTRA_SIZES = {0x48B320: 0xBF, 0x48B3E0: 0x227, 0x48CE90: 0x58}

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


def run_case(m, stubs, base, name, address, gen, rng):
    st = State(rng, base)
    st.exe_bytes = lambda a, n: m.read(a, n)
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


def func_sizes():
    sizes = dict(EXTRA_SIZES)
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "groups", "pu_owned.txt")
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
    rng = random.Random(0x9F)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c", "-f")]
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
