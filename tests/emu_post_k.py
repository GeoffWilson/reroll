#!/usr/bin/env python3
"""Differential test of the post-physics updates of src/game/post_k.c
(ripples, collapsing buildings, bomblets, glints, the race's state machine
and its messages) against the original, on random synthetic worlds.

The effect, sound, grid, network and menu routines they call stay original
in the game; here they are stubbed: each call is logged with its arguments
(stack buffers by content) and answers from a per-case script, and the logs
must match as well as memory and the result.

    tests/emu_post_k.py EXE build/emu_k.elf [iterations]
"""
import random
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.path.insert(0, __file__.rsplit('/', 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

S, SIZE = 0x01000000, 0x40000
RACE, CARS = S, S + 0x1000
CAR_SIZE, SLOTS, SLOT_SIZE = 0x800, 0x6BFB80, 0x274
RULES, RIPPLES, RVERTS, MARKS = S + 0x4000, S + 0x12000, S + 0x15000, S + 0x4500
NRIPPLES = 0x200
PARTICLES, EFFECTS = S + 0x5000, S + 0x6000
LHOLD, LINES = S + 0x7800, S + 0x7900
CRUMBLES, COBJ, CMODEL, CVERT, CFACE, CGROUP, CDONE, CHEAP = (
    S + 0x8000, S + 0x8400, S + 0x9000, S + 0x9400, S + 0xA400, S + 0xB400, S + 0xB800, S + 0xC000)
BOMBS, BOBJ, BMODEL = S + 0xD000, S + 0xD200, S + 0xFC00
SETTINGS, FONT, STRINGS, PACKETS = S + 0x10000, S + 0x10100, S + 0x11400, S + 0x11800
SHAPES, KINDS, OBJKINDS, ARGS = S + 0x11A00, S + 0x11B00, S + 0x11D00, S + 0x3FF00
NV, NF, NG = 24, 24, 6  # per collapsing object: vertices, faces, face groups

GLOBALS = [(0x4B1960, 4), (0x4B11D8, 4), (0x6C0A50, 4), (SLOTS, 6 * SLOT_SIZE), (0x6CD410, 4),
           (0x6C1EB0, 12), (0x4B2C64, 4), (0x6D04A0, 0x228), (0x4A7CE4, 4), (0x6CF160, 16),
           (0x4B2198, 4), (0x6C1E30, 4), (0x6BFB10, 8), (0x4B7E54, 4), (0x600600, 0x80),
           (0x600680, 0x70), (0x4D3DD8, 4), (0x4D3DE0, 4), (0x4C1B18, 0xE0), (0x6CCD84, 1),
           (0x6CC940, 4), (0x6CCB9C, 4), (0x5F6940, 4 + 0x1C * 0x50), (0x6B95A4, 8 * 0x50),
           (0x6B9824, 0x14), (0x6B8D6C, 0x20), (0x4D1098, 0xB0), (0x6CE700, 8), (0x4D3DA8, 8),
           (0x5D622C, 4), (0x4D1198, 4), (0x6B983A, 6)]

# stubbed originals: address -> (name, argument count, answer)
STUBS = {
    0x4411C0: ("ripple_marker", 2, None), 0x462450: ("particle_slot", 0, "slot"),
    0x460FF0: ("grid_place", 4, None), 0x42AC30: ("effect_new", 0, "effect"),
    0x42CD40: ("effect_fade", 3, None), 0x42AA30: ("effect_burst", 5, None),
    0x471C90: ("sound_play", 5, None), 0x419300: ("body_remove", 1, None),
    0x419150: ("body_shape", 4, None), 0x4611D0: ("grid_unlink", 1, None),
    0x460FB0: ("grid_link", 1, None), 0x4427A0: ("crumble_start", 1, None),
    0x43D750: ("lap_split", 1, None), 0x47AA60: ("slot_boost", 3, None),
    0x45E8C0: ("read_controls", 3, None), 0x45EF80: ("network_game", 0, "net"),
    0x45EED0: ("network_host", 0, "host"), 0x45FC40: ("network_finish", 0, None),
    0x439A70: ("race_end", 1, None), 0x471C30: ("sound_pause", 1, None),
    0x471A60: ("sound_master", 1, None), 0x471AF0: ("sound_balance", 2, None),
    0x45DDB0: ("menu_open", 3, "menu"), 0x48CF20: ("local_player", 0, "player"),
    0x45FA60: ("pause_send", 1, None), 0x45FE70: ("pause_receive", 1, "receive"),
    0x4600A0: ("pause_menu", 3, None), 0x423F20: ("pause_layout", 0, "layout"),
    0x463CA0: ("pause_panel", 1, None), 0x464C10: ("pause_bar", 3, None),
    0x463F90: ("pause_items", 0, None), 0x46FE60: ("pause_sounds", 0, None),
    0x489F70: ("pause_player", 1, None),
}

# bytes logged for a stack-buffer argument (default 12): out-buffers only by size
STACK_BYTES = {("read_controls", 1): 0, ("pause_receive", 0): 4}
VALUE_ONLY = {"ripple_marker", "sound_master", "sound_balance", "slot_boost"}  # never pointers

# name: (original, argument kinds)
TESTS = {
    "particle_spawn": (0x462690, "PVk"),
    "effect_puff": (0x42B8F0, "P"),
    "ripple_update": (0x43E270, ""),
    "glint_spawn": (0x442F70, "gTwws"),
    "glint_update": (0x443180, ""),
    "dust_add": (0x443100, "P"),
    "bomblet_spawn": (0x46BD60, "PVbo"),
    "bomblet_update": (0x46BFA0, ""),
    "crumble_alloc": (0x412BD0, "ew"),
    "crumble_release_lines": (0x4038E0, "O"),
    "cos_table": (0x480BC0, "a"),
    "crumble_setup": (0x412DD0, ""),
    "crumble_update": (0x405F70, ""),
    "hud_set_mode": (0x41B2D0, "w"),
    "countdown_tick": (0x46A9A0, ""),
    "text_slide": (0x479690, "t"),
    "text_free": (0x479280, "t"),
    "text_create": (0x479060, "SwwwFwm"),
    "finish_messages": (0x464E10, ""),
    "race_go_banner": (0x4651F0, ""),
    "players_buttons": (0x41C230, ""),
    "network_quit_menu": (0x45F920, ""),
    "pause_check": (0x4639B0, ""),
    "race_progress": (0x46A530, ""),
}
VOID = {"particle_spawn", "effect_puff", "ripple_update", "glint_update", "dust_add", "bomblet_spawn",
        "bomblet_update", "crumble_release_lines", "crumble_update", "hud_set_mode", "countdown_tick",
        "text_free", "finish_messages", "race_go_banner", "pause_check"}


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def s16(rng, lo=-0x8000, hi=0x7FFF):
    return rng.randrange(lo, hi + 1)


def unpacked(fmt):
    out, count = [], ""
    for c in fmt[1:]:
        if c.isdigit():
            count += c
        else:
            out += [c] * int(count or 1)
            count = ""
    return out


def build_world(rng):
    mem = bytearray(SIZE)
    glob = {}

    def put(addr, fmt, *v):
        struct.pack_into(fmt.upper(), mem, addr - S, *[x & {"B": 0xFF, "H": 0xFFFF, "I": 0xFFFFFFFF}[c] for x, c in
                                                       zip(v, unpacked(fmt.upper()))])

    def rand_fill(addr, size):
        mem[addr - S:addr - S + size] = rng.randbytes(size)

    # race record and cars
    rand_fill(RACE, 0x1000)
    n = rng.randrange(1, 7)
    mem[0x54D] = n
    put(RACE + 0x57C, "<i", rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, rng.randrange(-3, 20)]))
    put(RACE + 0x580, "<i", rng.choice([0, 1, 9, 10, 14, 20, 30, 0x33, 0x34, 0x35, 0x95, 0x3f, 7, 200, 201,
                                        rng.randrange(-5, 300)]))
    mem[0x551] = rng.choice([0, 1, 2, 3, 4, 8, 9, 10, 11, rng.getrandbits(8)])
    mem[0x552] = rng.choice([0, 1, 2])
    mem[0x54F] = rng.choice([0, 1, 2, 3])
    mem[0x58C] = rng.choice([0, 0, 0, 1, 2])
    put(RACE + 0x58E, "<h", rng.choice([0, 1, 2, 5, -1, s16(rng)]))
    for k in range(6):
        mem[8 + k] = rng.choice([0, 3, 14, 15, 16, rng.getrandbits(8)])
    put(RACE + 0xA7C, "<hh", s16(rng), s16(rng))
    put(RACE + 0xA80, "<HH", rng.choice([480, 600, 768, rng.getrandbits(16)]),
        rng.choice([640, 800, 1024, rng.getrandbits(16)]))
    slots = bytearray(rng.randbytes(6 * SLOT_SIZE))
    for i in range(6):
        car = CARS + i * CAR_SIZE
        rand_fill(car, CAR_SIZE)
        put(car + 0x6A0, "<i", rng.choice([0, 0, -1, 1, 0x100, 0x5DC, 0x600, rng.randrange(-0x1000, 0x2000)]))
        put(car + 0x63D, "<B", rng.choice([0, 1, 3, rng.getrandbits(8)]))
        put(car + 0x70C, "<B", rng.choice([8, 10, 10, 9]))
        put(car + 0x704, "<I", rng.getrandbits(32) & ~(0x800 if rng.randrange(3) else 0))
        put(car + 0x71C, "<I", PACKETS + i * 8)
        sl = i * SLOT_SIZE
        struct.pack_into("<I", slots, sl, car)
        slots[sl + 0x2D] = rng.randrange(0, 8)
        slots[sl + 0x25A] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 12, rng.getrandbits(8)])
        slots[sl + 0x25B] = rng.choice([0, 1, 0x32, 0x33, 0x96, 0x97, 0x1D, 0xFF, rng.getrandbits(8)])
        slots[sl + 0x25C] = rng.randrange(0, 0x1C)
        slots[sl + 0x25D] = rng.randrange(0, 0x1C)
    rand_fill(PACKETS, 6 * 8)
    glob[SLOTS] = bytes(slots)
    glob[0x4B1960] = p32(RACE)
    glob[0x4B11D8] = p32(rng.randrange(1, 0x80000000))
    glob[0x6C0A50] = p32(rng.choice([0, 0x40, 0x41, rng.getrandbits(32)]))
    glob[0x6CD410] = p32(rng.choice([0x100, 0x140, 0x200, rng.randrange(1, 0x400)]))

    # ripples
    count = rng.choice([0, 1, 5, 16, 0x100, NRIPPLES, NRIPPLES, rng.randrange(0, NRIPPLES + 1)])
    rand_fill(RULES, 0x200)
    mem[RULES - S + 0xFC] = rng.choice([0, 1, 1, 1])
    put(RULES + 0x118, "<I", MARKS)
    mem[RULES - S + 0x11C] = rng.choice([0, 1, 3, 8])
    rand_fill(MARKS, 0x80)
    for k in range(NRIPPLES):
        rec = RIPPLES + k * 0x18
        rand_fill(rec, 0x18)
        put(rec, "<I", RVERTS + k * 8)
        put(rec + 0x10, "<h", rng.choice([0, 0x2A, 0x3F, 0x55, 0x7F, s16(rng, -2, 0x90)]))
        mem[rec - S + 0x14] = rng.choice([0, 1, 2, 3, 4])
        mem[rec - S + 0x15] = rng.choice([0, 2, 4, 8, 11, 12, 13, rng.getrandbits(8)])
    rand_fill(RVERTS, NRIPPLES * 8)
    glob[0x6C1EB0] = p32(RULES) + p32(count) + p32(RIPPLES)
    glob[0x4B2C64] = p32(0)
    rand_fill(PARTICLES, 0x800)
    rand_fill(EFFECTS, 0x1600)

    # racing lines (+3: blocked count)
    put(LHOLD + 0x60, "<I", LINES)
    rand_fill(LINES, 0x100)

    # collapsing buildings
    queue = bytearray()
    for q in range(64):
        queue += p32(COBJ + rng.randrange(4) * 0x200) + p32(rng.choice([0, 0xFE, 0xFF, rng.randrange(0, 64)]))
    glob[0x6D04A0] = (bytes([0, rng.randrange(0, 0x40), rng.choice([0, 0, 1, 3])]) + b"\0" + p32(CRUMBLES)
                      + rng.randbytes(0x18) + bytes(queue))
    step = rng.choice([0, 1, 0xFF, 0xFF, 2])
    glob[0x4A7CE4] = p32(step)
    for k in range(4):
        obj, model, ent = COBJ + k * 0x200, CMODEL + k * 0x100, CRUMBLES + k * 0xA8
        verts, faces, groups = CVERT + k * 0x400, CFACE + k * 0x400, CGROUP + k * 0x100
        rand_fill(obj, 0x200)
        put(obj + 0x10, "<I", model)
        put(obj + 0x24, "<I", verts)
        put(obj + 0x28, "<I", faces)
        base = [rng.randrange(-0x1000, 0x1000) << 16 for _ in range(3)]
        put(obj + 0x34, "<3i", *base)
        if rng.randrange(2):
            put(obj + 0xC4, "<9h", 4096, 0, 0, 0, 4096, 0, 0, 0, 4096)
        else:
            put(obj + 0xC4, "<9h", *[s16(rng, -4096, 4096) for _ in range(9)])
        put(obj + 0x18E, "<HHH", rng.randrange(0, 40), rng.randrange(0, 16), rng.randrange(0, 0x800))
        put(obj + 0x194, "<I", OBJKINDS)
        mem[obj - S + 0x1A6] = rng.choice([0, rng.randrange(1, 16)])
        mem[obj - S + 0x1A7] = rng.choice([0, rng.randrange(1, 16)])
        rand_fill(model, 0x100)
        nf = rng.randrange(0, NF + 1)
        ng = rng.randrange(1, NG + 1)
        put(model + 4, "<HH", rng.randrange(1, NV + 1), nf)
        mem[model - S + 0x27] = ng
        put(model + 0x34, "<H", groups - model)
        for v in range(NV):
            put(verts + v * 8, "<3hH", *[s16(rng, -200, 200) for _ in range(3)], rng.choice([0, 0, 1, 2]))
        for f in range(NF):
            put(faces + f * 8, "<4BI", *[rng.choice([0, rng.randrange(1, NV + 1)]) for _ in range(4)],
                rng.getrandbits(32))
        first = 0
        for g in range(8):
            last = min(nf, first + rng.randrange(0, 8)) if g < 7 else 0xFF  # no group past the 8th
            put(groups + g * 20, "<3hhBB", s16(rng, -300, 300), s16(rng, -300, 300), s16(rng, -300, 300),
                s16(rng), first, last)
            rand_fill(groups + g * 20 + 10, 10)
            put(groups + g * 20 + 8, "<BB", first, last)
            first = last + 1
        rand_fill(ent, 0xA8)
        put(ent + 2, "<H", rng.choice([0, 1, 2, 3, 4, 4, 4]))
        put(ent + 4, "<I", obj)
        put(ent + 0x28, "<i", rng.choice([0, 0, 7, 8, 15, rng.getrandbits(32)]))
        put(ent + 0x2C, "<HH", rng.randrange(64), rng.choice([0xFF, rng.randrange(64)]))
        put(ent + 0x30, "<hh", rng.choice([0, ng, ng, -1]), rng.choice([1, 2, nf, 0]))
        put(ent + 0x34, "<i", rng.choice([0x10000, 0x1F0000, 0x200000, rng.getrandbits(32)]))
        put(ent + 0x40, "<i", rng.randrange(0, 0x8000))
        put(ent + 0x48, "<3h", *[s16(rng, -100, 100) for _ in range(3)])
        for g in range(4):
            mem[ent - S + 0x5C + g] = rng.choice([0, 1, 2, 3, 5])
        for g in range(16):
            mem[ent - S + 0x6C + g] = rng.choice([0, 0, 1, 2, 3, 0x81, 0x83, rng.getrandbits(8)])
            mem[ent - S + 0x7C + g] = rng.randrange(0, 4)
        put(ent + 0x8C, "<II", CHEAP + k * 0x400, CHEAP + k * 0x400)
        put(ent + 0x94, "<i", base[1] + rng.randrange(-0x1000000, 0x1000000))
        put(ent + 0x9C, "<II", groups, CDONE + k * 0x40)
        for f in range(0x40):
            mem[CDONE - S + k * 0x40 + f] = rng.choice([0, 0, 0, 5])
    for v in range(0x10):
        put(SHAPES + v * 0x10, "<8H", *[rng.randrange(0, 0x400) for _ in range(8)])
    rand_fill(KINDS, 0x200)
    rand_fill(OBJKINDS, 0x100)
    entry = CRUMBLES + rng.randrange(4) * 0xA8
    eobj = struct.unpack_from("<I", mem, entry - S + 4)[0]
    glob[0x6CF160] = p32(struct.unpack_from("<I", mem, eobj - S + 0x10)[0]) + p32(eobj) + p32(SHAPES) + p32(entry)
    put(SETTINGS + 0x2E, "<B", rng.choice([0, 1]))
    glob[0x4B2198] = p32(SETTINGS)
    glob[0x6C1E30] = p32(LHOLD)
    glob[0x6BFB10] = p32(KINDS) + p32(SHAPES)
    glob[0x4B7E54] = p32(rng.randrange(0, 8))

    # bomblets
    bombs = bytearray()
    busy = bytearray(rng.choice([0, 0, 1]) for _ in range(0x1C))
    for k in range(0x24):
        bombs += p32(BOBJ + rng.randrange(0x1C) * 0x180)
        bombs += bytes([rng.choice([0, 0, 1, 2, 0x3D, 0x3E, 0x40, rng.getrandbits(8)]), rng.randrange(0x1C),
                        rng.choice([1, 2, 3, 0, 4]), rng.getrandbits(8)])
    mem[BOMBS - S:BOMBS - S + len(bombs)] = bombs
    for k in range(0x1C):
        obj = BOBJ + k * 0x180
        rand_fill(obj, 0x180)
        put(obj + 0x10, "<I", BMODEL)
        put(obj + 0x160, "<H", rng.choice([0, 699, 700, 999, 1000, 1199, 1200, rng.getrandbits(16)]))
    put(BMODEL + 0x32, "<H", rng.randrange(0, 0x1000))
    glob[0x600600] = rng.randbytes(0x40)
    glob[0x600640] = bytes(busy) + bytes(8) + p32(BOMBS)
    glob[0x600680] = b"".join(p32(BOBJ + k * 0x180) for k in range(0x1C))
    glob[0x4D3DD8] = p32(rng.choice([0, 5, 0x1B, rng.randrange(0, 0x1C)]))
    glob[0x4D3DE0] = p32(rng.getrandbits(32))

    # glints
    glints = bytearray(rng.randbytes(0xC8))
    for k in range(10):
        struct.pack_into("<i", glints, k * 0x14 + 0xC, rng.choice([0, 1, 2, 0x1C1, 0x1C2, 0x1C3, 900, -1]))
        struct.pack_into("<i", glints, k * 0x14 + 0x10, rng.choice([0, 1, 4, 7, 100]))
        struct.pack_into("<h", glints, k * 0x14 + 6, rng.choice([0, 3, 6, 7, 99]))
    glob[0x4C1B18] = p32(rng.randrange(0, 10)) + bytes(4) + bytes(glints) + bytes(0xE0 - 8 - 0xC8)
    glob[0x6CCD84] = bytes([rng.choice([0, 2, 3])])
    glob[0x6CC940] = p32(rng.getrandbits(32))
    glob[0x6CCB9C] = p32(rng.getrandbits(32))

    # messages and font
    rand_fill(FONT, 0x1200)
    put(FONT + 4, "<H", rng.choice([0x100, 0xC0, 0x180]))
    put(FONT + 0x10, "<ii", rng.randrange(0, 40), rng.randrange(-4, 8))
    for c in range(256):
        mem[FONT - S + 0x101C + c] = rng.choice([0xFF, rng.randrange(0, 0x20)])
    for g in range(0x22):
        put(FONT + (g + 2) * 0x10, "<H", rng.randrange(0, 40))
    for k in range(16):
        txt = bytes(rng.choice(b"ABC xyz12!") for _ in range(rng.randrange(0, 70)))[:0x5F] + b"\0"
        mem[STRINGS - S + k * 0x60:STRINGS - S + k * 0x60 + len(txt)] = txt
    texts = bytearray(rng.randbytes(0x1C * 0x50))
    for k in range(0x1C):
        texts[k * 0x50 + 8] = rng.choice([0, 0, 1, 5])
        struct.pack_into("<hhH", texts, k * 0x50 + 4, s16(rng, -600, 1200), s16(rng, -100, 900), rng.randrange(0, 800))
    glob[0x5F6940] = p32(FONT) + bytes(texts)
    menu = bytearray(rng.randbytes(8 * 0x50))
    for k in range(8):
        menu[k * 0x50 + 8] = rng.choice([0, 1])
    glob[0x6B95A4] = bytes(menu)
    glob[0x6B9824] = rng.randbytes(0x14)
    glob[0x6B8D6C] = rng.randbytes(0x20)
    glob[0x4D1098] = b"".join(p32(STRINGS + rng.randrange(16) * 0x60) for _ in range(0x2C))
    glob[0x6CE700] = p32(rng.getrandbits(32)) + p32(rng.choice([0, 1, 3]))
    glob[0x4D3DA8] = p32(rng.choice([0, 1, 3, -1])) + p32(rng.getrandbits(32))
    glob[0x5D622C] = p32(rng.getrandbits(8))
    glob[0x4D1198] = p32(rng.getrandbits(32))
    glob[0x6B983A] = rng.randbytes(2) + p32(PARTICLES)

    # stub answers
    answers = {
        "slot": [rng.choice([-1, rng.randrange(16)]) for _ in range(8)],
        "net": rng.choice([0, 1, 2]), "host": rng.choice([0, 1, 2]), "menu": rng.choice([0, 1, 5]),
        "player": rng.randrange(n), "receive": rng.choice([0, 1, 1]), "layout": rng.getrandbits(32),
    }
    assert len(mem) == SIZE
    return mem, glob, n, answers


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self.hit, begin=address, end=address)

    def reset(self, answers):
        self.log = []
        self.answers = answers
        self.slot_i = 0
        self.effect_i = 0

    def hit(self, uc, address, size, _):
        name, nargs, kind = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret, *args = struct.unpack("<%dI" % (nargs + 1), uc.mem_read(esp, 4 * (nargs + 1)))
        rec = [name]
        for i, a in enumerate(args):
            if E.STACK <= a < E.STACK + E.STACK_SIZE and name not in VALUE_ONLY:
                rec.append(("stack", bytes(uc.mem_read(a, STACK_BYTES.get((name, i), 12)))))
            else:
                rec.append(a)
        self.log.append(tuple(rec))
        value = 0
        if kind == "slot":
            value = self.answers["slot"][self.slot_i % 8]
            self.slot_i += 1
        elif kind == "effect":
            value = EFFECTS + (self.effect_i % 32) * 0xB0
            self.effect_i += 1
        elif kind is not None:
            value = self.answers[kind]
        uc.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)


