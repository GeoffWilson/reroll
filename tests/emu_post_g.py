#!/usr/bin/env python3
"""Differential test of post_g.c (the snatch weapon entity 0x48dc40 and
its helpers, AI grudges, a few small checks) against the original, on
random snatch / car records.

Functions left original (sound, effects, sprite lists, placing cars,
network) are stubbed in both runs: each stub logs its arguments and
returns a scripted value, and the call logs are compared too.

    tests/emu_post_g.py EXE build/emu_g.elf [iterations]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH = 0x01000000
SNATCH, RACE, VEC, MATRIX = SCRATCH, SCRATCH + 0x3000, SCRATCH + 0x3C00, SCRATCH + 0x3C40
CARS = SCRATCH + 0x4000  # 6 cars, 0x800 each
SPRITES = SCRATCH + 0x7000  # 47 sprites, 0x120 each
STATE_SIZE = 0xA600
SLOTS = (0x6BFB80, 0x274 * 6)
TEXTURES = (0x6002E0, 0x6005F8 - 0x6002E0)
POINTERS = {0x5D1FC0: SNATCH, 0x4B1960: RACE}
GLOBALS = [(0x4B2D1C, 4), (0x4DA204, 2), (0x6CCD84, 1), (0x6B9D43, 1), (0x4B1C8C, 4), (0x4D3DAC, 4),
           (0x4DA1B8, 0x30), (0x4B11DC, 4)]

# address: (name, number of args, masks for args (None = all bits), returns)
STUBS = {
    0x4612A0: ("sprite_unlink", 1, None, False),
    0x460FF0: ("sprite_place", 4, None, False),
    0x419300: ("body_remove", 1, None, False),
    0x4611D0: ("grid_unlink", 1, None, False),
    0x471D50: ("sound_at", 6, {5: 0xFF}, False),
    0x471E60: ("sound_moving", 8, {7: 0xFF}, True),
    0x471F40: ("sound_stop", 1, {0: 0xFF}, False),
    0x42AA30: ("blast", 5, None, False),
    0x4687F0: ("car_place_test", 6, None, True),
    0x468F80: ("car_place_reset", 2, None, False),
    0x468BB0: ("car_place", 7, None, False),
    0x45EF80: ("net_is_game", 0, None, True),
    0x45EED0: ("net_is_host", 0, None, True),
    0x48CF20: ("net_local_car", 0, None, True),
    0x460A90: ("net_send_snatched", 0, None, False),
    0x4600A0: ("hit_message", 3, None, False),
    0x48F930: ("snatch_mesh", 1, None, False),
}

S, C, T, P = "S", "C", "T", "P"  # snatch, owner car, target car, any car
TESTS = {
    "snatch_update": (0x48DC40, "", False),
    "snatch_step": (0x48DC60, "S", False),
    "snatch_launch": (0x48DEA0, "S", False),
    "snatch_fly": (0x48E0C0, "S", True),
    "snatch_finish": (0x48DDD0, "Sb", False),
    "snatch_grab": (0x48E290, "S", False),
    "snatch_hold": (0x48E3C0, "S", False),
    "snatch_swap": (0x48E470, "S", False),
    "snatch_effects": (0x48EEF0, "S", False),
    "snatch_pick_target": (0x48DB80, "CS", False),
    "snatch_aim_point": (0x48EA80, "SV", False),
    "snatch_to_target": (0x48EA40, "VS", False),
    "snatch_side": (0x48EA10, "S", True),
    "snatch_save_matrix": (0x48E970, "S", False),
    "snatch_drop_point": (0x48E610, "SV", False),
    "snatch_copy_motion": (0x48E6A0, "S", False),
    "snatch_hide_sprites": (0x48EE70, "S", False),
    "snatch_ring_sprite": (0x48F130, "VVri6", False),
    "snatch_ring2_sprite": (0x48F2B0, "VVri6", False),
    "snatch_spark_reset": (0x48F430, "V", False),
    "snatch_spark_sprite": (0x48F4F0, "VVVSrkxx", False),
    "matrix_from_forward": (0x458F50, "VM", False),
    "car_facing": (0x48EB30, "PV", False),
    "vec_clamp": (0x48EBF0, "Vl", False),
    "body_pull_toward": (0x48EB80, "VS", False),
    "pad_effect_request": (0x41B4D0, "Dxxxxx", False),
    "car_clear_item": (0x467C90, "Pi", False),
    "car_kick_forward": (0x47A860, "Pi", False),
    "ai_grudge_reduce": (0x40FE00, "PPa", False),
    "ai_grudge_add": (0x40FEB0, "PPa", False),
    "ai_grudge_snatched": (0x4102B0, "S", False),
    "texture_anim_active": (0x46E3C0, "", True),
    "snatch_active": (0x4906C0, "", True),
    "race_early_ticks": (0x46A9C0, "", False),
}


def pick(rng, *choices):
    return rng.choice(choices)


def make_car(rng, index, count):
    car = bytearray(rng.getrandbits(8) for _ in range(0x800))
    for o in range(0x34, 0x40, 4):  # position
        struct.pack_into("<i", car, o, rng.randrange(-0x4000000, 0x4000000))
    for o in range(0x40, 0x4C, 4):
        struct.pack_into("<i", car, o, rng.randrange(-0x400000, 0x400000))
    for o in range(0x68, 0x74, 4):
        struct.pack_into("<i", car, o, rng.randrange(-0x400000, 0x400000))
    for o in range(0xC4, 0xD6, 2):
        struct.pack_into("<h", car, o, pick(rng, rng.randrange(-0x1000, 0x1001), rng.getrandbits(16) - 0x8000))
    struct.pack_into("<H", car, 0x64E, index)
    car[0x70C] = pick(rng, 8, 10, 8, rng.getrandbits(8))
    car[0x6E3] = pick(rng, 0xFF, 0xFF, rng.randrange(0, count + 1), rng.getrandbits(8))
    struct.pack_into("<I", car, 0x6E8, rng.getrandbits(32))
    car[0x6CB] = pick(rng, 0, rng.getrandbits(8))
    for i in range(8):
        struct.pack_into("<H", car, 0x6EC + i * 2, pick(rng, rng.randrange(0, 0x600), rng.getrandbits(16),
                                                         0xFFFF, 0xFFC0))
    for w in range(4):
        car[0x324 + w * 0xC4] = pick(rng, 1, 1, 1, 0, rng.getrandbits(8))
    struct.pack_into("<h", car, 0x710, pick(rng, 0, 1, rng.getrandbits(15)))
    car[0x63F] = pick(rng, 0, 1)
    car[0x67C] = pick(rng, 7 | 0x40, 7 | 0x60, 0xFF, rng.getrandbits(8))
    return car


def make_state(rng):
    count = rng.randrange(1, 7)
    cars = [make_car(rng, i, count) for i in range(6)]
    owner, target = rng.randrange(6), rng.randrange(6)
    snatch = bytearray(rng.getrandbits(8) for _ in range(0x3000))
    for o in range(0x34, 0x40, 4):
        struct.pack_into("<i", snatch, o, struct.unpack_from("<i", cars[target], o)[0] +
                         pick(rng, rng.randrange(-0x100000, 0x100000), rng.randrange(-0x4000000, 0x4000000)))
    for o in list(range(0x40, 0x4C, 4)) + list(range(0x68, 0x74, 4)):
        struct.pack_into("<i", snatch, o, rng.randrange(-0x400000, 0x400000))
    for o in range(0xC4, 0xD6, 2):
        struct.pack_into("<h", snatch, o, rng.randrange(-0x1000, 0x1001))
    for o in range(0x1AC, 0x1B8, 4):
        struct.pack_into("<i", snatch, o, pick(rng, rng.randrange(-0x10000, 0x10000), rng.randrange(-0x400, 0x400),
                                                rng.getrandbits(32) - 0x80000000, 0))
    state = pick(rng, 0, 1, 2, 4, 0x10, 0x40, 0x80, rng.getrandbits(32))
    struct.pack_into("<I", snatch, 0x188, state)
    struct.pack_into("<i", snatch, 0x180, pick(rng, rng.randrange(-2, 0x140), 0x28, 0x27, 299, 0x3b, 0x3a, 0x59,
                                                0x58, rng.getrandbits(32) - 0x80000000))
    struct.pack_into("<i", snatch, 0x18c, rng.randrange(-0x100000, 0x100000))
    struct.pack_into("<I", snatch, 0x194, CARS + owner * 0x800)
    struct.pack_into("<I", snatch, 0x198, CARS + target * 0x800 if rng.randrange(6) else 1)
    struct.pack_into("<I", snatch, 0x1A8, pick(rng, rng.randrange(0, 0x10000000), rng.getrandbits(32)))
    struct.pack_into("<i", snatch, 0x1DC, rng.getrandbits(32) - 0x80000000)
    struct.pack_into("<i", snatch, 0x33C, pick(rng, rng.randrange(0, 0xC0), rng.randrange(-0x100, 0x200), 0x7F, 0xAF))
    snatch[0x2A36] = pick(rng, 0xFF, rng.randrange(0, 0x40), rng.getrandbits(8))
    snatch[0x2A37] = pick(rng, 0xFF, rng.getrandbits(8))
    for i in range(5):
        struct.pack_into("<I", snatch, 0x1E0 + i * 4, SPRITES + i * 0x120)
    for i in range(32):
        struct.pack_into("<I", snatch, 0x1F4 + i * 4, SPRITES + (5 + i) * 0x120)
    for i in range(10):
        struct.pack_into("<I", snatch, 0x274 + i * 4, SPRITES + (37 + i) * 0x120)
        struct.pack_into("<iiiI", snatch, 0x29C + i * 0x10, rng.randrange(-0x40, 0x40), rng.randrange(-0x40, 0x40),
                         pick(rng, rng.randrange(-0x40, 0x1A0), 0x167, 0x168, 0x166, 0x5B, 0x5A, 0x127, 0x128,
                              rng.getrandbits(32) - 0x80000000), rng.getrandbits(32))
    race = bytearray(rng.getrandbits(8) for _ in range(0x600))
    race[0x54D] = count
    struct.pack_into("<i", race, 0x57C, pick(rng, 0, 3, 4, 5, rng.getrandbits(32) - 0x80000000))
    slots = bytearray(rng.getrandbits(8) for _ in range(SLOTS[1]))
    for i in range(6):
        struct.pack_into("<I", slots, i * 0x274, CARS + i * 0x800)
        struct.pack_into("<I", slots, i * 0x274 + 0x34, pick(rng, rng.randrange(0, 0x10000), rng.getrandbits(32)))
    textures = bytearray(TEXTURES[1])
    if rng.randrange(2):
        textures[rng.randrange(6) * 0x84] = rng.randrange(1, 256)
    if rng.randrange(4) == 0:
        textures[rng.randrange(TEXTURES[1])] = 1
    globals_ = [bytes(rng.getrandbits(8) for _ in range(n)) for _, n in GLOBALS]
    globals_[0] = struct.pack("<I", rng.randrange(1, 0x7FFFFFFF))
    globals_[2] = bytes([pick(rng, 0, 1, rng.getrandbits(8))])
    globals_[4] = struct.pack("<i", pick(rng, 0x20, 0x40, 0, rng.getrandbits(32) - 0x80000000))
    vec = bytes(rng.getrandbits(8) for _ in range(0x40))
    if rng.randrange(2):
        vec = b"".join(struct.pack("<i", pick(rng, rng.randrange(-0x20000, 0x20000), rng.randrange(-0x400, 0x400),
                                              rng.randrange(-0x4000000, 0x4000000))) for _ in range(16))
    matrix = bytes(rng.getrandbits(8) for _ in range(0x20))
    script = {
        "sound_moving": [rng.getrandbits(32) for _ in range(4)],
        "car_place_test": [(pick(rng, 0, 1, rng.getrandbits(32)), rng.choice([None, rng.getrandbits(32)]))
                           for _ in range(4)],
        "net_is_game": [pick(rng, 0, 1, 1, 2) for _ in range(4)],
        "net_is_host": [pick(rng, 0, 1, 1) for _ in range(4)],
        "net_local_car": [pick(rng, target, owner, rng.randrange(0, 8), rng.getrandbits(32)) for _ in range(4)],
    }
    return dict(snatch=snatch, cars=cars, race=race, slots=slots, textures=textures, globals=globals_, vec=vec,
                matrix=matrix, script=script, owner=owner, target=target)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        name, nargs, masks, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, mask in (masks or {}).items():
            args[i] &= mask
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                size = 4 if name == "car_place_test" else 12  # its out int / vectors
                args[i] = ("stack", bytes(uc.mem_read(a, size)))
        self.log.append((name, tuple(args)))
        eax = 0
        if name == "car_place_test":
            eax, out = self._next(name, (1, None))
            if out is not None:
                uc.mem_write(words[6], struct.pack("<I", out))
        elif returns:
            eax = self._next(name, 0)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, st):
    args = []
    vectors = iter([VEC, VEC + 0x10, VEC + 0x20])
    for k in kinds:
        if k == "S":
            args.append(SNATCH)
        elif k == "C":
            args.append(CARS + st["owner"] * 0x800)
        elif k == "P":
            args.append(CARS + rng.randrange(6) * 0x800)
        elif k == "D":
            args.append(CARS + rng.randrange(6) * 0x800 + 0x6F8)
        elif k == "V":
            args.append(next(vectors))
        elif k == "M":
            args.append(MATRIX)
        elif k == "6":
            args.append(SPRITES + rng.randrange(47) * 0x120 + 0x108)
        elif k == "b":
            args.append(pick(rng, 0, 1, 1, rng.getrandbits(32)))
        elif k == "r":
            args.append(pick(rng, rng.randrange(-0x10, 0x80), rng.getrandbits(32)))
        elif k == "i":
            args.append(pick(rng, rng.randrange(0, 0x20), 7, rng.getrandbits(32)))
        elif k == "k":
            args.append(rng.randrange(0, 10))
        elif k == "l":
            args.append(pick(rng, 0xE0000, rng.randrange(0, 0x100000), rng.getrandbits(32)))
        elif k == "a":
            args.append(pick(rng, 0x32, rng.randrange(0, 0x200), rng.getrandbits(16), rng.getrandbits(32)))
        else:
            args.append(pick(rng, 0, 1, rng.randrange(0, 0x40), rng.getrandbits(8), rng.getrandbits(32)))
    return args


def run_case(m, stubs, name, address, kinds, returns, rng):
    st = make_state(rng)
    args = make_args(rng, kinds, st)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(STATE_SIZE))
        m.write(SNATCH, bytes(st["snatch"]))
        m.write(RACE, bytes(st["race"]))
        m.write(VEC, st["vec"])
        m.write(MATRIX, st["matrix"])
        for i, car in enumerate(st["cars"]):
            m.write(CARS + i * 0x800, bytes(car))
        m.write(SLOTS[0], bytes(st["slots"]))
        m.write(TEXTURES[0], bytes(st["textures"]))
        for a, p in POINTERS.items():
            m.write(a, struct.pack("<I", p))
        for (a, n), v in zip(GLOBALS, st["globals"]):
            m.write(a, v)
        stubs.reset(st["script"])
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, STATE_SIZE) + m.read(*SLOTS) + b"".join(m.read(a, n) for a, n in GLOBALS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), struct.unpack_from("<i", a, i)[0], struct.unpack_from("<i", b, i)[0]) for i in diff[:8]])
        logs = "" if results[0][2] == results[1][2] else f"\n  calls: {results[0][2][:6]}\n  ours:  {results[1][2][:6]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x10000)
    stubs = Stubs(m)
    rng = random.Random(7)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
