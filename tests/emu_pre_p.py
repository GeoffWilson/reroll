#!/usr/bin/env python3
"""Differential test of the pre-physics race order and lap / finish code of
src/game/pre_p.c (race order, nearest racing line node, lap records, the
results table, lap weapons, handing a finished car to the AI, the replay
recorder and small getters) against the original, on random synthetic
races.

Routines owned elsewhere are stubbed at their original addresses (pre_p.c
calls them by address too): each call is logged with its arguments and
answers from a per-case script, and the logs must match as well as memory
and the result.

    tests/emu_pre_p.py EXE build/emu_p.elf [iterations] [names...]
"""
import random
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.path.insert(0, __file__.rsplit('/', 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

S, SIZE = 0x01000000, 0x40000
RACE, TRACK, LINES, NODES, CELLTAB, CELLS, CARS, MISC, REC = (
    S, S + 0x1000, S + 0x1800, S + 0x2000, S + 0x6000, S + 0x7000, S + 0x8000, S + 0xB000, S + 0x10000)
NODE_STRIDE = 0x400  # per line: 64 nodes of 0x10
CAR_SIZE, SLOTS, SLOT_SIZE = 0x800, 0x6BFB80, 0x274
REC_SIZE = 0x1E884

GLOBALS = [(0x4B1960, 4), (0x6C1E30, 4), (SLOTS, 6 * SLOT_SIZE), (0x4B11DC, 4), (0x4B11D8, 4),
           (0x6C0A50, 4), (0x5D622C, 4), (0x5D5358, 4), (0x5D535C, 4), (0x6CF6A0, 4), (0x6CCE40, 4),
           (0x6CF6D0, 8), (0x4D4560, 0x460), (0x4B820C, 2), (0x4B8228, 2), (0x6B9D41, 3), (0x6C30A0, 4),
           (0x6B8D60, 4)]

# stubbed originals owned elsewhere: address -> (name, argument count, answer)
STUBS = {
    0x43D8E0: ("checkpoints_follow", 2, "checkpoints"),
    0x47FBB0: ("rejoin_plan", 1, None),
    0x4351C0: ("clock_now", 0, "clock"),
    0x439910: ("time_digits_show", 2, None),
    0x45E800: ("award_send", 3, None),
}

# name: (original, argument kinds)
TESTS = {
    "net_state_host": (0x45EED0, ""),
    "net_state_bit5": (0x45EF30, ""),
    "net_state_game": (0x45EF80, ""),
    "car_index_of": (0x45CA10, "c"),
    "net_session_kind": (0x48CE80, ""),
    "net_local_slot": (0x48CF20, ""),
    "race_end_signal": (0x40A9A0, ""),
    "voice_slot_set": (0x472290, "iwwww"),
    "voice_slot_reset": (0x472130, "w"),
    "race_end_fade_set": (0x40AFB0, "w"),
    "race_time_digits": (0x439C70, "t"),
    "slot_nearest_line_node": (0x47AE40, "s"),
    "race_positions_update": (0x43D350, ""),
    "slot_lap_record": (0x43D750, "s"),
    "race_results_update": (0x439A70, "f"),
    "slot_finish_to_ai": (0x47FBE0, "s"),
    "car_award_weapon": (0x467880, "ck"),
    "slot_lap_weapon": (0x467490, "s"),
    "replay_checksum": (0x41C190, "rw"),
    "replay_finish_mark": (0x41C160, "c"),
    "replay_step": (0x41BA20, "c"),
}
VOID = {"race_end_signal", "voice_slot_set", "voice_slot_reset", "race_end_fade_set", "slot_nearest_line_node",
        "race_positions_update", "slot_lap_record", "race_results_update", "slot_finish_to_ai",
        "car_award_weapon", "slot_lap_weapon", "replay_checksum", "replay_finish_mark", "replay_step"}


OUTCOMES = {}


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def build_world(rng):
    mem = bytearray(rng.randbytes(0x10000)) + bytearray(SIZE - 0x10000)
    glob = {}

    def put(addr, fmt, *v):
        struct.pack_into(fmt, mem, addr - S, *v)

    # race
    n = rng.randrange(1, 7)
    laps = rng.randrange(1, 6)
    mem[RACE - S + 0x54D] = n
    mem[RACE - S + 0x551] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, rng.getrandbits(8)])
    mem[RACE - S + 0x58C] = rng.choice([0, 0, 0, 1, 2])
    mem[RACE - S + 0x586] = laps
    put(RACE + 0x57C, "<i", rng.choice([4, 5, 6, 7, 7, 8, 9, rng.randrange(-3, 20)]))
    # timing records: sorted best laps sometimes
    for c in range(6):
        best = sorted(rng.getrandbits(24) for _ in range(5))
        if rng.randrange(3):
            put(RACE + c * 0xDC + 0x30, "<5I", *best)

    # track: lines, nodes, cell table
    nl = rng.randrange(1, 9)
    centre = [rng.randrange(-0x6000, 0x6000) for _ in range(3)]
    spread = rng.choice([2, 4, 0x40, 0x400, 0x4000, 0x7000])  # 2, 4: equal distances
    counts = []
    for li in range(8):
        count = rng.choice([1, 2, 3, rng.randrange(1, 64), rng.randrange(1, 64)])
        counts.append(count)
        put(LINES + li * 0xC, "<HBB", count, rng.choice([0, 1]), 0)
        put(LINES + li * 0xC + 8, "<I", NODES + li * NODE_STRIDE)
        for k in range(64):
            pos = [max(-0x8000, min(0x7FFF, centre[a] + rng.randrange(-spread, spread))) for a in range(3)]
            flags = rng.getrandbits(32) & ~0x7C01F  # links: line index (0 none) < nl
            if rng.randrange(2):
                flags |= rng.randrange(nl)
            if rng.randrange(2):
                flags |= rng.randrange(nl) << 14
            flags = (flags & ~(0x1FF << 5)) | (rng.choice([0, 1, rng.randrange(64), rng.getrandbits(9)]) << 5)
            flags = (flags & ~(0x1FF << 19)) | (rng.choice([0, 1, rng.randrange(64), rng.getrandbits(9)]) << 19)
            put(NODES + li * NODE_STRIDE + k * 0x10, "<3h", *pos)
            put(NODES + li * NODE_STRIDE + k * 0x10 + 0xC, "<I", flags)
    for e in range(1024):
        li = rng.randrange(nl)
        put(CELLTAB + e * 4, "<HH", rng.randrange(counts[li]), li)
    put(TRACK + 0x44, "<B", rng.choice([0, 1, 1, 1, 1]))
    put(TRACK + 0x60, "<I", LINES)
    put(TRACK + 0x7C, "<I", CELLTAB)
    for j in range(64):
        put(CELLS + j * 4, "<I", rng.randrange(1024) << 22 | rng.getrandbits(22))

    # replay record
    rec = bytearray(rng.randbytes(0x400)) + bytearray(REC_SIZE - 0x400)
    state = rng.choice([2, 2, 3, 7, 5, 5, 6, 4, 0, rng.getrandbits(32)])
    struct.pack_into("<h", rec, 0, rng.choice([0, 3, 0x40, 0x4E12, 0x4E13, 0x4E14, rng.randrange(-5, 0x200)]))
    struct.pack_into("<h", rec, 2, rng.randrange(-5, 100))
    struct.pack_into("<I", rec, 0x4E28, REC + 8 + rng.randrange(0x300))
    struct.pack_into("<H", rec, 0x4E2C, rng.choice([0, 0, 1, 2, 3, 0xF, 0x10, 0x1F, 0x20, 0x7F, 0x80, 0x1FF, 0x200,
                                                     0x1FFF, 0x2000, rng.getrandbits(16)]))
    if state == 5 and rng.randrange(2):  # play the next run
        struct.pack_into("<H", rec, 0x4E2C, 0)
    bits = rng.choice([0, 1, 2, 3, 16])
    buttons = 0
    for _ in range(bits):
        buttons |= 1 << rng.randrange(16)
    struct.pack_into("<H", rec, 0x4E2E, buttons)
    for base in (0x4E30, 0x14C24, 0x19A50):
        used = rng.choice([0, 1, 2, 0x20, rng.randrange(0x100)])
        cap = rng.choice([0, used, used + 1, 0x10000, rng.getrandbits(32)])
        struct.pack_into("<BBBxIHH", rec, base, rng.getrandbits(8), rng.getrandbits(8),
                         rng.choice([0, 0, 1, 0x7E, 0x7F, rng.getrandbits(8)]), cap, used,
                         rng.choice([0, used, rng.randrange(used + 1)]))
        rec[base + 0xC:base + 0xC + 0x110] = rng.randbytes(0x110)
    if rng.randrange(4) == 0:  # long streams for the checksum
        struct.pack_into("<h", rec, 0, rng.randrange(0x7FFF))
        for base in (0x4E30, 0x14C24, 0x19A50):
            struct.pack_into("<H", rec, base + 8, rng.getrandbits(16))
    at = struct.unpack_from("<I", rec, 0x4E28)[0] - REC
    rec[at] = rng.choice([rng.randrange(0x80), rng.randrange(0x80, 0xA0), rng.randrange(0xA0, 0xC0),
                          rng.randrange(0xC0, 0xE0), rng.randrange(0xE0, 0x100)])
    struct.pack_into("<h", rec, 0x1E87C, rng.randrange(-3, 4))
    struct.pack_into("<I", rec, 0x1E880, state)
    struct.pack_into("<H", rec, 6, rng.getrandbits(16))
    mem[REC - S:REC - S + REC_SIZE] = rec

    # cars and slots
    hoard = rng.randrange(4) == 0
    slots = bytearray(rng.randbytes(6 * SLOT_SIZE))
    places = list(range(1, n + 1))
    rng.shuffle(places)
    for i in range(6):
        car = CARS + i * CAR_SIZE
        o = i * SLOT_SIZE
        struct.pack_into("<I", slots, o, car)
        lap = rng.randrange(0, laps + 2)
        slots[o + 0x2C] = lap
        slots[o + 0x2D] = places[i] if i < n else rng.randrange(1, 7)
        struct.pack_into("<i", slots, o + 0x34, rng.choice([0, rng.randrange(0x10000), rng.getrandbits(31)]))
        slots[o + 0x259] = rng.getrandbits(8)
        li = rng.randrange(nl)
        struct.pack_into("<I", slots, o + 0x64, rng.choice([LINES + li * 0xC, LINES + li * 0xC, 0]))
        struct.pack_into("<i", slots, o + 0x68, rng.choice([0, counts[li] - 1, counts[li],
                                                            rng.randrange(counts[li])]))
        for f in (0x3C, 0x44, 0x48):
            struct.pack_into("<I", slots, o + f, rng.choice([0, 0xFFFFFFFF, rng.getrandbits(26), rng.getrandbits(32)]))
        struct.pack_into("<I", slots, o + 0x40, rng.choice([0xFFFFFFFF, rng.getrandbits(26), rng.getrandbits(32)]))
        # car
        put(car + 0x34, "<3I", *[((centre[a] + rng.randrange(-spread, spread)) * 0x10000 + rng.getrandbits(16))
                                 & 0xFFFFFFFF for a in range(3)])
        put(car + 0xC, "<I", CELLS + rng.randrange(64) * 4 + rng.choice([0, 1]))
        put(car + 0x64E, "<H", rng.choice([i, i, rng.randrange(2)]))
        put(car + 0x650, "<I", REC)
        put(car + 0x70C, "<B", rng.choice([8, 10, 10, 9]))
        if hoard:  # most kinds are taken: refusals
            put(car + 0x67C, "<BB", rng.choice([0, 2, 3, 6, 7]), rng.choice([0, 2, 3, 6, 7, 0xFF]))
        else:
            put(car + 0x67C, "<BB", rng.choice([0xFF, 0xFF, rng.randrange(8)]), rng.choice([0xFF, rng.randrange(8)]))
        put(car + 0x6AC, "<I", rng.choice([0xFFFFFFFF, MISC + rng.randrange(4)]))
        if rng.randrange(2):  # unchanged controls extend a run
            put(car + 0x6F8, "<3B", rec[0x4E30], rec[0x14C24], rec[0x19A50])
        else:
            put(car + 0x6F8, "<3B", *rng.randbytes(3))
        put(car + 0x700, "<I", rng.choice([0, buttons, rng.getrandbits(16), rng.getrandbits(32)]))
    put(MISC, "<4B", *[rng.choice([0, 1, 2]) for _ in range(4)])
    glob[SLOTS] = bytes(slots)
    glob[0x4B1960] = p32(RACE)
    glob[0x6C1E30] = p32(TRACK)
    glob[0x4B11DC] = p32(rng.getrandbits(31))
    glob[0x4B11D8] = p32(rng.getrandbits(31))
    glob[0x6C0A50] = p32(rng.choice([0, 0, 1, rng.getrandbits(32)]))
    glob[0x5D622C] = p32(rng.getrandbits(8) if rng.randrange(2) else rng.getrandbits(32))
    glob[0x5D5358] = p32(rng.choice([0, 1]))
    glob[0x5D535C] = p32(rng.choice([-1, 0, 1, 2, rng.randrange(6)]))
    glob[0x6CF6A0] = p32(rng.choice([0xE, 0, 3, rng.getrandbits(32)]))
    glob[0x6CCE40] = p32(rng.getrandbits(32))
    glob[0x6CF6D0] = rng.randbytes(8)
    glob[0x4D4560] = rng.randbytes(0x460)
    glob[0x4B820C] = rng.randbytes(2)
    glob[0x4B8228] = rng.randbytes(2)
    glob[0x6B9D41] = bytes(rng.choice([0, 0, 1, 2, 5, 6] if not hoard else [2, 6, 9]) for _ in range(3))
    glob[0x6C30A0] = p32(rng.choice([0, 0, 1] if not hoard else [1]))
    glob[0x6B8D60] = p32(rng.choice([0, 0, 0, 1]))
    answers = {"checkpoints": rng.choice([0, 1, 2, 3]), "clock": rng.getrandbits(32)}
    return mem, glob, n, answers


