#!/usr/bin/env python3
"""Differential test of the AI's reaction to nearby cars (group D:
0x480d70 and helpers, src/game/ai_d.c) against the original, on random
synthetic worlds: linked racing lines, car slots, cars and route records.

    tests/emu_ai_d.py EXE build/emu_d.elf [iterations]
"""
import math
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH = 0x01000000
WORLD, LINES, NODES, INFO, RACE, PLANS, COUNTER, D, OUT, LIST, POS = (
    SCRATCH + o for o in (0, 0x100, 0x200, 0x2000, 0x2100, 0x2800, 0x2900, 0x2910, 0x2940, 0x29a0, 0x2a40))
CARS = SCRATCH + 0x4000
CAR_SIZE = 0x2300
NCARS = 8
SLOTS, SLOT_SIZE = 0x6BFB80, 0x274
NLINES = 6
GLOBALS = [(0x6C1E30, 4), (0x4B1960, 4), (0x6CF180, 0x500), (0x4B7E50, 4), (0x4D5808, 0x90),
           (0x5D6380, 0x70), (SLOTS, SLOT_SIZE * NCARS)]
SCRATCH_SIZE = 0x4000 + CAR_SIZE * NCARS

NAMES = {
    "ai_react_to_cars": 0x480D70, "ai_route_crosses": 0x481A10, "ai_dodge_scores": 0x481800,
    "ai_follow_target": 0x4814F0, "ai_overtake_target": 0x481160, "line_bend": 0x47F9F0,
    "line_side": 0x481C80, "approach_rate": 0x4819C0, "u16_list_contains": 0x47F430,
    "link_queue_push": 0x47F1C0, "link_queue_pop": 0x47F220, "link_queue_empty": 0x47F270,
    "link_queue_reset": 0x47F1B0, "slot_plan_direction": 0x47FCF0, "body_side": 0x47F610,
}


def s32(v):
    return (v + 2**31) % 2**32 - 2**31


