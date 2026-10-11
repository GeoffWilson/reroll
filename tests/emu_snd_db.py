#!/usr/bin/env python3
"""Differential test of snd_db.c (the sound engine 0x471000-0x473fff: voice
records, play / stop helpers, CD wrappers, resets and the mixer 0x4722d0)
against the original, on random voice tables and sound globals.

Everything left original (sound_play 0x471c90, 0x471da0, the EAX / COM
wrappers, the x87 3D placement 0x472bb0, the device side 0x46f000-0x470fff,
the CD player 0x496xxx, printf, allocation) is a logging stub in both runs;
the call logs and the memory are compared. The stack is zeroed before each
run (the mixer reads one stale stack byte, which ours takes as 0).

    tests/emu_snd_db.py EXE build/emu_db.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH = 0x01000000
VECS = SCRATCH  # 16 vectors, 0x10 apart
MATS = SCRATCH + 0x200  # 4 x 0x20
CFG = SCRATCH + 0x1000
RACE = SCRATCH + 0x2000
CDINFO = SCRATCH + 0x3000
ALLOCS = [SCRATCH + 0x3100, SCRATCH + 0x3200]
BUF = SCRATCH + 0x3400
STATE_SIZE = 0x4000

REGIONS = [(0x4AC984, 4), (0x4B1C00, 0x300), (0x4D4400, 0x800), (0x5FF2E0, 0x3D20), (0x6CB530, 0xF10)]
VOICES = 0x5FF4A0

# address: (name, number of args logged, masks, returns)
STUBS = {
    0x471C90: ("sound_play", 5, None, True),
    0x471DA0: ("sound_moving", 7, {0: 0xFF}, True),
    0x471740: ("eax_get_room", 1, None, True),
    0x471810: ("eax_get_room_hf", 1, None, True),
    0x4716C0: ("eax_set_room", 1, None, True),
    0x471780: ("eax_set_room_hf", 1, None, True),
    0x46F760: ("device_set_level", 1, None, False),
    0x4714C0: ("eax_voice_level", 2, None, True),
    0x471580: ("eax_voice_fade", 3, None, True),
    0x4291F0: ("nothing2", 0, None, False),
    0x479AE0: ("nothing", 1, None, False),
    0x428060: ("device_ok", 1, None, True),
    0x4445E0: ("alloc", 1, None, True),
    0x496C90: ("cd_set_volume", 2, None, False),
    0x496770: ("cd_play", 1, None, False),
    0x4967F0: ("cd_resume", 0, None, False),
    0x4967A0: ("cd_stop", 0, None, False),
    0x496890: ("cd_pause", 0, None, False),
    0x496690: ("cd_open", 0, None, False),
    0x496C50: ("cd_set_first", 1, None, False),
    0x496C80: ("cd_set_repeat", 1, None, False),
    0x497630: ("cd_track_count", 0, None, True),
    0x470EF0: ("device_frame", 0, None, False),
    0x46FD40: ("voice_free", 1, None, False),
    0x46FFD0: ("voice_pitch", 2, None, False),
    0x470040: ("voice_volume", 2, None, False),
    0x4700B0: ("voice_pos", 4, None, False),
    0x470160: ("voice_dir", 4, None, False),
    0x46FC40: ("voice_play", 3, None, False),
    0x46FFA0: ("voice_get", 1, None, True),
    0x496340: ("printf", 1, None, False),
    0x472BB0: ("voice_3d", 5, None, False),
    0x470740: ("debug_on", 0, None, False),
    0x470790: ("debug_off", 0, None, False),
    0x470E40: ("debug_clear", 0, None, False),
    0x4702A0: ("device_start", 0, None, False),
    0x470680: ("device_restart", 0, None, False),
    0x43692A: ("game_time", 0, None, True),
    0x472040: ("mix_thunk", 0, None, False),
}

# name: (address, argument kinds, return mask (None = void))
TESTS = {
    "sound_eax_open": (0x471370, "", 0xFFFFFFFF),
    "sound_eax_close": (0x4713B0, "", 0xFFFFFFFF),
    "sound_eax_first": (0x471700, "", 0xFFFFFFFF),
    "sound_cd_desc_init": (0x471850, "Ba", None),
    "sound_cd_apply": (0x4721F0, "B", None),
    "sound_cd_info_init": (0x471880, "B", None),
    "sound_voices_reset": (0x471970, "t", 0xFF),
    "sound_volume_mix": (0x471A60, "a", None),
    "voice_set_near": (0x471AA0, "va", None),
    "sound_settings_set": (0x471AC0, "aaa", None),
    "sound_cd_volume": (0x471AF0, "aa", None),
    "sound_cd_volume_keep": (0x471B30, "aa", None),
    "sound_volume_set": (0x471B80, "a", None),
    "sound_cd_channels": (0x471BA0, "aa", None),
    "sound_cd_play": (0x472220, "aB", 0xFFFFFFFF),
    "sound_cd_track": (0x471C00, "a", None),
    "sound_cd_command": (0x472250, "c", 0xFFFFFFFF),
    "sound_cd_pause": (0x471C30, "p", None),
    "voice_set_pitch": (0x471F80, "va", None),
    "voice_set_volume": (0x471FC0, "vaa", None),
    "voice_start": (0x471EB0, "vkaaaf", None),
    "sound_voice_stop": (0x471F40, "v", None),
    "sound_play_flagged": (0x471D50, "aaaaaf", 0xFFFFFFFF),
    "sound_moving_flagged": (0x471E60, "aaaaaaaf", 0xFFFFFFFF),
    "sound_distance": (0x471FF0, "VV", 0xFFFFFFFF),
    "sound_listener_set": (0x472050, "LPPM", None),
    "voice_alloc": (0x472920, "skaaa", 0xFF),
    "sound_play_volume": (0x472A40, "aaaaaw", 0xFFFFFFFF),
    "voice_play_plain": (0x472A90, "saa", 0xFFFFFFFF),
    "voice_play_plain_volume": (0x472B70, "saaW", 0xFFFFFFFF),
    "sound_debug_toggle": (0x472E90, "", None),
    "sound_debug_tick": (0x472ED0, "", None),
    "sound_mix": (0x4722D0, "", None),
    "sound_engine_start": (0x472F10, "", None),
    "sound_engine_restart": (0x472F70, "", None),
    "race_music_start": (0x472F90, "", None),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rand_float(rng):
    k = rng.randrange(4)
    if k == 0:
        return struct.pack("<f", rng.choice([0.0, 1.0, -1.0, 0.5, 8.0, -10000.0]))
    if k == 1:
        return struct.pack("<f", rng.uniform(-10000.0, 0.0))
    if k == 2:
        return struct.pack("<f", rng.uniform(-2.0, 2.0))
    return struct.pack("<I", rng.getrandbits(32))


def rand_vec(rng):
    scale = pick(rng, 1 << 18, 1 << 22, 1 << 26, 1 << 31)
    return b"".join(struct.pack("<i", rng.randrange(-scale, scale)) for _ in range(3))


def make_state(rng):
    mem = {a: bytearray(rng.getrandbits(8) for _ in range(n)) for a, n in REGIONS}

    def put(addr, fmt, value):
        for a, n in REGIONS:
            if a <= addr < a + n:
                if fmt[-1] in "iIhH" and isinstance(value, int):
                    value &= (1 << (8 * struct.calcsize(fmt))) - 1
                    fmt = fmt.upper()
                struct.pack_into(fmt, mem[a], addr - a, value)
                return
        raise KeyError(hex(addr))

    def putb(addr, data):
        for a, n in REGIONS:
            if a <= addr < a + n:
                mem[a][addr - a:addr - a + len(data)] = data
                return
        raise KeyError(hex(addr))

    count = pick(rng, 0, 1, 4, 16, rng.randrange(0, 65), rng.randrange(0, 65), 64, 70, -1)
    put(0x4B1C8C, "<i", count)
    put(0x4D4534, "<i", pick(rng, 0, 50, rng.randrange(-5, 120)))
    put(0x4AC984, "<i", pick(rng, 0, 0, 0, 1, rng.getrandbits(32)))
    put(0x4D4524, "<i", pick(rng, 0, 1, 1))
    put(0x4D4528, "<i", pick(rng, 0, 1, 1))
    put(0x4D45A0, "<H", pick(rng, 0, 0x4000, 0x3FFF, 0xFFFF, rng.getrandbits(16)))
    put(0x4D45A2, "<H", pick(rng, 0, 1, 2, 3, rng.getrandbits(16)))
    put(0x4D459C, "<I", pick(rng, 0, 1))
    put(0x4D4640, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    put(0x4D4648, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    put(0x4D464C, "<i", pick(rng, 0, 2, 3, 4, rng.getrandbits(32)))
    put(0x4B1CA4, "<i", pick(rng, -1, rng.getrandbits(32)))
    put(0x4B1CA8, "<i", pick(rng, -1, rng.getrandbits(32)))
    for a in (0x4B1EAC, 0x4D45F8, 0x4B1EA8):
        putb(a, rand_float(rng))
    put(0x4D45F4, "<i", rng.getrandbits(31))
    # EAX property sets: mostly empty
    sets = bytearray(0xF10)
    for _ in range(pick(rng, 0, 1, 3)):
        i = rng.randrange(64)
        struct.pack_into("<I", sets, i * 0x3C + 4, rng.getrandbits(32) | 1)
    for i in range(64):
        sets[i * 0x3C:i * 0x3C + 4] = rand_float(rng)
    mem[0x6CB530] = sets
    # vectors and listeners
    vecs = [rand_vec(rng) for _ in range(16)]
    base = rand_vec(rng)
    for i in range(16):
        if rng.randrange(2):  # near a common point
            b = struct.unpack("<3i", base)
            off = pick(rng, 1 << 16, 1 << 20, 1 << 23, 1 << 25)
            vecs[i] = struct.pack("<3i", *[(x + rng.randrange(-off, off) + 0x80000000) % (1 << 32) - 0x80000000
                                            for x in b])
    for slot in range(4):
        if slot == 0:  # listener 0 is read without a null check
            lpos = VECS + rng.randrange(16) * 0x10 if rng.randrange(40) else 0
        else:
            lpos = pick(rng, VECS + rng.randrange(16) * 0x10, VECS + rng.randrange(16) * 0x10, 0)
        put(0x4D4600 + slot * 4, "<I", lpos)
        put(0x4D4610 + slot * 4, "<I", pick(rng, VECS + rng.randrange(16) * 0x10, 0))
        put(0x4D4620 + slot * 4, "<I", rng.getrandbits(32))
        put(0x4D4630 + slot * 4, "<I", rng.getrandbits(32))
    for i in range(72):
        v = VOICES + i * 0x30
        put(v, "<B", pick(rng, 0, 1, 1, 0x40, 0x40, 0x41, rng.getrandbits(8)))
        put(v + 1, "<B", pick(rng, 0, 2, 0x10, 0x20, 0x12, 0x22, 0x30, 4, rng.getrandbits(8)))
        put(v + 2, "<B", pick(rng, 0, 1, 9, 9, rng.getrandbits(8)))
        put(v + 6, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
        for o in (3, 0x2C, 0x2D):
            put(v + o, "<B", pick(rng, 0x7F, 0x7F, rng.randrange(0, max(count, 1) + 2), rng.getrandbits(8)))
        put(v + 0x14, "<i", pick(rng, rng.randrange(0, 0x4000), rng.getrandbits(32) - 0x80000000))
        put(v + 0x24, "<I", VECS + rng.randrange(16) * 0x10 if rng.randrange(30) else 0)
        put(v + 0x28, "<I", pick(rng, VECS + rng.randrange(16) * 0x10, 0))
        if i < 64:
            put(0x5FF300 + i, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    put(0x6000D0, "<I", CDINFO)
    put(0x5FF384, "<I", rng.getrandbits(32))
    cfg = bytearray(rng.getrandbits(8) for _ in range(0x500))
    cfg[0x410] = pick(rng, 0, 1)
    cfg[0x411] = pick(rng, 0, 1)
    race = bytearray(rng.getrandbits(8) for _ in range(0x1000))
    race[0x579] = rng.randrange(20)
    if rng.randrange(2):
        struct.pack_into("<h", race, 0xA7E, -1)
    scratch = bytearray(rng.getrandbits(8) for _ in range(STATE_SIZE))
    for i, vec in enumerate(vecs):
        scratch[i * 0x10:i * 0x10 + 12] = vec
    scratch[0x1000:0x1500] = cfg
    scratch[0x2000:0x3000] = race
    script = {
        "sound_play": [pick(rng, 0x7F, rng.randrange(0, 70), rng.getrandbits(32)) for _ in range(4)],
        "sound_moving": [pick(rng, 0x7F, rng.randrange(0, 70), rng.randrange(0, 70), rng.getrandbits(32))
                         for _ in range(4)],
        "voice_get": [pick(rng, rng.randrange(0, 64), 0x40, 0xFF, 0x7F, rng.getrandbits(32)) for _ in range(4)],
        "device_ok": [pick(rng, 1, 1, 1, 0, rng.getrandbits(32)) for _ in range(4)],
        "alloc": list(ALLOCS),
        "cd_track_count": [rng.getrandbits(32) for _ in range(2)],
        "game_time": [rng.getrandbits(32) for _ in range(2)],
        "eax_get_room": [rng.getrandbits(32)],
        "eax_get_room_hf": [rng.getrandbits(32)],
    }
    return dict(mem=mem, scratch=scratch, script=script, count=count)


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
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        if name == "printf" and words[1] == 0x4B1EB0:
            nargs = 3
        args = list(words[1:1 + nargs])
        for i, mask in (masks or {}).items():
            args[i] &= mask
        for i, a in enumerate(args):
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        eax = self._next(name, 0) if returns else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, st):
    args = []
    for k in kinds:
        if k == "B":
            args.append(BUF)
        elif k == "V":
            args.append(VECS + rng.randrange(16) * 0x10)
        elif k == "P":
            args.append(pick(rng, 0, VECS + rng.randrange(16) * 0x10, VECS + rng.randrange(16) * 0x10))
        elif k == "M":
            args.append(pick(rng, 0, MATS, MATS + 0x20))
        elif k == "L":
            args.append(pick(rng, 0, 1, 2, 3, rng.getrandbits(8), rng.getrandbits(32)))
        elif k == "v":
            args.append(pick(rng, rng.randrange(0, 64), rng.randrange(0, 64), rng.getrandbits(32)))
        elif k == "t":
            args.append(pick(rng, 0xFF, 0x12340FF, rng.randrange(0, 20), rng.getrandbits(32)))
        elif k == "c":
            args.append(pick(rng, 3, 8, 9, rng.randrange(0, 12), rng.getrandbits(32)))
        elif k == "p":
            args.append(pick(rng, 0, 1, 0x100, rng.getrandbits(32)))
        elif k == "k":
            args.append(pick(rng, 9, 0, 1, 0x109, rng.getrandbits(32)))
        elif k == "f":
            args.append(pick(rng, 0, 1, 0x101, 2, rng.getrandbits(32)))
        elif k == "s":
            args.append(pick(rng, rng.randrange(0, 60), rng.randrange(0, 60), 0x80, rng.getrandbits(32)))
        elif k == "w":
            args.append(pick(rng, 0xFFFF, 0x1FFFF, rng.randrange(0, 0x8000), rng.getrandbits(32)))
        elif k == "W":
            args.append(pick(rng, 0xFFFFFFFF, 0xFFFF, rng.randrange(0, 0x8000), rng.getrandbits(32)))
        else:
            args.append(pick(rng, 0, 1, rng.randrange(0, 0x4000), rng.getrandbits(16), rng.getrandbits(32)))
    return args


def run_case(m, stubs, name, address, kinds, mask, rng):
    st = make_state(rng)
    args = make_args(rng, kinds, st)
    results = []
    for target in (address, m.symbols[name]):
        m.write(E.STACK, bytes(E.STACK_SIZE))
        m.write(SCRATCH, bytes(st["scratch"]))
        for a, data in st["mem"].items():
            m.write(a, bytes(data))
        m.write(0x4B1960, struct.pack("<I", RACE))
        m.write(0x4B2198, struct.pack("<I", CFG))
        stubs.reset(st["script"])
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
        mem = m.read(SCRATCH, STATE_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 4]}\n  ours:          {lb[k:k + 4]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x10000)
    stubs = Stubs(m)
    rng = random.Random(11)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, mask) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, mask, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