class Stubs:
    def __init__(self, m):
        self.log = []
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self.hit, begin=address, end=address)

    def reset(self, answers):
        self.log = []
        self.answers = answers

    def hit(self, uc, address, size, _):
        name, nargs, kind = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret, *args = struct.unpack("<%dI" % (nargs + 1), uc.mem_read(esp, 4 * (nargs + 1)))
        self.log.append((name, *args))
        value = self.answers[kind] if kind else 0
        uc.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)


def make_args(rng, kinds, n):
    args = []
    for k in kinds:
        if k == "s":
            args.append(SLOTS + rng.randrange(n) * SLOT_SIZE)
        elif k == "c":
            args.append(CARS + rng.randrange(n) * CAR_SIZE)
        elif k == "r":
            args.append(REC)
        elif k == "t":
            args.append(rng.choice([-1, 0, 1, 0x7FF, 0x800, 0x4650000, 0x464FFFF, 0x4650001, -2, -0x800,
                                    -rng.getrandbits(24), rng.randrange(0x4650000), rng.randrange(0x4650000),
                                    rng.getrandbits(32)]))
        elif k == "f":
            args.append(rng.choice([1, 1, 0, 2, rng.getrandbits(32)]))
        elif k == "w":
            args.append(rng.choice([0, 1, 2, 3, 4, 0xFF, 0x100, 0x103, rng.getrandbits(32)]))
        elif k == "i":
            args.append(rng.choice([-1, 0, 1, 2, 3, 4, rng.getrandbits(32)]))
        elif k == "k":
            args.append(rng.choice([0, 1, 2, 3, 4, 5, 6, 7, rng.getrandbits(32)]))
    return args


