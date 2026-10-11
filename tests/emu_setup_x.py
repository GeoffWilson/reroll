#!/usr/bin/env python3
"""Differential test of setup_x.c (race setup group X: AI start, the track's
fixed lights, effect pools, sound listeners, fonts' character maps) against
the original, on random race, track, body and light state.

Functions outside the group are stubbed in both runs: each stub logs its
arguments and returns a scripted value, and the call logs are compared as
well as memory.

    tests/emu_setup_x.py EXE build/emu_x.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
RACE = SCRATCH
TRACK = SCRATCH + 0x1000
LINES = SCRATCH + 0x1100  # 12 lines of 0xc
INFO = SCRATCH + 0x1200
SPOTS = SCRATCH + 0x1300  # 256 spots of 8
BODIES = SCRATCH + 0x2000  # 6 of 0x200
MODELS = SCRATCH + 0x3000  # 6 of 0x400
VERTS = SCRATCH + 0x5000  # 6 of 0x100
EDGES = SCRATCH + 0x6000  # 6 of 0x100
KINDS = SCRATCH + 0x6800  # 6 of 0x40
CARS = SCRATCH + 0x8000  # 6 of 0x2400
CAR_SIZE = 0x2400
CAMS = SCRATCH + 0x16000  # 6 of 0x10
GRID = SCRATCH + 0x17000  # 8 sections of 32 rows of 0x20
LIGHT_RECS = SCRATCH + 0x19000  # 8 of 0x20
SHAPES = SCRATCH + 0x19400
OBJECTS = SCRATCH + 0x19800  # 8 of 0x100
TEXTURES = SCRATCH + 0x1A000
IMAGE = SCRATCH + 0x1A800
LISTS = SCRATCH + 0x1A900
POOL = SCRATCH + 0x20000  # 0x10000
HEAP = SCRATCH + 0x30000  # 2 of 0x400
ARGS = SCRATCH + 0x31000

SLOTS = (0x6BFB80, 0x274 * 6)
OUTCOMES = {}
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, returns)
STUBS = {
    0x47FBB0: ("rejoin_plan", 1, False),
    0x4105E0: ("ai_plan", 2, False),
    0x460BA0: ("object_new", 4, True),
    0x462E30: ("path_start", 3, False),
    0x460AF0: ("pool_take", 1, True),
    0x4445E0: ("heap_take", 1, True),
    0x460B20: ("header_init", 2, False),
    0x4270E0: ("prim_flag", 2, False),
    0x42A9A0: ("glow_prim_init", 2, False),
    0x472130: ("voice_slot_set", 1, False),
    0x40AFB0: ("fade_set", 1, False),
    0x4432D0: ("font_load", 8, True),
    0x476B40: ("block_free", 1, False),
}

# fixed regions the tests use (randomised before and compared after)
AI_GLOBALS = [(0x4A7148, 8), (0x6D0ED0, 4), (0x4B5E4C, 1), (0x6CF654, 0xC), (0x4B5EA8, 0x100), (0x6D0A80, 0x48),
              (0x4B5F98, 4)]
LIGHTS = [(0x5D7DE0, 0x20), (0x5D7FE0, 0xC), (0x5D8000, 0x800), (0x6BFAE8, 8), (0x6BFB2C, 4), (0x6C1EB0, 4)]
FONTS = [(0x6C8180, 0x3370), (0x4BE314, 4), (0x4AC840, 0x60)]
TEXT = FONTS + [(0x5D9B40, 0x380 * 0x84), (0x5F6940, 0x1C * 0x50), (0x4D0774, 4), (0x6CCD84, 4)]
SOUND = [(0x4D4560, 0x30), (0x5FF1E0, 0x100)]

# name: (address, argument kinds, returns, fixed regions)
TESTS = {
    "ai_difficulty_band": (0x47FF40, "", True, []),
    "ai_lines_open_short": (0x47FFD0, "", False, []),
    "ai_bodies_tall_faces": (0x403750, "", False, AI_GLOBALS),
    "ai_pick_rivals": (0x411010, "", False, AI_GLOBALS),
    "ai_skill_init": (0x40F3E0, "C", False, AI_GLOBALS),
    "ai_car_race_start": (0x401470, "L", False, AI_GLOBALS),
    "light_cells_add": (0x441000, "iw", False, LIGHTS),
    "light_set": (0x4412E0, "ipcrwso", False, LIGHTS),
    "track_lights_init": (0x441B90, "", False, LIGHTS),
    "pieces_init": (0x462360, "", False, [(0x6B983C, 0x14)]),
    "tyre_trails_init": (0x481E50, "", False, []),
    "slot_glow_init": (0x490390, "Ck", False, [(0x5CF860, 0x690 * 6), (0x6C1E14, 4), (0x6C1EB0, 4)]),
    "countdown_init": (0x46A940, "", False, [(0x4D3CB8, 0xF8)]),
    "glints_clear": (0x4430F0, "", False, [(0x4C1B18, 4)]),
    "sound_device_ready": (0x428060, "", True, []),
    "sound_output_mode": (0x471A70, "m", True, []),
    "sound_listeners_set": (0x4718D0, "xxxxxxxxxxxxx", False, SOUND),
    "race_sounds_init": (0x4730B0, "", False, SOUND),
    "list_push": (0x428070, "hh", True, []),
    "fonts_init": (0x42E0C0, "f", False, FONTS + [(0x4AC86C, 0x34)]),
    "text_redraw_set": (0x45CFD0, "", False, [(0x4D0774, 4)]),
    "text_reset": (0x478FF0, "", False, TEXT + [(0x4AC86C, 0x34)]),
    "image_release": (0x477810, "I", False, []),
    "race_fade_init": (0x463930, "f", False, [(0x6CCE40, 4)]),
    "replay_state_clear": (0x463990, "", False, [(0x6B8D60, 0x10)]),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return bytes(rng.getrandbits(8) for _ in range(n))


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def make_state(rng, name):
    """A list of (address, bytes) writes making the input state."""
    w = []
    count = pick(rng, rng.randrange(0, 7), rng.randrange(1, 7))
    race = bytearray(rbytes(rng, 0x1000))
    race[0x54D] = count
    for i in range(6):
        race[i] = pick(rng, 8, 8, 10, rng.getrandbits(8))
        race[0x1C + i] = pick(rng, rng.randrange(0, 8), 6, rng.getrandbits(8))
    race[0x551] = pick(rng, rng.randrange(0, 10), 3, 8, rng.getrandbits(8))
    race[0x552] = pick(rng, 1, 2, 3, 4, 0, rng.getrandbits(8))
    race[0x579] = pick(rng, rng.randrange(0, 12), rng.getrandbits(8))
    race[0x585] = pick(rng, 0, 1, 2, rng.getrandbits(8))
    race[0x58C] = pick(rng, 0, 1, 1, rng.getrandbits(8))
    w.append((RACE, bytes(race)))
    w.append((0x4B1960, p32(RACE)))
    # track: racing lines
    track = bytearray(rbytes(rng, 0x100))
    track[0x46] = pick(rng, 0, 1, rng.randrange(2, 13), rng.getrandbits(8) % 13)
    struct.pack_into("<I", track, 0x60, LINES)
    w.append((TRACK, bytes(track)))
    w.append((0x6C1E30, p32(TRACK)))
    lines = bytearray(rbytes(rng, 0xC * 12))
    for i in range(12):
        struct.pack_into("<H", lines, i * 0xC, pick(rng, rng.randrange(0, 0x30), 0x14, 0x15, rng.getrandbits(16)))
        lines[i * 0xC + 4] = pick(rng, 0, 1, 1, 2, rng.getrandbits(8))
    w.append((LINES, bytes(lines)))
    # cars and slots
    slots = bytearray(rbytes(rng, SLOTS[1]))
    for i in range(6):
        car = bytearray(rbytes(rng, CAR_SIZE))
        struct.pack_into("<H", car, 0x64E, pick(rng, i, rng.randrange(0, 6)))
        w.append((CARS + i * CAR_SIZE, bytes(car)))
        struct.pack_into("<II", slots, i * 0x274 + 0, CARS + i * CAR_SIZE, 0)
        struct.pack_into("<I", slots, i * 0x274 + 0x28, CAMS + i * 0x10)
        w.append((CAMS + i * 0x10, p32(rng.getrandbits(32)) + rbytes(rng, 12)))
    w.append((SLOTS[0], bytes(slots)))
    # AI bodies
    nb = pick(rng, 0, rng.randrange(1, 7), 6)
    recs = bytearray(rbytes(rng, 0x48))
    for b in range(6):
        body = bytearray(rbytes(rng, 0x200))
        nverts = pick(rng, 0, rng.randrange(1, 33), 32)
        struct.pack_into("<I", body, 0x10, MODELS + b * 0x400)
        struct.pack_into("<I", body, 0x24, VERTS + b * 0x100)
        struct.pack_into("<I", body, 0x28, EDGES + b * 0x100)
        struct.pack_into("<I", body, 0x194, KINDS + b * 0x40)
        body[0x10D] = nverts
        w.append((BODIES + b * 0x200, bytes(body)))
        verts = bytearray(rbytes(rng, 0x100))
        low = pick(rng, -0x200, 0, 0x100)
        heights = [low] * 4 + [low + 0x18] * 4 + [low + 0x17, low + 0x19, low + 0x30, rng.randrange(low, low + 0x60)]
        for v in range(32):
            struct.pack_into("<h", verts, v * 8 + 2, low + 0x30 if v == 31 else pick(rng, *heights))
        w.append((VERTS + b * 0x100, bytes(verts)))
        edges = bytearray(rbytes(rng, 0x100))
        for e in range(32):
            struct.pack_into("<4B", edges, e * 8, *[pick(rng, 0, rng.randrange(1, 33)) for _ in range(4)])
        w.append((EDGES + b * 0x100, bytes(edges)))
        model = bytearray(rbytes(rng, 0x400))
        nf = pick(rng, 0, rng.randrange(1, 13), 12)
        model[0x27] = nf
        struct.pack_into("<H", model, 0x34, 0x40)
        for f in range(12):
            a = rng.randrange(0, 32)
            b2 = pick(rng, rng.randrange(a, 33), rng.randrange(0, 33))
            struct.pack_into("<I", model, 0x40 + f * 0x14 + 8, a | b2 << 8 | rng.getrandbits(16) << 16)
        w.append((MODELS + b * 0x400, bytes(model)))
        kinds = b"".join(p32(pick(rng, 0x4000000, rng.getrandbits(32))) for _ in range(16))
        w.append((KINDS + b * 0x40, kinds))
        struct.pack_into("<I", recs, b * 0xC, BODIES + b * 0x200)
    ai = [(0x4A7148, rbytes(rng, 4) + p32(pick(rng, 0, 1, 2, rng.getrandbits(32)))),
          (0x6D0ED0, p32(pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))),
          (0x4B5E4C, bytes([pick(rng, 0, 0, 1)])), (0x6CF654, rbytes(rng, 0xC)), (0x4B5EA8, rbytes(rng, 0x100)),
          (0x6D0A80, bytes(recs)), (0x4B5F98, p32(nb))]
    w += ai
    # lights
    nl = pick(rng, 0, rng.randrange(1, 8), 7)
    light_recs = bytearray(rbytes(rng, 0x100))
    for i in range(8):
        r = i * 0x20
        light_recs[r + 0xC] = rng.getrandbits(8)
        light_recs[r + 0xD] = pick(rng, 0, 0, rng.randrange(1, 8))
        light_recs[r + 0xE] = pick(rng, 0, 0, rng.randrange(1, 0x100))
        light_recs[r + 0xF] = rng.getrandbits(8)
        for k in range(4):
            struct.pack_into("<I", light_recs, r + 0x10 + k * 4, make_cell(rng))
    w.append((LIGHT_RECS, bytes(light_recs)))
    shapes = bytearray(rbytes(rng, 0x100))
    for i in range(10):
        struct.pack_into("<H", shapes, i * 2, rng.randrange(0x20, 0x100))
    w.append((SHAPES, bytes(shapes)))
    info = bytearray(rbytes(rng, 0x100))
    struct.pack_into("<I", info, 0xA8, SPOTS)
    struct.pack_into("<H", info, 0x2E, rng.randrange(0, 0x10))
    w.append((INFO, bytes(info)))
    w.append((SPOTS, rbytes(rng, 0x800)))
    grid = bytearray(rbytes(rng, 0x2000))
    for section in range(8):
        full = pick(rng, 0.0, 0.5, 0.9, 1.0)
        for i in range(section * 0x400, section * 0x400 + 0x400, 4):
            if rng.random() >= full:
                struct.pack_into("<I", grid, i, 0)
    w.append((GRID, bytes(grid)))
    w.append((0x5D7DE0, b"".join(p32(GRID + s * 0x400) for s in range(8))))
    w.append((0x5D7FE0, rbytes(rng, 0xC)))
    w.append((0x5D8000, rbytes(rng, 0x800)))
    w.append((0x6BFAE8, p32(nl) + p32(LIGHT_RECS)))
    w.append((0x6BFB2C, p32(SHAPES)))
    w.append((0x6C1EB0, p32(INFO)))
    w.append((OBJECTS, rbytes(rng, 0x800)))
    # glow, flares, texts, sound
    w.append((TEXTURES, rbytes(rng, 0x500)))
    w.append((0x6C1E14, p32(TEXTURES)))
    if name == "slot_glow_init":
        w.append((0x5CF860, rbytes(rng, 0x690 * 6)))
    if name in ("text_reset", "fonts_init"):
        for a, n in TEXT:
            w.append((a, rbytes(rng, n)))
        w.append((0x4AC86C, rbytes(rng, 0x34)))
    w.append((0x4D3CB8, rbytes(rng, 0xF8)))
    w.append((0x4C1B18, rbytes(rng, 4)))
    w.append((0x4D4560, rbytes(rng, 0x30)))
    w.append((0x5FF1E0, rbytes(rng, 0x100)))
    w.append((0x6CCE40, rbytes(rng, 4)))
    w.append((0x6B8D60, rbytes(rng, 0x10)))
    w.append((0x6B983C, rbytes(rng, 0x14)))
    w.append((IMAGE, rbytes(rng, 0x40)))
    w.append((LISTS, rbytes(rng, 0x40)))
    if name in ("pieces_init",):
        w.append((POOL, rbytes(rng, 0x10000)))
        w.append((HEAP, rbytes(rng, 0x800)))
    script = {
        "object_new": [pick(rng, OBJECTS + rng.randrange(8) * 0x100, 0) for _ in range(8)],
        "pool_take": [POOL],
        "heap_take": [HEAP, HEAP + 0x400],
        "font_load": [pick(rng, 0, 1) for _ in range(2)],
    }
    return w, script


def make_cell(rng):
    if rng.randrange(6) == 0:
        return 0
    first = rng.randrange(0, 32)
    last = pick(rng, rng.randrange(first, 32), rng.randrange(0, 32))
    return rng.randrange(0, 8) | rng.randrange(16) << 8 | first << 12 | rng.randrange(16) << 20 | last << 24


class Stubs:
    def __init__(self, m):
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _hook(self, uc, address, size, _):
        name, nargs, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        if name == "font_load":
            args[0] = bytes(uc.mem_read(args[0], 9))
        self.log.append((name, tuple(args)))
        values = self.script.get(name)
        eax = values.pop(0) if values else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF if returns else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args, writes = [], []
    for k in kinds:
        if k == "C":
            args.append(CARS + rng.randrange(6) * CAR_SIZE)
        elif k == "L":
            args.append(SLOTS[0] + rng.randrange(6) * 0x274)
        elif k == "k":
            args.append(rng.randrange(6))
        elif k == "i":
            args.append(pick(rng, rng.randrange(0, 0x20), rng.randrange(1, 8)))
        elif k == "w":
            a = ARGS + 0x100 * len(args)
            writes.append((a, b"".join(p32(make_cell(rng)) for _ in range(4)) + p32(0)))
            args.append(a)
        elif k == "p":
            a = ARGS + 0x100 * len(args)
            writes.append((a, rbytes(rng, 12)))
            args.append(a)
        elif k == "c":
            a = ARGS + 0x100 * len(args)
            writes.append((a, rbytes(rng, 4)))
            args.append(a)
        elif k == "r":
            args.append(pick(rng, rng.getrandbits(8), rng.getrandbits(32)))
        elif k == "s":
            a = ARGS + 0x100 * len(args)
            writes.append((a, rbytes(rng, 8)))
            args.append(a)
        elif k == "o":
            args.append(pick(rng, 0, OBJECTS + rng.randrange(8) * 0x100))
        elif k == "m":
            args.append(pick(rng, 0, 1, 2, rng.randrange(0, 0x300), rng.getrandbits(32)))
        elif k == "f":
            args.append(pick(rng, 0, 1, rng.getrandbits(32)))
        elif k == "h":
            args.append(LISTS + 4 * len(args))
        elif k == "I":
            a = IMAGE
            img = bytearray(rbytes(rng, 0x40))
            struct.pack_into("<I", img, 0xC, pick(rng, 0, rng.getrandbits(32)))
            struct.pack_into("<I", img, 0x18, pick(rng, 0, rng.getrandbits(32)))
            img[0x20] = pick(rng, 0, 1, rng.getrandbits(8))
            writes.append((a, bytes(img)))
            args.append(a)
        else:
            args.append(rng.getrandbits(32))
    return args, writes


def run_case(m, stubs, name, address, kinds, returns, regions, rng):
    state, script = make_state(rng, name)
    args, writes = make_args(rng, kinds)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        for a, data in state + writes:
            m.write(a, data)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(script)
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + m.read(*SLOTS) + b"".join(m.read(a, n) for a, n in regions)
        results.append((r, mem, stubs.log))
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), struct.unpack_from("<I", a, i)[0], struct.unpack_from("<I", b, i)[0])
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
    rng = random.Random(23)
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
