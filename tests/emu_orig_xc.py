#!/usr/bin/env python3
"""Differential test of orig_xc.c (group XC: code Ghidra never made into
functions, reached through stored code addresses: menu item callbacks, menu
action handlers and body hit callbacks, 0x450d40-0x48dc20) against the
original, on random state.

Every callee is a logging stub in both runs (the call log records as many
arguments as the callee reads, bytes and words masked; strings by content;
the choice callback's value pointer by the value it points at, and the stub
writes a scripted new value there), except the C runtime's _ftol, which runs
for real. Return value (when not void), the global regions below and the
scratch pages are compared bit for bit; the stack is zeroed before each run.

    tests/emu_orig_xc.py EXE build/emu_xc.elf [iterations] [names...] [-v]
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
SCR_SIZE = 0x20000
CONFIG = SCR + 0x0000  # 0x1000
RACE = SCR + 0x1000  # 0x1000
TEXT = SCR + 0x2000  # the callbacks' text buffer
NAME = SCR + 0x2200  # pad names
ITEMS = SCR + 0x4000  # menu items, 0x730 each (up to 0x10)
BODY = SCR + 0xC000
OTHER = SCR + 0xD000
OWNER = SCR + 0xE000

REGIONS = [
    (0x4AF400, 0x500),
    (0x4B1960, 4),
    (0x4B1C70, 0x30),
    (0x4B2198, 4),
    (0x4BEB38, 4),
    (0x4CB000, 0x800),
    (0x4D4540, 0x20),
    (0x5D6230, 0x10),
]


def S(name, kinds):
    return (name, kinds)


# argument kinds: v dword, b byte, h word, s string, p dword pointed at (the choice value)
STUBS = {
    0x450C00: S("menu_choice_callback", "vvvpv"),
    0x449160: S("name_entry_input", "vvvvvvvv"),
    0x405320: S("records_reset", "vvvv"),
    0x447F20: S("option_onoff_step", "vvv"),
    0x4481A0: S("menu_text_get", "vvvv"),
    0x4979F0: S("sprintf", "vsv"),
    0x470640: S("snd_channel_count_add", "v"),
    0x4705F0: S("snd_speaker_cycle", "vv"),
    0x4705D0: S("snd_settings_apply", ""),
    0x470E70: S("sound_voices_crowded", ""),
    0x470EB0: S("sound_voice_flag8", ""),
    0x471B30: S("sound_cd_volume_keep", "vh"),
    0x471B80: S("sound_volume_set", "h"),
    0x4475F0: S("fe_repeat_select", "v"),
    0x496C60: S("cd_next", ""),
    0x496C50: S("cdaudio_set_shuffle", "v"),
    0x4498D0: S("config_series_set", "v"),
    0x48A010: S("joy_count", ""),
    0x48A420: S("input_capture_binding", "v"),
    0x452640: S("control_entry_set", "vvvvb"),
    0x4526F0: S("control_type_set", "vvv"),
    0x472A90: S("voice_play_plain", "vvv"),
    0x452730: S("menu_state_reset", ""),
    0x48A560: S("input_capture_any", ""),
    0x48A590: S("joy_record", "v"),
    0x42E6D0: S("text_line_get", "vvsv"),
    0x452900: S("control_button_set", "vb"),
    0x452C20: S("f452c20", ""),
    0x48D490: S("f48d490", ""),
    0x4479D0: S("fe_menu_goto", "vv"),
    0x435D43: S("network_session_poll", "v"),
    0x45F790: S("network_flag9_set", "v"),
    0x452CE0: S("f452ce0", ""),
    0x48A840: S("network_restart", "vv"),
    0x447AB0: S("fe_menu_back", ""),
    0x43692A: S("game_time", ""),
    0x48B610: S("f48b610", ""),
    0x48B6D0: S("f48b6d0", ""),
    0x48B780: S("network_sessions_list", "vvvv"),
    0x458320: S("pickup_fallen", "v"),
    0x459610: S("homer_end", "vv"),
    0x4599C0: S("car_blast_hit", "vv"),
    0x4600A0: S("net_kill_message", "vvv"),
    0x48DDD0: S("snatch_finish", "vv"),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def cstr(m, address, limit=0x100):
    if not (address and (SCR <= address < SCR + SCR_SIZE or 0x400000 <= address < 0x700000 or on_stack(address))):
        return ("ptr?", address)
    return rd(m, address, limit).split(b"\0")[0]


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B2198, "<I", CONFIG)
        self.put(0x4B1960, "<I", RACE)
        self.put(0x4CB4B8, "<I", ITEMS)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        a = self.find(address)
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.mem[a], address - a, value)

    def sput(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.scr, address - SCR, value)

    def sputb(self, address, data):
        self.scr[address - SCR:address - SCR + len(data)] = data


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
        words = struct.unpack("<12I", bytes(uc.mem_read(esp, 48)))
        raw = list(words[1:1 + len(kinds)])
        logged = []
        for kind, a in zip(kinds, raw):
            if kind == "v":
                logged.append(None if on_stack(a) else a)
            elif kind == "b":
                logged.append(a & 0xFF)
            elif kind == "h":
                logged.append(a & 0xFFFF)
            elif kind == "s":
                logged.append(cstr(self.m, a))
            elif kind == "p":
                logged.append(struct.unpack("<I", rd(self.m, a, 4))[0])
        self.log.append((name, tuple(logged)))
        eax = self._next(name, 0)
        if name == "menu_choice_callback":
            new = self._next("choice_value", None)
            if new is not None:
                uc.mem_write(raw[3], struct.pack("<I", new & 0xFFFFFFFF))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def pick(rng, *choices):
    return rng.choice(choices)


def cmd(rng):
    return pick(rng, 0x12, 0x13, 0x14, 0x15, 0x16, 0x12, 0x14, 0x15, 0xC, 0xD, 0x11, 0x11, 6, 0xA, 0x17, 0x11,
                rng.getrandbits(32))


def small_byte(rng, hi):
    return pick(rng, *range(hi + 2), 0xFF, rng.getrandbits(8))


# ---------------------------------------------------------------- generators

def g_choice(off):
    def gen(st, rng):
        st.sput(CONFIG + off, "<B", small_byte(rng, 4))
        st.script["menu_choice_callback"] = [pick(rng, 0, 1, 1, 7)]
        if rng.randrange(4):
            st.script["choice_value"] = [pick(rng, 0, 1, 2, 3, 4, 0x1FF, rng.getrandbits(32))]
        return [cmd(rng), TEXT], True
    return gen


def g_name_action(st, rng):
    # 0x449860 (lead): player record b at *0x4cb4b8, 0x730 bytes each
    b = pick(rng, 0, 1, 3, 5, -1, -3, -128, 127)
    st.put(0x4CB588, "<b", b)
    st.put(0x4CB49C, "<i", pick(rng, 0, 1, 6, -1, -2, -200, rng.getrandbits(32)))
    rec = RACE + 0x100  # record b lands here, filled with random words
    st.put(0x4CB4B8, "<I", (rec - b * 0x730) & 0xFFFFFFFF)
    for off in range(0x510, 0x540, 4):
        st.sput(rec + off, "<I", rng.getrandbits(32))
    st.script["name_entry_input"] = [rng.getrandbits(32)]
    return [rng.getrandbits(32)], True


def g_records_thunk(st, rng):
    st.script["records_reset"] = [rng.getrandbits(32)]
    return [rng.getrandbits(32), rng.getrandbits(32)], True


def g_onoff_race(off):
    def gen(st, rng):
        st.sput(RACE + off, "<B", small_byte(rng, 2))
        st.script["option_onoff_step"] = [pick(rng, 0, 1, 2, 0x100, 0x201, rng.getrandbits(32))]
        return [cmd(rng)], True
    return gen


def g_onoff_config(off):
    def gen(st, rng):
        st.sput(CONFIG + off, "<B", small_byte(rng, 2))
        st.script["option_onoff_step"] = [pick(rng, 0, 1, 2, 0x100, 0x201, rng.getrandbits(32))]
        return [cmd(rng)], True
    return gen


def g_byte_choice(base, off, hi):
    def gen(st, rng):
        st.sput(base + off, "<B", small_byte(rng, hi))
        return [cmd(rng), TEXT], True
    return gen


def g_slider(word):
    def gen(st, rng):
        st.sput(RACE + word, "<H", rng.getrandbits(16))
        k = rng.randrange(6)
        if k == 0:
            v = rng.getrandbits(32)
        elif k == 1:
            v = fbits(pick(rng, 0.0, -0.0, 0.5, -0.5, 1.5, 2.5, -1.5, 16383.0, 16384.0, 2.0 ** 31, -(2.0 ** 31), 1e10))
        else:
            v = fbits(rng.uniform(-100.0, 20000.0))
        return [v], False
    return gen


def g_switch(address):
    def gen(st, rng):
        st.put(address, "<I", pick(rng, 0, 1, 2, rng.getrandbits(32)))
        st.put(0x4CB46C, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
        st.put(0x4B1C88, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
        st.script["option_onoff_step"] = [pick(rng, 0, 1, 2, rng.getrandbits(32))]
        st.script["sound_voice_flag8"] = [pick(rng, 0, 1, 5)]
        st.script["sound_voices_crowded"] = [pick(rng, 0, 1, 5)]
        return [cmd(rng)], True
    return gen


def g_channels(st, rng):
    st.put(0x4BEB38, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(0x4B1C74, "<i", pick(rng, 0, 7, 8, 9, 32, 64, 65, -1, rng.getrandbits(32)))
    return [cmd(rng), TEXT], True


def g_speaker(st, rng):
    st.put(0x4D454C, "<i", pick(rng, 0, 1, 2, 3, -1, rng.getrandbits(32)))
    return [cmd(rng), TEXT], True


def g_cd_track(st, rng):
    st.script["fe_repeat_select"] = [pick(rng, 0, 1, 7)]
    return [pick(rng, 6, 6, 6, 7, 5, 0xA, rng.getrandbits(32))], True


def g_shuffle(st, rng):
    st.sput(RACE + 0x61E, "<H", pick(rng, 0, 1, 2, 3, 0xFFFF, rng.getrandbits(16)))
    return [cmd(rng), TEXT], True


def g_series(st, rng):
    st.script["config_series_set"] = [rng.getrandbits(32)]
    return [rng.getrandbits(32), pick(rng, 0, 1, 5, rng.getrandbits(32))], True


def set_player(st, rng):
    st.put(0x4CB7E8, "<i", pick(rng, 0, 1, 2, 3, 0, 1, 2, 3, 4, -1, rng.getrandbits(32)))


def g_ctl_toggle(off):
    def gen(st, rng):
        st.put(0x4CB7E8, "<i", rng.randrange(4))
        for p in range(4):
            st.sput(CONFIG + p * 0x90 + off, "<B", small_byte(rng, 2))
        st.script["option_onoff_step"] = [pick(rng, 0, 1, 2, 0x100, rng.getrandbits(32))]
        return [cmd(rng)], True
    return gen


def g_binding(st, rng):
    set_player(st, rng)
    n = rng.randrange(0, 0x11)
    st.put(0x4CB49C, "<i", pick(rng, n, n, n, -1))
    st.put(0x4CB588, "<b", pick(rng, *range(-1, n + 1)) if rng.randrange(6) else rng.getrandbits(8))
    st.put(0x4CB7EC, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(0x4AF448, "<I", pick(rng, 0, 1, 0, 1, rng.getrandbits(32)))
    st.put(0x4AF43C, "<I", pick(rng, 0x1B, 0xE3, 0xE4, 0xE5, 0x1C, rng.getrandbits(32)))
    st.script["input_capture_binding"] = [pick(rng, 0, 1, 2, rng.getrandbits(32))]
    st.script["fe_repeat_select"] = [pick(rng, 0, 1)]
    return [pick(rng, 6, 7, 0xA, 0xA, 0xA, 5, 8, 9, 0xB, 0, -1, rng.getrandbits(32))], True


def g_pad(st, rng):
    set_player(st, rng)
    count = pick(rng, 0, 1, 2, 3, 8, -1, 255, 256)
    st.script["joy_count"] = [count]
    for p in range(4):
        st.sput(CONFIG + p * 0x90 + 0x1A1, "<B", pick(rng, 0, 1, 2, 7, 8, 0xFF, 0xFE, rng.getrandbits(8)))
    k = rng.randrange(4)
    if k == 0:
        st.script["joy_record"] = [0]
    else:
        n = pick(rng, 0, 5, 23, 24, 24, 25, 25, 30, 40, 0x3F)
        text = bytes(pick(rng, *b"abc XY Z  ") for _ in range(n))
        st.sputb(NAME, text + b"\0")
        st.script["joy_record"] = [NAME]
    return [pick(rng, 0x12, 0x12, cmd(rng)), TEXT], True


def g_countdown(st, rng):
    st.put(0x4CB168, "<i", pick(rng, 0, 1, 2, 5, -1, rng.getrandbits(32)))
    return [pick(rng, 0xA, 0xA, 0xB, 6, rng.getrandbits(32))], True


def g_net_menu(st, rng):
    for a in (0x4CB540, 0x4AF8A0, 0x4AF8A4):
        st.put(a, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    st.script["f48d490"] = [pick(rng, 0, 1, 1)]
    st.script["network_session_poll"] = [pick(rng, 0, 1, 2, 3, 4)]
    return [pick(rng, 0xA, 0xA, 0xB, 0xB, 6, rng.getrandbits(32))], True


def g_sessions(st, rng):
    st.sput(CONFIG + 0x80, "<B", pick(rng, 0, 1, 2, 3, 4, 0xFF))
    st.put(0x4CB168, "<i", pick(rng, 0, 0, 1, 1, 2, -1, rng.getrandbits(32)))
    st.put(0x4CB580, "<I", pick(rng, 0, 0, 1))
    st.put(0x4CB0D8, "<i", pick(rng, 0, 1, 3, -1))
    base = rng.getrandbits(31)
    st.put(0x4CB578, "<I", pick(rng, base, 0xFFFFFF00, base - 5000, base - 120000))
    st.put(0x4CB57C, "<I", pick(rng, base, base - 500, base - 501, 0xFFFFFF00))
    wait = 120000 if st.scr[0x80] == 3 else 5000
    s78 = struct.unpack_from("<I", st.mem[0x4CB000], 0x578)[0]
    s7c = struct.unpack_from("<I", st.mem[0x4CB000], 0x57C)[0]
    times = [pick(rng, base, base + 1, base + 500, base + 501, base + 5001, base + 120001, 0xFFFFFFF0,
                  s78 + wait, s78 + wait + 1, s78 + wait - 1, s7c + 500, s7c + 501) & 0xFFFFFFFF
             for _ in range(4)]
    st.script["game_time"] = times
    st.script["network_restart"] = [pick(rng, 0, 1, 1, 1)]
    st.script["f48b610"] = [pick(rng, 0, 1, 1, 1)]
    st.script["f48b6d0"] = [pick(rng, 0, 1, 1, 1)]
    st.script["network_sessions_list"] = [pick(rng, 0, 1, 1, 2)]
    return [pick(rng, 0xA, 0xA, 0xA, 0xB, 6, rng.getrandbits(32))], True


def g_select(st, rng):
    return [rng.getrandbits(32)], False


def g_counter(st, rng):
    st.put(0x4AF654, "<i", pick(rng, 0, 1, 0x29, 0x2A, 0x2B, -1, rng.getrandbits(32)))
    return [pick(rng, 0xA, 0xA, 0xB, rng.getrandbits(32))], True


def g_body(st, rng):
    return [BODY, pick(rng, 0, OTHER), rng.getrandbits(32)], True


def g_homer(st, rng):
    other = pick(rng, 0, OTHER, OTHER, OTHER)
    st.sput(OTHER, "<I", (rng.getrandbits(32) & ~0x1FF) | pick(rng, 3, 3, 3, 2, 0x103))
    st.sput(OTHER + 0x70C, "<B", pick(rng, 8, 0, 1, 7, 9))
    st.sput(BODY + 0x18C, "<I", pick(rng, OWNER, OWNER, OWNER, 0))
    return [BODY, other, rng.getrandbits(32)], True


# name: (address, generator)
TESTS = {
    "comm_parity_option": (0x450D40, g_choice(0x3F)),
    "comm_port_option": (0x450CA0, g_choice(0x3C)),
    "name_entry_action": (0x449860, g_name_action),
    "records_reset_thunk": (0x44FFA0, g_records_thunk),
    "comm_stopbits_option": (0x450D90, g_choice(0x3E)),
    "comm_flowcontrol_option": (0x450DE0, g_choice(0x3D)),
    "setup_weapons_toggle": (0x450E50, g_onoff_race(0xA86)),
    "setup_mirror_toggle": (0x450F10, g_onoff_config(0x25)),
    "setup_weather_choice": (0x450F70, g_byte_choice(RACE, 0xA89, 2)),
    "setup_daytime_choice": (0x451060, g_byte_choice(CONFIG, 0x27, 3)),
    "audio_music_volume_slider": (0x451300, g_slider(0xA7E)),
    "audio_effects_volume_slider": (0x451330, g_slider(0xA7C)),
    "audio_toggle_4d4548": (0x451360, g_switch(0x4D4548)),
    "audio_toggle_4b1c84": (0x4513A0, g_switch(0x4B1C84)),
    "audio_toggle_4b1c88": (0x451400, g_switch(0x4B1C88)),
    "audio_toggle_4d4540": (0x451450, g_switch(0x4D4540)),
    "audio_toggle_4d4544": (0x451490, g_switch(0x4D4544)),
    "audio_toggle_4b1c78": (0x4514D0, g_switch(0x4B1C78)),
    "audio_toggle_4b1c7c": (0x451510, g_switch(0x4B1C7C)),
    "audio_toggle_4b1c80": (0x451550, g_switch(0x4B1C80)),
    "audio_channels_choice": (0x451590, g_channels),
    "audio_speaker_choice": (0x451650, g_speaker),
    "audio_cd_track_action": (0x451720, g_cd_track),
    "audio_cd_shuffle_choice": (0x451750, g_shuffle),
    "series_cursor_arrived": (0x451BF0, g_series),
    "control_toggle_1a2": (0x452350, g_ctl_toggle(0x1A2)),
    "control_toggle_1a3": (0x4523C0, g_ctl_toggle(0x1A3)),
    "control_binding_action": (0x452430, g_binding),
    "control_pad_choice": (0x452760, g_pad),
    "menu_countdown_handler": (0x452950, g_countdown),
    "net_host_menu_handler": (0x452D30, g_net_menu),
    "net_join_menu_handler": (0x452DB0, g_net_menu),
    "net_sessions_found_select": (0x452E30, g_select),
    "net_sessions_handler": (0x452E40, g_sessions),
    "menu_counter_handler": (0x453000, g_counter),
    "pickup_fallen_hit": (0x458310, g_body),
    "homer_hit": (0x4596F0, g_homer),
    "snatch_hit": (0x48DC20, g_body),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, returns = gen(st, rng)
    cw = 0x27F if rng.randrange(4) else 0x37F
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok", r[1]) if returns else ("ok",)
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
            logs = f"\n  calls from #{k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(53)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v",)]
    for name, (address, gen) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, gen, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
