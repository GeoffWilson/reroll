#!/usr/bin/env python3
"""Differential test of orig_pl.c (group PL: the network game's messages,
0x45c340-0x45e600) against the original, on random state.

Every callee outside the group is a logging stub in both runs (the log
records as many arguments as the callee reads; strings by content, message
and copy buffers by content, stack pointers as "stack") with scripted
results; timeGetTime goes through its import slot. The group's own
functions run for real in both (the original's call the original's, ours
call ours), and so does __ftol (0x497a80).

The original leaves some message bytes as stack leftovers (headers 1..7,
string tails, the car state's +0x52); ours writes 0 there. Each case runs
the original twice with different stack fills: bytes of logged buffers that
differ between those two runs are masked before the comparison with ours.
Compared: the return value, ebx / esi / edi / ebp, the x87 stack top / tag
word, the globals and scratch memory, the call log. The x87 control word
is 0x27f, 0x37f or 0x07f.

    tests/emu_orig_pl.py EXE build/emu_pl.elf [iterations] [names...] [-v] [-c]
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
SCR_SIZE = 0x20000
RACE = SCR + 0x0000  # race table (0x1000)
OPTIONS = SCR + 0x1000  # options (0x1000)
MSG = SCR + 0x2000  # a received message (0x1000)
CARS = SCR + 0x4000  # 3 cars of 0x2400
OBJS = SCR + 0xB000  # 3 second objects of 0x100
BODIES = SCR + 0xB400  # 3 bodies of 0x100
BLK = SCR + 0xB800  # car +0x6ac blocks (0x100)
ARG = SCR + 0xC000  # records / out buffers (0x1000)
STR = SCR + 0xD000  # chat lines (5 x 0x40)
HEAP = SCR + 0xE000  # 0x476ae0's block (0x1000)
DATA = SCR + 0xF000  # 0x45e600's data (0x1000)
IATSTUB = 0x02200000

REGIONS = [(0x4B0000, 0x21000), (0x5D2000, 0x5000), (0x6BF000, 0x3000), (0x6CC000, 0x2000)]

IAT = {0x4A32B4: ("timeGetTime", "")}

# kinds: v value, s string, o out pointer, m message (length = 4th argument), c mem_copy (dst, src bytes, n)
STUBS = {
    0x439FD0: ("key_slots_reset", ""),
    0x43692A: ("game_time", ""),
    0x45EEE0: ("net_time", ""),
    0x476EC0: ("debug_printf", "s"),
    0x467900: ("weapon_fire", "vvvvv"),
    0x48CFD0: ("net_player_active", "v"),
    0x476AE0: ("mem_alloc", "vv"),
    0x476B40: ("mem_free", "v"),
    0x476F30: ("mem_copy", "c"),
    0x45F5B0: ("chat_lines", "oo"),
    0x479280: ("text_free", "v"),
    0x479060: ("text_create", "svvvvvv"),
    0x48BFB0: ("message_check", "vv"),
    0x48BD40: ("player_of", "v"),
    0x45FB90: ("cheat_note", "v"),
    0x42E6D0: ("text_line_get", "ovvv"),
    0x4979F0: ("sprintf", "oss"),
    0x45D150: ("chat_post", "s"),
    0x45EE60: ("car_state_seen", "vvvv"),
    0x48BED0: ("player_left", "v"),
    0x45F7B0: ("net_pause", "v"),
    0x45F710: ("send_setting", "vvvvv"),
    0x452D20: ("menu_dirty", ""),
    0x48BF70: ("drop_player", "v"),
    0x45D170: ("race_abort", ""),
    0x452FA0: ("race_options", "vvvvv"),
    0x45EED0: ("net_is_host", ""),
    0x45EF30: ("net_bit5", ""),
    0x45EF80: ("net_game", ""),
    0x45F990: ("results", "v"),
    0x45FA60: ("snatched", "v"),
    0x45FCA0: ("quit_ack", "v"),
    0x4906F0: ("net_close", ""),
    0x48CDD0: ("net_send", "vvmvv"),
    0x48C3F0: ("net_lock", "v"),
    0x48C440: ("net_unlock", "v"),
    0x444050: ("car_position_sum", "v"),
    0x45EB60: ("controls_ready", ""),
    0x48CF20: ("net_local_slot", ""),
    0x45EE10: ("car_state_changed", "v"),
    0x48D390: ("net_frame_due", ""),
    0x48D3E0: ("net_frame_skip", ""),
    0x45EEB0: ("player_free", "v"),
    0x4611D0: ("grid_unlink", "v"),
    0x460FB0: ("grid_link", "v"),
    0x465740: ("body_prepare", "v"),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def c_string(uc, a, limit=0x100):
    return bytes(uc.mem_read(a, limit)).split(b"\0")[0]


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(IATSTUB, 0x1000)
        m.uc.mem_write(IATSTUB, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, kinds)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, kinds, True)
        for address, (name, kinds) in STUBS.items():
            self.calls[address] = (name, kinds, False)
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
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            if k == "o":
                logged.append("stack" if on_stack(a) else a)
            elif k == "s":
                logged.append(c_string(uc, a))
            elif k == "m":
                logged.append(bytearray(uc.mem_read(a, min(args[3], 0x400))))
            elif k == "c":
                n = args[2]
                logged += ["stack" if on_stack(args[0]) else args[0], bytearray(uc.mem_read(args[1], min(n, 0x1000))),
                           n]
                uc.mem_write(args[0], bytes(uc.mem_read(args[1], n)))
            else:
                logged.append(a)
        eax = 0
        if name == "chat_lines":
            count, lines = self._next(name, (0, [0] * 5))
            uc.mem_write(args[0], struct.pack("<i", count))
            uc.mem_write(args[1], struct.pack("<5I", *lines))
        elif name == "text_line_get":
            text = b"%s cheat"
            if args[0] and 0 < args[1] < 0x1000:
                uc.mem_write(args[0], text[:args[1] - 1] + b"\0")
            eax = self._next(name, len(text))
        elif name == "sprintf":
            text = b"S" + c_string(uc, args[1])[:40] + b"/" + c_string(uc, args[2])[:40]
            uc.mem_write(args[0], text + b"\0")
            eax = len(text)
        elif name == "mem_copy":
            eax = args[0]
        else:
            eax = self._next(name, 0)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * len(kinds) if stdcall else 0))
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


def REC(i):
    return 0x5D2CD0 + i * 0x66C


def SLOT(i):
    return 0x6BFB80 + i * 0x274


def name_bytes(rng):
    n = pick(rng, 0, 3, 8, 15, rng.randrange(0, 16), rng.randrange(0, 22))
    return bytes(rng.randrange(0x20, 0x7F) for _ in range(n)) + b"\0"


def small_seq(rng):
    return pick(rng, 0, 1, 2, rng.randrange(0, 0x40))


def world(st, rng):
    """Pointers, counts and indices the functions use as addresses kept sane."""
    rng_ = rng
    st.put(0x4B1960, "<I", RACE)
    st.put(0x4B2198, "<I", OPTIONS)
    st.put(RACE + 0x54D, "<B", pick(rng, 0, 1, 2, 4, 6, 6, 7, 8, rng.randrange(0, 9)))
    st.put(RACE + 0x551, "<B", pick(rng, *range(10), rng.getrandbits(8)))
    st.put(RACE + 0x57C, "<i", pick(rng, 4, 5, 6, 7, 8, rng.randrange(-3, 12), r32(rng)))
    for i in range(9):
        st.put(SLOT(i), "<I", pick(rng_, CARS + (i % 3) * 0x2400, CARS + (i % 3) * 0x2400, 0))
        st.put(SLOT(i) + 0x28, "<I", OBJS + (i % 3) * 0x100)
        st.put(SLOT(i) + 0x2D, "<B", pick(rng, 0, 1, 2, rng.getrandbits(8)))
    for k in range(3):
        st.put(OBJS + k * 0x100, "<I", BODIES + k * 0x100)
        st.put(CARS + k * 0x2400 + 0x6AC, "<I", pick(rng, 0xFFFFFFFF, BLK + rng.randrange(0, 0xF0)))
    ids = [r32(rng) for _ in range(6)]
    for k in range(6):
        r = REC(k)
        st.put(r, "<I", pick(rng, 0, 1, 1, r32(rng)))
        st.put(r + 4, "<I", ids[k])
        st.putb(r + 8, name_bytes(rng)[:16])
        st.put(r + 0x18, "<i", pick(rng, k, rng.randrange(-1, 9), r32(rng)))
        st.put(r + 0x3C, "<I", small_seq(rng))
    st.put(0x5D535C, "<I", pick(rng, 0, 1, 2, 3, 4, 5))
    for a in (0x5D59A0, 0x5D59A4, 0x5D59A8):
        st.put(a, "<I", small_seq(rng))
    st.put(0x5D5360, "<I", pick(rng, ids[rng.randrange(6)], r32(rng)))
    st.put(0x4B0E30, "<I", pick(rng, st.get(0x5D59A8, "<I"), r32(rng)))
    st.put(0x5D622C, "<I", r32(rng) | pick(rng, 0, 0x23, 0xA3, 0xFF))


def chat_script(st, rng):
    count = pick(rng, 0, 1, 3, 5, rng.randrange(-2, 6))
    lines = []
    for k in range(5):
        st.putb(STR + k * 0x40, name_bytes(rng) if rng.randrange(4) else b"\0")
        lines.append(pick(rng, STR + k * 0x40, STR + k * 0x40, 0))
    st.script["chat_lines"] = [(count, lines)]


# ---------------------------------------------------------------- generators: (args, returns value?)

def g_none(st, rng):
    return [], True


def g_none_void(st, rng):
    return [], False


def g_pack(st, rng):
    return [CARS + rng.randrange(3) * 0x2400, ARG], False


def g_copy(st, rng):
    return [ARG + 0x200, ARG], False


def g_slot_rec(st, rng):
    return [pick(rng, 0, 1, 5, -1, 7, rng.randrange(-2, 9)), ARG], False


def slot_arg(rng):
    return pick(rng, 0, 1, 2, 5, -1, 7, 8, rng.randrange(-2, 9), r32(rng))


def g_slot(st, rng):
    st.script["net_time"] = [r32(rng)]
    return [slot_arg(rng)], False


def g_unpack(st, rng):
    return [ARG, pick(rng, CARS, CARS + 0x2400, BODIES)], False


def g_load(st, rng):
    return [CARS + rng.randrange(3) * 0x2400, ARG, OBJS + 0x100 * rng.randrange(3), ARG + 0xC4, ARG + 0x120], False


def g_tick(st, rng):
    t = r32(rng)
    st.script["game_time"] = [t]
    st.put(0x4D076C, "<I", pick(rng, t - 0x2EE, t - 0x2EF, t, t - 100, r32(rng)))
    st.put(0x4D0770, "<I", pick(rng, t - 0x2EE, t - 0x2EF, t, t - 100, r32(rng)))
    st.script["net_local_slot"] = [pick(rng, 0, 1, 5, 7, r32(rng))]
    st.script["car_state_changed"] = [pick(rng, 0, 1)]
    st.script["net_frame_due"] = [pick(rng, 0, 1)]
    st.script["net_frame_skip"] = [pick(rng, 0, 1)]
    st.script["net_bit5"] = [pick(rng, 0, 1, 1, 2)]
    st.script["player_free"] = [pick(rng, 0, 1) for _ in range(9)]
    st.script["net_time"] = [r32(rng) for _ in range(10)]
    return [], False


def g_kind_set(st, rng):
    return [slot_arg(rng), r32(rng), pick(rng, 0, 1, rng.randrange(-4, 0x40))], False


def g_fire(st, rng):
    return [slot_arg(rng), pick(rng, *range(9), r32(rng)), pick(rng, 0, 1, 2, -1, r32(rng)), r32(rng),
            pick(rng, 0x60, 0x60, 0, r32(rng)), ARG], False


def g_look(st, rng):
    return [slot_arg(rng), ARG, ARG + 1, ARG + 2, ARG + 4], False


def g_mark(st, rng):
    st.script["net_player_active"] = [pick(rng, 0, 1, 2) for _ in range(9)]
    return [], False


def g_apply(st, rng):
    return [MSG], False


def g_pack_settings(st, rng):
    st.script["mem_alloc"] = [pick(rng, HEAP, HEAP + 4, HEAP)]
    return [pick(rng, 0xC, 0, 0x10), ARG], True


def g_chat_show(st, rng):
    st.put(0x4D0774, "<I", pick(rng, 0, 1, 1, 1))
    for i in range(5):
        st.put(0x4D0758 + 4 * i, "<I", pick(rng, 0xFFFFFFFF, rng.randrange(0, 40)))
    st.put(0x6CCE40, "<I", pick(rng, 0, 0, 0x10, 2, r32(rng)))
    st.put(0x6CD410, "<I", pick(rng, 0x100, 0x200, 0x180, rng.randrange(0, 0x1000), r32(rng)))
    st.put(ARG + 0x2C, "<B", pick(rng, 0, 1, 2, rng.getrandbits(8)))
    st.put(RACE + 0x586, "<B", pick(rng, 0, 1, 2, rng.getrandbits(8)))
    st.put(RACE + 0x551, "<B", pick(rng, 0, 1, 2, 3, 4, 5, 9, rng.getrandbits(8)))
    st.put(OPTIONS + 0x25, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    chat_script(st, rng)
    st.script["text_create"] = [r32(rng) for _ in range(5)]
    st.script["game_time"] = [r32(rng)]
    return [ARG, pick(rng, 0xFF, 0x80, 0x18, 0x30, r32(rng))], False


def g_race_bytes(st, rng):
    return [MSG], False


MESSAGE_TYPES = list(range(0, 36)) + [1, 1, 1, 1, 3, 5, 6, 8, 10, 16, 28, 28, 28, 1, 6, 8, 11, 0x65, 0xFF]


def g_message(st, rng):
    t = pick(rng, *MESSAGE_TYPES)
    st.put(MSG, "<B", t)
    st.script["message_check"] = [pick(rng, 1, 1, 1, 1, 0)]
    st.script["player_of"] = [pick(rng, -1, 0, 1, 2, 5, 6, rng.randrange(0, 6))]
    st.script["text_line_get"] = [pick(rng, 8, 8, -1)]
    st.script["game_time"] = [r32(rng)]
    st.script["net_bit5"] = [pick(rng, 0, 1)]
    st.script["controls_ready"] = [pick(rng, 0, 1)]
    st.script["net_is_host"] = [pick(rng, 0, 1)]
    st.script["net_send"] = [pick(rng, 0, 1, 1)]
    st.script["mem_alloc"] = [pick(rng, HEAP, 0)]
    st.script["net_player_active"] = [pick(rng, 0, 1) for _ in range(9)]
    now = r32(rng)
    stamp = pick(rng, now, now - 1, now - 14, now - 15, now - 16, r32(rng))
    st.script["net_time"] = [now] * 4
    st.put(MSG + 8, "<I", stamp)
    seq = pick(rng, st.get(0x5D59A0, "<I"), small_seq(rng))
    if t == 1:
        st.put(OPTIONS + 0x93, "<B", pick(rng, 0, 1, 1, 2))
        st.put(MSG + 0x10, "<I", seq)
        i = st.script["player_of"][0]
        if 0 <= i < 6 and rng.randrange(2):
            st.put(MSG + 0x14, "<I", st.get(REC(i) + 0x254 + seq * 4, "<I"))
    elif t == 3:
        p = pick(rng, 0, 1, 2, 5, rng.randrange(0, 6))
        st.put(MSG + 0x14, "<I", p)
        cur = st.get(REC(p) + 0x40, "<I")
        st.put(MSG + 0x18, "<I", pick(rng, cur, cur + 1, cur - 1, r32(rng)))
        st.put(0x5D622C, "<I", st.get(0x5D622C, "<I") | pick(rng, 8, 8, 0))
    elif t == 5:
        st.put(MSG + 8, "<i", pick(rng, -3, -2, 0, 1, 0x30, rng.randrange(-3, 0x31)))
    elif t == 6:
        for k in range(6):
            q = MSG + 0x10 + k * 0x24
            st.putb(q + 4, name_bytes(rng))
            if rng.randrange(2):
                st.put(q, "<I", st.get(0x5D5360, "<I"))
    elif t in (8, 11):
        st.put(MSG + 0x2C, "<I", pick(rng, 0, 1, 1000, 0x7FFFFFFF, 0xFFFFFFFF, r32(rng)))
        st.put(MSG + 8, "<I", pick(rng, 0, 1, 1000, 0x7FFFFFFF, 0xFFFFFFFF, r32(rng)))
    elif t == 0x10:
        st.putb(MSG + 0x10, name_bytes(rng))
        st.put(MSG + 0xC, "<I", pick(rng, 0, 1, r32(rng)))
    elif t == 0x1C:
        st.put(MSG + 0x38, "<I", seq)
        for k in range(6):
            if rng.randrange(2):
                st.put(MSG + 0x3C + 4 * k, "<I", st.get(REC(k) + 0x254 + seq * 4, "<I"))
    elif t == 0xC:
        st.put(MSG + 0xC + 0x54D, "<B", pick(rng, 0, 2, 6, 8))
    return [MSG, pick(rng, 0x18, 0x54, r32(rng)), r32(rng)], False


def g_send_byte(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [pick(rng, 0, 1, 2), r32(rng), r32(rng)], True


def g_players_send(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [], True


def g_controls(st, rng):
    st.script["net_game"] = [pick(rng, 0, 1, 1, 1)]
    st.script["net_lock"] = [pick(rng, 0, 0x1234, 0x1234)]
    st.script["car_position_sum"] = [pick(rng, r32(rng), 0) for _ in range(6)]
    st.script["text_line_get"] = [pick(rng, 8, -1) for _ in range(6)]
    st.script["net_bit5"] = [pick(rng, 0, 1)]
    st.script["controls_ready"] = [pick(rng, 0, 1)]
    st.script["net_send"] = [pick(rng, 0, 1, 1)]
    st.put(OPTIONS + 0x93, "<B", pick(rng, 0, 1, 2))
    st.put(OPTIONS + 0x37, "<B", pick(rng, 0, 1))
    st.put(0x4C2458, "<I", r32(rng))
    seq = st.get(0x5D59A4, "<I")
    for k in range(6):
        if rng.randrange(2):
            st.script["car_position_sum"][k] = st.get(REC(k) + 0x254 + seq * 4, "<I")
    return [pick(rng, 0, 0, 0, 1, 5), r32(rng), r32(rng), pick(rng, 0, 1, 1)], True


def g_relay(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    st.put(OPTIONS + 0x93, "<B", pick(rng, 0, 1, 2))
    return [], True


def g_race_start(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    st.script["game_time"] = [r32(rng)]
    st.script["timeGetTime"] = [r32(rng)]
    st.script["net_time"] = [r32(rng)]
    return [], True


def g_clock(st, rng):
    now = r32(rng)
    st.script["timeGetTime"] = [now]
    st.put(0x4D0878, "<I", pick(rng, now - 60000, now - 60001, now, r32(rng)))
    st.script["net_time"] = [r32(rng)]
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [], True


def g_settings_send(st, rng):
    st.script["mem_alloc"] = [pick(rng, HEAP, HEAP, 0)]
    st.script["net_player_active"] = [pick(rng, 0, 1) for _ in range(9)]
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [], True


def g_look_send(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [], True


def g_reply(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [r32(rng), r32(rng)], True


def g_post(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [DATA, pick(rng, 0x154, 0, 1, 0x3D8, 0x3D9, rng.randrange(0, 0x400), r32(rng)), r32(rng),
            pick(rng, 0, 1, 5, rng.randrange(-1, 7))], True


TESTS = {
    "netplay_chat_reset": (0x45C340, g_none),
    "netplay_body_pack": (0x45C350, g_pack),
    "netplay_record_copy": (0x45C3C0, g_copy),
    "netplay_car_pack": (0x45C3E0, g_pack),
    "netplay_car_state_apply": (0x45C4E0, g_slot_rec),
    "netplay_car_state_send": (0x45C540, g_slot),
    "netplay_body_unpack": (0x45C5E0, g_unpack),
    "netplay_record_copy_keep": (0x45C660, g_copy),
    "netplay_car_unpack": (0x45C690, g_unpack),
    "netplay_car_state_load": (0x45C780, g_load),
    "netplay_car_state_tick": (0x45C7D0, g_tick),
    "netplay_weapon_kind_set": (0x45C880, g_kind_set),
    "netplay_weapon_fire": (0x45C8C0, g_fire),
    "netplay_car_look_get": (0x45CA30, g_look),
    "netplay_car_look_set": (0x45CA90, g_look),
    "netplay_players_mark": (0x45CAF0, g_mark),
    "netplay_settings_apply": (0x45CD00, g_apply),
    "netplay_settings_pack": (0x45CF50, g_pack_settings),
    "netplay_chat_texts_clear": (0x45CFB0, g_none_void),
    "netplay_chat_show": (0x45CFE0, g_chat_show),
    "netplay_slot_flag_find": (0x45D1E0, g_none),
    "netplay_race_bytes_set": (0x45D210, g_race_bytes),
    "netplay_message_handle": (0x45D230, g_message),
    "netplay_send_byte": (0x45DDB0, g_send_byte),
    "netplay_players_send": (0x45DDE0, g_players_send),
    "netplay_controls_send": (0x45DEB0, g_controls),
    "netplay_controls_relay": (0x45E120, g_relay),
    "netplay_race_start_send": (0x45E200, g_race_start),
    "netplay_clock_send": (0x45E340, g_clock),
    "netplay_player_count_update": (0x45E3C0, g_none_void),
    "netplay_settings_send": (0x45E400, g_settings_send),
    "netplay_look_send": (0x45E4A0, g_look_send),
    "netplay_bad_message_reply": (0x45E5B0, g_reply),
    "netplay_car_state_post": (0x45E600, g_post),
}

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
    mem = m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS)
    return r, mem, stubs.log


def mask_logs(la, lb, ours):
    """Zero the bytes of logged buffers that differ between the original's two runs (stack leftovers)."""
    for i, (ea, eb) in enumerate(zip(la, lb)):
        if ea[0] != eb[0] or not isinstance(ea[1], tuple) or not isinstance(eb[1], tuple):
            break
        for j, (xa, xb) in enumerate(zip(ea[1], eb[1])):
            if isinstance(xa, bytearray) and isinstance(xb, bytearray) and len(xa) == len(xb):
                diff = [k for k in range(len(xa)) if xa[k] != xb[k]]
                if not diff:
                    continue
                xo = ours[i][1][j] if i < len(ours) and j < len(ours[i][1]) else None
                for k in diff:
                    xa[k] = 0
                    if isinstance(xo, bytearray) and k < len(xo):
                        xo[k] = 0


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
    args, returns = gen(st, rng)
    args = [a & 0xFFFFFFFF for a in args]
    cw = pick(rng, 0x27F, 0x27F, 0x37F, 0x07F)
    a = run_once(m, stubs, st, address, args, cw, 0xCC, returns)
    b = run_once(m, stubs, st, address, args, cw, 0x5A, returns)
    o = run_once(m, stubs, st, m.symbols[name], args, cw, 0xCC, returns)
    mask_logs(a[2], b[2], o[2])
    if a == o:
        return None, a[0][0] != "ok"
    msg = f"{name}{tuple(hex(x) for x in args)} cw {cw:#x}: original {a[0]} ours {o[0]}"
    if a[1] != o[1]:
        x, y = a[1], o[1]
        diff = [i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]]
        msg += f"\n  memory differs at {[hex(where(i)) for i in diff[:8]]} ({len(diff)} words)"
        if "-v" in sys.argv:
            msg += f"\n  orig {[x[i:i + 4].hex() for i in diff[:8]]}\n  ours {[y[i:i + 4].hex() for i in diff[:8]]}"
    if a[2] != o[2]:
        la, lb = a[2], o[2]
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        msg += f"\n  calls from #{k}: orig {la[k:k + 2]}\n                ours {lb[k:k + 2]}"
    return msg, True


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pl_owned.txt"):
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
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    base = {a: m.read(a, n) for a, n in REGIONS}
    rng = random.Random(0x91)
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
