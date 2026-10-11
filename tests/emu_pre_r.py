#!/usr/bin/env python3
"""Differential test of pre_r.c (the small calls sim_step makes before the
world step, the car sounds 0x473250 and sim_step 0x444120 itself) against
the original, on random car, slot, race and sound state.

Functions outside the group (the sound engine, force feedback, network,
and for sim_step every per-step function it calls) are stubbed in both
runs: each stub logs its arguments and returns a scripted value, and the
call logs are compared as well as memory.

    tests/emu_pre_r.py EXE build/emu_r.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x20000
RACE = SCRATCH
CONFIG = SCRATCH + 0x1000
CARS = SCRATCH + 0x3000  # 6 cars, 0x1600 each
CAR_SIZE = 0x1600
PACKETS = SCRATCH + 0xB400  # 2 car-shaped packets, 0x800 each
TRACK = SCRATCH + 0xC400  # +0: pointer to the section pointer array
SECTION_PTRS = SCRATCH + 0xC500  # 256 pointers
SECTIONS = SCRATCH + 0xC900  # 32 records of 8 bytes
UNDER = SCRATCH + 0xCA00  # 8 records of 0x10 (surface under / touching)
TRACK_INFO = SCRATCH + 0xCB00
COPIES = SCRATCH + 0xCC00  # 12 records of 0x18
TARGETS = SCRATCH + 0xCF00
TUNES = SCRATCH + 0xD000  # 7 tunes of 0x100
SLOTS = (0x6BFB80, 0x274 * 6)
ENGINE = (0x5FF1E0, 0x18 * 6)
TUNE_STATE = (0x5FF280, 0x10 * 6)
VOICES = (0x5FF4A0, 0x30 * 0x100)
NET_PLAYERS = (0x5D2CE8, 0x66C * 8)
SURFACES = (0x4B1EE4, 0x14 * 0x1B)
POINTERS = {0x4B1960: RACE, 0x4B2198: CONFIG, 0x6C1E30: TRACK, 0x6C1EB0: TRACK_INFO, 0x6C1EB8: COPIES}
GLOBALS = [(0x5D535C, 4), (0x4BE388, 4), (0x4C2458, 4), (0x6C0A50, 4), (0x6CCE40, 4), (0x6C1EB4, 4),
           (0x4B2034, 4), (0x4B2038, 4), (0x4B2044, 4), (0x5D9B20, 4), (0x5D9B24, 4), (0x5D9B2C, 4),
           (0x4B2180, 0x18)]
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))
COMPARED = [SLOTS, ENGINE, TUNE_STATE, VOICES] + GLOBALS

# address: (name, number of args, masks for args, returns). Masks: bits the
# callee looks at, where the original passes leftover register bits.
STUBS = {
    0x471C90: ("sound_play", 5, None, True),
    0x471DA0: ("sound_moving", 7, {0: 0xFF}, True),
    0x471AA0: ("voice_reverb", 2, {0: 0xFF, 1: 0xFF}, False),
    0x471F80: ("voice_pitch", 2, {0: 0xFF}, False),
    0x471FC0: ("voice_volume", 3, {0: 0xFF}, False),
    0x471F40: ("voice_stop", 1, {0: 0xFF}, False),
    0x471AC0: ("music_mode", 3, None, False),
    0x41B4D0: ("pad_effect", 6, None, False),
    0x489E10: ("ff_kick", 4, None, False),
    0x489CD0: ("ff_engine", 3, None, False),
    0x489980: ("ff_road", 3, None, False),
    0x40D950: ("dot", 2, None, True),
    0x48CF20: ("net_local_car", 0, None, True),
    0x45EF80: ("net_is_game", 0, None, True),
    0x48D270: ("chat_send", 1, None, False),
}
SIM_CALLEES = {
    0x48CEF0: 0, 0x48E7B0: 1, 0x4015E0: 1, 0x425AD0: 1, 0x473250: 2, 0x41B520: 2, 0x41C7A0: 1, 0x48E7E0: 0,
    0x41B7D0: 1, 0x41AC50: 0, 0x43E570: 0, 0x43D350: 0, 0x4112E0: 0, 0x461410: 1, 0x442840: 0, 0x468450: 0,
    0x438620: 1, 0x47AA00: 1, 0x46E3C0: 0, 0x46D0C0: 0, 0x4906C0: 0, 0x486540: 0, 0x48DC40: 0, 0x47AB40: 0,
    0x458010: 0, 0x46A9C0: 0, 0x40DE60: 1, 0x406E30: 1, 0x47B8F0: 0, 0x462840: 0, 0x40D040: 0, 0x408360: 1,
    0x484080: 0, 0x405140: 0, 0x42B990: 0, 0x46C300: 0, 0x47C210: 0, 0x481F20: 0, 0x4751B0: 0, 0x43E270: 0,
    0x405F70: 0, 0x46BFA0: 0, 0x443180: 0, 0x443B50: 0, 0x442890: 0, 0x441450: 3, 0x41C2C0: 0, 0x472040: 0,
    0x46A530: 0,
}
for _a, _n in SIM_CALLEES.items():
    STUBS.setdefault(_a, (f"sim_{_a:x}", _n, None, True))
SOUND_STUBS = {0x471C90, 0x471DA0, 0x471AA0, 0x471F80, 0x471FC0, 0x471F40, 0x471AC0, 0x41B4D0, 0x489E10,
               0x489CD0, 0x489980, 0x40D950}
NET_STUBS = {0x48CF20, 0x45EF80, 0x48D270}

# name: (address, argument kinds, returns, active stubs)
TESTS = {
    "local_car_index": (0x48CEF0, "", True, set()),
    "sound_clock": (0x435100, "", True, set()),
    "car_step_buttons": (0x48E7B0, "C", False, set()),
    "cars_network_controls": (0x48E7E0, "", False, set()),
    "track_vertices_copy": (0x43E570, "", False, set()),
    "cars_catch_up": (0x4112E0, "", False, set()),
    "chat_send_macro": (0x41C6D0, "k", False, NET_STUBS),
    "car_extra_buttons": (0x41C7A0, "L", False, NET_STUBS),
    "car_sounds": (0x473250, "Cp", False, SOUND_STUBS | NET_STUBS),
    "sim_step": (0x444120, "nsc", True, set(SIM_CALLEES)),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rand_int(rng, *ranges):
    return pick(rng, *[rng.randrange(-r, r) for r in ranges], rng.getrandbits(32) - 0x80000000)


def make_car(rng):
    car = bytearray(rng.getrandbits(8) for _ in range(CAR_SIZE))
    struct.pack_into("<I", car, 0x680, pick(rng, 0, 0, PACKETS, PACKETS + 0x800, CARS + rng.randrange(6) * CAR_SIZE))
    car[0x688] = pick(rng, 1, 0, 0x3C, 0x3D, 0x80, rng.getrandbits(8))
    struct.pack_into("<I", car, 0x704, pick(rng, 0, 0, rng.getrandbits(32), 1 << rng.randrange(32)))
    struct.pack_into("<I", car, 0x700, pick(rng, 0, 0x100, rng.getrandbits(32)))
    for o in (0x5EC, 0x528):
        struct.pack_into("<i", car, o, rand_int(rng, 0x10000, 0x100000, 0x400000))
    car[0x63D] = pick(rng, 0, 0x20, 0x21, rng.getrandbits(8))
    car[0x4AC] = rng.getrandbits(8)
    car[0x570] = rng.getrandbits(8)
    struct.pack_into("<I", car, 0x1EC, pick(rng, 0, UNDER + rng.randrange(8) * 0x10))
    struct.pack_into("<I", car, 0x1D0, pick(rng, 0, UNDER + rng.randrange(8) * 0x10))
    car[0x70C] = pick(rng, 8, 10, 8, 10, rng.getrandbits(8))
    struct.pack_into("<I", car, 0x11C, pick(rng, 0xFFFFFFFF, rng.randrange(0, 0x40), rng.getrandbits(32)))
    for o in (0x6A0, 0x504, 0x5C8):
        struct.pack_into("<i", car, o, rand_int(rng, 0x1000, 0x40000, 0x400000))
    car[0x71A] = pick(rng, 0, 0, rng.getrandbits(8))
    car[0x6FA] = pick(rng, 0, 0, rng.getrandbits(8))
    car[0x65F] = pick(rng, 0, 0, rng.getrandbits(8))
    car[0x6A4] = pick(rng, 0, 1, rng.getrandbits(8))
    return car


def make_state(rng):
    count = pick(rng, rng.randrange(1, 7), rng.randrange(1, 7), 0)
    cars = [make_car(rng) for _ in range(6)]
    packets = bytes(rng.getrandbits(8) for _ in range(0x1000))
    race = bytearray(rng.getrandbits(8) for _ in range(0xC00))
    race[0x54D] = count
    race[0x551] = pick(rng, 4, 2, 0, rng.getrandbits(8))
    race[0xA87] = pick(rng, 0, 1, 1, rng.getrandbits(8))
    struct.pack_into("<i", race, 0x57C, pick(rng, 0, 3, 4, 5, 6, rng.getrandbits(32) - 0x80000000))
    struct.pack_into("<H", race, 0x61C, pick(rng, 0, 1, rng.getrandbits(16)))
    for i in range(6):
        race[0x1C + i] = pick(rng, 6, rng.randrange(0, 6), rng.randrange(0, 7))
    config = bytearray(rng.getrandbits(8) for _ in range(0x1200))
    name = bytes(rng.randrange(0x20, 0x7F) for _ in range(rng.randrange(0, 30))) + b"\0"
    config[0x120:0x120 + len(name)] = name
    for i in range(10):
        macro = bytes(rng.randrange(0x20, 0x7F) for _ in range(pick(rng, 0, rng.randrange(0, 60)))) + b"\0"
        config[0xDCE + i * 0x40:0xDCE + i * 0x40 + len(macro)] = macro
    slots = bytearray(rng.getrandbits(8) for _ in range(SLOTS[1]))
    order = list(range(1, 7))
    rng.shuffle(order)
    for i in range(6):
        o = i * 0x274
        struct.pack_into("<I", slots, o, CARS + i * CAR_SIZE)
        slots[o + 0x2C] = pick(rng, 0, 1, 2, rng.getrandbits(8))
        slots[o + 0x2D] = order[i] if i < count or rng.randrange(2) else order[i]
        struct.pack_into("<i", slots, o + 0x34, pick(rng, rng.randrange(0, 0x10000), rng.randrange(0, 0x3000),
                                                  rng.getrandbits(32) - 0x80000000))
        slots[o + 0x259] = pick(rng, 0, 0, 1, rng.getrandbits(8))
        slots[o + 0x268] = pick(rng, 0, 1)
    # positions must be 1..count for the racing cars (the original's order
    # array is uninitialised otherwise)
    racing = list(range(1, count + 1))
    rng.shuffle(racing)
    for i in range(count):
        slots[i * 0x274 + 0x2D] = racing[i]
    track = bytearray(0x100)
    struct.pack_into("<I", track, 0, SECTION_PTRS)
    section_ptrs = b"".join(struct.pack("<I", SECTIONS + rng.randrange(32) * 8) for _ in range(256))
    sections = bytes(rng.getrandbits(8) for _ in range(0x100))
    under = bytearray(rng.getrandbits(8) for _ in range(0x80))
    for i in range(8):
        struct.pack_into("<I", under, i * 0x10 + 0xC, pick(rng, 1, rng.randrange(0, 0x1C), rng.randrange(0, 0x30),
                                                           rng.getrandbits(32)))
        under[i * 0x10 + 0xC] = under[i * 0x10 + 0xC] if rng.randrange(3) else 1
    info = bytearray(rng.getrandbits(8) for _ in range(0x100))
    info[0xFC] = pick(rng, 0, 1, rng.getrandbits(8))
    copies = bytearray(rng.getrandbits(8) for _ in range(0x18 * 12))
    for i in range(12):
        struct.pack_into("<I", copies, i * 0x18, pick(rng, TARGETS + rng.randrange(0, 0xF8),
                                                      COPIES + rng.randrange(0, 0x18 * 12 - 6)))
    tunes = bytearray(rng.getrandbits(8) for _ in range(0x700))
    for t in range(7):
        for k in range(0x40):
            struct.pack_into("<H", tunes, t * 0x100 + k * 4 + 2, pick(rng, 0xFF, rng.randrange(0, 0x20),
                                                                      rng.getrandbits(16)))
    engine = bytearray(rng.getrandbits(8) for _ in range(ENGINE[1]))
    for i in range(6):
        e = i * 0x18
        struct.pack_into("<i", engine, e, pick(rng, rng.randrange(0, 0x40000000), 0x32C90000, 0x1F3F0000,
                                               rng.getrandbits(32) - 0x80000000, 0))
        for k in range(4, 8):
            engine[e + k] = pick(rng, 0x7F, rng.randrange(0, 0x40), rng.getrandbits(8))
        struct.pack_into("<h", engine, e + 8, pick(rng, rng.randrange(-200, 10000), rng.getrandbits(16) - 0x8000))
        struct.pack_into("<h", engine, e + 0xA, pick(rng, rng.randrange(-200, 20000), rng.getrandbits(16) - 0x8000))
        struct.pack_into("<h", engine, e + 0xC, pick(rng, 0, 1, 5, rng.getrandbits(16) - 0x8000))
        struct.pack_into("<h", engine, e + 0xE, pick(rng, 0, 1, -1, 2, 3, rng.getrandbits(16) - 0x8000))
        engine[e + 0x10] = pick(rng, 0, 1, 2, 0xFF, rng.getrandbits(8))
        engine[e + 0x11] = pick(rng, 0, 0, 1, 2, 5, rng.getrandbits(8))
        engine[e + 0x12] = pick(rng, 0, 1, rng.randrange(0, 0x1B), rng.getrandbits(8))
        struct.pack_into("<h", engine, e + 0x16, pick(rng, 0, 1, 2, 5, 6, rng.getrandbits(16) - 0x8000))
    tune_state = bytearray(rng.getrandbits(8) for _ in range(TUNE_STATE[1]))
    for i in range(6):
        t = i * 0x10
        struct.pack_into("<IiII", tune_state, t, pick(rng, 0xFF, rng.randrange(0, 0x30), rng.getrandbits(32) & 0x3FF),
                         rand_int(rng, 0x10, 0x1000), pick(rng, 0x7F, rng.randrange(0, 0x40), rng.getrandbits(32)),
                         rng.getrandbits(32))
    voices = bytes(pick(rng, 0, 0, 1, rng.getrandbits(8)) for _ in range(VOICES[1]))
    net = bytes(rng.getrandbits(8) for _ in range(NET_PLAYERS[1]))
    surfaces = bytearray(rng.getrandbits(8) for _ in range(SURFACES[1]))
    for r in range(0x1B):
        struct.pack_into("<i", surfaces, r * 0x14 + 0xC, rand_int(rng, 0x4000))
        struct.pack_into("<i", surfaces, r * 0x14 + 0x10, pick(rng, 0, 0, 1, rng.getrandbits(32) - 0x80000000))
    globals_ = {
        0x5D535C: struct.pack("<i", pick(rng, -1, -1, rng.randrange(0, 8))),
        0x4BE388: struct.pack("<I", rng.getrandbits(32)),
        0x4C2458: struct.pack("<I", rng.getrandbits(32)),
        0x6C0A50: struct.pack("<I", rng.getrandbits(32)),
        0x6CCE40: struct.pack("<I", rng.getrandbits(32)),
        0x6C1EB4: struct.pack("<i", pick(rng, 0, -1, rng.randrange(1, 13), rng.randrange(1, 13))),
        0x4B2034: struct.pack("<i", pick(rng, -1, -1, rng.randrange(0, 6), rng.getrandbits(32) - 0x80000000)),
        0x4B2038: struct.pack("<i", rand_int(rng, 0x1000, 0x100000)),
        0x4B2044: struct.pack("<I", rng.getrandbits(32)),
        0x5D9B20: struct.pack("<I", pick(rng, 0, 0, 1, 2, 4)),
        0x5D9B24: struct.pack("<I", pick(rng, CARS + rng.randrange(6) * CAR_SIZE, 0)),
        0x5D9B2C: struct.pack("<I", pick(rng, 0x10000, rng.randrange(0, 0x10000), rng.getrandbits(32))),
        0x4B2180: b"".join(struct.pack("<I", TUNES + t * 0x100) for t in range(6)),  # [6] is the config pointer
    }
    script = {
        "sound_play": [pick(rng, 0x7F, rng.randrange(0, 0x40)) for _ in range(8)],
        "sound_moving": [pick(rng, 0x7F, rng.randrange(0, 0x40)) for _ in range(4)],
        "dot": [rand_int(rng, 0x100, 0x1000000) for _ in range(2)],
        "net_local_car": [pick(rng, 0, rng.randrange(0, 6), rng.randrange(0, 8)) for _ in range(2)],
        "net_is_game": [pick(rng, 0, 1, 1) for _ in range(2)],
        "sim_48cef0": [rng.randrange(0, 6)],
        "sim_46e3c0": [pick(rng, 0, 1)],
        "sim_4906c0": [pick(rng, 0, 1)],
        "sim_40d040": [pick(rng, 0, 1, 2, 3, -1)],
        "sim_46a530": [pick(rng, 0, 0, 1)],
    }
    # sim_step: a callee may change the car count (its loops re-read it)
    change = None
    if rng.randrange(4) == 0:
        change = (f"sim_{rng.choice(list(SIM_CALLEES)):x}", rng.randrange(0, 7))
    return dict(cars=cars, packets=packets, race=race, config=config, slots=slots, track=track,
                section_ptrs=section_ptrs, sections=sections, under=under, info=info, copies=copies, tunes=tunes,
                engine=engine, tune_state=tune_state, voices=voices, net=net, surfaces=surfaces, globals=globals_,
                script=script, count=count, change=change)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.active = set()
        self.change = None
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active, change):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.active = active
        self.change = change

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address not in self.active:
            return
        name, nargs, masks, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, mask in (masks or {}).items():
            args[i] &= mask
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                if name == "chat_send":
                    text = bytes(uc.mem_read(a, 0x100))
                    args[i] = ("text", text[:text.index(b"\0")])
                else:
                    args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        if self.change and self.change[0] == name:
            uc.mem_write(RACE + 0x54D, bytes([self.change[1]]))
            self.change = None
        eax = self._next(name, 0) if returns else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, st):
    args = []
    p = None
    for k in kinds:
        if k == "C":
            p = rng.randrange(6)
            args.append(CARS + p * CAR_SIZE if rng.randrange(8) else CARS + rng.randrange(6) * CAR_SIZE)
        elif k == "p":
            args.append(p if p is not None else rng.randrange(6))
        elif k == "L":
            args.append(SLOTS[0] + rng.randrange(6) * 0x274)
        elif k == "k":
            args.append(rng.randrange(0, 10))
        elif k == "n":
            args.append(pick(rng, 0, 1, rng.getrandbits(32)))
        elif k == "s":
            step = rng.randrange(0, 4)
            args.append(step)
        elif k == "c":
            args.append(pick(rng, args[-1] + 1, args[-1] + 1, rng.randrange(0, 5)))
        else:
            args.append(rng.getrandbits(32))
    return args


def run_case(m, stubs, name, address, kinds, returns, active, rng):
    st = make_state(rng)
    args = make_args(rng, kinds, st)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        for i, car in enumerate(st["cars"]):
            m.write(CARS + i * CAR_SIZE, bytes(car))
        m.write(PACKETS, st["packets"])
        m.write(RACE, bytes(st["race"]))
        m.write(CONFIG, bytes(st["config"]))
        m.write(TRACK, bytes(st["track"]))
        m.write(SECTION_PTRS, st["section_ptrs"])
        m.write(SECTIONS, st["sections"])
        m.write(UNDER, bytes(st["under"]))
        m.write(TRACK_INFO, bytes(st["info"]))
        m.write(COPIES, bytes(st["copies"]))
        m.write(TUNES, bytes(st["tunes"]))
        m.write(SLOTS[0], bytes(st["slots"]))
        m.write(ENGINE[0], bytes(st["engine"]))
        m.write(TUNE_STATE[0], bytes(st["tune_state"]))
        m.write(VOICES[0], st["voices"])
        m.write(NET_PLAYERS[0], st["net"])
        m.write(SURFACES[0], bytes(st["surfaces"]))
        for a, p in POINTERS.items():
            m.write(a, struct.pack("<I", p))
        for a, v in st["globals"].items():
            m.write(a, v)
        # leftover stack bytes, the same for both runs
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"], active, st["change"])
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in COMPARED)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), struct.unpack_from("<i", a, i)[0], struct.unpack_from("<i", b, i)[0])
                                for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(11)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, returns, active) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, active, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
