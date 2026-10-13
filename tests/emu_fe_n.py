#!/usr/bin/env python3
"""Differential test of fe_n.c (front end group N: the menu to return to,
menu builders, the setup options' handlers, the network menu mode, the car
stat bars, the menu model lookup) against the original, on random front end
state.

Functions outside the group are stubbed in both runs: each stub logs its
arguments and returns a scripted value, and the call logs are compared as
well as memory.

    tests/emu_fe_n.py EXE build/emu_n.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x4000
OPTIONS = SCRATCH
RACE = SCRATCH + 0x1000
MATRIX = SCRATCH + 0x2000

OUTCOMES = {}
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, returns)
STUBS = {
    0x447650: ("menu_open", 1, True),
    0x44C6D0: ("option_onoff", 6, True),
    0x44C9D0: ("option_choice", 6, True),
    0x448A00: ("menu_title", 2, True),
    0x448140: ("menu_item", 5, True),
    0x448B50: ("menu_select", 1, True),
    0x448A50: ("text_list", 9, True),
    0x44A1C0: ("text_list_show", 3, True),
    0x42E6D0: ("string_get", 4, True),
    0x4979F0: ("sprintf", 3, True),
    0x45D150: ("network_text_set", 1, True),
    0x447F20: ("option_toggle", 3, True),
    0x450050: ("option_count", 6, True),
    0x4481A0: ("option_value_text", 4, True),
}

# fixed regions randomised before and compared after
FLAGS = [(0x4BEB38, 0x40), (0x6C1FC0, 0xD8)]
NETWORK = [(0x4D07F8, 0x80), (0x5D2CC4, 4), (0x5D5CCC, 8)]
MENUS = [(0x4CB48C, 4), (0x4AF798, 4)]
MODELS = (0x4CBA80, 0x4CD6A0 - 0x4CBA80)
GRID = [(0x4DA214, 4)]
CARS = (0x4A7910, 0x10 * 24)

# name: (address, argument kinds, returns, fixed regions)
TESTS = {
    "menu_after_race": (0x42DD60, "", True, FLAGS),
    "menu_return": (0x42DE50, "", True, FLAGS),
    "network_messages_title": (0x45F490, "", True, NETWORK),
    "network_messages_menu": (0x44A260, "", True, NETWORK),
    # multiplayer_menu 0x4518a0: not compared; it greys out "Networked" on purpose (fe_n.c)
    "laps_menu": (0x44FFD0, "rr", True, []),
    "laps_menu_race": (0x4500F0, "r", True, []),
    "laps_menu_other": (0x4501A0, "r", True, []),
    "option_meteors_add": (0x44FED0, "", True, []),
    "option_shield_texture_add": (0x450680, "", True, []),
    "option_water_reflections_add": (0x4506E0, "", True, []),
    "option_rubble_add": (0x450810, "", True, []),
    "option_ice_sheet_add": (0x450870, "", True, []),
    "option_speed_glow_add": (0x4508D0, "", True, []),
    "option_lov_add": (0x450BA0, "", True, []),
    "option_catch_up_add": (0x450E90, "", True, []),
    "option_meteors": (0x44FEF0, "er", True, []),
    "option_shield_texture": (0x4506A0, "e", True, []),
    "option_water_reflections": (0x450700, "e", True, []),
    "option_rubble": (0x450830, "e", True, []),
    "option_ice_sheet": (0x450890, "e", True, []),
    "option_speed_glow": (0x4508F0, "e", True, []),
    "option_lov": (0x450BC0, "e", True, []),
    "option_catch_up": (0x450EB0, "e", True, []),
    "option_laps_race": (0x450110, "er", True, []),
    "option_laps_other": (0x4501C0, "er", True, []),
    "network_menu_mode": (0x45F500, "m", True, NETWORK),
    "car_stat_bar": (0x456F70, "sc", True, [CARS]),
    "menu_model_find": (0x4539D0, "i", True, [MODELS]),
    "matrix_identity": (0x4264F0, "x", True, []),
    "grid_highlight_set": (0x490EC0, "r", True, GRID),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return bytes(rng.getrandbits(8) for _ in range(n))


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def flag(rng, size):
    v = pick(rng, 0, 0, 0, 1, rng.getrandbits(8 * size))
    return v.to_bytes(size, "little")


def make_state(rng, name):
    """A list of (address, bytes) writes making the input state, and the
    stubs' scripted return values."""
    w = []
    options = bytearray(rbytes(rng, 0x100))
    for off in (0x2A, 0x2B, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x35, 0x85):
        options[off] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    w.append((OPTIONS, bytes(options)))
    w.append((0x4B2198, p32(OPTIONS)))
    race = bytearray(rbytes(rng, 0x1000))
    race[0xA87] = pick(rng, 0, 1, rng.getrandbits(8))
    w.append((RACE, bytes(race)))
    w.append((0x4B1960, p32(RACE)))
    # the "come back to" flags: mostly clear, so every branch is reached
    flags = bytearray(0x40)
    for off in range(0, 0x40, 4):
        flags[off:off + 4] = flag(rng, 4) if rng.randrange(5) == 0 else bytes(4)
    w.append((0x4BEB38, bytes(flags)))
    bytes_ = bytearray(0xD8)
    for off in (0x0F, 0x01, 0x39, 0x0E, 0xD3):
        bytes_[off] = pick(rng, 0, 0, 0, 1, rng.getrandbits(8)) if rng.randrange(4) == 0 else 0
    w.append((0x6C1FC0, bytes(bytes_)))
    w.append((0x4D07F8, rbytes(rng, 0x80)))
    w.append((0x5D2CC4, rbytes(rng, 4)))
    w.append((0x5D5CCC, rbytes(rng, 8)))
    w.append((0x4CB48C, rbytes(rng, 4)))
    w.append((0x4AF798, p32(pick(rng, 0, 1, rng.getrandbits(32)))))
    w.append((0x4DA214, rbytes(rng, 4)))
    cars = bytearray()
    for _ in range(24):
        cars += struct.pack("<4i", pick(rng, rng.randrange(-80000, 80001), rng.getrandbits(32) - 2**31),
                            pick(rng, rng.randrange(-0xDAC, 5001), rng.getrandbits(32) - 2**31),
                            pick(rng, rng.randrange(0x40000, 0x120001), rng.getrandbits(32) - 2**31),
                            pick(rng, rng.randrange(-1, 3), rng.getrandbits(32) - 2**31))
    w.append((CARS[0], bytes(cars)))
    models = bytearray(rbytes(rng, MODELS[1]))
    ids = [rng.getrandbits(32) for _ in range(4)]
    for i in range(100):
        if rng.randrange(8) == 0:
            struct.pack_into("<I", models, i * 0x48, rng.choice(ids))
    if rng.randrange(3) == 0:  # an id only near the end of the table
        last = rng.getrandbits(32)
        ids.append(last)
        struct.pack_into("<I", models, rng.randrange(96, 100) * 0x48, last)
    w.append((MODELS[0], bytes(models)))
    script = {
        "menu_open": [rng.getrandbits(32)],
        "string_get": [pick(rng, -1, 0, 7, rng.getrandbits(32))],
        "sprintf": [rng.getrandbits(32)],
        "option_toggle": [pick(rng, 0, 1, 2, rng.getrandbits(32))],
        "option_count": [pick(rng, -1, -1, 1, 2, 3, rng.getrandbits(32))],
    }
    for name_ in ("option_onoff", "option_choice", "menu_title", "menu_select", "text_list", "text_list_show",
                  "network_text_set", "option_value_text"):
        script[name_] = [rng.getrandbits(32)]
    script["menu_item"] = [rng.getrandbits(32), rng.getrandbits(32)]
    return w, script, ids