def make_args(rng, kinds, mem, n):
    args = []
    vec = ARGS
    for k in kinds:
        if k in "PV":
            struct.pack_into("<3i", mem, vec - S, *[rng.randrange(-0x40000000, 0x40000000) for _ in range(3)])
            if k == "V":
                struct.pack_into("<3i", mem, vec - S, *[rng.randrange(-0x10000, 0x10000) for _ in range(3)])
            args.append(vec)
            vec += 0x10
        elif k == "k":
            args.append(rng.choice([0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0]))
        elif k == "g":
            args.append(RIPPLES + rng.randrange(16) * 0x18)
        elif k == "T":
            args.append(rng.choice([0x22, 8, rng.getrandbits(32)]))
        elif k == "w":
            args.append(rng.choice([0, 1, 2, 0xF, 0x31, 0x100, 0xFFFFFC00, rng.getrandbits(32), rng.getrandbits(12)]))
        elif k == "s":
            args.append(rng.getrandbits(8) | (rng.getrandbits(24) << 8 if rng.randrange(4) == 0 else 0))
        elif k == "b":
            args.append(rng.choice([0, 1, 2, 3, 4, 0x101, rng.getrandbits(32)]))
        elif k == "o":
            args.append(rng.getrandbits(32) if rng.randrange(2) else rng.randrange(0x24))
        elif k == "e":
            args.append(CRUMBLES + rng.randrange(4) * 0xA8)
        elif k == "O":
            args.append(COBJ + rng.randrange(4) * 0x200)
        elif k == "a":
            args.append(rng.choice([0, 0x19220, -0x19220, 0x3243F, 0x4B65F, 0x6487F, 0x80000000, 0x7FFFFFFF,
                                    rng.randrange(-0x200000, 0x200000), rng.getrandbits(32)]))
        elif k == "t":
            args.append(rng.choice([rng.randrange(0x1C), rng.randrange(0x1C), 0xFFFFFFFF]))
        elif k == "S":
            args.append(STRINGS + rng.randrange(16) * 0x60)
        elif k == "F":
            args.append(rng.getrandbits(10) | rng.choice([0, 0x100, 0x200]))
        elif k == "m":
            args.append(rng.choice([0, 0, 1, 2]))
    if kinds == "gTwws":  # glint_spawn takes a glint record's int16 point
        args[0] = ARGS + 0x40
        struct.pack_into("<3h3B", mem, ARGS + 0x40 - S, *[s16(rng) for _ in range(3)], *rng.randbytes(3))
    return args


