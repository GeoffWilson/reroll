#!/usr/bin/env python3
"""Differential test of gl_ga.c (the chase/replay camera director 0x408360,
its offset placement 0x40a720 and the analogue response curve 0x428e90)
against the original, on random viewport / car / camera state.

The x87 parts (the GTE matrix's float copy, __ftol of the zoom float, the
curve table built with the CRT's pow) run for real in both runs, under a
random FPU control word (precision 24/53/64 bits, rounding modes). Pure
callees run for real too (car_is_upside_down 0x408240, direction_fit
0x407e90, vec_normalise / vec_length / vec_div, fix_isqrt, body_side_axis
0x4081f0, gte_rotate 0x428090, __ftol, pow); the rest are logging stubs with
scripted results and side effects (camera_choose_shot sets a new shot,
camera_spot_nearest hands out a spot, bezier_point writes a point).
Pointer arguments are logged by content.

Not compared: 0x6ccc6a (the GTE vector's pad word, written by the original
from an uninitialised stack slot; it is verifier scratch in game).

    tests/emu_gl_ga.py EXE build/emu_ga.elf [iterations] [names...] [-v] [--cov]
"""
import math
import random
import struct
import subprocess
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

S = 0x01000000
CARS = S + 0x0000  # 8 x 0x700
CAMS = S + 0x4000  # 8 x 0x80
BODIES = S + 0x4800  # 8 x 0x120
OBJS = S + 0x5400  # 8 x 0x200
NODES = S + 0x6800  # 6 x 0x200
NSUB = S + 0x7400  # 6 x 0x40
SPOTS = S + 0x7800  # 8 x 0x40
RACE = S + 0x8000
CFG = S + 0x8800
OUT = S + 0x8900  # 0xd8 output body for 0x40a720
DIRV = S + 0x8a00
SCRATCH_SIZE = 0x9000
CODE = S + 0x10000  # trampoline: fninit; fldcw [CW]; jmp [TARGET] (code is written once: unicorn
CW = CODE + 0x80  # keeps translated blocks across mem_write)
TARGET = CODE + 0x84
TRAMPOLINE = b"\xdb\xe3\xd9\x2d" + struct.pack("<I", CW) + b"\xff\x25" + struct.pack("<I", TARGET)

REGIONS = [
    (0x4B11D8, 4),
    (0x4B6000, 0x600),
    (0x4BDD04, 0x410),
    (0x5D1FC0, 0x20),
    (0x6B9CC0, 0x40),
    (0x6BFB80, 6 * 0x274),  # six car slots; 0x6c0a40 follows
    (0x6C0A40, 0x14),
    (0x6CCBE0, 0x170),
    (0x6CCE6C, 4),
    (0x6CF6A0, 4 * 0x150),
]
IGNORE = [(0x6CCC6A, 2)]

V0 = 0x6CF6A0

# address: (name, [byte lengths of pointer args to log by content; 0 = value], returns)
STUBS = {
    0x40B310: ("find_trackside", [0, 0, 0], True),
    0x407FA0: ("check_events", [0], False),
    0x40A9B0: ("choose_shot", [12, 0], False),
    0x40B180: ("line_distance", [12, 12, 12, -1], True),
    0x40A8F0: ("probe", [0, 0xD8], True),
    0x40A940: ("look_or_keep", [12, 12, 0, 0], False),
    0x40A570: ("spot_nearest", [12, 0, -1, 0], False),
    0x40ADD0: ("zoom_for", [12, 12, 0, 0], True),
    0x40B000: ("speed_zoom", [0, 0xD8], False),
    0x40C940: ("factorials_init", [], False),
    0x40CAD0: ("points_scale", [0, 0], False),
    0x40C9E0: ("bezier", [60, 0, -1, 0], False),
    0x407220: ("matrix_look", [12, 12, 0], True),
}

COVERED = [(0x408360, 0x40A51E), (0x40A720, 0x40A8E9), (0x428E90, 0x428FD5)]
SWITCH = 0x40A520  # 0x408360's jump table, 17 entries

# name: (address, returns)
TESTS = {
    "controls_curve": (0x428E90, True),
    "camera_place_offset": (0x40A720, False),
    "camera_director": (0x408360, False),
}


