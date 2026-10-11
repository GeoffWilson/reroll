#!/usr/bin/env python3
"""Differential test of the AI state handlers and helpers in ai_a.c against
the original, on random car / slot / race records.

    tests/emu_ai_a.py EXE build/emu_a.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH = 0x01000000
CAR, RACE, SLOT, PLAN, SURFACE = (SCRATCH + o for o in (0, 0x1000, 0x2000, 0x2400, 0x2500))
LINES, NODES, TRACK, MARKERS, ENTRIES = (SCRATCH + o for o in (0x3000, 0x3100, 0x3800, 0x3900, 0x3A00))
STATE_SIZE = 0x3C00
TRACK_PTR, MARKERS_PTR = 0x6C1E30, 0x6C1EB0
QUEUE = [(0x4D5808, 4), (0x5D6380, 0x28), (0x5D63C0, 0x28)]
RACE_PTR = 0x4B1960
GLOBALS = [0x4A7148, 0x6C0A50, 0x4A714C, 0x4BDE7C, 0x4B5E50]  # seed, ticks, AI mode, start count, markers tick
SLOTS = (0x6BFB80, 0x274 * 8)

# name: (original, argument kinds, returns?); C = car, S = slot
TESTS = {
    "ai_start_delay": (0x40F980, "C", True),
    "ai_maybe_full_throttle": (0x40F900, "C", False),
    "ai_maybe_fire": (0x410D70, "C", False),
    "ai_fire_kind4": (0x47FF00, "C", True),
    "ai_may_attack": (0x480030, "S", True),
    "ai_state_full_throttle": (0x424D80, "C", False),
    "ai_state_coast": (0x424DD0, "C", True),
    "ai_state_start": (0x424E10, "C", False),
    "ai_stuck_check": (0x46AF10, "C", False),
    "ai_rejoin_plan": (0x47FBB0, "S", False),
    "ai_clear_buttons": (0x4255A0, "C", False),
    "route_reaches": (0x46AD00, "lLn", True),
    "ai_check_route": (0x46AC80, "C", False),
    "ai_markers_update": (0x42A510, "", False),
    "ai_state_rejoin": (0x424FE0, "C", False),
    "ai_state_swerve": (0x425610, "C", True),
    "ai_state_unstick": (0x46AFB0, "C", False),
    "ai_think": (0x4015E0, "C", False),
}


def small(rng, n):
    return rng.randrange(0, n) if rng.randrange(4) else rng.getrandbits(16)


def make_lines(rng):
    lines, nodes = bytearray(0x100), bytearray(0x700)
    for i in range(8):
        struct.pack_into("<HBxBxxxI", lines, i * 0xC, rng.randrange(2, 9), rng.choice([0, 1]), rng.choice([0, 1]),
                         NODES + i * 0x80)
        for j in range(8):
            o = i * 0x80 + j * 0x10
            struct.pack_into("<3h", nodes, o, *[rng.randrange(-0x40, 0x40) for _ in range(3)])
            nodes[o + 7] = rng.getrandbits(8)
            link = 0
            if rng.randrange(3):
                link |= rng.randrange(0, 8) | rng.randrange(0, 8) << 5
            if rng.randrange(3):
                link |= rng.randrange(0, 8) << 14 | rng.randrange(0, 8) << 19
            struct.pack_into("<I", nodes, o + 0xC, link | rng.getrandbits(4) << 28)
    track = bytearray(0x100)
    struct.pack_into("<I", track, 0x60, LINES)
    markers = bytearray(0x100)
    markers[0xF6] = rng.randrange(0, 9)
    struct.pack_into("<I", markers, 0xF8, ENTRIES)
    entries = bytearray()
    for _ in range(8):
        entries += struct.pack("<HHBB2x", rng.randrange(0, 8), rng.randrange(0, 8), rng.choice([2, 4, 1]),
                               rng.choice([0, 1]))
    return lines, nodes, track, markers, entries


def make_state(rng):
    car = bytearray(rng.getrandbits(8) for _ in range(0x800))
    struct.pack_into("<H", car, 0x64E, rng.randrange(0, 8))
    struct.pack_into("<h", car, 0x658, rng.randrange(-50, 400))
    struct.pack_into("<H", car, 0x6B6, small(rng, 0x50))
    car[0x67C] = rng.choice([0, 1, 4, rng.getrandbits(8)])
    car[0x67D] = rng.choice([0, 1, 4, rng.getrandbits(8)])
    car[0x6E2] = rng.choice([rng.randrange(0, 16), rng.getrandbits(8)])
    car[0x6D8] = rng.choice([0, 0x3c, 0x78, 0x96, 0xfe, 0xff, rng.getrandbits(8)])
    car[0x6D9] = rng.choice([0, 0x1e, 0x3c, rng.getrandbits(8)])
    for w in range(4):
        car[0x324 + w * 0xC4] = rng.getrandbits(8)
        struct.pack_into("<I", car, 0x198 + w * 0x1C, rng.choice([0, SURFACE + w * 0x10]))
    race = bytearray(rng.getrandbits(8) for _ in range(0xB00))
    struct.pack_into("<I", race, 0x57C, rng.choice([0, 1, 2, 3, 4, 5, rng.randrange(0, 12)]))
    struct.pack_into("<I", race, 0x580, rng.choice([rng.randrange(0, 0x80), rng.getrandbits(32)]))
    race[0x58C] = rng.choice([0, 1, 2])
    race[0x586] = rng.randrange(0, 6)
    slot = bytearray(rng.getrandbits(8) for _ in range(0x274))
    struct.pack_into("<I", slot, 0, CAR)
    struct.pack_into("<I", slot, 0x64, PLAN)
    slot[0x2C] = rng.randrange(0, 6)
    plan = bytes(rng.getrandbits(8) for _ in range(0x10))
    surface = bytes(rng.getrandbits(8) for _ in range(0x40))
    ticks = rng.choice([rng.randrange(0x80, 0x200), rng.randrange(0, 0x400), rng.getrandbits(32)])
    globals_ = [rng.randrange(0, 0x80000000), ticks, rng.choice([0, 1, 2, 3]), rng.randrange(-1, 4),
                rng.getrandbits(32)]
    slots = bytearray(rng.getrandbits(8) for _ in range(SLOTS[1]))
    for i in range(8):
        struct.pack_into("<I", slots, i * 0x274, CAR)
        struct.pack_into("<I", slots, i * 0x274 + 0x64, PLAN)
        struct.pack_into("<H", slots, i * 0x274 + 0x68, rng.randrange(0, 8))
    car[0x70C] = rng.choice([8, 10])
    struct.pack_into("<HH", car, 0x6B0, rng.randrange(0, 8), rng.randrange(0, 8))
    struct.pack_into("<H", car, 0x64E, rng.randrange(0, 8))
    plan = bytearray(plan)
    plan[5] = rng.randrange(0, 8)
    race[0x54D] = rng.randrange(0, 9)
    return car, race, slot, bytes(plan), surface, globals_, bytes(slots), make_lines(rng)


def run_case(m, name, address, kinds, returns, rng):
    state = make_state(rng)
    if name == "ai_think":
        # keep off the one-in-six step that runs 0x412290 / 0x401780 (other
        # groups, which fault on random records), and use real states
        state[0][0x6C9] = rng.randrange(0, 4)
        struct.pack_into("<H", state[0], 0x6B4, rng.choice([0, 1, 2, 0x20, 0x40, 0x80, 0x100, 0x200, 0x4001]))
    args = [{"C": CAR, "S": SLOT}.get(k) if k in "CS" else rng.randrange(0, 8) | rng.getrandbits(16) << 16
            for k in kinds]
    results = []
    for target in (address, m.symbols[name]):
        car, race, slot, plan, surface, globals_, slots, track = state
        for a, b in zip((LINES, NODES, TRACK, MARKERS, ENTRIES), track):
            m.write(a, bytes(b))
        m.write(TRACK_PTR, struct.pack("<I", TRACK))
        m.write(MARKERS_PTR, struct.pack("<I", MARKERS))
        for a, size in QUEUE:
            m.write(a, bytes(size))
        m.write(CAR, bytes(car))
        m.write(RACE, bytes(race))
        m.write(SLOT, bytes(slot))
        m.write(PLAN, plan)
        m.write(SURFACE, surface)
        m.write(SLOTS[0], slots)
        m.write(RACE_PTR, struct.pack("<I", RACE))
        for a, v in zip(GLOBALS, globals_):
            m.write(a, struct.pack("<I", v & 0xFFFFFFFF))
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, STATE_SIZE) + m.read(*SLOTS) + b"".join(m.read(a, 4) for a in GLOBALS) + \
            b"".join(m.read(a, n) for a, n in QUEUE)
        results.append((r, mem))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})")
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 20000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x10000)
    rng = random.Random(2)
    failed = 0
    for name, (address, kinds, returns) in TESTS.items():
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, name, address, kinds, returns, rng)
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