def run_case(m, stubs, name, address, kinds, rng):
    mem, glob, n, answers = build_world(rng)
    args = make_args(rng, kinds, mem, n)
    results = []
    for target in (address, m.symbols[name]):
        m.write(S, bytes(mem))
        for a, data in glob.items():
            m.write(a, data)
        m.write(0x6CCBE0, bytes(0x6CCCE4 - 0x6CCBE0))
        m.write(E.STACK, bytes(E.STACK_SIZE))  # stale locals read as 0 both ways
        stubs.reset(answers)
        r = m.call(target, args)
        if name in VOID and r[0] == "ok":
            r = ("ok",)
        state = m.read(S, SIZE) + b"".join(m.read(a, size) for a, size in GLOBALS)
        results.append((r, state, list(stubs.log)))
    shown = tuple(hex(a & 0xFFFFFFFF) for a in args)
    if any(r[0][0] == "cut off" for r in results):
        return f"{name}{shown}: cut off {results[0][0]} {results[1][0]}", 0
    if results[0] != results[1]:
        diff = ""
        if results[0][1] != results[1][1]:
            a, b = results[0][1], results[1][1]
            i = next(i for i in range(len(a)) if a[i] != b[i])
            where = f"scratch +{i:#x}" if i < SIZE else f"globals +{i - SIZE:#x}"
            diff += f" (memory differs at {where})"
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            j = next((j for j in range(min(len(la), len(lb))) if la[j] != lb[j]), min(len(la), len(lb)))
            diff += f" (calls differ at #{j}: {la[j] if j < len(la) else None} vs {lb[j] if j < len(lb) else None})"
        return f"{name}{shown}: original {results[0][0]} ours {results[1][0]}{diff}", 0
    return None, len(results[0][2])


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    only = sys.argv[4:] or list(TESTS)
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, SIZE)
    stubs = Stubs(m)
    rng = random.Random(11)
    failed = 0
    for name in only:
        address, kinds = TESTS[name]
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = calls = 0
        for _ in range(iterations):
            msg, made = run_case(m, stubs, name, address, kinds, rng)
            calls += made
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} ({calls} stubbed calls)")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