def pick(rng, *choices):
    return rng.choice(choices)


def s32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def rand_coord(rng):
    return pick(rng, rng.randrange(-0x4000000, 0x4000000), rng.randrange(-0x40000000, 0x40000000),
                rng.randrange(-0x100000, 0x100000), rng.getrandbits(32) - 0x80000000)


def rotation(rng):
    """An int16 rotation matrix (4096 = 1), sometimes junk."""
    if rng.randrange(8) == 0:
        return [rng.randrange(-0x8000, 0x8000) for _ in range(9)]
    a, b, c = (rng.uniform(-math.pi, math.pi) for _ in range(3))
    ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
    m = [cb * cc, -cb * sc, sb,
         sa * sb * cc + ca * sc, -sa * sb * sc + ca * cc, -sa * cb,
         -ca * sb * cc + sa * sc, ca * sb * sc + sa * cc, ca * cb]
    return [max(-0x8000, min(0x7FFF, int(round(x * 4096)))) for x in m]


def rand_float_bits(rng):
    k = rng.randrange(8)
    if k < 4:
        return struct.pack("<f", rng.uniform(60.0, 500.0))
    if k == 4:
        return struct.pack("<f", rng.choice([0.0, -0.0, 104.0, 0.5, -0.5, 1.5, 2.5, -2.5, 1e10, -1e10, 2147483647.0]))
    if k == 5:
        return struct.pack("<f", rng.uniform(-1e6, 1e6))
    if k == 6:
        return struct.pack("<I", rng.choice([0x7FC00000, 0x7F800000, 0xFF800000, 0x00000001, 0x80000001]))
    return struct.pack("<I", rng.getrandbits(32))


