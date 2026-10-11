#!/usr/bin/env python3
"""Differential test of the AI target-choice helpers (src/game/ai_b.c)
against the original, on a random synthetic world: racing lines with
linked nodes, the 20 candidate slots, cars in their slots, objects,
bodies and hazards.

    tests/emu_ai_b.py EXE build/emu_b.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UcError  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x20000
TILE_DATA = SCRATCH            # *(0x6c1e30); +0x60 the lines
LINES = SCRATCH + 0x100        # 8 lines of 0xc bytes
NODES = SCRATCH + 0x1000       # 16 nodes of 0x10 bytes per line
HAZARDS = SCRATCH + 0x2000     # *(0x6c1eb0): +0xf6 count, +0xf8 records
HAZARD_RECORDS = SCRATCH + 0x2200
PLANS = SCRATCH + 0x2800       # slot +0x64 targets
OBJECT_TABLE = SCRATCH + 0x3000  # *(0x600664): 36 x {object, active}
OBJECTS = SCRATCH + 0x3200     # 36 x 0x40
CARS = SCRATCH + 0x4000        # 8 cars of 0x800
BODIES = SCRATCH + 0x8000      # 32 bodies of 0x200
ARGS = SCRATCH + 0xC000        # argument data

NLINES, NNODES, NCARS = 8, 16, 8
CANDIDATES = (0x6CF180, 0x6CF660)   # set points .. set point limits
GLOBALS = [(0x6C1E30, 4), (0x6BFB20, 4), (0x6C1EB0, 4), (0x600664, 4), (0x6C0A50, 4),
           (0x4B5E54, 4), (0x4B5F98, 4), (0x4B5F9C, 4), (0x4B1960, 4)]
COMPARED = [CANDIDATES, (0x4B7DB0, 0x4B7E50), (0x6D0A80, 0x6D0F00), (0x6BFB80, 0x6C0A50),
            (0x4B5E54, 0x4B5E58), (0x4B5F9C, 0x4B5FA0), (SCRATCH, SCRATCH + SCRATCH_SIZE)]
RACE = SCRATCH + 0xD000        # *(0x4b1960)


def make_world(rng):
    mem = {}
    scratch = bytearray(SCRATCH_SIZE)

    def put(addr, fmt, *v):
        struct.pack_into(fmt, scratch, addr - SCRATCH, *v)

    put(TILE_DATA + 0x60, "<I", LINES)
    for i in range(NLINES):
        count = rng.randrange(2, NNODES + 1)  # a 1-node open line reads garbage in the original
        put(LINES + i * 0xC, "<HBBBBbBI", count, rng.choice([0, 1]), rng.choice([0, 0, 1]),
            rng.choice([0, 1]), 0, rng.randrange(-20, 20), 0, NODES + i * 0x100)
        for j in range(NNODES):
            base = NODES + i * 0x100 + j * 0x10
            put(base, "<3h", *[rng.randrange(-0x200, 0x200) for _ in range(3)])
            scratch[base + 6 - SCRATCH] = rng.choice([0, 0, rng.getrandbits(8)])
            scratch[base + 7 - SCRATCH] = rng.getrandbits(8)
            put(base + 8, "<I", rng.getrandbits(32))
            link = 0
            if rng.randrange(2):
                link |= rng.randrange(0, NLINES) | rng.randrange(0, NNODES) << 5
            if rng.randrange(2):
                link |= rng.randrange(0, NLINES) << 14 | rng.randrange(0, NNODES) << 19
            put(base + 0xC, "<I", link | rng.getrandbits(4) << 28)
    # hazards
    count = rng.randrange(0, 8)
    scratch[HAZARDS + 0xF6 - SCRATCH] = count
    put(HAZARDS + 0xF8, "<I", HAZARD_RECORDS)
    for k in range(count):
        base = HAZARD_RECORDS + k * 8
        put(base, "<HH", rng.randrange(0, NLINES), rng.randrange(0, NNODES))
        scratch[base + 4 - SCRATCH] = rng.choice([0, 1, 4, 2])
        scratch[base + 5 - SCRATCH] = rng.choice([0, 1])
        scratch[base + 6 - SCRATCH] = rng.choice([0, 0x80, 0xFF, rng.getrandbits(8)])
        scratch[base + 7 - SCRATCH] = rng.getrandbits(8)
    # objects
    for i in range(36):
        put(OBJECT_TABLE + i * 8, "<IB", OBJECTS + i * 0x40, rng.choice([0, 1]))
        put(OBJECTS + i * 0x40 + 0x10, "<I", OBJECTS + i * 0x40)
        put(OBJECTS + i * 0x40 + 0x34, "<3i", *[rng.randrange(-0x2000000, 0x2000000) for _ in range(3)])
    # race record
    for off in range(0, 0x600):
        scratch[RACE + off - SCRATCH] = rng.choice([0, 0, 1, rng.getrandbits(8)])
    scratch[RACE + 0x54D - SCRATCH] = rng.randrange(1, NCARS + 1)
    # cars
    for c in range(NCARS):
        base = CARS + c * 0x800
        scratch[base - SCRATCH:base - SCRATCH + 0x800] = bytes(rng.getrandbits(8) for _ in range(0x800))
        put(base + 0x34, "<3i", *[rng.randrange(-0x2000000, 0x2000000) for _ in range(3)])
        put(base + 0x68, "<3i", *[rng.randrange(-0x40000, 0x40000) for _ in range(3)])
        put(base + 0x64E, "<H", c)
        put(base + 0x6B0, "<HH", rng.randrange(0, NLINES), rng.randrange(0, NNODES))
        put(base + 0x654, "<i", rng.randrange(0, 0x40000))
        put(base + 0x658, "<h", rng.randrange(-50, 500))
        put(base + 0x10, "<I", base + 0x700)
        scratch[base + 0x6E1 - SCRATCH] = rng.randrange(0, 5)
        put(PLANS + c * 8, "<I", rng.getrandbits(32))
        scratch[PLANS + c * 8 + 5 - SCRATCH] = rng.randrange(0, NLINES)
    # bodies
    for b in range(32):
        base = BODIES + b * 0x200
        put(base + 0x34, "<3i", *[rng.randrange(-0x2000000, 0x2000000) for _ in range(3)])
        scratch[base + 0x19A - SCRATCH] = rng.choice([0, 5, 1])
        put(base + 0x10, "<I", base + 0x100)
    mem[SCRATCH] = bytes(scratch)

    # slots (car pointer, +0x2c lap, +0x34 distance, +0x64 plan, +0x68 node)
    slots = bytearray(0x6C0A50 - 0x6BFB80)
    for c in range(NCARS):
        o = c * 0x274
        slots[o:o + 0x274] = bytes(rng.getrandbits(8) for _ in range(0x274))
        struct.pack_into("<I", slots, o, CARS + c * 0x800)
        struct.pack_into("<I", slots, o + 0x64, PLANS + c * 8)
        struct.pack_into("<H", slots, o + 0x68, rng.randrange(0, NNODES))
    mem[0x6BFB80] = bytes(slots)

    cands = bytearray(CANDIDATES[1] - CANDIDATES[0])
    cands[:] = bytes(rng.getrandbits(8) for _ in range(len(cands)))
    for i in range(20):
        o = 0x6CF220 - CANDIDATES[0] + i * 0x34
        struct.pack_into("<HB", cands, o, rng.choice([0, rng.randrange(1, NLINES)]), rng.randrange(0, NNODES))
        for f in (0x2C, 0x30):
            struct.pack_into("<i", cands, o + f, rng.randrange(-0x400, 0x400))
    mem[CANDIDATES[0]] = bytes(cands)

    bodies = bytearray(0x6D0F00 - 0x6D0A80)
    for k in range(32):
        struct.pack_into("<I", bodies, k * 12, BODIES + k * 0x200)
    mem[0x6D0A80] = bytes(bodies)
    mem[0x4B7DB0] = bytes(0x4B7E50 - 0x4B7DB0)

    glob = {0x6C1E30: TILE_DATA, 0x6BFB20: LINES, 0x6C1EB0: HAZARDS, 0x600664: OBJECT_TABLE,
            0x6C0A50: rng.choice([0, 100, 0x15E, 1000]), 0x4B5E54: rng.getrandbits(32),
            0x4B5F98: rng.randrange(0, 33), 0x4B5F9C: rng.getrandbits(32), 0x4B1960: RACE}
    for a, v in glob.items():
        mem[a] = struct.pack("<I", v)
    return mem


def car(c):
    return CARS + c * 0x800


def vec(rng, scale=0x10000):
    return [rng.randrange(-scale, scale) for _ in range(3)]


def args_for(name, rng):
    """(args, extra memory writes into ARGS)"""
    blob = bytearray(0x400)
    c = rng.randrange(0, NCARS)
    n = rng.randrange(-2, 20)
    if name == "point_between":
        vs = [vec(rng, 0x1000000) for _ in range(4)]
        for k, v in enumerate(vs):
            struct.pack_into("<3i", blob, k * 12, *v)
        return [ARGS, ARGS + 12, ARGS + 24, ARGS + 36, rng.randrange(-5, 5)], blob
    if name == "car_has_item4":
        return [car(c)], blob
    if name == "line_node_near_end":
        return [rng.randrange(0, NLINES) | rng.getrandbits(16) << 16, rng.randrange(0, 20),
                rng.randrange(0, 20)], blob
    if name == "ai_link_cost":
        return [rng.randrange(0, NLINES), rng.randrange(0, NNODES)], blob
    if name == "ai_candidate_measure":
        struct.pack_into("<3i", blob, 0, *[rng.randrange(-0x2000000, 0x2000000) for _ in range(3)])
        struct.pack_into("<3i", blob, 12, *vec(rng))
        return [rng.randrange(0, 20), ARGS, ARGS + 12, rng.choice([0, 9999, 10000, rng.randrange(0, 0x40000)])], blob
    if name in ("ai_score_line_ends", "ai_score_line_bias", "ai_score_hazards"):
        return [car(c), n], blob
    if name == "ai_candidates_along":
        struct.pack_into("<i", blob, 0, rng.randrange(-1, 20))
        return [rng.randrange(0, NLINES), rng.randrange(0, NNODES), ARGS, rng.getrandbits(32),
                rng.randrange(0, 5)], blob
    if name == "ai_collect_candidates":
        return [car(c), rng.randrange(0, NLINES), rng.randrange(0, NNODES)], blob
    if name == "ai_candidates_clear":
        return [], blob
    if name in ("ai_find_objects_ahead", "ai_find_bodies_ahead"):
        return [car(c)], blob
    if name == "ai_line_cost":
        struct.pack_into("<3h", blob, 0, *[rng.randrange(-0x200, 0x200) for _ in range(3)])
        return [car(c), rng.randrange(0, NLINES), rng.randrange(0, NNODES), ARGS, rng.randrange(0, 40),
                rng.randrange(-50, 500), rng.randrange(0, 5)], blob
    if name == "ai_mark_inside_curve":
        return [car(c), rng.randrange(0, NLINES), rng.randrange(0, NNODES)], blob
    if name == "ai_choose_target":
        return [car(c)], blob
    raise KeyError(name)


TESTS = {
    "point_between": 0x411CB0,
    "car_has_item4": 0x47FEE0,
    "line_node_near_end": 0x47FF70,
    "ai_link_cost": 0x411870,
    "ai_candidate_measure": 0x4119A0,
    "ai_score_line_ends": 0x411D80,
    "ai_score_line_bias": 0x411E90,
    "ai_candidates_along": 0x4120B0,
    "ai_collect_candidates": 0x411F00,
    "ai_candidates_clear": 0x4117A0,
    "ai_score_hazards": 0x42A260,
    "ai_find_objects_ahead": 0x403200,
    "ai_find_bodies_ahead": 0x403A40,
    "ai_line_cost": 0x403620,
    "ai_mark_inside_curve": 0x411B90,
    "ai_choose_target": 0x412290,
}


RETURNS = {"point_between", "car_has_item4", "line_node_near_end", "ai_link_cost", "ai_collect_candidates",
           "ai_line_cost"}


def call(m, address, args):
    esp = E.STACK + E.STACK_SIZE - 0x100
    frame = struct.pack("<I", E.RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args)
    m.uc.mem_write(esp, frame)
    m.uc.reg_write(UC_X86_REG_ESP, esp)
    m.trapped = False
    try:
        m.uc.emu_start(address, E.RETURN, count=20000000)
    except UcError as e:
        return ("error", str(e))
    if m.trapped:
        return ("trap",)
    if m.uc.reg_read(UC_X86_REG_EIP) != E.RETURN:
        return ("cut off",)
    return ("ok", m.uc.reg_read(UC_X86_REG_EAX))


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
    only = sys.argv[4:] or list(TESTS)
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    rng = random.Random(2)
    failed = 0
    for name in only:
        address = TESTS[name]
        bad = errors = cut = 0
        for _ in range(iterations):
            world = make_world(rng)
            args, blob = args_for(name, rng)
            results = []
            for target in (address, m.symbols[name]):
                for a, data in world.items():
                    m.write(a, data)
                m.write(ARGS, bytes(blob))
                r = call(m, target, args)
                if name not in RETURNS and r[0] == "ok":
                    r = ("ok",)
                state = b"".join(m.read(lo, hi - lo) for lo, hi in COMPARED)
                results.append((r, state))
            if results[0][0][0] != "ok" or results[1][0][0] != "ok":
                errors += 1
            if "cut off" in (results[0][0][0], results[1][0][0]):
                cut += 1
            if results[0] != results[1]:
                if bad < 3:
                    print(f"{name}{tuple(hex(a & 0xFFFFFFFF) for a in args)}: original {results[0][0]} "
                          f"ours {results[1][0]}" + ("" if results[0][1] == results[1][1] else " (memory differs)"))
                bad += 1
        note = f" ({errors} runs hit an emulator error in both)" if errors else ""
        note += f" ({cut} CUT OFF)" if cut else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
