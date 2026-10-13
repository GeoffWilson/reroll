#!/usr/bin/env python3
"""Differential test of orig_xb.c (group XB: the front-end callbacks Ghidra
never made into functions, 0x44daa0..0x450cf0) against the original at
those addresses, on random state.

The options block (*0x4b2198) and the race block (*0x4b1960) live in
scratch memory; the text buffer argument, the text argument and the
profile record 0x4053e0 returns are scratch too. Every callee is a logging
stub in both runs (as many arguments as it reads; a pointer to the
caller's local is logged by content and the stub then writes a scripted
value through it, as 0x448330 / 0x450c00 do; the chat text 0x48d270 gets
is logged as a string), with scripted return values. Memory (scratch and
the globals the callbacks touch), the call logs, ebx / esi / edi / ebp and
the return value (except for the void entry actions) are compared.

    tests/emu_orig_xb.py EXE build/emu_xb.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x8000
CFG = SCR + 0x0000  # options block, 0x2000 (+0x104e + 0x1e * 0x40 = 0x182e)
RACE = SCR + 0x2000  # race block, 0x800
BUF = SCR + 0x3000  # the item text buffer argument
TEXT = SCR + 0x3200  # the text argument of 0x44e200 / 0x44e250
PROFILE = SCR + 0x3400  # what 0x4053e0 returns

REGIONS = [
    (0x4ACC40, 0x20),
    (0x4AF600, 0x110),
    (0x4B1960, 4),
    (0x4B2198, 4),
    (0x4BE444, 4),
    (0x4C2480, 0x50),
    (0x4C2900, 0x20),
    (0x4C2AC0, 0x20),
    (0x4C2F00, 0x10),
    (0x4CA520, 0x10),
    (0x4CA820, 0x40),
    (0x4CB0D0, 0x40),
    (0x4CB480, 0x10),
    (0x4CB580, 0x20),
]

# argument kinds: v value, p pointer to a dword (logged by content, then written), s string
STUBS = {
    0x447F20: ("onoff_step", "vvv"),
    0x4481A0: ("text_get", "vvvv"),
    0x448330: ("pick_available", "pv"),
    0x448380: ("mirror_flag", ""),
    0x450C00: ("choice_cb", "vvvpv"),
    0x450050: ("number_step", "vvvvvv"),
    0x4475F0: ("repeat_select", "v"),
    0x4475D0: ("repeat_updown", "v"),
    0x4479D0: ("menu_goto", "vv"),
    0x447AB0: ("menu_back", ""),
    0x472A90: ("voice_play", "vvv"),
    0x4503C0: ("setting_trim", ""),
    0x41FDA0: ("blend_choose", ""),
    0x428A50: ("render_flags", "vvvvv"),
    0x4219D0: ("refresh", ""),
    0x4979F0: ("sprintf", "vvv"),
    0x45F4F0: ("count", ""),
    0x48D270: ("chat_send", "s"),
    0x44E560: ("series_validate", ""),
    0x448820: ("track_set", "vv"),
    0x44EA70: ("choice_fix", ""),
    0x44EBB0: ("call_zero", "vv"),
    0x448880: ("f448880", ""),
    0x4053E0: ("profile_at", "v"),
    0x405860: ("records_rename", "vv"),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def pick(rng, *choices):
    return rng.choice(choices)


class State:
    def __init__(self, rng):
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B2198, "<I", CFG)
        self.put(0x4B1960, "<I", RACE)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1].upper() + fmt[-1].upper()
        if SCR <= address < SCR + SCR_SIZE:
            struct.pack_into(fmt, self.scr, address - SCR, value)
            return
        a = self.find(address)
        struct.pack_into(fmt, self.mem[a], address - a, value)

    def put_bytes(self, address, data):
        if SCR <= address < SCR + SCR_SIZE:
            self.scr[address - SCR:address - SCR + len(data)] = data
            return
        a = self.find(address)
        self.mem[a][address - a:address - a + len(data)] = data


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind == "p":
                logged.append(rd(self.m, a, 4))
                w = self._next(name + ".w", None)
                if w is not None:
                    uc.mem_write(a, struct.pack("<I", w & 0xFFFFFFFF))
            elif kind == "s":
                s = rd(self.m, a, 0x80)
                logged.append(s.split(b"\0")[0])
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

EVENTS = (0x12, 0x13, 0x14, 0x15, 0x16, 6, 7, 8, 9, 10, 0, 1, 0x11, 0x17, 5, -1)


def event(rng):
    k = rng.randrange(10)
    if k == 0:
        return rng.getrandbits(32)
    if k == 1:
        return rng.randrange(-4, 0x20)
    return rng.choice(EVENTS)


def small_byte(rng, hi=5):
    return pick(rng, rng.randrange(0, hi), rng.randrange(0, hi), rng.getrandbits(8), 0xFF)


def cstr(rng, n):
    return bytes(rng.randrange(1, 256) for _ in range(n)) + b"\0"


def common(st, rng):
    st.script = {
        "onoff_step": [pick(rng, 0, 1, rng.getrandbits(8), rng.getrandbits(32))],
        "choice_cb": [pick(rng, 0, 1, rng.getrandbits(32))],
        "choice_cb.w": [pick(rng, rng.randrange(0, 5), rng.getrandbits(32))],
        "pick_available.w": [pick(rng, rng.randrange(-1, 5), rng.getrandbits(32))] * 2,
        "number_step": [pick(rng, -1, -1, rng.randrange(0, 0x40), rng.getrandbits(32))],
        "repeat_select": [pick(rng, 0, 1, rng.getrandbits(32))],
        "repeat_updown": [pick(rng, 0, 1, rng.getrandbits(32))],
        "count": [pick(rng, 0, 1, 2, 3, rng.getrandbits(32))],
        "profile_at": [PROFILE],
        "sprintf": [rng.getrandbits(32)],
    }
    if rng.randrange(4) == 0:
        del st.script["pick_available.w"]
    if rng.randrange(4) == 0:
        del st.script["choice_cb.w"]


def g_onoff(base, off):
    def g(st, rng):
        common(st, rng)
        st.put((CFG if base == "c" else RACE) + off, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
        return [event(rng)]
    return g


def g_event_buf(setup=None):
    def g(st, rng):
        common(st, rng)
        if setup:
            setup(st, rng)
        return [event(rng), BUF]
    return g


def s_cfg_bytes(*offs):
    def s(st, rng):
        for off in offs:
            st.put(CFG + off, "<B", small_byte(rng))
    return s


def s_choice87(st, rng):
    st.put(CFG + 0x87, "<B", small_byte(rng))
    st.put(CFG + 0x88, "<B", small_byte(rng))
    st.put(0x4CB584, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    st.put(0x4CB48C, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))


def s_race585(st, rng):
    st.put(RACE + 0x585, "<B", small_byte(rng))
    st.put(RACE + 0x720, "<B", rng.getrandbits(8) & pick(rng, 0xFF, 0x18, 0x10, 0x08, 0))
    st.put(CFG + 0x2F, "<B", small_byte(rng, 0x40))
    st.put(CFG + 0x77, "<B", small_byte(rng, 0x40))


def s_blend(st, rng):
    st.put(CFG + 0x434, "<I", pick(rng, rng.randrange(0, 5), rng.randrange(0, 5), 0xFFFFFFFF, rng.getrandbits(32)))


def g_void(st, rng):
    common(st, rng)
    return []


def g_accept(st, rng):
    common(st, rng)
    st.put(0x4CB0D0, "<I", pick(rng, 0, 1, 2, 2, 3, rng.getrandbits(32)))
    return [event(rng)]


def g_chat(st, rng):
    common(st, rng)
    sep = st.sep
    room = 0x3F - len(sep)
    a = rng.randrange(0, min(11, room + 1))
    b = rng.randrange(0, room - a + 1)
    st.put_bytes(CFG + 0x120, cstr(rng, a))
    st.put_bytes(CFG + 0x16C, cstr(rng, b))
    return [event(rng)]


def g_store(st, rng):
    common(st, rng)
    st.put(0x4AF708, "<i", pick(rng, rng.randrange(-2, 0x20), rng.randrange(0, 10), rng.getrandbits(32)))
    st.put_bytes(TEXT, cstr(rng, rng.randrange(0, 0x40)))
    return [TEXT]


def g_value(st, rng):
    common(st, rng)
    return [pick(rng, rng.randrange(-2, 0x20), rng.getrandbits(32), event(rng))]


def g_two(st, rng):
    common(st, rng)
    return [event(rng), pick(rng, rng.randrange(0, 8), rng.getrandbits(32))]


def g_step(st, rng):
    common(st, rng)
    screen = pick(rng, rng.randrange(0, 0x40), rng.randrange(0, 0x40), 0)
    st.put(0x4AF64C, "<I", screen)
    n = pick(rng, rng.randrange(1, 12), rng.randrange(1, 12), 0, 0x100, 0x20, -1, rng.getrandbits(32))
    st.put(0x4CA528, "<i", n)
    st.put(0x4C2910, "<I", pick(rng, 0, rng.getrandbits(32), rng.getrandbits(32) & rng.getrandbits(32), 0xFFFFFFFF,
                                0xFFFFFFFE, 0x7FFFFFFF))
    st.put(CFG + 0xBCC + screen, "<B", pick(rng, rng.randrange(0, 12), rng.getrandbits(8), 0, 0xFF))
    return [pick(rng, 0, 1, 0, 1, 10, 2, -1, rng.getrandbits(32))]


def g_render(st, rng):
    common(st, rng)
    st.put(0x4ACC44, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(0x4ACC48, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    return [event(rng)]


# name: (address, generator, returns a value)
TESTS = {
    "fecb_cheat_58b": (0x44DAA0, g_onoff("r", 0x58B), True),
    "fecb_cheat_58d": (0x44DAE0, g_onoff("r", 0x58D), True),
    "fecb_onoff_92": (0x44DB20, g_onoff("c", 0x92), True),
    "fecb_choice4_80": (0x44DB60, g_event_buf(s_cfg_bytes(0x80)), True),
    "fecb_goto_48": (0x44DC50, g_accept, True),
    "fecb_refuse_sound": (0x44DC90, g_accept, True),
    "fecb_choice_8a": (0x44DCE0, g_event_buf(s_cfg_bytes(0x8A)), True),
    "fecb_choice_87": (0x44DD30, g_event_buf(s_choice87), True),
    "fecb_choice_88": (0x44DE10, g_event_buf(s_choice87), True),
    "fecb_onoff_34": (0x44DEA0, g_onoff("c", 0x34), True),
    "fecb_onoff_83": (0x44DEE0, g_onoff("c", 0x83), True),
    "fecb_onoff_37": (0x44DF20, g_onoff("c", 0x37), True),
    "fecb_onoff_93": (0x44DF60, g_onoff("c", 0x93), True),
    "fecb_store_a0": (0x44DFA0, g_void, True),
    "fecb_store_120": (0x44E040, g_void, True),
    "fecb_store_12b": (0x44E080, g_void, True),
    "fecb_store_16c": (0x44E0B0, g_void, True),
    "fecb_chat_send": (0x44E0E0, g_chat, True),
    "fecb_store_c0": (0x44E1C0, g_void, True),
    "fecb_edit_line": (0x44E1F0, g_two, False),
    "fecb_store_macro": (0x44E200, g_store, True),
    "fecb_store_message": (0x44E250, g_store, True),
    "fecb_value_60c": (0x44E460, g_value, False),
    "fecb_world_pick": (0x44E590, g_two, False),
    "fecb_set_1d": (0x44E7E0, g_value, False),
    "fecb_track_pick": (0x44EB00, g_two, False),
    "fecb_track_menu": (0x44EB20, g_accept, True),
    "fecb_step_choice": (0x44EEB0, g_step, True),
    "fecb_profile_rename": (0x44F510, g_void, True),
    "fecb_set_mode": (0x44FE60, g_value, False),
    "fecb_number_2f": (0x450000, g_event_buf(s_race585), True),
    "fecb_number_77": (0x450160, g_event_buf(s_race585), True),
    "fecb_race_585": (0x450260, g_event_buf(s_race585), True),
    "fecb_three_8b": (0x450490, g_event_buf(s_cfg_bytes(0x8B)), True),
    "fecb_onoff_24": (0x450580, g_onoff("c", 0x24), True),
    "fecb_onoff_28": (0x4505E0, g_onoff("c", 0x28), True),
    "fecb_onoff_29": (0x450640, g_onoff("c", 0x29), True),
    "fecb_three_2c": (0x450760, g_event_buf(s_cfg_bytes(0x2C)), True),
    "fecb_render_44": (0x450950, g_render, True),
    "fecb_render_48": (0x4509C0, g_render, True),
    "fecb_three_33": (0x450A30, g_event_buf(s_cfg_bytes(0x33)), True),
    "fecb_blend_434": (0x450AE0, g_event_buf(s_blend), True),
    "fecb_choice_3a": (0x450CF0, g_event_buf(s_cfg_bytes(0x3A)), True),
}


def apply(m, st):
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.uc.reg_write(UC_X86_REG_EBX, 0x11111111)
    m.uc.reg_write(UC_X86_REG_ESI, 0x22222222)
    m.uc.reg_write(UC_X86_REG_EDI, 0x33333333)
    m.uc.reg_write(UC_X86_REG_EBP, 0x44444444)


SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, sep, name, address, gen, returns, rng):
    st = State(rng)
    st.sep = sep
    args = gen(st, rng)
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = (("ok", r[1]) if returns else ("ok",)) + tuple(m.uc.reg_read(x) for x in SAVED)
        mem = rd(m, SCR, SCR_SIZE) + b"".join(rd(m, a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = None
        if first is not None:
            if first < SCR_SIZE:
                where = SCR + first
            else:
                off = first - SCR_SIZE
                for ra, n in REGIONS:
                    if off < n:
                        where = ra + off
                        break
                    off -= n
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True
    return None, results[0][0][0] != "ok"


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    sep = rd(m, 0x4A7F94, 0x40).split(b"\0")[0]
    rng = random.Random(0x58B)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, gen, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        for _ in range(iterations):
            msg, fault = run_case(m, stubs, sep, name, address, gen, returns, rng)
            faults += fault and not msg
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        both = f" ({faults} fault the same way)" if faults else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