class State:
    def __init__(self, rng):
        self.mem = {a: bytearray(rng.getrandbits(8) for _ in range(n)) for a, n in REGIONS}
        self.scratch = bytearray(rng.getrandbits(8) for _ in range(SCRATCH_SIZE))

    def put(self, addr, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt.upper()
        data = struct.pack(fmt, value)
        self.putb(addr, data)

    def putb(self, addr, data):
        if S <= addr < S + SCRATCH_SIZE:
            self.scratch[addr - S:addr - S + len(data)] = data
            return
        for a, n in REGIONS:
            if a <= addr < a + n:
                self.mem[a][addr - a:addr - a + len(data)] = data
                return
        raise KeyError(hex(addr))


def make_state(rng, view):
    st = State(rng)
    base = [rand_coord(rng) for _ in range(3)]

    def near(spread=None):
        spread = spread or pick(rng, 1 << 20, 1 << 23, 1 << 26)
        return [(b + rng.randrange(-spread, spread) + 0x80000000) % (1 << 32) - 0x80000000 for b in base]

    # cars and their camera records
    for i in range(6):
        car = CARS + i * 0x700
        for k, x in enumerate(near()):
            st.put(car + 0x34 + 4 * k, "<i", x)
        for k in range(3):
            st.put(car + 0x68 + 4 * k, "<i", pick(rng, 0, rng.randrange(-0x80000, 0x80000), rng.getrandbits(32)))
        for k, x in enumerate(rotation(rng)):
            st.put(car + 0xC4 + 2 * k, "<h", x)
        st.put(car + 0x63F, "<B", pick(rng, 0, 0, 0, 1, rng.getrandbits(8)))
        st.put(car + 0x64E, "<H", rng.randrange(6))
        st.put(car + 0x654, "<i", pick(rng, rng.randrange(0, 0x400000), 0x140000, 0x150000, 0x160000,
                                       rng.getrandbits(32)))
        cam = CAMS + i * 0x80
        st.put(cam, "<I", BODIES + i * 0x120)
        for k in range(3):
            st.put(cam + 0x14 + 4 * k, "<i", pick(rng, rng.randrange(-0x10000, 0x10001), rng.getrandbits(32)))
            st.put(cam + 0x2C + 4 * k, "<i", pick(rng, rng.randrange(-0x1000000, 0x1000000), rng.getrandbits(32)))
        st.put(cam + 0x3C, "<h", pick(rng, 4, 4, 3, 0, rng.getrandbits(16)))
        body = BODIES + i * 0x120
        for k, x in enumerate(near()):
            st.put(body + 0x34 + 4 * k, "<i", x)
        slot = 0x6BFB80 + i * 0x274
        st.put(slot, "<I", car)
        st.put(slot + 0x28, "<I", cam)
    # event objects (0x6b9ce0, 0x6b9cc0, 0x5d1fc0 tables)
    for i in range(8):
        obj = OBJS + i * 0x200
        for k, x in enumerate(near()):
            st.put(obj + 0x34 + 4 * k, "<i", x)
        st.put(obj + 0x188, "<I", pick(rng, 0x10, 0x40, 0, 0x50, rng.getrandbits(32)))
        st.put(obj + 0x190, "<I", CARS + rng.randrange(6) * 0x700)
        st.put(obj + 0x198, "<I", CARS + rng.randrange(6) * 0x700)
        st.put(0x6B9CC0 + 4 * i, "<I", OBJS + rng.randrange(8) * 0x200)
        st.put(0x6B9CE0 + 4 * i, "<I", OBJS + rng.randrange(8) * 0x200)
        st.put(0x5D1FC0 + 4 * i, "<I", OBJS + rng.randrange(8) * 0x200)
    # kind 0xf nodes: a parent chain and sibling chains
    for i in range(6):
        node = NODES + i * 0x200
        st.put(node + 0x10, "<I", pick(rng, 0, NSUB + i * 0x40))
        st.put(NSUB + i * 0x40 + 0x22, "<H", pick(rng, rng.randrange(0, 300), rng.getrandbits(16)))
        for k, x in enumerate(near()):
            st.put(node + 0x34 + 4 * k, "<i", x)
        for k, x in enumerate(rotation(rng)):
            st.put(node + 0xC4 + 2 * k, "<h", x)
        st.put(node + 0x1B0, "<I", NODES + (i + 1) * 0x200 if i in (0, 1) and rng.randrange(4) else 0)
        st.put(node + 0x19C, "<I", NODES + (i + 1) * 0x200 if i < 5 and rng.randrange(3) else 0)
    # trackside spots
    for i in range(8):
        spot = SPOTS + i * 0x40
        for k, x in enumerate(near()):
            st.put(spot + 4 + 4 * k, "<i", x)
        for k, x in enumerate(near()):
            st.put(spot + 0x10 + 4 * k, "<i", x)
        st.put(spot + 0x34, "<i", pick(rng, rng.randrange(0x140000 * 40, 0x200000 * 400),
                                       rng.randrange(0x140000 * 40, 0x200000 * 400), rng.randrange(0, 0x4000000),
                                       rng.getrandbits(32)))
    # viewports
    for w in range(4):
        v = V0 + w * 0x150
        kind = pick(rng, *range(17), *range(17), 4, 9, 0xE, 0xF, 0x10, 17, 0xFFFFFFFF)
        st.put(v, "<I", kind)
        st.put(v + 0x32, "<h", pick(rng, 0, 0, 0, 1, 2, 19, 20, 29, 30, 75, 85, 100, rng.randrange(-5, 400),
                                    rng.getrandbits(16)))
        st.put(v + 0x34, "<h", pick(rng, 0x5A, 0xD2, 0x78, 300, 0, rng.randrange(-5, 400), rng.getrandbits(16)))
        st.put(v + 0x36, "<H", rng.randrange(6))
        st.put(v + 0x38, "<h", rng.randrange(8))
        st.put(v + 0x3A, "<h", pick(rng, -1, 0, 1, 5, 300, rng.getrandbits(16)))
        st.put(v + 0x3C, "<i", 3 if kind == 0xB and rng.randrange(2) else
               pick(rng, 0, 0, 1, 2, 3, 3, 4, 4, rng.getrandbits(32)))
        st.put(v + 0x20, "<i", pick(rng, 0, 0, 1, 1, 2, 0x1E, -200, 0x780000, 45000, rng.randrange(-300, 300),
                                    rng.getrandbits(32)))
        if kind == 0xB and rng.randrange(2):  # a pickup cut-away just starting
            st.put(v + 0x20, "<i", 0)
            st.put(v + 0x3C, "<i", 3)
        st.put(v + 0x2C, "<h", rng.getrandbits(16))
        st.put(v + 0x2E, "<h", pick(rng, 1, -1, 2, -2, rng.getrandbits(16)))
        for k in range(3):
            st.put(v + 0x10 + 4 * k, "<i", pick(rng, 0, rng.randrange(-300, 300), rng.getrandbits(32)))
        for k, x in enumerate(near()):
            st.put(v + 4 + 4 * k, "<i", x)
        st.put(v + 0x140, "<I", NODES + rng.randrange(6) * 0x200)
    # per-view arrays
    for w in range(4):
        st.put(0x4B65B0 + 4 * w, "<I", SPOTS + rng.randrange(8) * 0x40)
        st.put(0x4B6040 + 4 * w, "<i", pick(rng, 0, 1))
        st.put(0x4B6120 + 4 * w, "<i", pick(rng, 0, 5, 10, 11, 100, rng.getrandbits(32)))
        st.put(0x4B6540 + 4 * w, "<i", pick(rng, 0, 0x8000, 0xFF00, 0x10000, rng.getrandbits(32)))
        st.put(0x4B6010 + 4 * w, "<i", pick(rng, 0x1B4, rng.getrandbits(32)))
        st.put(0x6C0A40 + 4 * w, "<I", (rng.getrandbits(16) << 16) | rng.randrange(6))
        for arr in (0x4B6080, 0x4B6050, 0x4B60B0, 0x4B60F0, 0x4B6130, 0x4B6248, 0x4B65C0, 0x4B6550, 0x4B6580):
            if arr == 0x4B6080 and w == 3:
                continue  # 0x4b60b0 follows
            vec = near() if arr in (0x4B6080, 0x4B60F0, 0x4B6248, 0x4B65C0, 0x4B6580) else \
                [pick(rng, rng.randrange(-0x10000, 0x10001), rng.getrandbits(32)) for _ in range(3)]
            for k, x in enumerate(vec):
                st.put(arr + w * 12 + 4 * k, "<i", x)
    st.put(0x4B65F4, "<i", pick(rng, 0, 1, 5, -1))
    st.put(0x4B11D8, "<I", rng.getrandbits(31))
    st.put(0x6C0A50, "<I", rng.getrandbits(32))
    st.putb(0x6CCE6C, rand_float_bits(rng))
    race = bytearray(rng.getrandbits(8) for _ in range(0x600))
    struct.pack_into("<i", race, 0x57C, pick(rng, 6, 0, 1, 2, 6))
    race[0x552] = pick(rng, 0, 0, 1, 2, 3, 4)
    st.scratch[RACE - S:RACE - S + 0x600] = race
    st.put(CFG + 0x31, "<B", pick(rng, 0, 1, 1, 2))
    # curve table
    st.put(0x4BE110, "<i", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    stuck = rng.randrange(5) == 0  # every shot fails to fit: the director gives up after five
    script = {
        "check_events": [rng.randrange(4) == 0],
        "find_trackside": [pick(rng, 0, 0, 0, 0, 1, rng.getrandbits(32)) for _ in range(2)],
        "choose_shot": [(pick(rng, 2, 4, 6, 8) if stuck else pick(rng, *range(17), 4, 9, 17),
                         pick(rng, 0x5A, 0xD2, 0, 300), rng.randrange(6)) for _ in range(8)],
        "line_distance": [pick(rng, 0x3E80000, 0x3E80001, rng.randrange(0, 0x8000000), rng.getrandbits(32))
                          for _ in range(8)],
        "probe": [0 if stuck else pick(rng, 0, 1, 1, 1, rng.getrandbits(32)) for _ in range(8)],
        "spot_nearest": [pick(rng, SPOTS + rng.randrange(7) * 0x40, 0x6CF660, "same") for _ in range(4)],
        "zoom_for": [rng.getrandbits(32) for _ in range(4)],
        "bezier": [pick(rng, "near", [rng.randrange(-0x2000000, 0x2000000) for _ in range(3)]) for _ in range(4)],
        "matrix_look": [rng.getrandbits(32) for _ in range(4)],
    }
    cw = pick(rng, 0x027F, 0x007F, 0x037F, 0x027F, 0x007F, 0x0C7F, 0x047F, 0x087F, 0x0E7F)
    return dict(st=st, script=script, cw=cw, view=view)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, view):
        self.view = view
        self.log = []
        self.script = {k: [x for x in v] for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _read(self, uc, a, n):
        try:
            return bytes(uc.mem_read(a, n))
        except Exception:
            return "unmapped"

    def _hook(self, uc, address, size, _):
        name, kinds, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        args = []
        for i, n in enumerate(kinds):
            a = words[1 + i]
            on_stack = E.STACK <= a < E.STACK + E.STACK_SIZE
            if n == -1:
                args.append("stack" if on_stack else a)
            elif n == 0:
                args.append(a)
            else:
                args.append(("ptr", "stack" if on_stack else a, self._read(uc, a, n)))
        self.log.append((name, tuple(args)))
        eax = 0
        if name == "choose_shot":
            view = words[2]
            if view < 4:
                kind, length, car = self._next(name, (9, 0x5A, 0))
                v = V0 + view * 0x150
                uc.mem_write(v, struct.pack("<I", kind & 0xFFFFFFFF))
                uc.mem_write(v + 0x32, struct.pack("<HHH", 0, length, car))
        elif name == "check_events":
            if self._next(name, False) and words[1] < 4:
                uc.mem_write(V0 + words[1] * 0x150 + 0x3C, bytes(4))  # the event ended
        elif name == "spot_nearest":
            spot = self._next(name, SPOTS)
            if spot == "same":  # a spot at the camera's current position
                spot = SPOTS + 7 * 0x40
                uc.mem_write(spot + 4, bytes(uc.mem_read(0x4B6080 + self.view * 12, 12)))
            uc.mem_write(words[3], struct.pack("<I", spot))
        elif name == "bezier":
            point = self._next(name, [0, 0, 0])
            if point == "near":  # put the camera just off the car, so the look direction is tiny
                view = (words[1] - 0x4B6298) // 0x3C
                start = struct.unpack("<3i", bytes(uc.mem_read(0x4B6248 + view * 12, 12)))
                index = struct.unpack("<H", bytes(uc.mem_read(V0 + view * 0x150 + 0x36, 2)))[0]
                car = struct.unpack("<I", bytes(uc.mem_read(0x6BFB80 + index * 0x274, 4)))[0]
                at = struct.unpack("<3i", bytes(uc.mem_read(car + 0x34, 12)))
                point = [(a - b - d + 0x80000000) % (1 << 32) - 0x80000000
                         for a, b, d in zip(at, start, (10, 7, 0x20010))]
            uc.mem_write(words[3], struct.pack("<3i", *point))
        elif returns:
            eax = self._next(name, 0)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


class Coverage:
    """Conditional branch outcomes in the original's code range."""

    def __init__(self, m, ranges):
        self.ranges = ranges
        self.seen = set()
        self.executed = set()
        self.pending = None
        self.on = False
        if "--cov" in sys.argv:  # a ranged hook stops firing once other code has run (unicorn quirk)
            m.uc.hook_add(UC_HOOK_CODE, self._hook)

    def _hook(self, uc, address, size, _):
        if not self.on or not any(lo <= address < hi for lo, hi in self.ranges):
            return
        self.executed.add(address)
        if self.pending:
            jcc, fall = self.pending
            self.seen.add((jcc, address != fall))
            self.pending = None
        code = bytes(uc.mem_read(address, 2))
        if 0x70 <= code[0] <= 0x7F or (code[0] == 0x0F and 0x80 <= code[1] <= 0x8F):
            self.pending = (address, address + size)


def jcc_sites(exe, lo, hi):
    out = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={lo:#x}",
                          f"--stop-address={hi:#x}", exe], capture_output=True, text=True).stdout
    sites = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].endswith(":") and parts[1].startswith("j") and parts[1] not in ("jmp", "jmpl"):
            sites.add(int(parts[0][:-1], 16))
    return sites