def build_world(rng):
    mem = {}
    scratch = bytearray(SCRATCH_SIZE)
    counts = [rng.randrange(3, 13) for _ in range(NLINES)]
    struct.pack_into("<I", scratch, 0x60, LINES)
    nodes_at = []
    node_pos = []
    off = 0
    for l in range(NLINES):
        base = NODES + off
        nodes_at.append(base)
        struct.pack_into("<HBxBxxxI", scratch, LINES - SCRATCH + l * 0xC, counts[l], rng.choice([0, 0, 1]),
                         rng.choice([0, 1]), base)
        x0, step = rng.randrange(-200, 200), rng.randrange(40, 160)
        z0 = rng.randrange(-400, 0)
        pts = []
        for i in range(counts[l]):
            p = (x0 + rng.randrange(-30, 30), rng.randrange(-10, 10), z0 + i * step + rng.randrange(-10, 10))
            pts.append(p)
            o = base - SCRATCH + i * 16
            struct.pack_into("<3h", scratch, o, *p)
            scratch[o + 7] = rng.getrandbits(8)
            link = rng.getrandbits(4) << 28
            if rng.randrange(2):
                ll = rng.randrange(1, NLINES)
                link |= ll | rng.randrange(counts[ll]) << 5
            if rng.randrange(3) == 0:
                ll = rng.randrange(1, NLINES)
                link |= ll << 14 | rng.randrange(counts[ll]) << 19
            struct.pack_into("<I", scratch, o + 12, link)
        node_pos.append(pts)
        off += counts[l] * 16
    struct.pack_into("<H", scratch, INFO - SCRATCH + 0x32, rng.randrange(4, 40))
    count = rng.randrange(2, NCARS + 1)
    scratch[RACE - SCRATCH + 0x54D] = count
    slots = bytearray(SLOT_SIZE * NCARS)
    cluster = rng.choice(node_pos)[0]
    for c in range(NCARS):
        car = CARS + c * CAR_SIZE
        o = car - SCRATCH
        l = rng.randrange(NLINES)
        n = rng.randrange(counts[l])
        near = node_pos[l][n] if rng.randrange(2) else cluster
        pos = [(near[i] + rng.randrange(-120, 120)) * 0x10000 + rng.getrandbits(16) for i in range(3)]
        struct.pack_into("<3i", scratch, o + 0x34, *[s32(p) for p in pos])
        vel = [rng.randrange(-0x8000, 0x8000), rng.randrange(-0x1000, 0x1000), rng.randrange(-0x8000, 0x60000)]
        struct.pack_into("<3i", scratch, o + 0x68, *vel)
        a = rng.gauss(0, 0.25) if rng.randrange(6) else rng.uniform(-3.2, 3.2)
        c_, s_ = int(math.cos(a) * 4096), int(math.sin(a) * 4096)
        struct.pack_into("<9h", scratch, o + 0xC4, c_, 0, s_, 0, 4096, 0, -s_, 0, c_)
        struct.pack_into("<I", scratch, o + 0x10, INFO)
        struct.pack_into("<i", scratch, o + 0x634, rng.randrange(-0xA000, 0xA000))
        struct.pack_into("<H", scratch, o + 0x64E, c)
        struct.pack_into("<i", scratch, o + 0x654, rng.randrange(0, 0x40000))
        struct.pack_into("<h", scratch, o + 0x658, rng.randrange(0, 600))
        struct.pack_into("<i", scratch, o + 0x6AC, rng.choice([-1, -1, 0, 3]))
        struct.pack_into("<HH", scratch, o + 0x6B0, l, n)
        scratch[o + 0x6B9] = rng.choice([0, 0, 1, 2])
        scratch[o + 0x6BA] = rng.choice([0, 1])
        scratch[o + 0x6BB] = rng.choice([0, 0, 1])
        struct.pack_into("<I", scratch, o + 0x6DC, rng.getrandbits(32) | rng.choice([0, 0x23]))
        scratch[o + 0x6E1] = rng.randrange(0, 5)
        struct.pack_into("<I", scratch, o + 0x6E8, rng.getrandbits(32))
        struct.pack_into("<8H", scratch, o + 0x6EC, *[rng.getrandbits(16) for _ in range(8)])
        scratch[o + 0x70C] = rng.choice([8, 8, 10, 9])
        struct.pack_into("<i", scratch, o + 0x2280, rng.randrange(0, 4))
        so = c * SLOT_SIZE
        struct.pack_into("<I", slots, so, car)
        slots[so + 0x2C] = rng.randrange(0, 3)
        struct.pack_into("<i", slots, so + 0x34, rng.randrange(0, 0x10000))
        plan = PLANS + c * 8
        pl = rng.randrange(NLINES)
        scratch[plan - SCRATCH + 5] = pl
        struct.pack_into("<I", slots, so + 0x64, plan)
        struct.pack_into("<H", slots, so + 0x68, rng.randrange(counts[pl]))
        slots[so + 0x259] = rng.getrandbits(8)
    routes = bytearray(rng.getrandbits(8) for _ in range(0x500))
    for k in range(20):
        o = 0xA0 + k * 0x34
        struct.pack_into("<H", routes, o, rng.choice([0, 1, 2, 3, 4, 5, rng.randrange(NLINES)]))
        vec = [rng.randrange(-0x10000, 0x10000) for _ in range(3)]
        struct.pack_into("<3i", routes, o + 4, *vec)
        routes[o + 0x26] = rng.choice([0, 0, 1])
        struct.pack_into("<i", routes, o + 0x2C, rng.randrange(-1000, 1000))
    scratch[COUNTER - SCRATCH:COUNTER - SCRATCH + 4] = struct.pack("<i", rng.choice([-1, 0, 5, 19]))
    for i in range(32):
        struct.pack_into("<H", scratch, LIST - SCRATCH + i * 2, rng.choice([0, 1, 2, 3, rng.getrandbits(16)]))
    mem[0x6C1E30] = struct.pack("<I", WORLD)
    mem[0x4B1960] = struct.pack("<I", RACE)
    mem[0x6CF180] = bytes(routes)
    mem[0x4B7E50] = struct.pack("<I", rng.choice([0, 3, 19, 20]))
    queue = bytearray(rng.getrandbits(8) for _ in range(0x90))
    struct.pack_into("<HH", queue, 0, rng.randrange(20), rng.randrange(20))
    mem[0x4D5808] = bytes(queue)
    mem[0x5D6380] = bytes(rng.randrange(0, 6) for _ in range(0x70))
    mem[SLOTS] = bytes(slots)
    mem[SCRATCH] = scratch
    return mem, counts, count


