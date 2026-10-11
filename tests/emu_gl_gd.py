#!/usr/bin/env python3
"""Differential test of gl_gd.c (group GD: front-end / input / setup leftovers
and six x87 functions) against the original, on random state.

Everything outside the functions under test is a logging stub in both runs
(the call log records as many arguments as the callee reads, strings and
buffers by content, stack pointers never by value): menu builders, binding
lookups, joystick helpers, DirectInput / DirectDraw wrappers, QMixer (through
its import slots, pointed at stdcall trampolines), the sound voice setters.
The C runtime (strstr, strlwr, memcpy, atan2, sqrt, fmod, sin, cos, ftol)
runs for real. Return value, all the global regions listed below and the
scratch pages are compared bit for bit. The stack is zeroed before each run.

    tests/emu_gl_gd.py EXE build/emu_gd.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPCW,  # noqa: E402
                               UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

SCR = 0x01000000
SCR_SIZE = 0x10000
CONFIG = SCR + 0x0000  # 0x600
RACE = SCR + 0x1000  # 0x1000
ITEMS = SCR + 0x2000  # 3 menu items of 0x730
ARG = SCR + 0x4000  # argument blocks (pos / vel / matrix / records), 0x4000
STR = SCR + 0x8000  # strings
ENTRY = SCR + 0x7f00  # a controls entry (4 bytes)
TRAMP = 0x00800000  # stub code for import slots and callbacks

REGIONS = [
    (0x4ACC38, 0x10),
    (0x4AF610, 4),
    (0x4AF640, 0x20),
    (0x4AF790, 0x10),
    (0x4B0084, 0x30),
    (0x4B1960, 4),
    (0x4B197C, 4),
    (0x4B1C70, 0x40),
    (0x4B1E90, 0x20),
    (0x4B2198, 4),
    (0x4B86C4, 0x338 * 4),
    (0x4BA6F0, 8),
    (0x4BE3B0, 0x50),
    (0x4BE47C, 4),
    (0x4BE4A4, 4),
    (0x4BEB38, 4),
    (0x4C9DF8, 0x560),
    (0x4CB480, 0x180),
    (0x4CB7F0, 0x10),
    (0x4D4520, 0x60),
    (0x4D45F0, 0x10),
    (0x4D5F40, 0x50),
    (0x4D5FA0, 8),
    (0x4D5FD0, 0x100),
    (0x4D6238, 0x10),
    (0x4D6678, 0x700),
    (0x4D74A0, 0x1400),
    (0x4D96E0, 0x1B0),
    (0x5D5CD0, 0x240),
    (0x5D6230, 0x20),
    (0x5FF2E0, 0x120),
    (0x6CB500, 0xF10),
]

SLOTS = {0x4A3290: 0x00, 0x4A31F8: 0x10, 0x4A31FC: 0x20, 0x4A3200: 0x30, 0x4A3208: 0x40}  # import slot -> trampoline
CALLBACK = TRAMP + 0x100  # menu item callback stub


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def cstr(m, address, limit=0x200):
    if not (address and (SCR <= address < SCR + SCR_SIZE or 0x400000 <= address < 0x700000 or
                         E.STACK <= address < E.STACK + E.STACK_SIZE)):
        return ("ptr?", address)
    data = rd(m, address, min(limit, 0x100))
    return data.split(b"\0")[0]


def mk_float(rng):
    k = rng.randrange(9)
    if k == 0:
        return struct.pack("<f", rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 8.0, 100.0, 0.1, 15.0, 360.0]))
    if k == 1:
        return struct.pack("<f", rng.uniform(-2.0, 2.0))
    if k == 2:
        return struct.pack("<f", rng.uniform(-200.0, 200.0))
    if k == 3:
        return struct.pack("<f", rng.uniform(0.0, 1.0))
    if k == 4:
        return struct.pack("<I", rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FA00000, 1, 0x80000001,
                                             0x00800000, 0x7F7FFFFF]))
    if k == 5:
        return struct.pack("<f", rng.uniform(0.0, 40000.0))
    return struct.pack("<I", rng.getrandbits(32))


def fl_u32(rng):
    return struct.unpack("<I", mk_float(rng))[0]


def real_float(rng, lo, hi):
    return struct.pack("<f", rng.uniform(lo, hi))


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.fixed = []  # (address, bytes) written after the random fill (outside the regions)
        self.put(0x4B2198, "<I", CONFIG)
        self.put(0x4B1960, "<I", RACE)
        self.put(0x4CB4B8, "<I", ITEMS)

    def put(self, address, fmt, value):
        for a, n in REGIONS:
            if a <= address < a + n:
                if isinstance(value, int) and fmt[-1] in "iIhHbB":
                    value &= (1 << (8 * struct.calcsize(fmt))) - 1
                    fmt = fmt[:-1] + fmt[-1].upper()
                struct.pack_into(fmt, self.mem[a], address - a, value)
                return
        raise KeyError(hex(address))

    def putb(self, address, data):
        for a, n in REGIONS:
            if a <= address < a + n:
                self.mem[a][address - a:address - a + len(data)] = data
                return
        raise KeyError(hex(address))

    def sput(self, address, fmt, value):
        off = address - SCR
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.scr, off, value)

    def sputb(self, address, data):
        off = address - SCR
        self.scr[off:off + len(data)] = data

    def sget(self, address, fmt):
        return struct.unpack_from(fmt, self.scr, address - SCR)[0]


# ---------------------------------------------------------------- stubs

# address: (name, number of args, argument kinds, calling convention, returns)
# kinds: v value, s C string at the pointer, g 16 bytes at the pointer, x 24 bytes at the pointer, - ignored (a
# pointer into the callee's own stack frame)
def S(name, kinds, conv="c", returns=True):
    return (name, len(kinds), kinds, conv, returns)


STUBS = {
    0x43775A: S("cheat_mode", ""),
    0x447F60: S("cheat_flag7", ""),
    0x471C90: S("sound_play", "vvvvv"),
    0x48C630: S("net_a", ""),
    0x48C460: S("net_b", ""),
    0x45F530: S("net_c", ""),
    0x45F0C0: S("net_d", ""),
    0x45DDE0: S("net_e", ""),
    0x45E400: S("net_f", ""),
    0x45F2C0: S("net_g", ""),
    0x45F310: S("net_h", ""),
    0x452D20: S("net_i", ""),
    0x45F390: S("net_j", ""),
    0x45F3A0: S("net_k", ""),
    0x45F0B0: S("net_l", ""),
    0x48D450: S("slot_ok", "v"),
    0x4479F0: S("menu_4479f0", "vvv"),
    0x4475D0: S("menu_4475d0", "v"),
    0x4475F0: S("menu_4475f0", "v"),
    0x447610: S("menu_447610", "v"),
    0x447630: S("menu_447630", "v"),
    0x448580: S("saved_get", "vv"),
    0x4481A0: S("text_get", "pvvv"),
    0x449C40: S("binding_get", "vv"),
    0x449960: S("binding_apply", "vvv"),
    0x46B240: S("pair_a", "v"),
    0x46B330: S("pair_b", "v"),
    0x46B200: S("pair_c", ""),
    0x48A170: S("joy_axis", "vv"),
    0x4897C0: S("joy_pov_dir", "vv"),
    0x48A010: S("joy_count", ""),
    0x48A230: S("joy_axis_edge", "vv"),
    0x48A3E0: S("pov0", "v"),
    0x48A3F0: S("pov1", "v"),
    0x48A400: S("pov2", "v"),
    0x48A410: S("pov3", "v"),
    0x48A040: S("joy_buttons", "v"),
    0x48A270: S("joy_button_edge", "vv"),
    0x489500: S("joy_range", "vxvv"),
    0x4891F0: S("joy_deadzone", "vv"),
    0x489180: S("joy_saturation", "vvv"),
    0x487160: S("modes_list", ""),
    0x42E6D0: S("text_entry", "pvvv"),
    0x4979F0: S("sprintf", "psvvv"),
    0x472B70: S("sound_click", "vvvv"),
    0x448260: S("item_label", "vs"),
    0x431670: S("tex_width", "v"),
    0x4316A0: S("tex_height", "v"),
    0x4482A0: S("item_commit", "vvvv"),
    0x448A00: S("menu_title", "vv"),
    0x4977B0: S("snd_check", ""),
    0x44CB10: S("slider_begin", "vvvvvvvvv"),
    0x4516E0: S("sound_extra_a", ""),
    0x44C980: S("add_label", "vv"),
    0x448140: S("add_item", "vvvvv"),
    0x470E70: S("snd_flag_a", ""),
    0x470EB0: S("snd_flag_b", ""),
    0x44C6D0: S("add_choice", "vvvvvv"),
    0x44C9D0: S("add_slider", "vvvvvv"),
    0x451630: S("sound_extra_b", ""),
    0x4714C0: S("eax_voice_level", "vv"),
    0x4700B0: S("voice_pos", "vvvv"),
    0x470160: S("voice_dir", "vvvv"),
    0x4716C0: S("eax_room", "v"),
    0x4963C0: S("debug_print", "vsssss s".replace(" ", "")),
    # trampolines: import slots (stdcall) and the menu item callback (cdecl)
    TRAMP + 0x00: S("send_dlg_item", "vvvvv", "s"),
    TRAMP + 0x10: S("mix_is_done", "vv", "s"),
    TRAMP + 0x20: S("mix_pause", "vvv", "s"),
    TRAMP + 0x30: S("mix_flush", "vvv", "s"),
    TRAMP + 0x40: S("mix_resume", "vvv", "s"),
    0x00800100: S("item_callback", "v"),
}


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.active = set()
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.active = active

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address not in self.active:
            return
        name, nargs, kinds, conv, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<12I", bytes(uc.mem_read(esp, 48)))
        raw = list(words[1:1 + nargs])
        logged = []
        for kind, a in zip(kinds, raw):
            if kind == "v":
                logged.append(a)
            elif kind == "s":
                logged.append(cstr(self.m, a))
            elif kind == "g":
                logged.append(rd(self.m, a, 16))
            elif kind == "x":
                logged.append(rd(self.m, a, 24))
            elif kind == "p":  # a pointer into the callee's frame is not a value
                logged.append(None if E.STACK <= a < E.STACK + E.STACK_SIZE else a)
        if name == "send_dlg_item":
            logged = raw[:4] + [cstr(self.m, raw[4]) if raw[2] == 0x143 else raw[4]]
        self.log.append((name, tuple(logged)))
        # effects
        if name in ("text_get", "text_entry"):
            r = self._next(name, 0)
            text = self._next(name + "_text", b"Text")
            if (name == "text_get" or r != 0xFFFFFFFF) and (E.STACK <= raw[0] < E.STACK + E.STACK_SIZE):
                uc.mem_write(raw[0], text + b"\0")
            eax = r
        elif name == "sprintf":
            fmt = cstr(self.m, raw[1])
            out = (b"S|" + (fmt if isinstance(fmt, bytes) else b"?") + b"|%d|%d|%d" % tuple(
                a if a < 0x80000000 else a - (1 << 32) for a in raw[2:5]))
            if E.STACK <= raw[0] < E.STACK + E.STACK_SIZE:
                uc.mem_write(raw[0], out + b"\0")
            eax = len(out)
        elif name == "eax_voice_level":
            eax = self._next(name, 0)
            if self._next("level_writes", 0):
                v, level = raw
                if 0 <= v < 64:
                    uc.mem_write(0x6CB530 + 0x3C * v, struct.pack("<I", level))
        else:
            eax = self._next(name, 0) if returns else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        pop = 4 + (4 * nargs if conv == "s" else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + pop)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def pick(rng, *choices):
    return rng.choice(choices)


def sword(v):
    return v & 0xFFFFFFFF


def depth_flag(bpp):
    return {1: 0x4000, 2: 0x2000, 4: 0x1000, 8: 0x800, 15: 0x400, 16: 0x400, 24: 0x200, 32: 0x100}.get(bpp, -1)


def rand_text(rng, n):
    return bytes(rng.choice(b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ _-0123456789") for _ in range(n))


def g_d3d(st, rng, m):
    st.put(0x4BA6F4, "<i", rng.randrange(0, 4))
    bpp = pick(rng, 1, 2, 4, 8, 15, 16, 16, 24, 32, 32, rng.randrange(0, 40))
    st.put(0x4D5F70 - 0x4D5F40 + 0x4D5F40, "<i", bpp)
    st.put(0x4D5FA4, "<I", pick(rng, 0, 0, 1))
    flag = depth_flag(bpp)
    descs = []
    for i in range(2):
        d = bytearray(rng.randbytes(0xFC))
        if rng.randrange(10):
            struct.pack_into("<I", d, 0x9C, (flag & 0xFFFFFFFF if flag != -1 else 0) | rng.getrandbits(32) & rng.getrandbits(32))
        if rng.randrange(10):
            d[0x80] |= 0xC
        if rng.randrange(10):
            d[0x84] |= 1
        if rng.randrange(3):
            d[8] ^= rng.randrange(2)
        descs.append(d)
    struct.pack_into("<I", descs[0], 8, pick(rng, 0, 0, 1, 3, rng.getrandbits(32)))
    st.sputb(ARG, bytes(descs[0]))
    st.sputb(ARG + 0x100, bytes(descs[1]))
    st.sputb(ARG + 0x200, rng.randbytes(16))
    needle = b"MMX"
    for k, off in enumerate((0x300, 0x400)):
        text = rand_text(rng, rng.randrange(0, 70))
        if k == 1 and rng.randrange(2):
            text = rand_text(rng, rng.randrange(0, 5)) + needle + text
        st.sputb(ARG + off, text + b"\0")
    return [ARG + 0x200, ARG + 0x300, ARG + 0x400, ARG, ARG + 0x100, rng.getrandbits(32)], ("std", 6)


def g_depth(st, rng, m):
    return [pick(rng, 1, 2, 4, 8, 15, 16, 24, 32, 0, -1, 3, 33, 17, 0x100000001 & 0xFFFFFFFF, rng.getrandbits(32))], None


def g_net_poll(st, rng, m):
    st.put(0x4BE4A4, "<i", pick(rng, 0, 0xE, 0xF, 0x10, rng.randrange(-3, 40)))
    for name in ("net_c", "net_d", "net_e", "net_f", "net_g", "net_j", "net_l"):
        st.script[name] = [pick(rng, 0, 1, 1, 5, 0x100) for _ in range(2)]
    return [pick(rng, 1, 2, 3, 0, 1, 2)], None


def g_cheat(st, rng, m):
    codes = [rd(m, a, 40).split(b"\0")[0] for a in
             (0x4ACD5C, 0x4ACD6C, 0x4ACD7C, 0x4ACD94, 0x4ACDA4, 0x4ACDAC, 0x4ACDBC, 0x4ACDCC, 0x4ACDDC, 0x4ACDEC,
              0x4ACDFC, 0x4ACE08, 0x4ACE14, 0x4ACE20)]
    st.put(0x4BE47C, "<I", pick(rng, 0, 0, 0, 1))
    if rng.randrange(5):
        code = rng.choice(codes)
        # newest first: the code is typed ending with its first character
        buf = bytearray(code[1:])
        for i in range(len(buf)):
            if rng.randrange(3) == 0:
                buf[i] = bytes([buf[i]]).upper()[0]
        ch = code[:1]
        if rng.randrange(3) == 0:
            ch = ch.upper()
        buf = bytes(buf)
        if rng.randrange(8) == 0:
            buf = buf[:-1] + b"?"
        st.putb(0x4BE3BC, (buf + b"\0" * 0x22)[:0x21] + b"\0")
        ch = ch[0]
    else:
        st.putb(0x4BE3BC, rand_text(rng, 0x20) + b"\0")
        ch = rng.getrandbits(8)
    return [pick(rng, 0, 5), ch, pick(rng, 1, 1, 1, 0, 2, -1)], None


def g_none(st, rng, m):
    for a in (0x4CB5A8, 0x4CB5B8, 0x4CB5C8, 0x4CB5D8):
        if rng.randrange(2):
            st.put(a, "<I", 0)
    return [], None


def g_flag7(st, rng, m):
    st.put(0x4AF64C, "<i", pick(rng, 4, 4, 0, 3, rng.getrandbits(32)))
    return [], None


def g_pick_avail(st, rng, m):
    st.sput(ARG, "<i", pick(rng, 0, 1, 2, 3, -1, 4, 7, rng.randrange(-5, 9)))
    st.script["slot_ok"] = [pick(rng, 0, 0, 0, 1, 5) for _ in range(6)]
    return [ARG, pick(rng, 1, -1, 2, -3, 0, 5, rng.randrange(-8, 8))], None


def g_two(st, rng, m):
    return [rng.getrandbits(32), rng.getrandbits(32)], None


def g_param_cb(st, rng, m):
    for a in (0x4B008C, 0x4B0090, 0x4B0094, 0x4B0098, 0x4B009C, 0x4B00A0, 0x4B00A4, 0x4B00A8, 0x4B00AC):
        pass
    st.sputb(ARG, rng.randbytes(16))
    return [pick(rng, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0, -1, rng.getrandbits(32)), ARG,
            pick(rng, 100, 100, 100, 0, 1, 2, 19, 20, 21, -3, 99, 101, rng.randrange(-5, 150)),
            pick(rng, 0, 1, rng.getrandbits(32))], None


def g_trim(st, rng, m):
    st.put(0x4AF794, "<i", pick(rng, -1, 1, 2, 2, 1, 0, 3, rng.getrandbits(32)))
    st.sput(RACE + 0x585, "<B", pick(rng, 0, 1, 2, 3, 2, 1, rng.getrandbits(8)))
    st.sput(RACE + 0x720, "<B", rng.getrandbits(8))
    return [], None


def g_choice_cb(st, rng, m):
    st.sput(ARG, "<i", pick(rng, 0, 1, 2, -1, -5, 5, rng.randrange(-4, 8), rng.getrandbits(32)))
    return [pick(rng, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, rng.getrandbits(32)), rng.getrandbits(32),
            pick(rng, 0, 1, 3, 4, -1, rng.randrange(-2, 8)), ARG, rng.getrandbits(32)], None


def g_entry_set(st, rng, m):
    st.script["binding_get"] = [pick(rng, ENTRY, ENTRY, ENTRY, 0)]
    return [rng.randrange(0, 5), rng.randrange(0, 30), pick(rng, -1, 0, 1, 2, 3, 4, -2, rng.randrange(-3, 6)),
            rng.getrandbits(32), rng.getrandbits(32)], None


def g_type_set(st, rng, m):
    st.script["binding_get"] = [pick(rng, ENTRY, ENTRY, ENTRY, 0)]
    return [rng.randrange(0, 5), rng.randrange(0, 30), pick(rng, 0, 1, 2, 3, -1, rng.randrange(-3, 6))], None


def g_button_set(st, rng, m):
    return [pick(rng, 0, 1, 2, 3, 4, -1, rng.randrange(-2, 6)), rng.getrandbits(32)], None


def g_pair(st, rng, m):
    for name in ("pair_a", "pair_b"):
        st.script[name] = [pick(rng, -1, 0, 3)]
    return [rng.getrandbits(32), rng.getrandbits(32)], None


def g_one(st, rng, m):
    return [pick(rng, 0, 1, 2, 3, -1, 4, rng.randrange(-3, 10))], None


def g_pov(st, rng, m):
    st.script["joy_pov_dir"] = [pick(rng, 0, 1, 2, 0x100)]
    for i in range(12):
        st.put(0x4D96E4 + i, "<B", pick(rng, 0, 1))
    return [pick(rng, 0, 1, 2), pick(rng, 0, 0x10, 0x20, 0x30, 5, 0x31, 0x40, 0x11,
                                                              rng.getrandbits(32), 0xFFFFFFF0)], None


def g_pov_n(st, rng, m):
    st.script["joy_pov_dir"] = [pick(rng, 0, 1, 2)]
    return [rng.randrange(0, 3)], None


def g_capture(st, rng, m):
    st.script["joy_count"] = [pick(rng, 0, 1, 2, 3, 4) for _ in range(40)]
    hit = rng.randrange(3) == 0
    st.script["joy_axis_edge"] = [pick(rng, 1, 3, 1, 3, 4, 5, 1, 3, 1, 3, 1, 3, 1, 3, 0, 2) if hit else pick(rng, 1, 3) for _ in range(40)]
    for n in ("pov0", "pov1", "pov2", "pov3"):
        st.script[n] = [pick(rng, 0, 1, 1, 3, 1, 1)]
    st.script["joy_buttons"] = [pick(rng, 0, 1, 2, 5, -1) for _ in range(40)]
    st.script["joy_button_edge"] = [pick(rng, 0, 0, 0, 0, 1, 2, 0, 0) for _ in range(40)]
    return [pick(rng, -1, 0, 1, 2, 3, 4, rng.randrange(-2, 6))], None


def g_capture_any(st, rng, m):
    g_capture(st, rng, m)
    return [], None


def g_joy_record(st, rng, m):
    st.script["joy_count"] = [pick(rng, 0, 1, 2, 3, 4)]
    return [pick(rng, -1, 0, 1, 2, 3, 4, rng.randrange(-3, 6))], None


def g_joy_value(st, rng, m):
    st.put(0x4D623C, "<i", pick(rng, 0, 1, 2, 3, rng.randrange(-2, 5)))
    return [pick(rng, -1, 0, 1, 2, 3, rng.randrange(-2, 5))], None


def g_joy_object(st, rng, m):
    obj = bytearray(rng.randbytes(0x24))
    obj[0x18] = pick(rng, 2, 3, 1, 0, 0xFF, rng.getrandbits(8))
    obj[0x1C] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    st.sputb(ARG, bytes(obj))
    joy = rng.randrange(0, 3)
    st.put(0x4D75F0 + 0x43C * joy, "<i", pick(rng, 0, 1, 5, 11, 12, 13, rng.randrange(-2, 14)))
    st.script["joy_range"] = [pick(rng, 0, 1, 1, 5)]
    return [ARG, joy], None


GUID = None


def g_joy_names(st, rng, m):
    guid = bytes(rd(m, 0x4A4240, 16)) if rng.randrange(3) else rng.randbytes(16)
    if rng.randrange(8) == 0:
        guid = bytearray(rd(m, 0x4A4240, 16))
        guid[rng.randrange(16)] ^= 1 << rng.randrange(8)
        guid = bytes(guid)
    st.sputb(ARG, guid)
    names = b""
    for _ in range(rng.randrange(0, 5)):
        n = pick(rng, rng.randrange(1, 20), rng.randrange(1, 70), 63, 64, 65)
        names += rand_text(rng, n) + b"\0"
    names += b"\0\0"
    st.sputb(ARG + 0x100, names)
    st.put(0x5D5CD4, "<I", rng.randrange(0, 4))
    return [ARG, rng.getrandbits(32), ARG + 0x100, rng.getrandbits(32)], None


def g_modes(st, rng, m):
    n = pick(rng, 0, 1, 2, 3, 5, rng.randrange(-1, 8))
    st.put(0x4D5FD4, "<i", n)
    cur = (pick(rng, 640, 800, 1024), pick(rng, 480, 600, 768), pick(rng, 16, 32))
    st.put(0x4ACC38, "<i", cur[0])
    st.put(0x4ACC3C, "<i", cur[1])
    st.put(0x4ACC40, "<i", cur[2])
    for i in range(7):
        t = pick(rng, cur, (640, 480, 16), (640, 480, 16), (rng.randrange(0, 2000), rng.randrange(0, 2000), pick(rng, 8, 16, 24, 32)))
        st.putb(0x4D5FE4 + 12 * i, struct.pack("<3i", *t))
    st.script["modes_list"] = [pick(rng, 0, 1, 1, 1, 7)]
    st.script["text_entry"] = [pick(rng, 0xFFFFFFFF, 0, 1, 1, 1) for _ in range(8)]
    st.script["text_entry_text"] = [rand_text(rng, rng.randrange(0, 20)) for _ in range(8)]
    return [rng.getrandbits(32), rng.getrandbits(32)], None


def rand_item(st, rng, off):
    for o in (0x534, 0x538, 0x53C, 0x540, 0x544, 0x548):
        st.sputb(off + o, mk_float(rng))
    if rng.randrange(3) == 0:  # a sensible slider
        lo = rng.uniform(-10, 0)
        hi = rng.uniform(1, 40000)
        st.sputb(off + 0x534, struct.pack("<f", lo))
        st.sputb(off + 0x538, struct.pack("<f", hi))
        st.sputb(off + 0x540, struct.pack("<f", rng.uniform(lo, hi)))
        st.sputb(off + 0x544, struct.pack("<f", rng.uniform(0, 8)))
        st.sputb(off + 0x548, struct.pack("<f", rng.uniform(0, 100)))
        st.sputb(off + 0x53C, struct.pack("<f", rng.uniform(0, 100)))
    st.sput(off + 0x514, "<I", pick(rng, 0, CALLBACK, CALLBACK))


def g_slider_step(st, rng, m):
    st.put(0x4CB49C, "<i", pick(rng, 1, 2, 3, 0, rng.randrange(-1, 5)))
    st.put(0x4CB588, "<B", pick(rng, 0, 1, 2, 0xFF, 5, 0x80, rng.getrandbits(8)))
    st.put(0x4BEB38, "<I", pick(rng, 0, 1))
    for i in range(3):
        rand_item(st, rng, ITEMS + i * 0x730)
    st.script["item_callback"] = [0]
    return [pick(rng, 0x17, 0x18, 0x17, 0x18, 0x19, 0x16, 0, rng.getrandbits(32))], None


def g_slider_begin(st, rng, m):
    st.script["text_get"] = [0]
    st.script["text_get_text"] = [rand_text(rng, rng.randrange(0, 30))]
    st.script["tex_width"] = [rng.getrandbits(16)]
    st.script["tex_height"] = [rng.getrandbits(16)]
    return [rng.getrandbits(32), rng.getrandbits(32), fl_u32(rng), fl_u32(rng), fl_u32(rng), fl_u32(rng),
            fl_u32(rng), rng.getrandbits(32), rng.getrandbits(32)], None


def g_slider_begin_real(st, rng, m):
    st.script["text_get"] = [0]
    st.script["text_get_text"] = [rand_text(rng, rng.randrange(0, 30))]
    st.script["tex_width"] = [rng.getrandbits(16)]
    st.script["tex_height"] = [rng.getrandbits(16)]
    lo = rng.uniform(-10, 10)
    hi = rng.uniform(-10, 10) if rng.randrange(3) == 0 else lo + rng.uniform(0, 1000)
    cur = pick(rng, lo, hi, rng.uniform(min(lo, hi) - 5, max(lo, hi) + 5))
    f = lambda v: struct.unpack("<I", struct.pack("<f", v))[0]
    return [rng.getrandbits(32), rng.getrandbits(32), f(lo), f(hi), f(cur), f(rng.uniform(0, 20)), f(rng.uniform(0, 20)),
            rng.getrandbits(32), rng.getrandbits(32)], None


def g_sound_menu(st, rng, m):
    st.sput(RACE + 0xA7C, "<h", pick(rng, 0, 100, -1, 32767, -32768, rng.randrange(-32768, 32768)))
    st.sput(RACE + 0xA7E, "<h", pick(rng, 0, 100, -1, 32767, -32768, rng.randrange(-32768, 32768)))
    st.sput(CONFIG + 0x410, "<B", pick(rng, 0, 0, 1))
    st.put(0x4B1C88, "<i", pick(rng, 0, 0, 1))
    for n in ("snd_check", "snd_flag_a", "snd_flag_b"):
        st.script[n] = [pick(rng, 0, 1, 7)]
    return [], None


def g_fade(st, rng, m):
    st.put(0x4D4524, "<i", pick(rng, 1, 1, 1, 0))
    st.put(0x4D4528, "<i", pick(rng, 1, 1, 1, 0))
    voice = pick(rng, rng.randrange(0, 64), rng.randrange(0, 64), 63, 0, 64, -1, rng.getrandbits(32))
    if 0 <= voice < 64:
        st.putb(0x6CB530 + 60 * voice, mk_float(rng) if rng.randrange(3) == 0 else real_float(rng, 0, 1))
    st.script["eax_voice_level"] = [pick(rng, 0, 0, 1, 0xFFFFFFFF)]
    st.script["level_writes"] = [pick(rng, 0, 1)]
    level = pick(rng, struct.unpack("<I", real_float(rng, -0.5, 1.5))[0], fl_u32(rng), fl_u32(rng))
    step = pick(rng, struct.unpack("<I", real_float(rng, 0, 0.5))[0], fl_u32(rng))
    return [voice, level, step], None


def g_3d(st, rng, m):
    real = rng.randrange(4) != 0
    if real:
        st.putb(0x5FF380, struct.pack("<f", rng.choice([1 / 4096, 1 / 65536, 1.0, rng.uniform(1e-6, 1e-2)])))
        st.putb(0x5FF2E0, struct.pack("<f", rng.choice([1 / 4096, 1 / 65536, 1.0, rng.uniform(1e-6, 1e-2)])))
        st.putb(0x4B1EA4, struct.pack("<f", pick(rng, 100.0, 12.5, rng.uniform(0, 200))))
        st.putb(0x4B1E98, struct.pack("<f", pick(rng, 0.001, rng.uniform(0, 5))))
        st.putb(0x4B1E9C, struct.pack("<f", pick(rng, 0.6, rng.uniform(0, 5))))
        st.putb(0x4B1EA0, mk_float(rng))
        scale = pick(rng, 1 << 14, 1 << 18, 1 << 22, 1 << 26)
        vscale = pick(rng, 1 << 10, 1 << 16, 1 << 22)
    else:
        for a in (0x5FF380, 0x5FF2E0, 0x4B1EA4, 0x4B1E98, 0x4B1E9C, 0x4B1EA0):
            st.putb(a, mk_float(rng))
        scale = vscale = 1 << 31
    st.sput(RACE + 0x58B, "<B", pick(rng, 0, 1, 0xFF))
    st.sputb(ARG, struct.pack("<3i", *[rng.randrange(-scale, scale) for _ in range(3)]))
    st.sputb(ARG + 0x20, struct.pack("<3i", *[rng.randrange(-vscale, vscale) for _ in range(3)]))
    mat = bytearray(rng.randbytes(0x20))
    struct.pack_into("<h", mat, 0xC, rng.randrange(-0x1000, 0x1000))
    struct.pack_into("<h", mat, 0x10, rng.randrange(-0x1000, 0x1000))
    st.sputb(ARG + 0x40, bytes(mat))
    a4 = pick(rng, 0, 0, struct.unpack("<I", real_float(rng, -360, 360))[0], fl_u32(rng))
    voice = pick(rng, rng.randrange(0, 64), 0x80, 0xFF, rng.getrandbits(32))
    return [voice, ARG, ARG + 0x20, pick(rng, 0, ARG + 0x40, ARG + 0x40), a4], None


def g_refresh(st, rng, m):
    st.put(0x4D4524, "<i", pick(rng, 1, 1, 1, 1, 0))
    st.put(0x4D4528, "<i", pick(rng, 1, 1, 1, 1, 0))
    st.put(0x4D453C, "<I", rng.getrandbits(32))
    st.put(0x4D455C, "<I", pick(rng, 0, 1))
    st.put(0x4B1C74, "<i", pick(rng, 0, 1, 4, 8, 10, 63, 64, rng.randrange(-1, 70)))
    st.putb(0x4B1C90, struct.pack("<f", pick(rng, 1.0, rng.uniform(0, 5))))
    st.putb(0x4B1C94, struct.pack("<f", pick(rng, 100.0, rng.uniform(5, 200))))
    st.putb(0x4B1C9C, struct.pack("<f", pick(rng, 15.0, rng.uniform(0, 50))))
    st.putb(0x4B1CA0, struct.pack("<f", pick(rng, 20.0, rng.uniform(0, 50))))
    st.sput(CONFIG + 0x411, "<B", pick(rng, 0, 1, 1))
    base = 0x6CB500
    ch = bytearray(0xF10)
    for i in range(64):
        e = bytearray(rng.randbytes(0x3C))
        if rng.randrange(6):
            e[1] = rng.randrange(1, 256)
        else:
            e[1] = 0
        e[0] = pick(rng, 0x11, 0x13, 0x19, 0x1B, 0x01, 0x10, 0x15, 0x1D, rng.getrandbits(8), 0x11, 0x11)
        e[2] = pick(rng, 0, 0, 1, 2, rng.getrandbits(8), 0x80 | rng.getrandbits(7))
        struct.pack_into("<i", e, 0xC, rng.randrange(-100, 100000))
        e[4] = rng.getrandbits(8)
        e[5] = pick(rng, 0xFF, rng.randrange(0, 40), rng.getrandbits(8))
        struct.pack_into("<f", e, 0x14, rng.choice([rng.uniform(0.5, 150), rng.uniform(0.5, 150), 1.0, 100.0, 0.0, -3.0]))
        for off in (0x28, 0x2C, 0x30):
            struct.pack_into("<f", e, off, rng.choice([rng.uniform(0, 80), rng.uniform(-3, 0), rng.uniform(-0.5, 3)]))
        if rng.randrange(25) == 0:
            e[0x14:0x18] = mk_float(rng)
        ch[i * 0x3C:(i + 1) * 0x3C] = e
    st.mem[0x6CB500] = ch
    st.script["mix_is_done"] = [pick(rng, 0, 0, 1) for _ in range(70)]
    return [], None


# name: (address, generator, return mask (None = void), stubbed callee addresses, argument count of a stdcall function)
TESTS = {
    "d3d_enum_device": (0x41D280, g_d3d, 0xFFFFFFFF, (), 6),
    "ddraw_depth_flag": (0x41D640, g_depth, 0xFFFFFFFF, (), 0),
    "network_session_poll": (0x435D43, g_net_poll, 0xFFFFFFFF,
                             (0x48C630, 0x48C460, 0x45F530, 0x45F0C0, 0x45DDE0, 0x45E400, 0x45F2C0, 0x45F310, 0x452D20,
                              0x45F390, 0x45F3A0, 0x45F0B0), 0),
    "cheat_code_char": (0x4360E1, g_cheat, None, (0x43775A, 0x447F60, 0x471C90), 0),
    "menu_hold_clear": (0x4474E0, g_none, None, (), 0),
    "menu_hold_reset": (0x447500, g_none, None, (), 0),
    "config_flag7_toggle": (0x447F60, g_flag7, None, (), 0),
    "menu_pick_available": (0x448330, g_pick_avail, None, (0x48D450,), 0),
    "config_mirror_flag": (0x448380, g_none, None, (), 0),
    "menu_call_with_zero": (0x44EBB0, g_two, None, (0x4479F0,), 0),
    "menu_state_reset": (0x452730, g_none, None, (0x4475D0, 0x4475F0, 0x447610, 0x447630), 0),
    "menu_param_callback": (0x44D000, g_param_cb, 0xFFFFFFFF, (0x448580,), 0),
    "race_setting_trim": (0x4503C0, g_trim, None, (), 0),
    "menu_choice_callback": (0x450C00, g_choice_cb, 0xFFFFFFFF, (0x4481A0,), 0),
    "control_entry_set": (0x452640, g_entry_set, 0xFFFFFFFF, (0x449C40, 0x449960), 0),
    "control_type_set": (0x4526F0, g_type_set, 0xFFFFFFFF, (0x449C40, 0x449960), 0),
    "control_button_set": (0x452900, g_button_set, None, (0x449960,), 0),
    "pair_lookup": (0x46B510, g_pair, 0xFFFFFFFF, (0x46B240, 0x46B330, 0x46B200), 0),
    "pair_lookup_ok": (0x46B7B0, g_pair, 0xFFFFFFFF, (0x46B240, 0x46B330, 0x46B200), 0),
    "pair_lookup_default": (0x474B70, g_pair, 0xFFFFFFFF, (0x46B240, 0x46B330, 0x46B200), 0),
    "joy_axis_edge_call": (0x48A230, g_two, 0xFFFFFFFF, (0x48A170,), 0),
    "joy_pov_edge": (0x48A2F0, g_pov, 0xFFFFFFFF, (0x4897C0,), 0),
    "joy_pov_edge_0": (0x48A3E0, g_pov_n, 0xFFFFFFFF, (0x4897C0,), 0),
    "joy_pov_edge_1": (0x48A3F0, g_pov_n, 0xFFFFFFFF, (0x4897C0,), 0),
    "joy_pov_edge_2": (0x48A400, g_pov_n, 0xFFFFFFFF, (0x4897C0,), 0),
    "joy_pov_edge_3": (0x48A410, g_pov_n, 0xFFFFFFFF, (0x4897C0,), 0),
    "input_capture_binding": (0x48A420, g_capture, 0xFFFFFFFF,
                              (0x48A010, 0x48A230, 0x48A3E0, 0x48A3F0, 0x48A400, 0x48A410, 0x48A040, 0x48A270), 0),
    "input_capture_any": (0x48A560, g_capture_any, 0xFFFFFFFF,
                          (0x48A010, 0x48A230, 0x48A3E0, 0x48A3F0, 0x48A400, 0x48A410, 0x48A040, 0x48A270), 0),
    "joy_record": (0x48A590, g_joy_record, 0xFFFFFFFF, (0x48A010,), 0),
    "joy_device_value": (0x488D60, g_joy_value, 0xFFFFFFFF, (), 0),
    "input_enum_joystick_object": (0x4890B0, g_joy_object, 0xFFFFFFFF, (0x489500, 0x4891F0, 0x489180), 2),
    "joy_names_collect": (0x48AC40, g_joy_names, 0xFFFFFFFF, (), 4),
    "video_mode_list_fill": (0x488470, g_modes, None, (0x487160, 0x42E6D0, 0x4979F0, TRAMP + 0x00), 0),
    "menu_slider_step": (0x447D80, g_slider_step, None, (0x472B70, 0x00800100), 0),
    "menu_slider_begin": (0x44CB10, g_slider_begin, None, (0x4481A0, 0x448260, 0x431670, 0x4316A0, 0x4482A0), 0),
    "menu_slider_begin_real": (0x44CB10, g_slider_begin_real, None, (0x4481A0, 0x448260, 0x431670, 0x4316A0, 0x4482A0), 0),
    "sound_menu_build": (0x451110, g_sound_menu, None,
                         (0x448A00, 0x4977B0, 0x44CB10, 0x4516E0, 0x44C980, 0x448140, 0x470E70, 0x470EB0, 0x44C6D0,
                          0x44C9D0, 0x451630), 0),
    "eax_voice_fade_step": (0x471580, g_fade, 0xFFFFFFFF, (0x4714C0,), 0),
    "voice_3d_place": (0x472BB0, g_3d, None, (0x4700B0, 0x470160), 0),
    "sound_channels_refresh": (0x470EF0, g_refresh, 0xFFFFFFFF,
                               (TRAMP + 0x10, TRAMP + 0x20, TRAMP + 0x30, TRAMP + 0x40, 0x4716C0, 0x4963C0), 0),
}
SYMBOL = {"menu_slider_begin_real": "menu_slider_begin"}
# the stdcall functions pop their own arguments; the stack pointer is not compared


def fix_names(name):
    return SYMBOL.get(name, name)


def apply(m, st):
    # a clean x87: the CRT's ftol / fmod may leave the control word changed, which would leak into the next run
    m.uc.reg_write(UC_X86_REG_FPCW, 0x37F)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.write(0x4DB37C, b"\0\0\0\0")  # strlwr's locale flag
    for slot, off in SLOTS.items():
        m.write(slot, struct.pack("<I", TRAMP + off))


def run_case(m, stubs, name, address, gen, mask, active, rng):
    st = State(rng)
    args, extra = gen(st, rng, m)
    # the generators call m only to read constant strings; reset them
    results = []
    for target in (address, m.symbols[fix_names(name)]):
        apply(m, st)
        stubs.reset(st.script, set(active))
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
        mem = rd(m, SCR, SCR_SIZE) + b"".join(rd(m, a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/gd_owned.txt"):
            if not line.startswith("#"):
                parts = line.split()
                sizes[int(parts[0], 16)] = int(parts[2])
    except OSError:
        pass
    return sizes


def instruction_addresses(address, size):
    """Addresses of the instructions of the original function (build/rollcage.asm)."""
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
    m.uc.mem_map(TRAMP, 0x1000)
    m.uc.mem_write(TRAMP, b"\xc3" * 0x1000)
    stubs = Stubs(m)
    rng = random.Random(23)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, mask, active, _) in TESTS.items():
        if only and name not in only:
            continue
        if fix_names(name) not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        seen = set()
        size = sizes.get(address, 0)
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, gen, mask, active, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if "-c" in sys.argv and size:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:12]}" if missed else "") + "]"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
