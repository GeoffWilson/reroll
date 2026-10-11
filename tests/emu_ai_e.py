#!/usr/bin/env python3
"""Differential test of the AI route-target functions (src/game/ai_e.c)
against the original, on random synthetic worlds: a route, racing lines,
hazards, obstacles (convex parts), pickups, weapon items and markers.

    tests/emu_ai_e.py EXE build/emu_e.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

S = 0x01000000
CAR, CARMODEL, RACE, WORLD, MARKW, MARKS, LINES, NODES = (S + o for o in (
    0x0, 0x800, 0x900, 0xF00, 0x1000, 0x1100, 0x1300, 0x1400))
PLANS, HAZ, HOBJ, HMODEL, OBJ, OMODEL, OVERTS, OPLANES = (S + o for o in (
    0x2000, 0x2100, 0x2200, 0x2400, 0x2600, 0x2E00, 0x3200, 0x3400))
PARTS, PLIST, PICK, PICKLIST, ITEMS, ITA, ITB, ITT, ITW, ARGS = (S + o for o in (
    0x3600, 0x3800, 0x3900, 0x4900, 0x4A00, 0x4B00, 0x4C00, 0x5C00, 0x6000, 0x7000))
SIZE = 0x8000
NODE_COUNT = 24

GLOBALS = [  # (address, size): exe globals written by the generator and compared
    (0x6CF180, 0x4E0),   # targets, route, target lifetimes
    (0x4B7E50, 1),       # target count
    (0x6BFB80, 8 * 0x274),
    (0x4B1960, 4), (0x6C1E30, 4), (0x6C1EB0, 4),
    (0x4B5E54, 1), (0x6D0E00, 8), (0x600664, 4), (0x6D0E40, 32),
    (0x4B5F9C, 4), (0x6D0DC0, 8), (0x6D0A80, 48), (0x6D0CE0, 16),
    (0x6B8CE0, 8), (0x5F72A4, 4),
]


def s16(v):
    return (v + 0x8000) % 0x10000 - 0x8000


def s32(v):
    return (v + 0x80000000) % 0x100000000 - 0x80000000


class World:
    def __init__(self, rng):
        self.mem = bytearray(SIZE)
        self.glob = {a: bytearray(n) for a, n in GLOBALS}
        self.rng = rng
        r = rng
        self.line_max = r.choice([2, 3, 7])
        self.node_max = r.choice([2, 4, NODE_COUNT])

        def put(addr, fmt, *v):
            for a, n in GLOBALS:
                if a <= addr < a + n:
                    struct.pack_into(fmt, self.glob[a], addr - a, *v)
                    return
            struct.pack_into(fmt, self.mem, addr - S, *v)
        self.put = put

        def near(scale=900):
            return r.randrange(-scale, scale)
        self.near = near
        # race, world, lines, nodes
        put(0x4B1960, "<I", RACE)
        put(RACE + 0x586, "<B", r.randrange(0, 4))
        put(0x6C1E30, "<I", WORLD)
        put(WORLD + 0x60, "<I", LINES)
        put(WORLD + 8, "<I", MARKW)
        for line in range(8):
            put(LINES + line * 0xC, "<HBxBxxxI", NODE_COUNT, r.randrange(2), r.randrange(2),
                NODES + line * NODE_COUNT * 16)
            for node in range(NODE_COUNT):
                a = NODES + (line * NODE_COUNT + node) * 16
                put(a, "<3h", near(), near(200), near())
                flags = r.getrandbits(32) if r.randrange(4) == 0 else (
                    r.choice([0, 4, 0x100, 0x200, 0x400, 0x704]) | r.getrandbits(4) << 28)
                put(a + 8, "<I", flags)
                put(a + 12, "<I", r.randrange(8) | r.randrange(8) << 14 | r.getrandbits(32) & 0xFFE03FE0)
        # car slots and plans
        for i in range(8):
            slot = 0x6BFB80 + i * 0x274
            put(slot, "<I", CAR if i == 0 else CAR)
            put(slot + 0x2C, "<B", r.randrange(0, 4))
            put(slot + 0x64, "<I", PLANS + i * 8)
            put(slot + 0x68, "<H", r.randrange(self.node_max))
            put(PLANS + i * 8 + 5, "<B", r.randrange(self.line_max + 1))
        # car
        put(CAR + 0x10, "<I", CARMODEL)
        put(CARMODEL + 0x32, "<H", r.choice([0, 20, 60, r.randrange(0x10000)]))
        put(CAR + 0x34, "<3i", *[near(300) << 16 | r.getrandbits(16) for _ in range(3)])
        put(CAR + 0x64E, "<H", r.randrange(8))
        put(CAR + 0x658, "<h", r.choice([0, 50, 99, 100, 300, r.randrange(-0x8000, 0x8000)]))
        put(CAR + 0x67C, "<BB", r.choice([0xFF, 1]), r.choice([0xFF, 2]))
        put(CAR + 0x6CA, "<b", r.choice([-1, r.randrange(8)]))
        put(CAR + 0x6CC, "<i", r.choice([r.randrange(-0x10000, 0x10000), r.randrange(-0x3000, 0x3000),
                                          r.getrandbits(31) - 2**30]))
        put(CAR + 0x6DC, "<I", r.getrandbits(32) if r.randrange(3) else 0xFFFFFFFF)
        put(CAR + 0x6E1, "<B", r.randrange(6))
        put(CAR + 0x6E8, "<I", r.getrandbits(32))
        # route
        for k in range(20):
            a = 0x6CF220 + k * 0x34
            line = r.choice([0, r.randrange(1, self.line_max + 1), r.randrange(1, self.line_max + 1)])
            put(a, "<HBB", line, r.randrange(self.node_max), r.getrandbits(8))
            v = [r.gauss(0, 1) for _ in range(3)]
            n = sum(c * c for c in v) ** 0.5 or 1
            put(a + 0x10, "<3i", *[int(c / n * 0x10000) for c in v])
            put(a + 0x25, "<BB", r.getrandbits(8), 0 if r.randrange(4) else 1)
            put(a + 0x2C, "<2i", r.randrange(-500, 500), r.randrange(-500, 500))
        for i in range(20):
            put(0x6CF180 + i * 8, "<3h", near(), near(), near())
        put(0x4B7E50, "<B", r.choice([0, 1, 5, 18, 19, 20]))
        # hazards
        put(0x4B5E54, "<B", r.randrange(0, 5))
        for i in range(8):
            put(0x6D0E00 + i, "<B", r.randrange(8))
            put(HAZ + i * 8, "<IxxBx", HOBJ + i * 0x40, r.randrange(5))
            put(HOBJ + i * 0x40 + 0x10, "<I", HMODEL + i * 0x40)
            put(HMODEL + i * 0x40 + 0x32, "<H", r.choice([10, 50, 150, r.randrange(0x10000)]))
            put(HOBJ + i * 0x40 + 0x34, "<3i", *[near(600) << 16 | r.getrandbits(16) for _ in range(3)])
            put(0x6D0E40 + i * 4, "<i", r.choice([r.randrange(0, 400000), r.randrange(0, 4000), r.getrandbits(31)]))
        put(0x600664, "<I", HAZ)
        # obstacles
        put(0x4B5F9C, "<I", r.randrange(0, 5))
        for i in range(4):
            put(0x6D0DC0 + i, "<B", r.randrange(4))
            obj, model = OBJ + i * 0x200, OMODEL + i * 0x100
            parts = r.randrange(0, 5)
            put(0x6D0A80 + i * 12, "<3I", obj, parts, PLIST + i * 16)
            put(0x6D0CE0 + i * 4, "<i", r.choice([r.randrange(0, 100000), r.randrange(0, 3000000), r.getrandbits(31)]))
            put(obj + 0x10, "<I", model)
            put(obj + 0x2C, "<I", OPLANES + i * 0x80)
            put(obj + 0x34, "<3i", *[near(400) << 16 | r.getrandbits(16) for _ in range(3)])
            put(obj + 0x199, "<BB", r.getrandbits(8), r.choice([0, 1, 5]))
            put(model + 0x32, "<H", r.choice([20, 100, 400, r.randrange(0x10000)]))
            put(model + 0x38, "<H", 0x40)
            put(model + 0x40, "<I", OVERTS + i * 0x80 + 8)
            for f in range(0x48, 0x100):
                put(model + f, "<B", r.randrange(16))
            for v in range(16):
                put(OVERTS + i * 0x80 + v * 8, "<3h", near(80), near(80), near(80))
                n = [r.randrange(-511, 512) for _ in range(3)]
                put(OPLANES + i * 0x80 + v * 8, "<BxxxI", r.randrange(16),
                    (n[0] & 0x3FF) << 20 | (n[1] & 0x3FF) << 10 | (n[2] & 0x3FF) | r.getrandbits(2) << 30)
            for p in range(4):
                part = PARTS + (i * 4 + p) * 0x20
                put(PLIST + i * 16 + p * 4, "<I", 0 if r.randrange(12) == 0 else part)
                put(part + 0x10, "<I", r.choice([0, r.randrange(1, 9)]) << 16 | r.randrange(4) |
                    r.getrandbits(3) << 21)
        # pickups
        put(0x6B8CE0, "<Ii", PICKLIST, r.randrange(0, 7))
        for i in range(8):
            p = PICK + i * 0x200
            put(PICKLIST + i * 4, "<I", p)
            put(p + 0x182, "<H", r.choice([0x81, 0x82, 0x80, 0x03, r.getrandbits(16)]))
            put(p + 0x18C, "<HH", r.randrange(self.line_max + 1), r.randrange(self.node_max))
            put(p + 0x34, "<3i", *[near() << 16 | r.getrandbits(16) for _ in range(3)])
        # weapon items
        put(0x6C1EB0, "<I", ITW)
        put(ITW + 0xEC, "<I", ITEMS)
        put(0x5F72A4, "<I", r.randrange(0, 7))
        for i in range(8):
            it = ITEMS + i * 20
            put(it, "<II3hHB", ITA + i * 16, ITB + i * 0x200, near(), near(), near(),
                r.randrange(self.node_max), r.randrange(self.line_max + 1))
            put(ITA + i * 16 + 0xC, "<I", r.getrandbits(32))
            put(ITB + i * 0x200 + 0x192, "<B", r.getrandbits(8))
            put(ITB + i * 0x200 + 0x194, "<I", ITT + i * 64)
            for j in range(16):
                put(ITT + i * 64 + j * 4, "<I", r.choice([0x5000000, 0x1000000, r.getrandbits(32)]))
        # markers
        put(MARKW + 0xE4, "<I", MARKS)
        put(MARKW + 0xF5, "<B", r.randrange(0, 7))
        for i in range(16):
            put(MARKS + i * 20, "<3h", near(), near(), near())
            put(MARKS + i * 20 + 0x10, "<HB", r.randrange(self.node_max), r.randrange(self.line_max + 1))

        self.align()

    def get(self, addr, fmt):
        for a, n in GLOBALS:
            if a <= addr < a + n:
                return struct.unpack_from(fmt, self.glob[a], addr - a)
        return struct.unpack_from(fmt, self.mem, addr - S)

    def align(self):
        """Point some route entries at real targets so they get taken."""
        r, put, get = self.rng, self.put, self.get
        car = [c / 65536 for c in get(CAR + 0x34, "<3i")]
        flags = get(CAR + 0x6DC, "<I")[0]
        if r.randrange(4):
            flags |= 0x86C
        put(CAR + 0x6DC, "<I", flags)
        found = []
        for i in range(8):
            p = PICK + i * 0x200
            put(p + 0x182, "<H", 0x81)
            found.append((p + 0x18C, "<HH", [c / 65536 for c in get(p + 0x34, "<3i")], 0x100))
            found.append((ITEMS + i * 20 + 0x10, "<HB", get(ITEMS + i * 20 + 8, "<3h"), 0x200))
        for i in range(16):
            found.append((MARKS + i * 20 + 0x10, "<HB", get(MARKS + i * 20, "<3h"), 0x400))
        for at, fmt, pos, bit in found:
            if r.randrange(3):
                continue
            k = r.randrange(20)
            line, node = r.randrange(1, self.line_max + 1), r.randrange(self.node_max)
            if fmt == "<HH":
                put(at, fmt, line, node)
            else:
                put(at, fmt, node, line)
            a = 0x6CF220 + k * 0x34
            put(a, "<HB", line, node)
            put(a + 0x26, "<B", 0)
            fl = NODES + (line * NODE_COUNT + node) * 16 + 8
            put(fl, "<I", get(fl, "<I")[0] | bit)
            d = [pos[j] - car[j] for j in range(3)]
            n = sum(c * c for c in d) ** 0.5 or 1
            put(a + 0x10, "<3i", *[int(c / n * 0xFFF0) for c in d])

    def write(self, m):
        m.write(S, bytes(self.mem))
        for a, data in self.glob.items():
            m.write(a, bytes(data))

    def state(self, m):
        return m.read(S, SIZE) + b"".join(m.read(a, n) for a, n in GLOBALS)


def svec(r, scale=900):
    return struct.pack("<3hxx", *[r.randrange(-scale, scale) for _ in range(3)])


def args_for(name, w, r):
    """Arguments (and ARGS-area contents) for one call."""
    args_area = bytearray(0x100)
    A, B, N, LEN, INV = ARGS, ARGS + 8, ARGS + 0x10, ARGS + 0x14, ARGS + 0x18
    args_area[0:8] = svec(r, 300)
    args_area[8:16] = svec(r)
    struct.pack_into("<i", args_area, 0x10, r.choice([-1, 0, 3, 10, 18, 19, r.randrange(-3, 20), 19, 15]))
    struct.pack_into("<ii", args_area, 0x14, -1, 0)
    speed = r.choice([0, 99, 100, 250, r.randrange(-0x8000, 0x8000)])
    margin = r.choice([0, 10, 60, r.randrange(0x10000)])
    extra = r.choice([0, 0, r.randrange(0, 400000)])
    k = r.randrange(20)
    table = {
        "svec_dot": [A, B],
        "ai_player_weight": [CAR],
        "ai_player_takes_line": [r.randrange(8), r.randrange(8) | r.getrandbits(16) << 16],
        "ai_route_branch_score": [CAR, r.choice([0x8000, 0x10000, -0x10000, -0x4000, 0x4000]), k],
        "ai_route_branches": [CAR, r.choice([-1, 0, 5, 18, 19, r.randrange(-3, 22)])],
        "ai_hazards_cost": [CAR, A, B, margin, speed, extra],
        "ai_obstacle_hit": [A, B, OBJ + r.randrange(4) * 0x200, r.choice([0, 5, -5, r.randrange(-500, 500)]),
                            r.randrange(4)],
        "ai_obstacle_cost": [A, B, r.randrange(4), margin, speed, extra],
        "ai_obstacles_cost": [A, B, margin, speed, extra],
        "ai_target_danger": [CAR, k, r.randrange(2), r.randrange(2)],
        "ai_target_route_danger": [0x6CF180 + r.randrange(20) * 8, r.randrange(0, 0x225511), k, CAR],
        "ai_route_add_pickups": [CAR, N],
        "ai_route_add_items": [CAR, N],
        "ai_route_add_markers": [CAR, N],
        "ai_route_targets": [CAR, N],
    }
    if name == "ai_hazard_cost":
        d = struct.unpack("<3h", args_area[8:14])
        len2 = s32(sum(c * c for c in d))
        return [A, B, r.randrange(8), margin, speed, extra, max(len2, 4), LEN, INV], args_area
    return table[name], args_area


TESTS = {
    "svec_dot": (0x47F3A0, True),
    "ai_player_weight": (0x4297A0, True),
    "ai_player_takes_line": (0x429AD0, True),
    "ai_route_branch_score": (0x4298E0, True),
    "ai_route_branches": (0x429680, False),
    "ai_hazard_cost": (0x403430, True),
    "ai_hazards_cost": (0x403350, True),
    "ai_obstacle_hit": (0x403D90, True),
    "ai_obstacle_cost": (0x403C00, True),
    "ai_obstacles_cost": (0x403BA0, True),
    "ai_target_danger": (0x4117D0, True),
    "ai_target_route_danger": (0x42A1A0, True),
    "ai_route_add_pickups": (0x429EA0, False),
    "ai_route_add_items": (0x429B50, False),
    "ai_route_add_markers": (0x429380, False),
    "ai_route_targets": (0x429350, False),
}


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    only = sys.argv[4:] or list(TESTS)
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, SIZE)
    rng = random.Random(5)
    failed = 0
    for name in only:
        address, returns = TESTS[name]
        bad, results, changed = 0, {}, 0
        for i in range(iterations):
            w = World(rng)
            args, area = args_for(name, w, rng)
            struct.pack_into(f"<{len(area)}s", w.mem, ARGS - S, bytes(area))
            out = []
            for target in (address, m.symbols[name]):
                w.write(m)
                r = m.call(target, args)
                if not returns and r[0] == "ok":
                    r = ("ok",)
                out.append((r, w.state(m)))
            if out[0][1] != bytes(w.mem) + b"".join(bytes(w.glob[a]) for a, _ in GLOBALS):
                changed += 1
            key = out[0][0][0] if len(out[0][0]) < 2 or not returns else ("ok", out[0][0][1] != 0)
            results[key] = results.get(key, 0) + 1
            if out[0] != out[1]:
                bad += 1
                if bad <= 3:
                    diff = ""
                    if out[0][1] != out[1][1]:
                        a, b = out[0][1], out[1][1]
                        j = next(x for x in range(len(a)) if a[x] != b[x])
                        diff = f" memory differs at state offset {j:#x}"
                    print(f"{name} case {i}: args {[hex(x & 0xFFFFFFFF) for x in args]} original {out[0][0]} "
                          f"ours {out[1][0]}{diff}")
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} "
              f"(results {results}, memory changed in {changed})")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
