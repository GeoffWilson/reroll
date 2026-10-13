#!/usr/bin/env python3
"""Differential test of orig_pm.c (group PM: the rest of the network game's
source file, 0x45e6f0-0x460a90, and the front end's sparks 0x462ae0 /
0x462bb0) against the original, on random state.

Every callee outside the group is a logging stub in both runs (the log
records as many arguments as the callee reads; strings by content, message
and copy buffers by content, stack pointers as "stack") with scripted
results; timeGetTime goes through its import slot. The group's own
functions run for real in both (the original's call the original's, ours
call ours), and so does __ftol (0x497a80).

The original leaves some message bytes as stack leftovers (headers 1..7,
the top bits of message 9's word, message 5's gaps); ours writes 0 there.
Each case runs the original twice with different stack fills: bytes of
logged buffers that differ between those two runs are masked before the
comparison with ours. Compared: the return value, ebx / esi / edi / ebp,
the x87 stack top / tag word, the globals and scratch memory, the call log.
The x87 control word is 0x27f, 0x37f, 0x07f or 0x67f.

    tests/emu_orig_pm.py EXE build/emu_pm.elf [iterations] [names...] [-v] [-c]
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
OPTIONS = SCR + 0x1000  # options (0x2000)
ARG = SCR + 0x3000  # out buffers / records (0x1000)
DATA = SCR + 0x4000  # pickup data (0x1000)
PART = SCR + 0x8000  # 255 sparks of 0x4c (0x5000)
IATSTUB = 0x02200000

REGIONS = [(0x4AC000, 0x1000), (0x4B0000, 0x22000), (0x5D2000, 0x5000), (0x6CC000, 0x2000)]

IAT = {0x4A32B4: ("timeGetTime", "")}

# kinds: v value, s string, o out pointer, m message (length = 4th argument), c mem_copy (dst, src bytes, n),
# r 16 bytes at the pointer; special handling by name below
STUBS = {
    0x43692A: ("game_time", ""),
    0x43693F: ("sim_clock_start", ""),
    0x476EC0: ("debug_printf", "s"),
    0x476F30: ("mem_copy", "c"),
    0x476F50: ("mem_set", "vvv"),
    0x48CDD0: ("net_send", "vvmvv"),
    0x48BD40: ("player_of", "v"),
    0x48BF70: ("drop_player", "v"),
    0x48C630: ("pump_a", ""),
    0x48C460: ("pump_b", ""),
    0x48CF20: ("net_local_slot", ""),
    0x45DDB0: ("send_byte", "vvv"),
    0x45DDE0: ("players_send", ""),
    0x45E200: ("race_start_send", ""),
    0x45D1E0: ("slot_flag_find", ""),
    0x45EED0: ("net_is_host", ""),
    0x45EF80: ("net_game", ""),
    0x42E6D0: ("text_line_get", "ovvv"),
    0x4979F0: ("sprintf", ""),
    0x45D150: ("chat_post", "s"),
    0x4484A0: ("driver_name", "ov"),
    0x48D270: ("chat_send", "s"),
    0x4987B5: ("rand", ""),
    0x426B80: ("vertex_clip", "r"),
    0x429080: ("word_exchange", "vvv"),
    0x4290A0: ("dword_store", "vv"),
}

LONG_NAMES = "-long" in sys.argv  # client names of 16+ characters (the original's stack overrun)
DEFAULTS = {"timeGetTime": 0xFFFFFFFF}


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
        self.rng = random.Random(1)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else DEFAULTS.get(name, default)

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
                logged.append(bytearray(uc.mem_read(a, min(args[3], 0x2000))))
            elif k == "r":
                logged += ["stack" if on_stack(a) else a, bytes(uc.mem_read(a, 16))]
            elif k == "c":
                n = args[2]
                logged += ["stack" if on_stack(args[0]) else args[0], bytearray(uc.mem_read(args[1], min(n, 0x1000))),
                           n]
                uc.mem_write(args[0], bytes(uc.mem_read(args[1], n)))
            else:
                logged.append(a)
        eax = 0
        if name == "debug_printf":
            fmt = c_string(uc, args[0])
            if b"%" in fmt:
                logged.append(args[1])
        elif name == "text_line_get":
            text = b"%s waited"
            if args[0] and 0 < args[1] < 0x1000:
                uc.mem_write(args[0], text[:args[1] - 1] + b"\0")
            eax = self._next(name, len(text))
        elif name == "sprintf":
            fmt = c_string(uc, args[1])
            n = fmt.count(b"%s")
            strings = [c_string(uc, args[2 + i])[:40] for i in range(n)]
            logged += ["stack" if on_stack(args[0]) else args[0], fmt] + strings
            out = fmt
            for s in strings:
                out = out.replace(b"%s", s, 1)
            uc.mem_write(args[0], out[:0xF0] + b"\0")
            eax = len(out)
        elif name == "driver_name":
            text = b"Driver %d" % (args[1] & 0xFF)
            uc.mem_write(args[0], text + b"\0")
        elif name == "mem_copy":
            eax = args[0]
        elif name == "mem_set":
            uc.mem_write(args[0], bytes([args[1] & 0xFF]) * min(args[2], 0x1000))
            eax = args[0]
        elif name == "vertex_clip":
            uc.mem_write(args[0], struct.pack("<I", self._next(name, 0)))
        elif name == "dword_store":
            uc.mem_write(args[0], struct.pack("<I", args[1]))
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


def name_bytes(rng, longest=15):
    n = pick(rng, 0, 3, 8, longest, rng.randrange(0, longest + 1))
    return bytes(rng.randrange(0x20, 0x7F) for _ in range(n)) + b"\0"


def safe_text(rng, n):
    return bytes(rng.choice(b"abcdefghij XYZ.!") for _ in range(n))


def small_seq(rng):
    return pick(rng, 0, 1, 2, 0x3F, rng.randrange(0, 0x40))


def world(st, rng):
    """Pointers, counts and indices the functions use as addresses kept sane."""
    st.put(0x4B1960, "<I", RACE)
    st.put(0x4B2198, "<I", OPTIONS)
    st.put(RACE + 0x54D, "<B", pick(rng, 0, 1, 2, 4, 6, 6, 7, rng.randrange(0, 8)))
    st.put(RACE + 0x57C, "<i", pick(rng, 4, 5, 6, 7, 8, rng.randrange(-3, 12), r32(rng)))
    st.put(RACE + 0x586, "<B", pick(rng, 0, 1, 3, 5, 10, rng.randrange(0, 53)))
    st.put(RACE + 0x721, "<B", rng.getrandbits(8))
    for k in range(6):
        st.put(RACE + 0x1C + k, "<B", pick(rng, k, 0xFF, rng.randrange(0, 8), rng.getrandbits(8)))
    st.put(OPTIONS + 0x37, "<B", pick(rng, 0, 1))
    st.put(OPTIONS + 0x18, "<B", rng.getrandbits(8))
    st.putb(OPTIONS + 0x120, name_bytes(rng))
    for i in range(30):
        n = pick(rng, 0, 5, 20, rng.randrange(0, 60))
        t = bytearray(safe_text(rng, n))
        for ch in pick(rng, b"", b"1", b"2", b"12", b"21", b"1", b"2", b"112", b"122", b"12"):
            if n:
                t[rng.randrange(n)] = ch
        st.putb(OPTIONS + 0x104E + i * 64, bytes(t) + b"\0")
    for k in range(6):
        r = REC(k)
        st.put(r, "<I", pick(rng, 0, 1, 1, 1, r32(rng)))
        st.putb(r + 8, name_bytes(rng))
        st.put(r + 0x1C, "<i", pick(rng, -1, k, rng.randrange(0, 8), r32(rng)))
        st.put(r + 0x20, "<I", pick(rng, 0, 4, 5, 5, rng.randrange(0, 8)))
        st.put(r + 0x24, "<I", pick(rng, 0, 1))
        st.put(r + 0x3C, "<I", small_seq(rng))
        st.put(r + 0x654, "<I", pick(rng, *range(0x40), 0x3F, 0x40, 0))
        st.put(r + 0x668, "<I", pick(rng, 0, 1))
        a, b = r32(rng), r32(rng)
        st.put(r + 0x2C, "<I", a)
        st.put(r + 0x30, "<I", b)
        st.put(r + 0x34, "<I", pick(rng, a, a, r32(rng)))
        st.put(r + 0x38, "<I", pick(rng, b, b, r32(rng)))
    st.put(0x5D535C, "<I", pick(rng, 0, 1, 2, 3, 4, 5))
    for a in (0x5D59A0, 0x5D59A4, 0x5D59A8):
        st.put(a, "<I", small_seq(rng))
    st.put(0x5D622C, "<I", r32(rng) | pick(rng, 0, 0x23, 0xA3, 0x83, 0x3))
    st.put(0x4C2458, "<I", pick(rng, r32(rng), rng.randrange(0, 1000)))
    st.put(0x5D5CB8, "<I", pick(rng, r32(rng), st.get(0x4C2458, "<I") + rng.randrange(-20, 20)))
    st.put(0x5D5CBC, "<I", pick(rng, r32(rng), st.get(0x4C2458, "<I") + rng.randrange(-20, 20)))
    st.put(0x4D1198, "<I", pick(rng, 0, 0, 1))
    st.put(0x4AC984, "<I", pick(rng, 0, 0, 1))
    for k in range(4):
        st.putb(0x5D6128 + k * 0x41, name_bytes(rng))


# ---------------------------------------------------------------- generators: (args, returns value?)

def g_none(st, rng):
    return [], True


def g_none_void(st, rng):
    return [], False


def slot_arg(rng):
    return pick(rng, 0, 1, 2, 5, -1, 6, 7, rng.randrange(-2, 9), r32(rng))


def clock(st, rng):
    st.script["timeGetTime"] = [r32(rng)]
    st.put(0x5D5998, "<I", pick(rng, r32(rng), st.script["timeGetTime"][0] - rng.randrange(0, 100000)))


def g_pickup(st, rng):
    clock(st, rng)
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    size = pick(rng, 0, 1, 4, 0x60, 0xEC, 0xED, 0x100, rng.randrange(0, 0x110))
    return [slot_arg(rng), pick(rng, 0, 1, 2, -1, r32(rng)), pick(rng, 0, 1, 2, 7, r32(rng)), DATA, size], True


def g_kind(st, rng):
    clock(st, rng)
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [slot_arg(rng), pick(rng, 0, 1, 2, -1, r32(rng)), pick(rng, 0, 1, 2, 7, r32(rng))], True


def g_store(st, rng):
    clock(st, rng)
    if rng.randrange(20) == 0:
        st.put(REC(rng.randrange(6)) + 0x654, "<I", r32(rng))
    return [r32(rng), r32(rng)], True


def g_time(st, rng):
    clock(st, rng)
    return [], True


def g_wait(st, rng):
    start = r32(rng)
    st.script["game_time"] = [start, r32(rng)]
    st.put(0x5D2CCC, "<I", pick(rng, 1000, 5000, 0, r32(rng)))
    limit = (start + st.get(0x5D2CCC, "<I")) & 0xFFFFFFFF
    st.script["timeGetTime"] = [pick(rng, limit, limit + 1, limit - 1, 0, r32(rng)) & 0xFFFFFFFF for _ in range(4)]
    st.script["text_line_get"] = [pick(rng, 8, -1) for _ in range(6)]
    for k in range(6):
        if rng.randrange(2):
            st.put(REC(k) + 0x3C, "<I", pick(rng, st.get(0x5D59A0, "<I"), st.get(0x5D59A4, "<I")))
    return [], True


def g_index(st, rng):
    return [slot_arg(rng)], True


def g_history(st, rng):
    i = pick(rng, 0, 1, 5, rng.randrange(0, 6))
    t = r32(rng)
    for k in range(0x40):
        st.put(REC(i) + 0x354 + k * 12, "<I", pick(rng, t, t + 1, t - 1, r32(rng)))
    return [i, r32(rng), r32(rng), t], False


def g_ready(st, rng):
    t = r32(rng)
    st.script["timeGetTime"] = [t] + [pick(rng, t, t + 9999, t + 10000, t + 10001, r32(rng)) for _ in range(4)]
    for k in range(6):
        st.put(REC(k), "<I", pick(rng, 0, 1, 1, 2))
        st.put(REC(k) + 0x24, "<I", pick(rng, 0, 1, 1))
    return [], True


def g_race_start(st, rng):
    g_ready(st, rng)
    st.script["race_start_send"] = [pick(rng, 0, 1)]
    return [], True


def g_cars(st, rng):
    g_ready(st, rng)
    st.script["net_send"] = [pick(rng, 0, 1, 1)]
    for k in range(6):
        st.put(REC(k) + 0x1C, "<i", pick(rng, -1, k, rng.randrange(0, 6), rng.getrandbits(8)))
    return [], True


def g_count(st, rng):
    return [ARG], False


def g_states_changed(st, rng):
    act = sum(1 for k in range(6) if st.get(REC(k), "<I"))
    fin = sum(1 for k in range(6) if st.get(REC(k), "<I") and st.get(REC(k) + 0x20, "<I") == 5)
    st.put(0x5D5CC8, "<B", pick(rng, act, act, rng.getrandbits(8)))
    st.put(0x5D5CCA, "<B", pick(rng, fin, fin, rng.getrandbits(8)))
    return [], True


def g_states_send(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [], True


def g_chat(st, rng):
    st.put(ARG, "<i", pick(rng, -2, -1, 0, 1, 3, 4, 5, 6))
    return [ARG, ARG + 0x10], False


def g_list(st, rng):
    st.script["net_game"] = [pick(rng, 0, 1, 1)]
    st.put(ARG, "<i", pick(rng, -2, -1, 0, 1, 3, 6, 7, 8, 12))
    return [ARG, ARG + 0x100, ARG + 0x200], False


def g_name(st, rng):
    host = st.get(0x5D622C, "<I") & 0x20
    st.putb(OPTIONS + 0x120, name_bytes(rng, 40 if host or LONG_NAMES else 15))
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    st.script["players_send"] = [pick(rng, 0, 1, 2)]
    return [pick(rng, 0, 1, r32(rng))], True


def g_allowed(st, rng):
    st.script["net_is_host"] = [pick(rng, 0, 1, 2)]
    return [], True


def g_results(st, rng):
    g_allowed(st, rng)
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    return [pick(rng, 0, 1, 2, 3, r32(rng))], True


def g_pause(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    st.script["send_byte"] = [pick(rng, 0, 1, 2)]
    return [pick(rng, 0, 1, 5, 6, 7, r32(rng))], True


def g_cheat(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    st.script["player_of"] = [pick(rng, 0, 1, 5, -1)]
    return [r32(rng)], True


def g_finish(st, rng):
    st.script["net_send"] = [pick(rng, 0, 1, 2)]
    for k in range(6 * 0xDC // 4):
        st.put(RACE + 0x24 + 4 * k, "<I", r32(rng))
    return [], True


def g_race_results(st, rng):
    g_finish(st, rng)
    return [pick(rng, -1, -1, 0, 1, r32(rng))], True


def g_results_check(st, rng):
    st.script["net_is_host"] = [pick(rng, 0, 1, 2), pick(rng, 0, 1)]
    return [], True


def g_pause_due(st, rng):
    st.script["net_is_host"] = [pick(rng, 0, 1, 2)]
    st.script["net_local_slot"] = [pick(rng, 0, 1, 5)]
    st.script["send_byte"] = [pick(rng, 0, 1, 2)]
    return [ARG], True


def g_format(st, rng):
    st.putb(ARG + 0x300, name_bytes(rng))
    st.putb(ARG + 0x340, name_bytes(rng))
    return [pick(rng, 0, 1, 6, 0x1D, 0x1E, 0x1F, rng.randrange(0, 30), -1), ARG + 0x300, ARG + 0x340, ARG + 0x400,
            pick(rng, 0x80, 0x80, 1, 2, 10, 0, rng.randrange(0, 0x80))], False


def g_kill(st, rng):
    st.put(0x4B11D8, "<I", r32(rng))
    st.script["slot_flag_find"] = [pick(rng, 0, 1, 5)]
    for k in range(6):
        if rng.randrange(3) == 0:
            st.put(REC(k) + 8, "<B", 0)
    return [pick(rng, 0, 1, 5, 6, 7, r32(rng)), pick(rng, 0, 1, 5, 6, 7, r32(rng)),
            pick(rng, *range(0x12), *range(0x80, 0x92), r32(rng))], False


def g_compact(st, rng):
    return [], False


def g_paused(st, rng):
    st.script["net_is_host"] = [pick(rng, 0, 1)]
    for k in range(6):
        st.put(REC(k) + 0x668, "<I", pick(rng, 0, 1, 1, 1))
    return [], False


def g_snatched(st, rng):
    st.script["send_byte"] = [r32(rng)]
    return [], True


def sparks(st, rng):
    st.put(0x4D108C, "<I", PART)
    st.put(0x4D1094, "<I", pick(rng, 0, 1, 0xFD, 0xFE, rng.randrange(0, 0xFF), 0xFF))
    for k in range(255):
        p = PART + k * 0x4C
        st.put(p, "<B", pick(rng, 0, 0, 1, 2, 0x14, rng.getrandbits(8)))
        st.put(p + 0x40, "<h", rng.randrange(-0x1000, 0x1000))
        st.put(p + 0x42, "<h", pick(rng, rng.randrange(-0x1000, 0x1000), rng.randrange(-0x8000, 0x8000)))
        st.put(p + 0x48, "<h", rng.randrange(-0x400, 0x400))
        st.put(p + 0x4A, "<h", rng.randrange(-0x400, 0x400))


def g_spawn(st, rng):
    sparks(st, rng)
    st.script["rand"] = [r32(rng) & 0x7FFF for _ in range(4 * 256)]
    return [pick(rng, 0, 1, 0x20, 0xFF, 0x100, 0x120, r32(rng)), r32(rng), r32(rng)], False


def rand_float(rng):
    return struct.unpack("<I", struct.pack("<f", pick(rng, 0.0, 1.0, 0.5, 2.0, rng.uniform(-400, 400))))[0]


def g_draw(st, rng):
    sparks(st, rng)
    st.put(0x6CD418, "<I", rand_float(rng))
    for a in (0x6CCD64, 0x6CCD68, 0x6CCD6C, 0x6CCD70):
        st.put(a, "<I", rand_float(rng))
    st.put(0x4BE334, "<i", pick(rng, 0, 160, 320, rng.randrange(-0x10000, 0x10000), r32(rng)))
    st.put(0x4BE338, "<i", pick(rng, 0, 120, 240, rng.randrange(-0x10000, 0x10000), r32(rng)))
    st.script["vertex_clip"] = [pick(rng, 0, 0, 0, 0, 1, 0x10, r32(rng)) for _ in range(510)]
    return [pick(rng, 0, 1, 2, 7, 0x100, r32(rng) & 0x3FF, r32(rng))], False


TESTS = {
    "net_pickup_send": (0x45E6F0, g_pickup),
    "net_weapon_kind_send": (0x45E800, g_kind),
    "net_controls_store": (0x45EA60, g_store),
    "net_controls_ready": (0x45EB60, g_none),
    "net_race_over": (0x45EBA0, g_none),
    "net_controls_wait": (0x45EBC0, g_wait),
    "net_controls_changed": (0x45EE10, g_index),
    "net_controls_history_set": (0x45EE60, g_history),
    "net_player_free": (0x45EEB0, g_index),
    "net_time": (0x45EEE0, g_time),
    "net_sequence_advance": (0x45EF40, g_none_void),
    "net_wait_players_ready": (0x45EF90, g_ready),
    "net_race_started": (0x45F030, g_none),
    "net_wait_race_start": (0x45F040, g_race_start),
    "net_settings_sent": (0x45F0B0, g_none),
    "net_cars_collect": (0x45F0C0, g_cars),
    "net_states_count": (0x45F270, g_count),
    "net_states_changed": (0x45F2C0, g_states_changed),
    "net_states_send": (0x45F310, g_states_send),
    "net_menu_mode_unset": (0x45F390, g_none),
    "net_menu_mode_send": (0x45F3A0, g_states_send),
    "net_all_finished": (0x45F530, g_none),
    "net_chat_lines": (0x45F5B0, g_chat),
    "net_player_list": (0x45F650, g_list),
    "net_name_send": (0x45F7B0, g_name),
    "net_results_allowed": (0x45F970, g_allowed),
    "net_results_send": (0x45F990, g_results),
    "net_pause_request": (0x45FA60, g_pause),
    "net_player_cheated": (0x45FB90, g_cheat),
    "net_finish_send": (0x45FC40, g_finish),
    "net_race_results_send": (0x45FCA0, g_race_results),
    "net_results_check": (0x45FDE0, g_results_check),
    "net_pause_due": (0x45FE70, g_pause_due),
    "net_message_format": (0x45FF00, g_format),
    "net_kill_message": (0x4600A0, g_kill),
    "net_players_compact": (0x4609B0, g_compact),
    "net_all_paused": (0x460A20, g_paused),
    "net_snatched_send": (0x460A90, g_snatched),
    "sparks_spawn": (0x462AE0, g_spawn),
    "sparks_draw": (0x462BB0, g_draw),
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
    cw = pick(rng, 0x27F, 0x27F, 0x37F, 0x07F, 0x67F)
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
        for line in open("build/groups/pm_owned.txt"):
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
    rng = random.Random(0x92)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c", "-long")]
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
