#!/usr/bin/env python3
"""Differential test of fe_o.c (front end group O: pad and menu input, the
menu action dispatcher and screen history, menu resets, key repeat, the
attract timer, a few screen builders and the network player drop) against
the original, on random config, menu, pad and network state.

Functions outside the group (bindings, the clock, sound, the menu text and
layout helpers, the formatter and sprintf, network messages, replay and
race-end helpers) are stubbed in both runs: each stub logs its arguments and
returns a scripted value; formatters write a deterministic string. Menu
handlers are stub callbacks (some move the cursor). The whole game data
section is restored before and compared after each run, with the scratch
memory and the call logs.

    tests/emu_fe_o.py EXE build/emu_o.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

DATA = (0x4A7000, 0x6D3000 - 0x4A7000)
SCRATCH, SCRATCH_SIZE = 0x01000000, 0x30000
CONFIG = SCRATCH  # 0x1000
RACE = SCRATCH + 0x1000  # 0xc00
ITEMS = SCRATCH + 0x2000  # 0x40 items of 0x730 = 0x1cc00
BINDINGS = SCRATCH + 0x1F000  # 0x10 each
CARS = SCRATCH + 0x21000  # 6 of 0x800
TEXT = SCRATCH + 0x24000  # destinations / sources
PACKET = SCRATCH + 0x26000
TIMER = SCRATCH + 0x27000
CB = 0x02000000
ITEM = 0x730
STACK_FILL = bytes((i * 0x6B + 0x11) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, returns)
STUBS = {
    0x43692A: ("game_time", 0, True),
    0x449BF0: ("binding_get", 2, True),
    0x48A010: ("joy_count", 0, True),
    0x472A90: ("menu_sound", 3, True),
    0x42E6D0: ("text_format", 4, True),
    0x4481A0: ("text_format2", 4, True),
    0x4484A0: ("text_number", 2, True),
    0x4979F0: ("sprintf", 3, True),
    0x448A00: ("menu_title", 2, True),
    0x448140: ("menu_text", 5, True),
    0x448A50: ("menu_row", 9, True),
    0x448AF0: ("menu_buttons", 4, True),
    0x448B10: ("menu_buttons_n", 5, True),
    0x448B50: ("menu_default", 1, True),
    0x449060: ("menu_text_wrap", 7, True),
    0x448EC0: ("menu_text_colour", 4, True),
    0x44C6D0: ("menu_option_row", 6, True),
    0x44C9D0: ("menu_option_row2", 6, True),
    0x44EBF0: ("menu_message", 4, True),
    0x4455C0: ("item_reset", 1, True),
    0x4456E0: ("items_reset_more", 0, True),
    0x445650: ("panel_reset", 3, True),
    0x447D80: ("item_cycle", 1, True),
    0x45DDB0: ("net_send_flag", 3, True),
    0x48BED0: ("net_quit_ack", 1, True),
    0x45EF80: ("net_is_game", 0, True),
    0x45EED0: ("net_is_host", 0, True),
    0x45EF30: ("net_results_due", 0, True),
    0x439A70: ("race_results", 1, True),
    0x45FC40: ("net_send_results", 0, True),
    0x41C160: ("replay_mark", 1, True),
    0x41BA20: ("replay_step", 1, True),
    0x40AFB0: ("race_fade", 1, True),
    0x40A9A0: ("race_fade_voice", 0, True),
}
# menu handler callbacks: address: (name, args, moves the cursor)
CALLBACKS = {
    CB + 0x00: ("cb1a", 1, False), CB + 0x10: ("cb1b", 1, False), CB + 0x20: ("cb1m", 1, True),
    CB + 0x40: ("cb2a", 2, False), CB + 0x50: ("cb2b", 2, False), CB + 0x60: ("cb2m", 2, True),
}
CB1 = [CB + 0x00, CB + 0x10, CB + 0x20]
CB2 = [CB + 0x40, CB + 0x50, CB + 0x60]
FORMATTERS = {"text_format", "text_format2", "text_number", "sprintf"}

# name: (address, argument kinds, returns: True, False or a mask)
TESTS = {
    "fe_time": (0x436935, "", True),
    "fe_joy_axis": (0x489890, "pa", True),
    "fe_joy_valid": (0x48A020, "p", True),
    "fe_joy_button_count": (0x48A040, "p", True),
    "fe_joy_button_valid": (0x48A070, "pb", True),
    "fe_joy_button_down": (0x48A0A0, "pb", True),
    "fe_joy_axis_edge": (0x48A170, "pa", True),
    "fe_joy_x_edge": (0x48A250, "p", True),
    "fe_joy_y_edge": (0x48A260, "p", True),
    "fe_joy_button_edge": (0x48A270, "pb", True),
    "fe_menu_pad": (0x449E10, "P", True),
    "fe_menu_axis_x": (0x449DB0, "Pxxx", True),
    "fe_menu_axis_y": (0x449E80, "Pxxx", True),
    "fe_menu_action_pressed": (0x449F10, "PA", True),
    "fe_menu_select_pressed": (0x449EE0, "P", True),
    "fe_menu_back_pressed": (0x449F80, "P", True),
    "fe_menu_input_poll": (0x42DE70, "", False),
    "fe_history_clear": (0x4447D0, "", False),
    "fe_menu_item_clear": (0x445580, "i", False),
    "fe_menu_items_clear": (0x445560, "", False),
    "fe_menu_panels_reset": (0x445730, "", False),
    "fe_menu_title_reset": (0x445760, "", False),
    "fe_menu_reset": (0x445480, "", False),
    "fe_repeat": (0x447540, "Teww", True),
    "fe_repeat_updown": (0x4475D0, "e", True),
    "fe_repeat_select": (0x4475F0, "e", True),
    "fe_repeat_leftright": (0x447610, "e", True),
    "fe_repeat_page": (0x447630, "e", True),
    "fe_history_push": (0x447A60, "ss", False),
    "fe_history_pop": (0x447B40, "", True),
    "fe_menu_goto_ex": (0x4479F0, "sme", False),
    "fe_menu_goto": (0x4479D0, "sm", False),
    "fe_menu_back": (0x447AB0, "", False),
    "fe_menu_cursor_move": (0x447B60, "e", False),
    "fe_menu_item_toggle": (0x447CB0, "v", False),
    "fe_menu_item_spin": (0x447C40, "v", False),
    "fe_menu_item_adjust": (0x447BD0, "ek", False),
    "fe_menu_action": (0x447650, "c", True),
    "fe_menu_action_b": (0x447EF0, "", True),
    "fe_attract_left": (0x448030, "v", True),
    "fe_attract_due": (0x448070, "", True),
    "fe_menu_text_box": (0x4480D0, "SDnvv", False),
    "fe_split_screen_label": (0x44E320, "D", False),
    "fe_world_label": (0x44E840, "Dvw", False),
    "fe_world_row": (0x44E800, "v", False),
    "fe_quit_confirm_menu": (0x44E3C0, "", False),
    "fe_message": (0x44EBD0, "vv", False),
    "fe_menu_buttons3": (0x44FAA0, "vvvv", False),
    "fe_league_points": (0x44F860, "evvh", False),
    "fe_option_reflections": (0x450560, "", False),
    "fe_option_wormhole_trails": (0x4505C0, "", False),
    "fe_option_bilinear": (0x450930, "", False),
    "fe_option_dithering": (0x4509A0, "", False),
    "fe_option_clouds": (0x450A10, "", False),
    "fe_option_daytime": (0x451040, "", False),
    "fe_net_player_active": (0x48CFD0, "N", True),
    "fe_net_checksum": (0x48CAB0, "Kz", True),
    "fe_net_sequence_next": (0x48C920, "", 0xFFFF),
    "fe_net_leave": (0x45E550, "", True),
    "fe_net_race_abort": (0x45D170, "", False),
    "fe_net_player_left": (0x45CB90, "N", False),
    "fe_net_drop_player": (0x48BF70, "N", True),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rand_int(rng):
    return pick(rng, rng.randrange(-0x40, 0x40), rng.randrange(0, 0x10000), rng.getrandbits(32) - 0x80000000,
                0, 1, -1, 0x7FFFFFFF, -0x80000000)


def put(regions, address, data):
    regions.append((address, bytes(data)))


def make_state(rng):
    regions = []
    now = rng.getrandbits(32) if rng.randrange(8) else rng.randrange(0, 3000)

    def near(span=3000):
        return pick(rng, (now - rng.randrange(0, span)) & 0xFFFFFFFF, (now + rng.randrange(0, span)) & 0xFFFFFFFF,
                    0, now, rng.getrandbits(32))

    config = bytearray(rng.getrandbits(8) for _ in range(0x1000))
    config[8] = pick(rng, 0, 1, 1, 1)
    struct.pack_into("<H", config, 0x12, pick(rng, 0, 30000, rng.randrange(1, 0x10000), 1))
    config[0x10] = pick(rng, 0, 1, 1)
    config[0x74] = pick(rng, 0, 1, 2, 3, 4, 5, rng.getrandbits(8))
    config[0x76] = pick(rng, 0, 1)
    config[0x1C] = rng.randrange(0, 0x20)
    config[0x1D] = pick(rng, 0xF, 0x10, 0x11, 0x12, rng.randrange(0, 0x20))
    for p in range(6):
        config[p * 0x90 + 0x1A1] = pick(rng, 0, 1, 2, 3, 7, 8, 9, 0xFF, rng.getrandbits(8))
    race = bytearray(rng.getrandbits(8) for _ in range(0xC00))
    race[0x54D] = pick(rng, 0, 1, 2, 3, 4, 5, 6, 6)
    for i in range(6):
        race[i] = pick(rng, 8, 8, 10, 0, rng.getrandbits(8))
        race[0x1C + i] = pick(rng, 0, 1, 3, 6, 7, rng.getrandbits(8))
    struct.pack_into("<i", race, 0x57C, pick(rng, 4, 5, 5, 6, 7, 8, 9, rng.randrange(-2, 12)))
    count = pick(rng, 0, 1, 2, 3, 5, 8, 12, rng.randrange(0, 0x40), -1)
    items = bytearray(rng.getrandbits(8) for _ in range(0x40 * ITEM))
    for i in range(0x40):
        b = i * ITEM
        items[b] = pick(rng, 0, 1, 2, 3, 4, 5, rng.getrandbits(8))
        items[b + 2] = pick(rng, 0, 1, 1, rng.getrandbits(8))
        struct.pack_into("<ii", items, b + 0x4EC, pick(rng, -1, rng.randrange(0, 0x82), 0x81, 0x82, rand_int(rng)),
                         pick(rng, 0, 1, 2, 3, -1, rand_int(rng)))
        struct.pack_into("<i", items, b + 0x4F8, rand_int(rng))
        for off, cbs in ((0x500, CB1), (0x504, CB2), (0x508, CB1), (0x50C, CB2), (0x510, CB2)):
            struct.pack_into("<I", items, b + off, pick(rng, 0, 0, *cbs))
    # menu globals 0x4cb490..0x4cb600
    menu = bytearray(rng.getrandbits(8) for _ in range(0x170))

    def m32(address, value):
        struct.pack_into("<I", menu, address - 0x4CB490, value & 0xFFFFFFFF)

    m32(0x4CB4B8, ITEMS)
    m32(0x4CB49C, count)
    m32(0x4CB498, pick(rng, 0, 0, 1))
    m32(0x4CB4A4, pick(rng, 0, 1) if count > 0 else 0)
    m32(0x4CB4FC, pick(rng, 0, 0, 1))
    m32(0x4CB4B0, pick(rng, 0, 0, 0, 1))
    m32(0x4CB594, pick(rng, 0, 1))
    m32(0x4CB4A0, pick(rng, 0, 0, 1, 2, 5, 15, 16, 17, -1, -3))
    for a, cbs in ((0x4CB524, CB1), (0x4CB52C, CB1), (0x4CB530, CB2)):
        m32(a, pick(rng, 0, 0, *cbs))
    m32(0x4CB534, near(40000))
    menu[0x4CB588 - 0x4CB490] = pick(rng, 0xFF, 0, 1, 2, count & 0xFF, rng.randrange(0, max(1, count + 2)) & 0xFF,
                                     rng.getrandbits(8))
    menu[0x4CB589 - 0x4CB490] = pick(rng, 0, 1, rng.getrandbits(8))
    clock = sorted(((now + rng.randrange(0, 2500)) & 0xFFFFFFFF for _ in range(8)),
                   key=lambda v: v if rng.randrange(6) else -v)
    for t in (0x4CB5A8, 0x4CB5B8, 0x4CB5C8, 0x4CB5D8):
        m32(t, pick(rng, 0, 0, 1))
        m32(t + 4, pick(rng, 0, 1, 1))
        wait = pick(rng, 0, 100, 300, 500, 1500, rng.randrange(0, 3000), rng.getrandbits(32))
        last = near()
        if rng.randrange(3) == 0:  # due exactly now, or one ms either side
            last = (rng.choice(clock[:2]) - wait + pick(rng, 0, 0, 1, -1)) & 0xFFFFFFFF
        m32(t + 8, last)
        m32(t + 12, wait)
    put(regions, 0x4CB490, menu)
    history = bytearray(rng.getrandbits(8) for _ in range(0x20))
    for i in range(16):
        history[4 + i] = pick(rng, rng.randrange(0, 0x90), rng.getrandbits(8))
    put(regions, 0x4C98E0, history)
    put(regions, 0x4C99BC, rng.getrandbits(32).to_bytes(4, "little"))
    put(regions, 0x4AF5F0, struct.pack("<i", pick(rng, -1, -1, 0, 1, 3, rng.randrange(-2, 0x40))) +
        bytes(rng.getrandbits(8) for _ in range(0x1C)))
    put(regions, 0x4AF64C, struct.pack("<ii", pick(rng, -1, 4, 4, rng.randrange(0, 0x82), 0xFF, 0x100, 0x200),
                                       rng.getrandbits(31)))
    put(regions, 0x4BE32C, bytes(rng.getrandbits(8) for _ in range(8)))
    put(regions, 0x4AC73C, struct.pack("<i", pick(rng, -1, 0, 0, 1, 5)))
    put(regions, 0x4BE2C0, struct.pack("<I", near()))
    put(regions, 0x4BE450, struct.pack("<I", pick(rng, 0, 0, 1)))
    put(regions, 0x4AC984, struct.pack("<I", pick(rng, 0, 0, 1)))
    put(regions, 0x4AC844, rng.getrandbits(32).to_bytes(4, "little"))
    put(regions, 0x4AC988, bytes(rng.getrandbits(8) for _ in range(8)))
    put(regions, 0x4AC848, rng.getrandbits(32).to_bytes(4, "little"))
    put(regions, 0x4AF70C, bytes(rng.getrandbits(8) for _ in range(8)))
    put(regions, 0x4AF784, bytes(rng.getrandbits(8) for _ in range(12)))
    put(regions, 0x4C24C8, bytes(pick(rng, 0, 1, 2, 3, 4, 5, 6, 0xFF, rng.getrandbits(8)) for _ in range(6)))
    # bindings
    nbind = pick(rng, 0, 1, 3, 8, 12, -1)
    actions = bytearray(rng.getrandbits(8) for _ in range(0x80))
    for i in range(16):
        struct.pack_into("<I", actions, i * 8, pick(rng, rng.randrange(0, 12), rng.randrange(0, 32)))
    put(regions, 0x4AF7A0, actions)
    put(regions, 0x4AF878, struct.pack("<i", nbind))
    bind = {}
    records = bytearray(rng.getrandbits(8) for _ in range(0x2000))
    k = 0
    for p in range(6):
        for a in range(32):
            if rng.randrange(3):
                struct.pack_into("<ii", records, k * 0x10 + 4, pick(rng, 0, 0, 1, 2, 3, -1),
                                 0)
                struct.pack_into("<i", records, k * 0x10 + 0xC, pick(rng, rng.randrange(0, 12),
                                                                      rng.randrange(-2, 0x90)))
                bind[(p, a)] = BINDINGS + k * 0x10
                k += 1
    # pads
    joyn = pick(rng, 0, 1, 2, 4, 8, 9)
    put(regions, 0x4D9554, struct.pack("<i", pick(rng, joyn, joyn, rng.randrange(0, 10))))
    pads = bytearray(rng.getrandbits(8) for _ in range(9 * 0x43C))
    for j in range(9):
        b = j * 0x43C
        struct.pack_into("<i", pads, b, pick(rng, 0, 4, 12, 0x81, 0x90, rng.randrange(-2, 0x100)))
        for ax in range(24):
            struct.pack_into("<I", pads, b + 0x10 + ax * 4, pick(rng, 0, 1, 1))
            struct.pack_into("<I", pads, b + 0x40 + ax * 4, rng.randrange(0, 0x18) * 4)
        for ax in range(24):
            struct.pack_into("<i", pads, b + 0xB8 + ax * 4, pick(rng, 0, 0x3F, 0x40, -0x3F, -0x40, 0x7F, -0x80,
                                                                 rng.randrange(-0x100, 0x100), rand_int(rng)))
        for bt in range(0x91):
            pads[b + 0xE8 + bt] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    put(regions, 0x4D75E8, pads)
    edges = bytes(pick(rng, 0, 1, 2, 3, 4, 5, 0xFF, 0xFF, 6, rng.getrandbits(8)) for _ in range(0x500))
    put(regions, 0x4D9658, edges)
    # network
    put(regions, 0x5D622C, struct.pack("<I", rng.getrandbits(8) | rng.getrandbits(32) & ~0xFF))
    put(regions, 0x5D535C, struct.pack("<I", rng.randrange(0, 6)))
    net = bytearray(rng.getrandbits(8) for _ in range(6 * 0x66C))
    for i in range(6):
        struct.pack_into("<I", net, i * 0x66C, pick(rng, 0, 1, rng.getrandbits(32)))
    put(regions, 0x5D2CD0, net)
    put(regions, 0x6B8D60, rng.getrandbits(32).to_bytes(4, "little"))
    slots = bytearray(rng.getrandbits(8) for _ in range(6 * 0x274))
    for i in range(6):
        struct.pack_into("<I", slots, i * 0x274, pick(rng, CARS + i * 0x800, CARS + i * 0x800, 0))
    put(regions, 0x6BFB80, slots)
    put(regions, 0x4C9E94, bytes(rng.getrandbits(8) for _ in range(0x40)))
    for a in (0x4B2198, 0x4B1960):
        pass
    put(regions, 0x4B2198, struct.pack("<I", CONFIG))
    put(regions, 0x4B1960, struct.pack("<I", RACE))
    script = {
        "game_time": clock,
        "cb": [pick(rng, 0, 0, 0, 1, rng.getrandbits(32)) for _ in range(12)],
        "move": [pick(rng, 0, 1, 2, 0xFF, count & 0xFF, (count - 1) & 0xFF, (count + 1) & 0xFF,
                      rng.getrandbits(8)) for _ in range(12)],
        "net_is_game": [pick(rng, 0, 1, 1)], "net_is_host": [pick(rng, 0, 1, 1)],
        "net_results_due": [pick(rng, 0, 1)], "net_send_flag": [rand_int(rng)],
        "menu_text": [rand_int(rng) for _ in range(3)],
        "menu_sound": [rand_int(rng) for _ in range(4)],
    }
    return dict(regions=regions, config=config, race=race, items=items, records=records, bind=bind, joyn=joyn,
                script=script)


def c_string(uc, address, limit=0x40):
    data = bytes(uc.mem_read(address, limit))
    return data.split(b"\0")[0]


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.state = None
        for address in list(STUBS) + list(CALLBACKS):
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, state):
        self.log = []
        self.script = {k: list(v) for k, v in state["script"].items()}
        self.state = state

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address in CALLBACKS:
            name, nargs, moves = CALLBACKS[address]
        else:
            name, nargs, _r = STUBS[address]
            moves = False
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        logged = list(args)
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                logged[i] = "out" if (name in FORMATTERS and i == 0) else ("stack", c_string(uc, a))
        eax = 0
        if name == "game_time":
            eax = self._next(name, 0x12345678)
        elif name == "binding_get":
            eax = self.state["bind"].get((args[0], args[1]), 0)
        elif name == "joy_count":
            eax = self.state["joyn"]
        elif name in ("text_format", "text_format2"):
            key = c_string(uc, args[2]) if args[2] > 0x400000 else b"?"
            text = b"<" + key + b":" + str(args[3] - (1 << 32) if args[3] >= 1 << 31 else args[3]).encode() + b">"
            uc.mem_write(args[0], text + b"\0")
            eax = len(text)
        elif name == "text_number":
            uc.mem_write(args[0], b"#%d\0" % args[1])
        elif name == "sprintf":
            uc.mem_write(args[0], b"%02d\0" % args[2])
        elif name.startswith("cb"):
            if moves:
                uc.mem_write(0x4CB588, bytes([self._next("move", 0)]))
            eax = self._next("cb")
        else:
            eax = self._next(name, 0)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, st):
    args = []
    writes = []
    for k in kinds:
        if k == "p":
            args.append(pick(rng, 0, 1, 2, 3, 7, 8, 9, -1, rng.randrange(-3, 12)))
        elif k == "a":
            args.append(pick(rng, 0, 1, 2, 5, rng.randrange(0, 24)))
        elif k == "b":
            args.append(pick(rng, 0, 1, 3, 0x7F, 0x80, 0x81, 0x90, -1, rng.randrange(0, 20)))
        elif k == "P":
            args.append(pick(rng, 0, 0, 1, 2, 3, 5))
        elif k == "A":
            args.append(pick(rng, 2, 3, 4, 5, 6, rng.randrange(0, 32)))
        elif k == "x":
            args.append(rand_int(rng))
        elif k == "i":
            args.append(pick(rng, 0, 1, 5, 0x3F, 0x40, -1, rng.randrange(0, 0x40)))
        elif k == "T":
            args.append(pick(rng, TIMER, 0x4CB5A8, 0x4CB5B8))
            if args[-1] == TIMER:
                wait = pick(rng, 0, 100, 500, rng.randrange(0, 3000), rng.getrandbits(32))
                first = st["script"]["game_time"][0]
                last = pick(rng, 0, first - rng.randrange(0, 3000), first - wait, first - wait - 1, first - wait + 1,
                            first + 1, rng.getrandbits(32))
                writes.append((TIMER, struct.pack("<IIII", pick(rng, 0, 0, 1), pick(rng, 0, 1, 1),
                                                  last & 0xFFFFFFFF, wait & 0xFFFFFFFF)))
        elif k == "e":
            args.append(pick(rng, 0, 1, 1, 2))
        elif k == "w":
            args.append(pick(rng, 0, 100, 500, 1500, rng.getrandbits(32)))
        elif k == "s":
            args.append(pick(rng, -1, 0, 3, 4, 0x81, 0x82, rng.randrange(0, 0x90), rng.getrandbits(8),
                             rng.getrandbits(32)))
        elif k == "m":
            args.append(pick(rng, 0, 1, 1, 2, 3, -1))
        elif k == "v":
            args.append(rand_int(rng))
        elif k == "k":
            args.append(pick(rng, 0, 1, 2, 3, 4, 5))
        elif k == "c":
            args.append(pick(rng, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -2, rand_int(rng)))
        elif k == "S":
            args.append(TEXT)
            writes.append((TEXT, bytes(rng.getrandbits(8) for _ in range(0x100))))
        elif k == "D":
            args.append(TEXT + 0x800 + rng.randrange(0, 8))
        elif k == "n":
            args.append(pick(rng, 0, 1, 3, 4, 5, 8, 0x41, rng.randrange(0, 0x100)))
        elif k == "h":
            args.append(pick(rng, 0, 1, 2, 5, -1, 6))
        elif k == "N":
            args.append(pick(rng, 0, 1, 2, 5, 6, 7, -1, rand_int(rng)))
        elif k == "K":
            args.append(PACKET + rng.randrange(0, 0x10))
            writes.append((PACKET, bytes(rng.getrandbits(8) for _ in range(0x100))))
        elif k == "z":
            args.append(rng.randrange(1, 0x60))
    if kinds == "Dvw":  # the world: mostly 0xf..0x12
        args[2] = pick(rng, 0xF, 0x10, 0x11, 0x12, rng.randrange(0, 0x20), -1)
    return args, writes


class Runner:
    def __init__(self, m):
        self.m = m
        self.base = m.read(*DATA)

    def run(self, target, st, args, writes, stubs, mask):
        m = self.m
        m.write(DATA[0], self.base)
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        m.write(CONFIG, bytes(st["config"]))
        m.write(RACE, bytes(st["race"]))
        m.write(ITEMS, bytes(st["items"]))
        m.write(BINDINGS, bytes(st["records"]))
        for a, v in st["regions"]:
            m.write(a, v)
        for a, v in writes:
            m.write(a, v)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st)
        r = m.call(target, args)
        if r[0] == "ok":
            if mask is False:
                r = ("ok",)
            elif mask is not True:
                r = ("ok", r[1] & mask)
        return r, m.read(*DATA), m.read(SCRATCH, SCRATCH_SIZE), stubs.log


def run_case(runner, stubs, name, address, kinds, mask, rng, symbols):
    st = make_state(rng)
    args, writes = make_args(rng, kinds, st)
    a = runner.run(address, st, args, writes, stubs, mask)
    b = runner.run(symbols[name], st, args, writes, stubs, mask)
    if a == b:
        return None, a[0]
    msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {a[0]} ours {b[0]}"
    for label, i in (("data", 1), ("scratch", 2)):
        if a[i] != b[i]:
            base = DATA[0] if i == 1 else SCRATCH
            diff = [j for j in range(len(a[i])) if a[i][j] != b[i][j]]
            msg += f"\n  {label} differs at " + ", ".join(f"{base + j:#x}: {a[i][j]:#x}/{b[i][j]:#x}"
                                                          for j in diff[:6])
    if a[3] != b[3]:
        la, lb = a[3], b[3]
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        msg += f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
    return msg, a[0]


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    m.uc.mem_map(CB, 0x1000)
    m.write(CB, b"\xc3" * 0x1000)
    stubs = Stubs(m)
    runner = Runner(m)
    rng = random.Random(41)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, mask) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        for _ in range(iterations):
            msg, r = run_case(runner, stubs, name, address, kinds, mask, rng, m.symbols)
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
