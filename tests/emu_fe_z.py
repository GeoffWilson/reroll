#!/usr/bin/env python3
"""Differential test of fe_z.c (front end group Z: the menu item builder,
the track menu, the new record entry, the controls menu and key / joystick
names, the audio and network lobby menus, the menu water) against the
original, on random menu, config and text state.

Everything outside the group is stubbed in both runs: the string table
reader (0x42e6d0) writes a text made from its section and index, sprintf
and strncpy are done in Python, menu callbacks write a short text, the text
item list hands out scripted indices, and so on. Each stub logs its
arguments (strings by content; output buffers on the stack by kind only)
and the call logs are compared as well as memory. The stack is filled with
one constant byte so that the 63-byte label copies, which carry whatever
follows a string in its buffer, read the same leftovers in both runs.

    tests/emu_fe_z.py EXE build/emu_z.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
CONFIG = SCRATCH  # 0x1000
RACE = SCRATCH + 0x1000  # 0x800
TEXREC = SCRATCH + 0x1800  # 0x1000 (front-end texture records)
INFO = SCRATCH + 0x2800  # 0x100
FONT = SCRATCH + 0x2900  # 0x10
STRINGS = SCRATCH + 0x3000  # 0x1000
BINDINGS = SCRATCH + 0x4000  # 16 of 0x10
BUFFER = SCRATCH + 0x5000  # 0x3000: dst buffers, counts, out arrays
WATER = SCRATCH + 0x8000  # 0x4000
MENU = SCRATCH + 0x10000  # 0x40 * 0x730
CALLBACK = 0x02000000  # a page of ret instructions: menu callbacks

POINTERS = {0x4B2198: CONFIG, 0x4B1960: RACE, 0x4BE340: TEXREC, 0x4BE344: INFO, 0x4CB4B8: MENU,
            0x5D1FE0: WATER}
REGIONS = [
    (0x4AC840, 0x10), (0x4AC980, 0x10), (0x4AF5F0, 0x60), (0x4AF750, 0x30), (0x4AF7A0, 0xE0),
    (0x4BE340, 8), (0x4BE494, 8), (0x4C2480, 0x50), (0x4C2900, 0xC0), (0x4C3100, 0x6800),
    (0x4C98E0, 0xE0), (0x4C9DF8, 0x730), (0x4CA854, 0x10), (0x4CB100, 0x500), (0x4CB7E0, 0x60),
    (0x4D0770, 0x110), (0x4D4524, 8), (0x5D1FE0, 0x34), (0x5D26B0, 0x2CB0), (0x5D5CC8, 4), (0x5D622C, 4),
    (0x6BF160, 8), (0x6C20C0, 0x400), (0x6CB500, 0xF40),
]
STACK_FILL = b"\xcc" * E.STACK_SIZE
TEXT_SECTIONS = [0x4AFA88, 0x4B015C, 0x4B0078, 0x4B05BC, 0x4B059C, 0x4B0580, 0x4B0E34, 0x4AFA24, 0x4B00F8]

# address: (name, arguments: v value, o output buffer, s string, p pointer; returns)
STUBS = {
    0x42E6D0: ("text_entry", "ovvv", True),
    0x4979F0: ("sprintf", "", True),
    0x498350: ("strncpy", "osv", True),
    0x42E050: ("font_find", "v", True),
    0x4456E0: ("menu_changed", "", False),
    0x445340: ("text_fit", "osvvv", False),
    0x448A50: ("menu_text_add", "svvvvvvvv", True),
    0x4484A0: ("driver_name", "ov", False),
    0x45F5B0: ("lobby_lines", "oo", False),
    0x4053E0: ("profile_name", "v", True),
    0x431780: ("text_box_draw", "vvvvvvv", False),
    0x448BA0: ("menu_dialog", "vvvvv", True),
    0x448670: ("series_track_count", "v", True),
    0x4486B0: ("series_locked_mask", "v", True),
    0x4486F0: ("series_names", "v", True),
    0x448B50: ("menu_select", "v", False),
    0x44EBF0: ("menu_go", "vvvv", True),
    0x448BE0: ("menu_mode", "vv", False),
    0x405BA0: ("lap_time_text", "ov", False),
    0x48A010: ("joystick_count", "", True),
    0x449BF0: ("binding_get", "vv", True),
    0x48D270: ("chat_show", "s", False),
    0x48CDD0: ("net_send", "vvpvv", True),
    0x451650: ("cb_speaker", "vo", True),
    0x451750: ("cb_shuffle", "vo", True),
    0x452760: ("cb_device", "vo", True),
    0x4523C0: ("cb_steer", "vo", True),
    0x452350: ("cb_buttons", "vo", True),
    CALLBACK: ("callback", "vo", True),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rint(rng):
    return pick(rng, rng.randrange(-4, 0x40), rng.getrandbits(32), rng.getrandbits(16), 0, 1, 0xFFFFFFFF)


def string_ptr(rng):
    return STRINGS + rng.randrange(0, 16) * 0x100


def section(rng):
    return pick(rng, 0, rng.choice(TEXT_SECTIONS), string_ptr(rng))


def callback(rng):
    return pick(rng, CALLBACK, CALLBACK, 0)


def out(rng):
    return BUFFER + rng.randrange(0, 0x10) * 0x200


# name: (address, argument maker, returns)
TESTS = {
    "track_record_held_by": (0x405380, lambda r: [r.randrange(-2, 18), r.randrange(-2, 8)], True),
    "track_records_rename": (0x405860, lambda r: [r.randrange(-2, 18), r.randrange(-2, 8)], False),
    "word_exchange": (0x429080, lambda r: [(BUFFER + r.randrange(0, 0x100) * 4) | pick(r, 0, 0x80000000),
                                            r.getrandbits(32), BUFFER + 0x2000 + r.randrange(0, 0x100) * 4], True),
    "font_scaled_size": (0x42E090, lambda r: [rint(r)], True),
    "frontend_texture_width": (0x431670, lambda r: [r.randrange(-8, 0x40)], True),
    "frontend_texture_height": (0x4316A0, lambda r: [r.randrange(-8, 0x40)], True),
    "timer_ratio": (0x436B7E, lambda r: [], True),
    "key_code_lookup": (0x43D230, lambda r: [pick(r, r.randrange(-3, 0x103), r.randrange(0, 0x100))], True),
    "menu_text_box": (0x446200, lambda r: [rint(r), rint(r), rint(r)], False),
    "menu_text_get": (0x4481A0, lambda r: [pick(r, out(r), out(r), 0), pick(r, -1, 0, -5, 1, 2, 0x10, 0x200, 0x40),
                                           pick(r, 0, section(r)), pick(r, -3, -2, -2, -1, -1, 0, 1, 2, 5,
                                                                        r.randrange(0, 50))], True),
    "menu_item_label": (0x448260, lambda r: [out(r), pick(r, 0, string_ptr(r), string_ptr(r))], True),
    "menu_item_commit": (0x4482A0, lambda r: [rint(r), rint(r), rint(r), r.getrandbits(8)], True),
    "menu_add_item": (0x448140, lambda r: [section(r), pick(r, -3, -1, 0, 3), rint(r), rint(r), rint(r)], True),
    "menu_player_title": (0x448430, lambda r: [out(r), r.randrange(-2, 8)], False),
    "series_menu_kind": (0x448770, lambda r: [r.randrange(-2, 9)], True),
    "series_track_get": (0x4487C0, lambda r: [r.randrange(-2, 7)], True),
    "series_track_set": (0x448820, lambda r: [r.randrange(-2, 7), r.getrandbits(32)], False),
    "menu_set_title": (0x448A00, lambda r: [section(r), pick(r, -3, -2, -1, 0, 1, 4)], False),
    "name_entry_text": (0x448EC0, lambda r: [string_ptr(r), pick(r, -1, 0, 0x40, 3), pick(r, 0, 1),
                                             pick(r, -1, rint(r))], False),
    "name_entry_setup": (0x449060, lambda r: [r.randrange(-1, 3)] + [rint(r) for _ in range(6)], True),
    "network_player_slots": (0x45F5E0, lambda r: [BUFFER + 0x2F00, BUFFER + 0x2E00, BUFFER + 0x2E80], False),
    "network_chat_post": (0x45D150, lambda r: [string_ptr(r)], False),
    "network_players_line": (0x45F430, lambda r: [], True),
    "menu_text_slot_set": (0x44A1C0, lambda r: [r.randrange(-2, 0x42), r.randrange(0, 6), string_ptr(r)], False),
    "network_lobby_build": (0x449FD0, lambda r: [], False),
    "menu_add_choice": (0x44C6D0, lambda r: [section(r), r.randrange(-3, 3), section(r), r.randrange(-3, 3),
                                             callback(r), rint(r)], True),
    "menu_add_toggle": (0x44C850, lambda r: [section(r), r.randrange(-3, 3), callback(r), rint(r), section(r),
                                             r.randrange(-3, 3), section(r), r.randrange(-3, 3)], True),
    "menu_add_on_off": (0x44C950, lambda r: [section(r), r.randrange(-3, 3), callback(r), rint(r)], True),
    "menu_add_label": (0x44C980, lambda r: [section(r), r.randrange(-3, 3)], True),
    "menu_add_slider": (0x44C9D0, lambda r: [section(r), r.randrange(-3, 3), section(r), r.randrange(-3, 3),
                                             callback(r), rint(r)], True),
    "menu_open_dialog": (0x44E430, lambda r: [rint(r) for _ in range(4)], True),
    "series_validate": (0x44E560, lambda r: [], False),
    "track_menu_choice_fix": (0x44EA70, lambda r: [], False),
    "track_menu_build": (0x44E8D0, lambda r: [], False),
    "menu_go_3": (0x44EFC0, lambda r: [rint(r), rint(r)], True),
    "menu_go_4": (0x44EFE0, lambda r: [rint(r), rint(r)], True),
    "new_record_menu": (0x44F2D0, lambda r: [r.randrange(-2, 18), r.randrange(-2, 8), rint(r), rint(r)], False),
    "new_record_check": (0x44F560, lambda r: [r.randrange(0, 0x40), rint(r)], True),
    "audio_speaker_item": (0x451630, lambda r: [], True),
    "audio_shuffle_item": (0x4516E0, lambda r: [], True),
    "audio_detect_menu": (0x451800, lambda r: [], False),
    "menu_text_next_set": (0x451EC0, lambda r: [rint(r)], False),
    "activity_name": (0x451ED0, lambda r: [out(r), r.randrange(-1, 0x20)], False),
    "binding_text": (0x451F10, lambda r: [out(r), pick(r, BINDINGS + r.randrange(16) * 0x10,
                                                      BINDINGS + r.randrange(16) * 0x10, 0)], False),
    "binding_description": (0x451EF0, lambda r: [out(r), r.randrange(-1, 5), r.randrange(-1, 0x20)], False),
    "joystick_button_text": (0x452220, lambda r: [pick(r, out(r), 0), rint(r)], False),
    "joystick_axis_text": (0x452130, lambda r: [pick(r, out(r), out(r), 0), pick(
        r, r.getrandbits(8), rint(r), r.randrange(0, 8) | r.randrange(0, 3) << 4, 6 | r.randrange(-2, 3) << 4,
        7 | r.randrange(-2, 3) << 4)], False),
    "joystick_hat_text": (0x4522A0, lambda r: [pick(r, out(r), 0), pick(r, 0x10, 0x20, 0x30, rint(r))], False),
    "key_text": (0x451FC0, lambda r: [pick(r, out(r), out(r), 0), pick(r, r.randrange(-2, 0x102),
                                                                     r.randrange(0, 0x100))], False),
    "input_text": (0x451F40, lambda r: [pick(r, out(r), out(r), 0), r.randrange(-3, 6),
                                        pick(r, r.randrange(0, 0x100), rint(r))], False),
    "controls_menu_build": (0x451C00, lambda r: [r.randrange(-1, 5)], False),
    "menu_settled": (0x452FF0, lambda r: [], True),
    "network_send_setting": (0x45F710, lambda r: [rint(r) for _ in range(5)], True),
    "sound_voices_crowded": (0x470E70, lambda r: [], True),
    "sound_voice_flag8": (0x470EB0, lambda r: [], True),
    "network_sessions_list": (0x48B780, lambda r: [r.randrange(-2, 12), BUFFER + 0x2F00, BUFFER + 0x2E00,
                                                   BUFFER + 0x2E80], True),
    "menu_water_update": (0x48D630, lambda r: [pick(r, 0, 1, 1), pick(r, 0, 1, 2, 3, 4, 0xFFFFFFFF)], False),
}


def make_text(rng):
    alphabet = b"ab Cd_EF 01~"
    return bytes(rng.choice(alphabet) for _ in range(pick(rng, 0, rng.randrange(1, 12), rng.randrange(1, 90))))


def make_state(rng):
    config = bytearray(rng.getrandbits(8) for _ in range(0x1000))
    config[0x41] = pick(rng, rng.randrange(0, 9), rng.randrange(1, 5), rng.randrange(1, 5), rng.getrandbits(8))
    for i in range(0x44, 0x48):
        config[i] = pick(rng, rng.randrange(0, 7), rng.randrange(0, 7), 100, 0x65, rng.getrandbits(8), 0)
    config[0x1D] = pick(rng, rng.randrange(0, 6), 100, 0x65, rng.getrandbits(8))
    for t in range(16):  # record tables: used flags / profiles
        for j in range(6):
            config[0x451 + t * 0x54 + j * 0xE] = pick(rng, rng.getrandbits(8), 1 | rng.randrange(0, 8) << 3, 0)
    race = bytes(rng.getrandbits(8) for _ in range(0x800))
    texrec = bytes(rng.getrandbits(8) for _ in range(0x1000))
    info = bytearray(rng.getrandbits(8) for _ in range(0x100))
    struct.pack_into("<H", info, 0x2E, rng.randrange(0, 0xA0))
    font = struct.pack("<4I", *(v & 0xFFFFFFFF for v in (rint(rng), pick(rng, rng.randrange(-0x40, 0x40), rint(rng)),
                                                          pick(rng, rng.randrange(-0x40, 0x40), rint(rng)), 0)))
    strings = b"".join((make_text(rng) + b"\0").ljust(0x100, b"\0") for _ in range(16))
    bindings = b"".join(struct.pack("<4I", *(v & 0xFFFFFFFF for v in (
        rint(rng), pick(rng, -2, -1, 0, 1, 2, 3, 4, 7), rint(rng),
        pick(rng, rng.randrange(0, 0x100), rint(rng), 0x10, 0x20, 0x30, 0x16, 0x27)))) for _ in range(16))
    buffer = bytearray(rng.getrandbits(8) for _ in range(0x3000))
    struct.pack_into("<i", buffer, 0x2F00, pick(rng, rng.randrange(0, 8), 6, 0, -1))  # counts for list tests
    water = bytes(b for _ in range(0x1000) for b in struct.pack("<i", rng.randrange(-0x400000, 0x400000)))
    menu = bytes(rng.getrandbits(8) for _ in range(0x40 * 0x730))
    regions = {a: bytearray(rng.getrandbits(8) for _ in range(n)) for a, n in REGIONS}

    def put(address, fmt, value):
        for a, n in REGIONS:
            if a <= address < a + n:
                struct.pack_into(fmt, regions[a], address - a, value)
                return
        raise KeyError(hex(address))

    put(0x4CB498, "<I", pick(rng, 0, 1, 1, 1))
    put(0x4CB49C, "<i", pick(rng, rng.randrange(0, 0x40), 0x3F, 0x40, 0x41, -1))
    put(0x4CB4B4, "<I", pick(rng, 0, rng.choice(TEXT_SECTIONS), string_ptr(rng)))
    put(0x4CB4A8, "<i", pick(rng, 0, 1, rng.randrange(0, 20), -1))
    put(0x4C99BC, "<i", pick(rng, -1, rng.randrange(0, 12), 0))
    put(0x4AF878, "<i", pick(rng, rng.randrange(0, 7), 0, -1))
    for i in range(14):
        put(0x4AF7A0 + i * 8, "<i", pick(rng, 0xF, 0x10, rng.randrange(0, 0x20)))
    put(0x4AF5F0, "<i", pick(rng, -1, rng.randrange(0, 0x40)))
    for a in (0x4AC844, 0x4AC848, 0x4AC84C):
        put(a, "<i", pick(rng, -1, rng.randrange(0, 8)))
    put(0x5D1FE4, "<i", pick(rng, rng.randrange(0, 7) * 50 + rng.randrange(50), rng.randrange(1, 7) * 50, -50, 0))
    put(0x5D1FE8, "<i", pick(rng, rng.randrange(0, 7) * 50 + rng.randrange(50), rng.randrange(1, 7) * 50, -60, 30))
    put(0x5D200C, "<i", rng.randrange(-3, 9))
    put(0x5D2010, "<i", rng.randrange(-3, 9))
    put(0x5D2CC4, "<i", pick(rng, rng.randrange(-2, 10), 0))
    for p in range(0x5D2CD8, 0x5D5360, 0x66C):
        put(p - 8, "<I", pick(rng, 0, rng.getrandbits(32)))
    put(0x5D622C, "<B", rng.getrandbits(8))
    for a in (0x4D4524, 0x4D4528, 0x4D0774):
        put(a, "<I", pick(rng, 0, 1, 1))
    for p in range(0x6CB534, 0x6CC434, 0x3C):
        put(p, "<I", pick(rng, 0, 0, rng.getrandbits(32)))
    for p in range(0x6CB501, 0x6CC401, 0x3C):
        put(p, "<B", pick(rng, 0, rng.getrandbits(8) & ~8, rng.getrandbits(8)))
    for i in range(0x100):
        put(0x6C20C0 + i * 4, "<I", pick(rng, 0, 0, rng.getrandbits(8) | 0x800000, rng.getrandbits(8),
                                          rng.getrandbits(8) | 0x80000000, rng.getrandbits(32)))
    for i in range(0x40):
        put(0x4C2908 + i, "<B", pick(rng, 0, 1))
    put(0x4BE498, "<I", pick(rng, 0, rng.getrandbits(32), rng.randrange(1, 100)))
    put(0x4CB500, "<I", pick(rng, 0, 1))
    for a in (0x4CB590, 0x4CB598, 0x4CB59C, 0x4CB5A0):
        put(a, "<I", pick(rng, 0, 0, 1))
    put(0x4CB528, "<I", pick(rng, 0, rng.getrandbits(32)))
    for i in range(6):
        put(0x4CB110 + i * 4, "<i", pick(rng, -1, rng.randrange(0, 8)))
    put(0x4CB7F8, "<I", 0x414243)
    for p in POINTERS:
        for a, n in REGIONS:
            if a <= p < a + n:
                struct.pack_into("<I", regions[a], p - a, POINTERS[p])
    script = {
        "text_entry": [pick(rng, -1, 0, 1, 1, 1) for _ in range(40)],
        "font_find": [pick(rng, 0, FONT, FONT) for _ in range(4)],
        "menu_text_add": [pick(rng, -1, rng.randrange(0, 0x40), rng.randrange(0, 0x40), 0x40) for _ in range(20)],
        "menu_dialog": [rint(rng)],
        "series_track_count": [pick(rng, 0, 3, 4, 5, -1, 9, rng.randrange(0, 7))] * 4,
        "series_locked_mask": [pick(rng, 0, 4, 0x10, rng.getrandbits(32))] * 4,
        "series_names": [pick(rng, 0, rng.choice(TEXT_SECTIONS), string_ptr(rng))] * 4,
        "menu_go": [rint(rng)],
        "joystick_count": [pick(rng, 0, 1, 2, 3)],
        "binding_get": [pick(rng, BINDINGS + rng.randrange(16) * 0x10, BINDINGS) for _ in range(8)],
        "net_send": [pick(rng, 0, 1)],
        "callback": [pick(rng, 0, 1, rng.getrandbits(32)) for _ in range(8)],
    }
    return dict(config=config, race=race, texrec=texrec, info=info, font=font, strings=strings,
                bindings=bindings, buffer=buffer, water=water, menu=menu, regions=regions, script=script)


def c_string(uc, address, limit=0x400):
    data = b""
    while len(data) < limit:
        chunk = bytes(uc.mem_read(address + len(data), 0x10 - (address + len(data)) % 0x10))
        if b"\0" in chunk:
            return data + chunk.split(b"\0", 1)[0]
        data += chunk
    return data[:limit]


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(CALLBACK, 0x1000)
        m.uc.mem_write(CALLBACK, b"\xc3" * 0x1000)
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _write(self, uc, address, data):
        if address:
            uc.mem_write(address, data)

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _stub(self, uc, address):
        name, kinds, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<12I", bytes(uc.mem_read(esp, 48)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            on_stack = E.STACK <= a < E.STACK + E.STACK_SIZE
            if k == "o":
                logged.append("stack" if on_stack else a)
            elif k == "s":
                logged.append(c_string(uc, a) if a else 0)
            elif k == "p":
                logged.append(("stack", bytes(uc.mem_read(a, 12))) if on_stack else a)
            else:
                logged.append(a)
        eax = 0
        if name == "text_entry":
            dst, size, sec, index = args[0], struct.unpack("<i", struct.pack("<I", args[1]))[0], args[2], args[3]
            text = b"T%x.%d" % (sec, index if index < 0x80000000 else index - (1 << 32))
            if sec == 0x4B0E34:
                text += b" %d"
            if index & 1:
                text += b" x"
            ok = self._next(name, 1)
            if dst and (size == -1 or size > 0):
                if ok == -1:
                    self._write(uc, dst, b"\0")
                elif size == -1 or len(text) < size:
                    self._write(uc, dst, text + b"\0")
                else:
                    self._write(uc, dst, text[:size - 1] + b"\0")
            eax = -1 if ok == -1 else len(text)
        elif name == "sprintf":
            dst, fmt = args[0], c_string(uc, args[1])
            logged = ["stack" if E.STACK <= dst < E.STACK + E.STACK_SIZE else dst, fmt]
            outp, k, i = b"", 2, 0
            while i < len(fmt):
                c = fmt[i:i + 1]
                if c == b"%" and i + 1 < len(fmt):
                    f = fmt[i + 1:i + 2]
                    v = args[k] if k < len(args) else 0
                    k += 1
                    sv = struct.unpack("<i", struct.pack("<I", v))[0]
                    if f == b"s":
                        text = c_string(uc, v) if v else b"(null)"
                        logged.append(text)
                    else:
                        text = {b"d": b"%d" % sv, b"x": b"%x" % v, b"c": bytes([v & 0xFF])}.get(f, b"?")
                        logged.append(v)
                    outp += text
                    i += 2
                else:
                    outp += c
                    i += 1
            self._write(uc, dst, outp + b"\0")
            eax = len(outp)
        elif name == "strncpy":
            dst, src, n = args[:3]
            s = c_string(uc, src, 0x400)[:n]
            self._write(uc, dst, s + b"\0" * (n - len(s)))
            eax = dst
        elif name == "text_fit":
            self._write(uc, args[0], c_string(uc, args[1])[:60] + b"\0")
        elif name in ("driver_name", "lap_time_text"):
            self._write(uc, args[0], b"%s%d\0" % (name[:3].encode(), args[1]))
        elif name == "lobby_lines":
            n = struct.unpack("<i", bytes(uc.mem_read(args[0], 4)))[0]
            n = min(n, 4)
            uc.mem_write(args[0], struct.pack("<i", n))
            for i in range(max(n, 0)):
                uc.mem_write(args[1] + 4 * i, struct.pack("<I", STRINGS + i * 0x100))
        elif name == "profile_name":
            eax = CONFIG + 0x984 + args[0] * 10 if args[0] < 6 else 0
        elif name.startswith("cb_") or name == "callback":
            if args[1]:
                self._write(uc, args[1], b"cb%d\0" % args[0])
            eax = self._next("callback", 1)
        elif name == "net_send":
            packet = bytearray(uc.mem_read(args[2], 0x1C))
            packet[1:8] = b"\0" * 7  # the original's are stack leftovers
            logged[2] = bytes(packet)
            eax = self._next(name, 1)
        elif returns:
            eax = self._next(name, 0)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)


# 63-byte label fields (item template, menu items, title): copied whole from
# a stack buffer, so what follows the label's NUL is whatever that buffer
# held before; only the text up to the NUL is compared.
LABELS = [(0x4C2970, 0x40)] + [(base + off, size) for base in [0x4C9DF8] + [MENU + k * 0x730 for k in range(-1, 0x41)]
                               for off, size in ((0x5C, 0x40), (0x1FC, 0x40), (0x9C, 0x3F), (0x23C, 0x3F))]


def offset_of(address):
    if SCRATCH <= address < SCRATCH + SCRATCH_SIZE:
        return address - SCRATCH
    off = SCRATCH_SIZE
    for a, n in REGIONS:
        if a <= address < a + n:
            return off + address - a
        off += n
    raise KeyError(hex(address))


LABEL_OFFSETS = [(offset_of(a), n) for a, n in LABELS]


def normalise(mem):
    mem = bytearray(mem)
    for off, n in LABEL_OFFSETS:
        end = mem.find(b"\0", off, off + n)
        if end != -1:
            mem[end:off + n] = bytes(off + n - end)
    return bytes(mem)


def run_case(m, stubs, name, address, maker, returns, rng):
    st = make_state(rng)
    args = maker(rng)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        m.write(CONFIG, bytes(st["config"]))
        m.write(RACE, st["race"])
        m.write(TEXREC, st["texrec"])
        m.write(INFO, bytes(st["info"]))
        m.write(FONT, st["font"])
        m.write(STRINGS, st["strings"])
        m.write(BINDINGS, st["bindings"])
        m.write(BUFFER, bytes(st["buffer"]))
        m.write(WATER, st["water"])
        m.write(MENU, st["menu"])
        for a, v in st["regions"].items():
            m.write(a, bytes(v))
        for a, v in POINTERS.items():
            m.write(a, struct.pack("<I", v))
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"])
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = normalise(m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS))
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            where = []
            for i in diff[:8]:
                if i < SCRATCH_SIZE:
                    addr = SCRATCH + i
                else:
                    off = i - SCRATCH_SIZE
                    for ra, rn in REGIONS:
                        if off < rn:
                            addr = ra + off
                            break
                        off -= rn
                where.append((hex(addr), hex(struct.unpack_from("<I", a, i)[0]),
                              hex(struct.unpack_from("<I", b, i)[0])))
            print("  args", [hex(x & 0xFFFFFFFF) for x in args], "differ:", where)
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else " (memory differs)") + logs, results[0][0]
    return None, results[0][0]


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(26)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, maker, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        for _ in range(iterations):
            msg, r = run_case(m, stubs, name, address, maker, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs failed the same way in both)" if errors else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