class Stubs:
    def __init__(self, m):
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _cstring(self, uc, a):
        data = bytes(uc.mem_read(a, 0x80))
        return data.split(b"\0", 1)[0]

    def _hook(self, uc, address, size, _):
        name, nargs, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        if name == "string_get":  # fills the caller's buffer with a format
            uc.mem_write(args[0], b"Messages %d\0")
            args[0] = "stack"
        elif name == "sprintf":
            args[1] = ("format", self._cstring(uc, args[1]))
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if isinstance(a, int) and E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", self._cstring(uc, a))
        self.log.append((name, tuple(args)))
        values = self.script.get(name)
        eax = values.pop(0) if values else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF if returns else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, ids):
    args = []
    for k in kinds:
        if k == "e":
            args.append(pick(rng, 0x12, 0x13, 0x14, 0x15, 0x16, rng.randrange(0x10, 0x18), rng.getrandbits(32)))
        elif k == "m":
            args.append(pick(rng, 0, 1, 2, rng.getrandbits(32)))
        elif k == "s":
            args.append(pick(rng, 0, 1, 2, 3, rng.randrange(0, 8), rng.getrandbits(32)))
        elif k == "c":
            args.append(rng.randrange(0, 24))
        elif k == "i":
            args.append(pick(rng, *ids, ids[-1], ids[-1], rng.getrandbits(32)))
        elif k == "x":
            args.append(MATRIX + 4 * rng.randrange(0, 8))
        else:
            args.append(rng.getrandbits(32))
    return args


def run_case(m, stubs, name, address, kinds, returns, regions, rng):
    state, script, ids = make_state(rng, name)
    args = make_args(rng, kinds, ids)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        m.write(MATRIX, rbytes(random.Random(len(args)), 0x40))
        for a, data in state:
            m.write(a, data)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(script)
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in regions)
        results.append((r, mem, stubs.log))
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}: args {[hex(x) for x in args]} original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(31)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, returns, regions) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        OUTCOMES.clear()
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, regions, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {OUTCOMES}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
