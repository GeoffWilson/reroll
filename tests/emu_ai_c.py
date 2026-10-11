#!/usr/bin/env python3
"""Differential test of the AI weapon logic (0x401780 and its tree, src/game/ai_c.c)
against the original, on random synthetic races: cars and their slots,
racing lines, scenery entries, pickups and targets.

    tests/emu_ai_c.py EXE build/emu_c.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

S = 0x01000000
SIZE = 0x10000
RACE, CARS, HOLDER, LINES, NODES = S, S + 0x1000, S + 0x5000, S + 0x5100, S + 0x5200
RULES, ARR, ENTRIES, SHAPES, OBJECTS, OBJTABLE = (S + o for o in (0x6000, 0x6100, 0x6200, 0x6400, 0x6800, 0x7000))
PICKUPS, TTABLE, TARGETS, SLOT84 = S + 0x7100, S + 0x9800, S + 0x9900, S + 0x9C00
CAR_SIZE, SLOTS, SLOT_SIZE = 0x800, 0x6BFB80, 0x274
NLINES, NNODES = 4, 16
MAXCARS = 6  # the slot table ends at the race clock (0x6c0a50)
GLOBALS = [(0x4B1960, 4), (0x6C1E30, 4), (0x6C1EB0, 4), (0x600664, 4), (0x5F72A4, 4), (0x4B5E54, 1),
           (0x6D0E00, 8), (0x6D0E40, 32), (0x6B9CE0, 96), (0x6C0A50, 4), (0x4A714C, 4), (0x6D0ED0, 4),
           (0x4A7148, 4), (0x5D9B20, 4), (0x6B9D42, 1), (SLOTS, MAXCARS * SLOT_SIZE)]

# name: (original, argument kinds) - c = a car, f = flag byte, t = timer,
# L = racing line, k = node, o = 2-byte out, e = scenery entry, s = short out
TESTS = {
    "ai_weapons": (0x401780, "c"),
    "ai_weapon0_ready": (0x401CA0, "cf"),
    "ai_weapon1_ready": (0x4024A0, "ct"),
    "ai_weapon2_ready": (0x402830, "ct"),
    "ai_weapon3_ready": (0x401EA0, "cft"),
    "ai_weapon4_ready": (0x402ED0, "cf"),
    "ai_weapon5_ready": (0x401C90, "c"),
    "ai_weapon6_ready": (0x401C70, "ct"),
    "ai_weapon7_ready": (0x402190, "cft"),
    "ai_corner_ahead": (0x402E50, "c"),
    "line_node_ahead": (0x47F550, "kLno"),
    "scenery_position": (0x402750, "es"),
    "ai_press_rear_fire": (0x47F330, "c"),
    "ai_press_fire_a": (0x47F350, "c"),
    "ai_press_fire_b": (0x47F370, "c"),
}
VOID = {"ai_weapons", "scenery_position", "ai_press_rear_fire", "ai_press_fire_a", "ai_press_fire_b"}


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def build_world(rng):
    mem = bytearray(SIZE)
    glob = {}

    def put(addr, fmt, *v):
        struct.pack_into(fmt, mem, addr - S, *v)

    n = rng.randrange(1, MAXCARS + 1)
    laps = rng.randrange(1, 5)
    for a in range(0, 0x1000, 4):
        put(RACE + a, "<I", rng.getrandbits(32))
    mem[0x54D] = n
    mem[0x586] = laps
    put(RACE + 0x57C, "<i", rng.choice([3, 4, 5, 6, 6, 7]))
    put(RACE + 0x580, "<i", rng.choice([50, 200, 400, rng.randrange(0, 500)]))
    mem[0x58C] = rng.choice([0, 1])
    # racing lines
    put(HOLDER + 0x60, "<I", LINES)
    for j in range(NLINES):
        count = rng.randrange(2, NNODES + 1)
        rec = LINES + j * 0xC
        put(rec, "<HBBBBH", count, rng.choice([0, 1]), 0, rng.choice([0, 1]), j, 0)
        put(rec + 8, "<I", NODES + j * NNODES * 0x10)
        base = [rng.randrange(-3000, 3000) for _ in range(3)]
        step = [rng.randrange(-200, 200) for _ in range(3)]
        for k in range(NNODES):
            node = NODES + (j * NNODES + k) * 0x10
            jitter = 0 if rng.randrange(3) else 20
            put(node, "<3h", *[max(-0x7FFF, min(0x7FFF, base[c] + step[c] * k + rng.randrange(-jitter - 1, jitter + 1)))
                               for c in range(3)])
            mem[node - S + 7] = rng.getrandbits(8) if rng.randrange(12) == 0 else 0xFF
    # cars and slots
    slots = bytearray(MAXCARS * SLOT_SIZE)
    leader = rng.randrange(-1, n)
    centre = [rng.randrange(-2000, 2000) for _ in range(3)]
    for i in range(MAXCARS):
        car = CARS + i * CAR_SIZE
        for a in range(0, CAR_SIZE, 4):
            put(car + a, "<I", rng.getrandbits(32))
        put(car + 0x34, "<3i", *[((centre[c] + rng.randrange(-1500, 1500)) << 16) + rng.getrandbits(16) for c in range(3)])
        put(car + 0x68, "<3i", *[rng.randrange(-0x40000, 0x40000) for _ in range(3)])
        put(car + 0xC4, "<9h", *[rng.randrange(-4096, 4097) for _ in range(9)])
        put(car + 0x11C, "<B", rng.randrange(0, 4))
        put(car + 0x11E, "<h", rng.randrange(-20, 20))
        put(car + 0x64E, "<H", i)
        put(car + 0x654, "<i", rng.randrange(0, 0x80000))
        put(car + 0x658, "<h", rng.choice([0, 40, 60, 150, rng.randrange(-50, 400)]))
        put(car + 0x67C, "<bb", rng.choice([-1, 0, 1, 2, 3, 4, 5, 6, 7, 0x20, -5]),
            rng.choice([-1, 0, 1, 2, 3, 4, 5, 6, 7, 0x21]))
        put(car + 0x6AC, "<i", rng.choice([-1, 0, 3]))
        put(car + 0x6B0, "<HH", rng.randrange(NLINES), rng.randrange(NNODES))
        put(car + 0x6CB, "<B", rng.choice([0, 0, 0, rng.getrandbits(8), 1 << rng.randrange(8)]))
        put(car + 0x6D0, "<i", rng.choice([5000, 20000]))
        put(car + 0x6D4, "<hh", rng.choice([-30, -1, 0, 40, 60, 120, rng.randrange(-100, 300)]),
            rng.choice([-30, -1, 0, 40, 60, 120, rng.randrange(-100, 300)]))
        put(car + 0x6E1, "<B", rng.choice([0, 1, 2, 3, 4, rng.getrandbits(8)]))
        for k in range(8):
            put(car + 0x6EC + 2 * k, "<H", rng.randrange(0, 0x4000))
        put(car + 0x70C, "<B", rng.choice([8, 8, 9, 10]))
        if rng.randrange(2):
            put(car + 0x6FC, "<I", 0)
            put(car + 0x700, "<I", 0)
            put(car + 0x6B4, "<I", rng.getrandbits(32) & ~0x20)
        sl = i * SLOT_SIZE
        struct.pack_into("<I", slots, sl, car)
        slots[sl + 0x2C] = rng.randrange(0, laps + 1)
        slots[sl + 0x2D] = 1 if i == leader else rng.choice([0, 0, 0, 2])
        struct.pack_into("<i", slots, sl + 0x34, rng.randrange(0, 0x10000))
        struct.pack_into("<I", slots, sl + 0x64, LINES + rng.randrange(NLINES) * 0xC)
        struct.pack_into("<H", slots, sl + 0x68, rng.randrange(NNODES))
        struct.pack_into("<H", slots, sl + 0x7A, rng.choice([0, 1, 1, 2, 2]))
        struct.pack_into("<3i", slots, sl + 0x6C, *[((centre[c] + rng.randrange(-3000, 3000)) << 16) for c in range(3)])
        struct.pack_into("<i", slots, sl + 0x80, rng.choice([5, 6, 7]))
        struct.pack_into("<I", slots, sl + 0x84, SLOT84)
        struct.pack_into("<i", slots, sl + 0x8C, rng.choice([4, 4, 4, 1]))
        slots[sl + 0x259] = rng.choice([0, 0, 0, 0, 1])
    # line some cars up along their line, just before a node
    for i in range(MAXCARS):
        if rng.randrange(3):
            continue
        car = CARS + i * CAR_SIZE
        sl = i * SLOT_SIZE
        rec = struct.unpack_from("<I", slots, sl + 0x64)[0]
        node = struct.unpack_from("<H", slots, sl + 0x68)[0]
        count, rev = struct.unpack_from("<HB", mem, rec - S)
        node %= count
        struct.pack_into("<H", slots, sl + 0x68, node)
        struct.pack_into("<HH", mem, car - S + 0x6B0, (rec - LINES) // 0xC, node)
        nodes = struct.unpack_from("<I", mem, rec - S + 8)[0]
        a = struct.unpack_from("<3h", mem, nodes - S + node * 0x10)
        b = struct.unpack_from("<3h", mem, nodes - S + ((node + (-1 if rev else 1)) % count) * 0x10)
        d = [b[c] - a[c] for c in range(3)]
        length = sum(x * x for x in d) ** 0.5 or 1
        put(car + 0xC8, "<h", int(d[0] / length * 4000))
        put(car + 0xCE, "<h", int(d[1] / length * 4000))
        put(car + 0xD4, "<h", int(d[2] / length * 4000))
        put(car + 0x34, "<3i", *[((a[c] - int(d[c] / length * 30)) << 16) for c in range(3)])
    # if the leader gaps are close, cars are interesting: bunch some distances
    if rng.randrange(2):
        d = rng.randrange(0, 0x10000)
        for i in range(n):
            struct.pack_into("<i", slots, i * SLOT_SIZE + 0x34, (d + rng.randrange(-7000, 7000)) & 0xFFFF)
            slots[i * SLOT_SIZE + 0x2C] = laps - 1 if rng.randrange(4) else laps
    put(SLOT84 + 0x192, "<H", rng.choice([7, 9]))
    # rules, scenery
    nrules = rng.randrange(0, 6)
    put(RULES + 0xEC, "<I", ENTRIES)
    mem[RULES - S + 0xF6] = nrules
    put(RULES + 0xF8, "<I", ARR)
    for k in range(nrules):
        put(ARR + k * 8, "<IBBBB", rng.choice([7, 9, 11]), rng.choice([1, 1, 0]), rng.choice([0, 1]), rng.getrandbits(8), 0)
    nent = rng.randrange(0, 6)
    for k in range(nent):
        shape, obj = SHAPES + k * 0x10, OBJECTS + k * 0x200
        put(ENTRIES + k * 0x14, "<II", shape, obj)
        put(shape, "<hhh", *[rng.randrange(-300, 300) for _ in range(3)])
        put(shape + 0xC, "<I", rng.getrandbits(32))
        put(obj + 0x34, "<3i", *[((centre[c] + rng.randrange(-2500, 2500)) << 16) for c in range(3)])
        put(obj + 0x192, "<H", rng.choice([7, 9, 11, 13]))
        put(obj + 0x194, "<I", OBJTABLE)
        mem[obj - S + 0x199] = rng.getrandbits(8)
    for k in range(16):
        put(OBJTABLE + 4 * k, "<I", rng.choice([0x5000000, 0x1000000, 0, rng.getrandbits(32)]))
    # pickups
    pickups = b""
    for k in range(24):
        p = PICKUPS + k * 0x1A0
        pickups += p32(p)
        put(p + 0x184, "<ii", rng.choice([1, 1, 0]), rng.choice([0, 1]))
        put(p + 0x190, "<i", rng.choice([5, 6, 7, 8]))
        if rng.randrange(8) == 0:
            src = rng.randrange(n) * SLOT_SIZE
            put(p + 0x194, "<3i", *struct.unpack_from("<3i", slots, src + 0x6C))
        else:
            put(p + 0x194, "<3i", *[rng.getrandbits(31) for _ in range(3)])
    # targets
    n2 = rng.randrange(0, 5)
    for k in range(8):
        put(TTABLE + 8 * k, "<I", TARGETS + k * 0x40)
        put(TARGETS + k * 0x40 + 0x34, "<3i", *[((centre[c] + rng.randrange(-3000, 3000)) << 16) for c in range(3)])
    glob[0x4B1960] = p32(RACE)
    glob[0x6C1E30] = p32(HOLDER)
    glob[0x6C1EB0] = p32(RULES)
    glob[0x600664] = p32(TTABLE)
    glob[0x5F72A4] = p32(nent)
    glob[0x4B5E54] = bytes([n2])
    glob[0x6D0E00] = bytes(rng.randrange(8) for _ in range(8))
    glob[0x6D0E40] = b"".join(p32(rng.choice([1000, 9000000, 9000001, 20000000])) for _ in range(8))
    glob[0x6B9CE0] = pickups
    glob[0x6C0A50] = p32(rng.choice([100, 349, 350, 5000]))
    glob[0x4A714C] = p32(rng.choice([0, 1, 2]))
    glob[0x6D0ED0] = p32(rng.choice([0, 1, 2, 3, 4]))
    glob[0x4A7148] = p32(rng.randrange(1, 0x80000000))
    glob[0x5D9B20] = p32(rng.choice([0, 1]))
    glob[0x6B9D42] = bytes([rng.choice([0, 0, 1])])
    glob[SLOTS] = bytes(slots)
    return mem, glob, n


def run_case(m, name, address, kinds, rng):
    mem, glob, n = build_world(rng)
    car = CARS + rng.randrange(n) * CAR_SIZE
    args = []
    for k in kinds:
        if k == "c":
            args.append(car)
        elif k == "f":
            args.append(rng.choice([0, 1, 1, 2]) | (rng.getrandbits(24) << 8 if rng.randrange(2) else 0))
        elif k == "t":
            args.append(rng.choice([0, 40, 0x32, 60, 101, -5, rng.randrange(-200, 400)]) & 0xFFFF |
                        (rng.getrandbits(16) << 16 if rng.randrange(2) else 0))
        elif k == "k":
            args.append(rng.randrange(NNODES) | (rng.getrandbits(16) << 16 if rng.randrange(2) else 0))
        elif k == "L":
            args.append(LINES + rng.randrange(NLINES) * 0xC)
        elif k == "n":
            args.append(rng.choice([0, 1, 3, 10, rng.getrandbits(8)]) | (rng.getrandbits(24) << 8))
        elif k == "o":
            args.append(S + 0xFF00)
        elif k == "e":
            args.append(ENTRIES + rng.randrange(max(1, struct.unpack_from("<I", glob[0x5F72A4])[0])) * 0x14)
        elif k == "s":
            args.append(S + 0xFF10)
    results = []
    for target in (address, m.symbols[name]):
        m.write(S, bytes(mem))
        for a, data in glob.items():
            m.write(a, data)
        r = m.call(target, args)
        if name in VOID and r[0] == "ok":
            r = ("ok",)
        state = m.read(S, SIZE) + b"".join(m.read(a, size) for a, size in GLOBALS)
        results.append((r, state))
    if any(r[0][0] == "cut off" for r in results):
        return f"{name}{tuple(hex(a & 0xFFFFFFFF) for a in args)}: cut off {results[0][0]} {results[1][0]}", 0
    if results[0] != results[1]:
        diff = ""
        if results[0][1] != results[1][1]:
            a, b = results[0][1], results[1][1]
            i = next(i for i in range(len(a)) if a[i] != b[i])
            diff = f" (memory differs at state offset {i:#x})"
        return f"{name}{tuple(hex(a & 0xFFFFFFFF) for a in args)}: original {results[0][0]} ours {results[1][0]}{diff}", 0
    fired = results[0][0] == ("ok", 1) or (name in VOID and m.read(car + 0x700, 4) != mem[car - S + 0x700:car - S + 0x704])
    return None, int(bool(fired))


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, SIZE)
    rng = random.Random(3)
    failed = 0
    for name, (address, kinds) in TESTS.items():
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = fired = 0
        for _ in range(iterations):
            msg, hit = run_case(m, name, address, kinds, rng)
            fired += hit
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} ({fired} fired / true / pressed)")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