def make_args(rng, name, st):
    view = st["view"]
    if name == "camera_director":
        return [view]
    if name == "controls_curve":
        value = pick(rng, rng.randrange(0, 256), rng.randrange(0, 256), rng.randrange(0x70, 0x90),
                     rng.randrange(-30, 31), rng.choice([24, 25, -24, -25, 0xFFE8, 0x10019]),
                     rng.choice([0, 0x7F, 0x80, 0x81, 0xFF, 0x100, 0x8000, 0xFFFF, 0x7FFF]), rng.getrandbits(32))
        return [value, rng.getrandbits(32), pick(rng, 0, 0, 1, 0x100, 0x101, rng.getrandbits(32))]
    car = CARS + rng.randrange(6) * 0x700
    out = pick(rng, OUT, V0 + view * 0x150 + 0x40)
    return [car, pick(rng, 0, rng.getrandbits(16), rng.getrandbits(32)),
            pick(rng, -10, 10, rng.getrandbits(16), rng.getrandbits(32)),
            pick(rng, 0x14, rng.getrandbits(16), rng.getrandbits(32)), out, DIRV, pick(rng, 0, 1, 1, 2)]


def run_case(m, stubs, cov, name, address, returns, rng, curve_table):
    view = pick(rng, 0, 0, 0, 1, 2, 3)
    st = make_state(rng, view)
    args = make_args(rng, name, st)
    results = []
    for target in (address, m.symbols[name]):
        m.write(E.STACK, bytes(E.STACK_SIZE))
        m.write(S, bytes(st["st"].scratch))
        for a, data in st["st"].mem.items():
            m.write(a, bytes(data))
        if curve_table and struct.unpack("<i", st["st"].mem[0x4BDD04][0x40C:0x410])[0] != 0:
            m.write(0x4BDF04, curve_table)  # an already built table
        m.write(0x4B1960, struct.pack("<I", RACE))
        m.write(0x4B2198, struct.pack("<I", CFG))
        m.write(0x6BFBA8, struct.pack("<I", CAMS))
        m.write(CW, struct.pack("<H", st["cw"]))
        m.write(TARGET, struct.pack("<I", target))
        stubs.reset(st["script"], view)
        cov.on = target == address
        r = m.call(CODE, args)
        cov.on = False
        if r[0] == "ok":
            r = ("ok", r[1]) if returns else ("ok",)
        mem = bytearray(m.read(S, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS))
        offset = SCRATCH_SIZE
        for a, n in REGIONS:
            for ia, inn in IGNORE:
                if a <= ia < a + n:
                    mem[offset + ia - a:offset + ia - a + inn] = bytes(inn)
            offset += n
        results.append((r, bytes(mem), stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            if first < SCRATCH_SIZE:
                where = f" (memory differs at {S + first:#x})"
            else:
                off = first - SCRATCH_SIZE
                for ra, rn in REGIONS:
                    if off < rn:
                        where = f" (memory differs at {ra + off:#x})"
                        break
                    off -= rn
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}: view {view} cw {st['cw']:#x} original {results[0][0]} ours {results[1][0]}{where}{logs}"
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, 0x20000)
    stubs = Stubs(m)
    cov = Coverage(m, COVERED)
    rng = random.Random(29)
    # a curve table as the game builds it, for runs where it is already built
    m.write(0x4BE110, bytes(4))
    m.write(CW, struct.pack("<H", 0x027F))
    m.write(CODE, TRAMPOLINE)
    m.write(TARGET, struct.pack("<I", 0x428E90))
    m.call(CODE, [0x80, 0, 0])
    curve_table = m.read(0x4BDF04, 0x204)
    failed = 0
    only = [x for x in sys.argv[3:] if not x.isdigit() and not x.startswith("-")]
    for name, (address, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, cov, name, address, returns, rng, curve_table)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    if "--cov" in sys.argv:
        for lo, hi in COVERED:
            sites = jcc_sites(exe, lo, hi)
            both = {s for s in sites if (s, True) in cov.seen and (s, False) in cov.seen}
            one = {s for s in sites if ((s, True) in cov.seen) != ((s, False) in cov.seen)}
            print(f"{lo:#x} branch coverage: {len(both)}/{len(sites)} both ways, {len(one)} one way, "
                  f"{len(sites) - len(both) - len(one)} never reached")
            for s in sorted(sites - both):
                ways = [w for w in (True, False) if (s, w) in cov.seen]
                print(f"  {s:#x}: {'taken only' if ways == [True] else 'not taken only' if ways else 'unreached'}")
        cases = set(struct.unpack("<17I", m.read(SWITCH, 68)))
        print(f"0x408360 switch targets reached: {len(cases & cov.executed)}/{len(cases)}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