def make_args(rng, name, counts, count):
    a, b = rng.randrange(NCARS), rng.randrange(NCARS)
    car, other = CARS + a * CAR_SIZE, CARS + b * CAR_SIZE
    sa, sb = SLOTS + a * SLOT_SIZE, SLOTS + b * SLOT_SIZE
    l = rng.randrange(NLINES)
    n = rng.randrange(counts[l]) | (rng.getrandbits(16) << 16 if rng.randrange(3) == 0 else 0)
    dist = rng.choice([rng.randrange(0, 600), rng.randrange(0, 2100)])
    small = lambda: rng.choice([rng.randrange(-3000, 3000), rng.randrange(-0x200000, 0x200000), E.rand_int(rng)])
    return {
        "ai_react_to_cars": [car, COUNTER],
        "ai_route_crosses": [car],
        "ai_dodge_scores": [car, other, D, dist, rng.randrange(-0x2000, 0x20000), rng.randrange(0, NLINES)],
        "ai_follow_target": [sa, sb, D, rng.randrange(0, 0x5000000), rng.randrange(4, 40), small(), small(), COUNTER],
        "ai_overtake_target": [D, dist, small(), sa, sb, small(), COUNTER],
        "line_bend": [l, n, OUT, OUT + 12, OUT + 24],
        "line_side": [l, n, rng.getrandbits(32), POS],
        "approach_rate": [E.rand_int(rng), E.rand_int(rng)],
        "u16_list_contains": [rng.choice([0, 1, 2, 3, rng.getrandbits(16)]) | (rng.getrandbits(16) << 16),
                              LIST, rng.randrange(0, 33)],
        "link_queue_push": [rng.getrandbits(32), rng.getrandbits(32)],
        "link_queue_pop": [OUT, OUT + 2],
        "link_queue_empty": [],
        "link_queue_reset": [],
        "slot_plan_direction": [OUT, sa],
        "body_side": [OUT, car],
    }[name], car, other


def call(m, address, args):
    """E.Machine.call with a far larger instruction budget (the car loop
    with its line walks runs well past 100000), reporting a cut-off."""
    from unicorn import UcError
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
    esp = E.STACK + E.STACK_SIZE - 0x100
    m.uc.mem_write(esp, struct.pack("<I", E.RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args))
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


def snapshot(m):
    return b"".join(m.read(a, s) for a, s in GLOBALS) + m.read(SCRATCH, SCRATCH_SIZE)


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, (SCRATCH_SIZE + 0xFFFF) & ~0xFFFF)
    rng = random.Random(7)
    failed = 0
    for name, address in NAMES.items():
        bad = 0
        for _ in range(iterations):
            mem, counts, count = build_world(rng)
            args, car, other = make_args(rng, name, counts, count)
            scratch = mem[SCRATCH]
            cp = struct.unpack_from("<3i", scratch, car - SCRATCH + 0x34)
            op = struct.unpack_from("<3i", scratch, other - SCRATCH + 0x34)
            d = [s32(op[i] - cp[i] + (rng.randrange(-0x40000, 0x40000) if rng.randrange(4) == 0 else 0))
                 for i in range(3)]
            struct.pack_into("<3i", scratch, D - SCRATCH, *d)
            struct.pack_into("<3i", scratch, POS - SCRATCH,
                             *[s32(cp[i] + rng.randrange(-0x2000000, 0x2000000)) for i in range(3)])
            mem[SCRATCH] = bytes(scratch)
            results = []
            for target in (address, m.symbols[name]):
                for a, data in mem.items():
                    m.write(a, data)
                r = call(m, target, args)
                if r[0] == "ok" and name in VOID:
                    r = ("ok",)
                results.append((r, snapshot(m)))
            if results[0] != results[1]:
                bad += 1
                if bad <= 3:
                    diff = next((i for i in range(len(results[0][1])) if results[0][1][i] != results[1][1][i]), None)
                    print(f"{name}{tuple(hex(a & 0xFFFFFFFF) for a in args)}: original {results[0][0]} "
                          f"ours {results[1][0]} first memory diff at snapshot offset {diff}")
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


VOID = {"ai_react_to_cars", "ai_dodge_scores", "ai_follow_target", "ai_overtake_target", "link_queue_push",
        "link_queue_pop", "link_queue_reset", "slot_plan_direction", "body_side"}


if __name__ == "__main__":
    main()