def run_case(m, stubs, name, address, kinds, rng):
    mem, glob, n, answers = build_world(rng)
    args = make_args(rng, kinds, n)
    results = []
    for target in (address, m.symbols[name]):
        m.write(S, bytes(mem))
        for a, data in glob.items():
            m.write(a, data)
        m.write(E.STACK, bytes(E.STACK_SIZE))
        stubs.reset(answers)
        r = m.call(target, args)
        if name in VOID and r[0] == "ok":
            r = ("ok",)
        state = m.read(S, SIZE) + b"".join(m.read(a, size) for a, size in GLOBALS)
        results.append((r, state, list(stubs.log)))
    shown = tuple(hex(a & 0xFFFFFFFF) for a in args)
    if results[0][0][0] == "cut off" and results[1][0][0] == "cut off":
        return None, 0, 1
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        diff = ""
        if results[0][1] != results[1][1]:
            a, b = results[0][1], results[1][1]
            i = next(i for i in range(len(a)) if a[i] != b[i])
            where = f"scratch +{i:#x}" if i < SIZE else f"globals +{i - SIZE:#x}"
            diff += f" (memory differs at {where}: {a[i]:#x} vs {b[i]:#x})"
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            j = next((j for j in range(min(len(la), len(lb))) if la[j] != lb[j]), min(len(la), len(lb)))
            diff += f" (calls differ at #{j}: {la[j] if j < len(la) else None} vs {lb[j] if j < len(lb) else None})"
        return f"{name}{shown}: original {results[0][0]} ours {results[1][0]}{diff}", 0, 0
    return None, len(results[0][2]), 0


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    only = sys.argv[4:] or list(TESTS)
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, SIZE)
    stubs = Stubs(m)
    rng = random.Random(16)
    failed = 0
    for name in only:
        address, kinds = TESTS[name]
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = calls = cut = 0
        OUTCOMES.clear()
        for _ in range(iterations):
            msg, made, was_cut = run_case(m, stubs, name, address, kinds, rng)
            calls += made
            cut += was_cut
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        extra = f", {cut} cut off both ways" if cut else ""
        if set(OUTCOMES) != {"ok"}:
            extra += f", outcomes {OUTCOMES}"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} ({calls} stubbed calls{extra})")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
