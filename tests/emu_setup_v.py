#!/usr/bin/env python3
"""Differential test of setup_v.c (race setup group V: weather, falling
rock, special track objects, body pools, car texture table, freeze map,
pads, race clock, time warp and small helpers) against the original, on
random race, config, track and object state.

Functions outside the group (the allocator, sound, network checks, the
clock, body / sphere / script / shape helpers and the weather emitter
updates of post_j.c) are stubbed in both runs: each stub logs its arguments
and returns a scripted value (the allocator hands out the same addresses in
both runs), and the call logs are compared as well as memory.

    tests/emu_setup_v.py EXE build/emu_v.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x60000
RACE = SCRATCH  # 0xc00
CONFIG = SCRATCH + 0x1000
TRACK_INFO = SCRATCH + 0x1400
TRACK_LISTS = SCRATCH + 0x1800  # 0x200 entries of 0x10
CAMERAS = SCRATCH + 0x3800  # 8 dwords
EMITTERS = SCRATCH + 0x4000  # 6 of 0xca4
BODIES = SCRATCH + 0x9000  # 16 of 0x200
MODEL = SCRATCH + 0xB000  # 0x1400
LIST = SCRATCH + 0xC800  # special list
BUFFER = SCRATCH + 0xD000  # 0x3000 for strings / copies / pads / queues
HEAP = SCRATCH + 0x10000  # stub allocator, 0x50000
SLOTS = (0x6BFB80, 0x274 * 6)
POINTERS = {0x4B1960: RACE, 0x4B2198: CONFIG, 0x6C1EB0: TRACK_INFO, 0x6C1E14: TRACK_LISTS}
# global state the group writes (filled at random before each run, compared after)
REGIONS = [
    (0x5D6360, 0x18), (0x5D6248, 4), (0x5D6338, 0xC), (0x5D6240, 8), (0x4D5898, 4), (0x4D58E8, 0x18),
    (0x4B11DC, 4), (0x4B2C50, 0x14), (0x6CCCDC, 8),
    (0x6B9D44, 0x18),
    (0x6CE6FC, 0x64),
    (0x6B9850, 0x340),
    (0x5F72C0, 0x140), (0x5F7400, 0x1088 * 6),
    (0x6C30A0, 4), (0x6C3080, 4), (0x4BEB24, 8), (0x6C3E90, 8),
    (0x4BE388, 8), (0x4BE114, 4),
    (0x5D9B20, 0x10),
    (0x4ADAA4, 4), (0x6C25C0, 0x200),
]
FIXED = {0x5D6360, 0x4ADAA4, 0x6CE6FC}  # set up by make_state
STACK_FILL = bytes((i * 0x6B + 0x11) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, returns)
STUBS = {
    0x4445E0: ("alloc", 1, True),
    0x471C90: ("sound_play", 5, True),
    0x45EF80: ("net_is_game", 0, True),
    0x48CF20: ("net_local_car", 0, True),
    0x43692A: ("game_time", 0, True),
    0x4846C0: ("weather_indoors", 2, False),
    0x4848C0: ("weather_box", 3, False),
    0x4846F0: ("weather_drops_spawn", 3, False),
    0x460BA0: ("body_new", 4, True),
    0x465640: ("body_shape_set", 2, False),
    0x4611D0: ("grid_unlink", 1, False),
    0x45C010: ("shape_list_add", 1, False),
    0x419150: ("body_spheres_set", 4, False),
    0x462E30: ("body_script_start", 3, False),
    0x478400: ("body_flag_set", 2, False),
    0x475430: ("car_texture_frames", 1, False),
    0x4291F0: ("clock_nop", 0, False),
    0x428290: ("clock_read", 0, True),
    0x498540: ("char_upper", 1, True),
}

# name: (address, argument kinds, returns)
TESTS = {
    "time_stamp": (0x486160, "b", False),
    "weather_level_setup": (0x486170, "", False),
    "rain_setup": (0x483FA0, "", False),
    "snow_setup": (0x483FF0, "", False),
    "weather_emitter_setup": (0x483EA0, "e", False),
    "weather_flakes_setup": (0x4856A0, "X", False),
    "weather_setup": (0x483D90, "", False),
    "rock_setup": (0x457F80, "", False),
    "spray_reset": (0x43E620, "B", False),
    "special_script_start": (0x41AD20, "B", False),
    "special_body_setup": (0x4013F0, "B", False),
    "special_spheres4_setup": (0x45BFD0, "B", False),
    "screen_uv_setup": (0x41AEF0, "B", False),
    "track_specials_setup": (0x41AB80, "", False),
    "event_queue_setup": (0x434E70, "qb", False),
    "body_pools_setup": (0x418FA0, "", False),
    "car_texture_setup": (0x4750B0, "t", False),
    "car_textures_setup": (0x475090, "", False),
    "freeze_setup": (0x43A8D0, "", False),
    "pad_reset": (0x41B440, "b", False),
    "slots_controls_setup": (0x41B460, "L", True),
    "timer_callback_set": (0x429200, "r", False),
    "clock_start": (0x435110, "", False),
    "clock_restart": (0x4351A0, "", False),
    "time_warp_reset": (0x479720, "", False),
    "next_pow2": (0x41E1A0, "n", True),
    "mem_copy": (0x476F30, "ddN", False),
    "mem_fill": (0x476F50, "dcN", True),
    "archive_name_equal": (0x43B5A0, "ss", True),
    "file_cache_init": (0x43B730, "", False),
}

KINDS = [0x14, 0x19, 0x1D, 0x1F, 0x20, 0x23, 0x15, 0x1E, 0x24, 0x13, 0x0, 0x1FF]


def pick(rng, *choices):
    return rng.choice(choices)


def rand_int(rng):
    return pick(rng, rng.randrange(-0x40, 0x40), rng.randrange(0, 0x10000), rng.getrandbits(32) - 0x80000000,
                1 << rng.randrange(31), (1 << rng.randrange(31)) + pick(rng, -1, 1), 0, 1, -1, 0x7FFFFFFF,
                -0x80000000)


def name_string(rng):
    alphabet = b"abcABC_\\/.xyz09"
    s = bytearray(rng.choice(alphabet) for _ in range(rng.randrange(0, 12)))
    return s


def mangle(rng, s):
    """A string that names the same file (case, '\\' vs '/', '.' vs '_',
    '/' vs '__') or nearly."""
    out = bytearray()
    for c in s:
        ch = chr(c)
        r = rng.randrange(6)
        if ch == "/" and r < 2:
            out += pick(rng, b"\\", b"__")
        elif ch == "." and r < 2:
            out += b"_"
        elif ch.isalpha() and r < 3:
            out += ch.swapcase().encode()
        else:
            out.append(c)
    if rng.randrange(5) == 0 and out:
        out[rng.randrange(len(out))] = rng.choice(b"aZ_\\/.")
    return out


def make_state(rng):
    race = bytearray(rng.getrandbits(8) for _ in range(0xC00))
    race[0x54D] = pick(rng, rng.randrange(1, 7), rng.randrange(1, 7), 0)
    for i in range(6):
        race[i] = pick(rng, rng.randrange(0, 8), 8, 8, 10, rng.getrandbits(8))
    race[0x553] = pick(rng, 0, 1, 2, 1, 2)
    race[0x552] = pick(rng, 0, 1, 2, 3, 4, rng.randrange(5, 256))
    race[0x554] = pick(rng, 0, 1, rng.getrandbits(8))
    race[0x555] = pick(rng, 0, 1, rng.getrandbits(8))
    config = bytearray(rng.getrandbits(8) for _ in range(0x400))
    config[0x26] = pick(rng, 0, 1, 2, rng.getrandbits(8))
    info = bytearray(rng.getrandbits(8) for _ in range(0x100))
    struct.pack_into("<H", info, 0x2E, rng.randrange(0, 0x150))
    lists = bytes(rng.getrandbits(8) for _ in range(0x2000))
    cameras = bytes(rng.getrandbits(8) for _ in range(0x20))
    slots = bytearray(rng.getrandbits(8) for _ in range(SLOTS[1]))
    for i in range(6):
        struct.pack_into("<I", slots, i * 0x274, BODIES + i * 0x200)
        struct.pack_into("<I", slots, i * 0x274 + 0x28, CAMERAS + i * 4)
    emitters = bytes(rng.getrandbits(8) for _ in range(6 * 0xCA4))
    bodies = bytearray(rng.getrandbits(8) for _ in range(16 * 0x200))
    for i in range(16):
        b = i * 0x200
        kind = rng.choice(KINDS)
        struct.pack_into("<I", bodies, b, (rng.getrandbits(32) & ~0x1FF) | kind)
        struct.pack_into("<I", bodies, b + 0x14, MODEL)
        struct.pack_into("<H", bodies, b + 0x138, pick(rng, 0, rng.randrange(1, 0x40), rng.getrandbits(16)))
    model = bytes(rng.getrandbits(8) for _ in range(0x1400))
    count = pick(rng, 0, rng.randrange(1, 12), rng.randrange(1, 12))
    special = b"".join(struct.pack("<I", BODIES + rng.randrange(16) * 0x200) for _ in range(12))
    regions = {}
    for a, n in REGIONS:
        regions[a] = bytes(rng.getrandbits(8) for _ in range(n))
    # emitter pointers and the emitter count
    regions[0x5D6360] = b"".join(struct.pack("<I", EMITTERS + i * 0xCA4) for i in range(6))
    em = bytearray(regions[0x5D6248])
    struct.pack_into("<I", em, 0, pick(rng, rng.randrange(1, 7), rng.randrange(3, 7)))
    regions[0x5D6248] = bytes(em)
    regions[0x4ADAA4] = struct.pack("<i", pick(rng, 0, -1, rng.randrange(1, 0x40)))
    cache = bytearray(regions[0x4BEB24])
    struct.pack_into("<I", cache, 4, pick(rng, 0, 0, 1, rng.getrandbits(32)))  # 0x4beb28
    regions[0x4BEB24] = bytes(cache)
    spec = bytearray(regions[0x6CE6FC])
    struct.pack_into("<I", spec, 0, count)
    struct.pack_into("<I", spec, 0x6CE738 - 0x6CE6FC, LIST)
    regions[0x6CE6FC] = bytes(spec)
    buffer = bytes(rng.getrandbits(8) for _ in range(0x3000))
    script = {
        "sound_play": [pick(rng, 0x7F, rng.randrange(0, 0x40), rng.getrandbits(32)) for _ in range(8)],
        "net_is_game": [pick(rng, 0, 1)],
        "net_local_car": [rng.randrange(0, 6)],
        "game_time": [rng.getrandbits(32)],
        "body_new": [BODIES + rng.randrange(16) * 0x200],
        "clock_read": [rng.getrandbits(32)],
    }
    return dict(race=race, config=config, info=info, lists=lists, cameras=cameras, slots=slots,
                emitters=emitters, bodies=bodies, model=model, special=special, regions=regions,
                buffer=buffer, script=script)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.heap = HEAP
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.heap = HEAP

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        name, nargs, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        if name == "char_upper":
            c = args[0] & 0xFF
            eax = c - 0x20 if 0x61 <= c <= 0x7A else c
            args = [c]
        elif name == "alloc":
            eax = self.heap
            self.heap += (args[0] + 0xF) & ~0xF & 0xFFFFF
            if self.heap > HEAP + 0x50000:
                self.heap = HEAP
        else:
            eax = self._next(name, 0) if returns else 0
        self.log.append((name, tuple(args)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args = []
    ecx = None
    strings = []
    for k in kinds:
        if k == "b":
            args.append(BUFFER + rng.randrange(0, 0x100) * 4)
        elif k == "B":
            args.append(BODIES + rng.randrange(16) * 0x200)
        elif k == "e":
            args.append(pick(rng, rng.randrange(0, 6), rng.randrange(0, 6), 0))
        elif k == "X":
            ecx = TRACK_LISTS + rng.randrange(0, 0x200) * 0x10
        elif k == "q":
            args.append((rng.getrandbits(16) << 16) | pick(rng, 0, 1, rng.randrange(0, 0x200), 100))
        elif k == "t":
            args.append(rng.randrange(0, 6))
        elif k == "L":
            args.append(SLOTS[0])
        elif k == "r":
            args.append(rng.getrandbits(32))
        elif k == "n":
            args.append(rand_int(rng) & 0xFFFFFFFF)
        elif k == "d":
            args.append(BUFFER + rng.randrange(0, 0x1000))
        elif k == "N":
            args.append(pick(rng, 0, 1, 2, 3, 4, 5, 6, 7, rng.randrange(0, 0x400)))
        elif k == "c":
            args.append(rng.getrandbits(32))
        elif k == "s":
            if strings and rng.randrange(3):
                s = mangle(rng, strings[0])
            else:
                s = name_string(rng)
            strings.append(s)
            args.append(s)
    # strings go into the buffer
    out = []
    offset = 0x2000
    for a in args:
        if isinstance(a, bytearray):
            out.append((BUFFER + offset, bytes(a) + b"\0"))
            offset += 0x40
        else:
            out.append((a, None))
    return out, ecx


def run_case(m, stubs, name, address, kinds, returns, rng):
    st = make_state(rng)
    if name in ("weather_flakes_setup", "weather_level_setup", "weather_emitter_setup") and rng.randrange(4) == 0:
        st["race"][0x553] = rng.randrange(3, 256)  # no track has these; weather_setup would pass a leftover ecx
    args, ecx = make_args(rng, kinds)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        m.write(RACE, bytes(st["race"]))
        m.write(CONFIG, bytes(st["config"]))
        m.write(TRACK_INFO, bytes(st["info"]))
        m.write(TRACK_LISTS, st["lists"])
        m.write(CAMERAS, st["cameras"])
        m.write(EMITTERS, st["emitters"])
        m.write(BODIES, bytes(st["bodies"]))
        m.write(MODEL, st["model"])
        m.write(LIST, st["special"])
        m.write(BUFFER, st["buffer"])
        m.write(SLOTS[0], bytes(st["slots"]))
        for a, p in POINTERS.items():
            m.write(a, struct.pack("<I", p))
        for a, v in st["regions"].items():
            m.write(a, v)
        for a, text in args:
            if text is not None:
                m.write(a, text)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"])
        if ecx is not None:
            m.uc.reg_write(UC_X86_REG_ECX, ecx)
        r = m.call(target, [a for a, _ in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + m.read(*SLOTS) + b"".join(m.read(a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), struct.unpack_from("<I", a, i)[0], struct.unpack_from("<I", b, i)[0])
                                for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs, results[0][0]
    return None, results[0][0]


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(23)
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
        errors = 0
        for _ in range(iterations):
            msg, r = run_case(m, stubs, name, address, kinds, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs failed the same way in both)" if errors else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
