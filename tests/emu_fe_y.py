#!/usr/bin/env python3
"""Differential test of fe_y.c (front end group Y: records and results
screens, world / league / players menus, option entries, track tables,
network player slots) against the original, on random options (save data),
race state and menu globals.

Functions outside the group are stubbed in both runs: each stub logs its
arguments (strings by content) and returns a scripted value; the string
lookups write scripted strings. Call logs are compared as well as memory.

    tests/emu_fe_y.py EXE build/emu_y.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x10000
OPTS = SCRATCH
RACE = SCRATCH + 0x1000
ENTRIES = SCRATCH + 0x2000  # 8 of 0x730
ARGS = SCRATCH + 0x6000  # 0x100 per argument
STRINGS = SCRATCH + 0x7000

STACK_FILL = bytes((i * 0x6B + 0x11) & 0xFF for i in range(E.STACK_SIZE))
OUTCOMES = {}

# address: (name, number of args, returns, kind); kind: "w" writes a string
# to arg 0, "p" sprintf, "s" logs string args
STUBS = {
    0x42E6D0: ("string_lookup", 4, False, "w"),
    0x436935: ("game_time", 0, True, ""),
    0x448140: ("entry_add", 5, True, ""),
    0x4481A0: ("menu_string", 4, False, "w"),
    0x448260: ("text_set", 2, False, ""),
    0x448430: ("car_name", 2, False, "w"),
    0x4484A0: ("car_kind_name", 2, False, "w"),
    0x448770: ("world_entry_action", 1, True, ""),
    0x448A00: ("menu_begin", 2, False, ""),
    0x448EC0: ("name_edit", 4, False, ""),
    0x449060: ("name_entry", 7, False, ""),
    0x44A1C0: ("text_append", 3, False, ""),
    0x44C6D0: ("option_cycle", 6, True, ""),
    0x44C850: ("option_toggle", 8, True, ""),
    0x44C9D0: ("option_list", 6, True, ""),
    0x44E560: ("worlds_prepare", 0, False, ""),
    0x44FFD0: ("submenu_open", 2, True, ""),
    0x4979F0: ("sprintf", 3, False, "p"),
}

# mutable globals: (address, size) randomised before and compared after
GLOBALS = [(0x4AF5F0, 4), (0x4AF5FC, 8), (0x4AF664, 4), (0x4AF794, 4), (0x4C2910, 4), (0x4C2F00, 0xC),
           (0x4C30E0, 0x40 * 0x1A0), (0x4C9DF8, 8), (0x4CA2F0, 0x14), (0x4CA528, 4), (0x4CB0D0, 8),
           (0x4CB4DC, 8), (0x4CB4F4, 8), (0x4CB524, 4), (0x4CB534, 4), (0x4CB594, 0xC), (0x4AC848, 4),
           (0x5D2CD0, 6 * 0x66C), (0x5D59A4, 4)]
COMPARE = [(0x4AF5F0, 0x210), (0x4C2900, 0x7000), (0x4C9DF0, 0x750), (0x4CB0D0, 0x4D0), (0x4AC840, 0x10),
           (0x5D2CD0, 6 * 0x66C), (0x5D59A0, 8), (0x5D5CD0, 0x30)]

# name: (address, argument kinds, returns)
TESTS = {
    "player_name_at": (0x4053E0, "s", True),
    "time_entry_fill": (0x405480, "Osli", False),
    "time_entry_make": (0x405460, "Osi", False),
    "record_is_new": (0x4058F0, "tp", True),
    "record_is_players": (0x405930, "tp", True),
    "record_name": (0x405970, "Otp", True),
    "hex_pair": (0x405B20, "Ov", False),
    "bcd_time_text": (0x405A80, "OB", False),
    "record_time_text": (0x405A10, "Otp", False),
    "slot_time_text": (0x405B70, "Osi", False),
    "slot_lap_time_text": (0x405BA0, "Os", False),
    "slot_race_time_text": (0x405BF0, "Os", False),
    "track_world": (0x433550, "T", True),
    "track_world_number": (0x433570, "T", True),
    "track_world_place": (0x4335C0, "T", True),
    "world_track": (0x4335E0, "wq", True),
    "world_track_count": (0x448670, "w", True),
    "world_locked_tracks": (0x4486B0, "w", True),
    "world_track_names": (0x4486F0, "w", True),
    "attract_timer_start": (0x448000, "", False),
    "player_network_name": (0x448530, "Os", False),
    "menu_text_add": (0x448A50, "Srrrrrrcc", True),
    "menu_entry_select": (0x448B50, "e", True),
    "menu_common_entry": (0x448BA0, "irrrr", True),
    "menu_entry_with_text": (0x448C00, "ZiZirrr", False),
    "menu_common_footer": (0x448BE0, "rr", False),
    "menu_back_entry": (0x448B10, "irrrr", True),
    "menu_back_footer": (0x448AF0, "irrr", True),
    "menu_continue_entry": (0x44E410, "irrr", True),
    "option_enabled_entry": (0x44C820, "rrrr", True),
    "deathmatch_report": (0x448D70, "rnrr", False),
    "player_name_entry": (0x44DFD0, "rr", False),
    "league_title": (0x44E2A0, "OL", False),
    "world_menu": (0x44E470, "xx", False),
    "worlds_menu_plain": (0x44E5B0, "rrrr", False),
    "worlds_menu": (0x44E640, "ee", False),
    "records_menu": (0x44EBF0, "wPrr", False),
    "records_menu_world2": (0x44EFA0, "rr", False),
    "records_menu_track": (0x44F000, "Trr", False),
    "splitscreen_report": (0x44F030, "rnirr", False),
    "game_points_menu": (0x44F5A0, "rrsi", False),
    "players_menu": (0x44FDB0, "rr", False),
    "options_submenu": (0x44FFB0, "r", True),
    "setup_difficulty_option": (0x450210, "rr", True),
    "timewarp_texture_option": (0x450620, "", True),
    "skidmarks_option": (0x450740, "", True),
    "weapons_option": (0x450E30, "", True),
    "mirror_option": (0x450EF0, "", True),
    "weather_option": (0x450F50, "", True),
    "league_menu": (0x451900, "rrii", False),
    "network_players_list": (0x45F570, "CA", False),
    "network_player_set": (0x48BE20, "sZr", True),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return rng.randbytes(n)


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def rstring(rng):
    n = pick(rng, 0, rng.randrange(1, 12), rng.randrange(1, 40))
    return bytes(rng.randrange(0x20, 0x7F) for _ in range(n))


def small(rng):
    return pick(rng, -1, 0, 1, 2, 3, rng.randrange(0, 8), rng.randrange(-3, 0x90), rng.getrandbits(32))


def make_state(rng):
    w = []
    opts = bytearray(rbytes(rng, 0x1000))
    opts[4] = pick(rng, 0, 1, 2, 3, 4, 5, rng.getrandbits(8))
    struct.pack_into("<H", opts, 0x12, pick(rng, 0, rng.getrandbits(16)))
    opts[0x1D] = pick(rng, 0x10, 0x11, 0x12, 0x13, 0, rng.getrandbits(8))
    opts[0x40] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    opts[0x41] = pick(rng, 1, 2, 3, 4, 5, 6, 0)
    opts[0x43] = pick(rng, 0, 1, rng.getrandbits(8))
    for i in range(8):
        opts[0x8C + i] = pick(rng, 0, 1)
    for r in range(16 * 6):
        a = 0x444 + r * 0xE
        opts[a:a + 3] = pick(rng, b"YY\x99", b"\0\0\0", rbytes(rng, 3), rbytes(rng, 3))
        name = rstring(rng)[:10]
        name += bytes(10 - len(name)) if rng.randrange(3) else rbytes(rng, 10 - len(name))
        opts[a + 3:a + 13] = name
    for i in range(7):  # network names: always terminated in the block
        s = rstring(rng)[:10] + b"\0"
        opts[0xD8C + i * 11:0xD8C + i * 11 + len(s)] = s
    opts[0xFFF] = 0
    w.append((OPTS, bytes(opts)))
    w.append((0x4B2198, p32(OPTS)))
    race = bytearray(rbytes(rng, 0x1000))
    for i in range(6):
        race[i] = pick(rng, 8, 10, rng.getrandbits(8))
        race[0x1C + i] = pick(rng, 0, 3, 6, 7, 8, rng.getrandbits(8))
    race[0x54D] = pick(rng, 0, 1, 2, 4, 6, rng.randrange(0, 7))
    race[0x585] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    race[0x720] = rng.getrandbits(8)
    race[0x721] = pick(rng, rng.getrandbits(8), rng.getrandbits(8) & 0xF0)
    w.append((RACE, bytes(race)))
    w.append((0x4B1960, p32(RACE)))
    w.append((ENTRIES, rbytes(rng, 8 * 0x730)))
    for a, n in GLOBALS:
        w.append((a, rbytes(rng, n)))
    w.append((0x4CB4B8, p32(ENTRIES)))
    w.append((0x4CB550, p32(pick(rng, 0, 1, 0x3F, 0x40, 0x41, rng.randrange(0, 0x41)))))
    w.append((0x4CB588, bytes([pick(rng, 0, 1, 2, 3, 0xFF, rng.getrandbits(8))])))
    w.append((0x4CB590, p32(pick(rng, 0, 1, rng.getrandbits(32)))))
    w.append((0x4AF64C, p32(pick(rng, -1, 0, 5, 0x81, 0x82, rng.randrange(-2, 0x84)))))
    w.append((0x4AF5F0, p32(pick(rng, -1, 0, rng.getrandbits(32)))))
    w.append((0x4C24C8, bytes(pick(rng, -1, 0, 1, 2, 3, 4, 5, 6, 7) & 0xFF for _ in range(6))))
    w.append((0x4CA84C, bytes(pick(rng, -1, 0, 1, 2, 3, 4, 5, 6, 7) & 0xFF for _ in range(6))))
    w.append((0x5D5CD4, p32(pick(rng, -1, 0, 1, 3, 6, 8, rng.randrange(-4, 10)))))
    script = {
        "str": [rstring(rng) for _ in range(64)],
        "game_time": [rng.getrandbits(32)],
        "entry_add": [pick(rng, -1, 0, 1, 2, 3, 4, 5, 6, 7) for _ in range(16)],
        "world_entry_action": [rng.getrandbits(32) for _ in range(8)],
        "option_cycle": [rng.getrandbits(32)],
        "option_toggle": [rng.getrandbits(32)],
        "option_list": [rng.getrandbits(32) for _ in range(2)],
        "submenu_open": [rng.getrandbits(32)],
    }
    return w, script


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

    def cstr(self, uc, a, limit=64):
        try:
            data = bytes(uc.mem_read(a, limit))
        except Exception:
            return ("bad", a)
        return data.split(b"\0")[0]

    def _hook(self, uc, address, size, _):
        name, nargs, returns, kind = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        logged = []
        for i, a in enumerate(args):
            if kind in ("w", "p") and i == 0:
                logged.append("buf" if E.STACK <= a < E.STACK + E.STACK_SIZE else a)
            elif E.STACK <= a < E.STACK + E.STACK_SIZE or SCRATCH <= a < SCRATCH + SCRATCH_SIZE \
                    or 0x4A7000 <= a < 0x4C0000:
                logged.append((a if not E.STACK <= a < E.STACK + E.STACK_SIZE else "stack", self.cstr(uc, a)))
            else:
                logged.append(a)
        if kind == "w":
            strings = self.script.get("str")
            s = strings.pop(0) if strings else b"?"
            uc.mem_write(args[0], s + b"\0")
            logged.append(s)
        elif kind == "p":
            fmt = self.cstr(uc, args[1]).decode("latin-1")
            v = struct.unpack("<i", p32(args[2]))[0]
            s = (fmt.replace("%02d", "%02d").replace("%d", "%d") % v).encode()
            uc.mem_write(args[0], s + b"\0")
        self.log.append((name, tuple(logged)))
        values = self.script.get(name)
        eax = values.pop(0) if values else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF if returns else 0x5A5A5A5A)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args, writes = [], []
    for k in kinds:
        slot = ARGS + 0x100 * len(args)
        if k == "O":
            writes.append((slot, rbytes(rng, 0x40)))
            args.append(pick(rng, slot, slot, slot, 0))
        elif k == "s":
            args.append(pick(rng, 0, 1, 2, 3, 4, 5, -1, 6, 7, rng.randrange(-3, 9)))
        elif k == "l":
            args.append(pick(rng, -1, 0, 5, 9, 10, rng.randrange(-3, 12)))
        elif k == "i":
            args.append(pick(rng, 0, 1, 1, rng.getrandbits(32)))
        elif k == "t":
            args.append(pick(rng, 0, 7, 15, 16, -1, rng.randrange(-1, 18)))
        elif k == "p":
            args.append(pick(rng, 0, 3, 5, 6, -1, rng.randrange(-1, 8)))
        elif k == "v":
            args.append(pick(rng, rng.getrandbits(8), rng.getrandbits(32)))
        elif k == "B":
            writes.append((slot, pick(rng, b"YY\x99", b"\0\0\0", b"YY\x98", b"\0\0\1", rbytes(rng, 3))))
            args.append(slot)
        elif k == "T":
            args.append(pick(rng, 0, 5, 14, 19, 20, -1, rng.randrange(-2, 23)))
        elif k == "w":
            args.append(pick(rng, 1, 2, 3, 4, 0, 5, -1, rng.randrange(-1, 7)))
        elif k == "q":
            args.append(rng.randrange(0, 6))
        elif k == "P":
            args.append(pick(rng, -1, -1, 0, 1, 2, 3, 4, 5, -2, 7))
        elif k == "S":
            writes.append((slot, rstring(rng) + b"\0"))
            args.append(slot)
        elif k == "Z":
            writes.append((slot, rstring(rng) + b"\0"))
            args.append(pick(rng, slot, slot, 0))
        elif k == "c":
            args.append(pick(rng, 0, -1, rng.getrandbits(32)))
        elif k == "e":
            args.append(pick(rng, -1, 0, 1, 2, 3, 0xFF, rng.getrandbits(32)))
        elif k == "n":
            args.append(pick(rng, 0, 1, 2, 4, 6, -1, rng.randrange(0, 7)))
        elif k == "L":
            args.append(pick(rng, 7, 8, 9, 6, 10, rng.getrandbits(32)))
        elif k == "x":
            args.append(pick(rng, -1, rng.getrandbits(32)))
        elif k == "C":
            writes.append((slot, p32(pick(rng, -1, 0, 1, 3, 6, 8, rng.randrange(-2, 10)))))
            args.append(slot)
        elif k == "A":
            writes.append((slot, rbytes(rng, 0x40)))
            args.append(slot)
        else:
            args.append(rng.getrandbits(32))
    return args, writes


def run_case(m, stubs, name, address, kinds, returns, rng):
    state, script = make_state(rng)
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
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in COMPARE)
        results.append((r, mem, stubs.log))
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
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
        return f"{name} {args}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(29)
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
        OUTCOMES.clear()
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {OUTCOMES}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
