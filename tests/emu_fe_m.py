#!/usr/bin/env python3
"""Differential test of fe_m.c (front end group M: menu page layout, menu
item placing, the driver / time-attack / difficulty menus, the front-end
mode dispatcher and the network/serial set-up helpers) against the
original, on random menu pages, items and globals.

Everything outside the group (text strings, colour pulses, text lookups,
sprintf, menu building, network transport, the allocator and fill) is
stubbed in both runs: each stub logs its arguments (strings and buffers on
the stack by content) and returns scripted values; stubs that fill a buffer
fill it the same way in both runs. Call logs, return values and memory are
compared.

    tests/emu_fe_m.py EXE build/emu_m.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x20000
ROWS = SCRATCH  # up to 16 rows of 0x730, then filler
CONFIG = SCRATCH + 0x10000
RACE = SCRATCH + 0x11000
OUT = SCRATCH + 0x12000  # output text buffers
ITEM = SCRATCH + 0x13000  # a loose item for menu_item_place
COLOURS = SCRATCH + 0x14000
ROW_SIZE = 0x730
# game data the group reads or writes (filled at random, compared)
DATA = [(0x4AC800, 0x800), (0x4AF500, 0x800), (0x4BE300, 0x100), (0x4C2900, 0x8D00), (0x5D5300, 0x1000),
        (0x4CB100, 0x100)]
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, returns, buffer action)
STUBS = {
    0x42EAB0: ("string_hide", 1, True, None),
    0x42EA40: ("string_show", 5, False, None),
    0x42ED10: ("string_bounds", 5, True, None),
    0x42ED40: ("string_place", 5, True, None),
    0x42EED0: ("string_justify", 3, True, None),
    0x42F0F0: ("string_colours", 5, True, None),
    0x42F2B0: ("string_colour", 3, True, None),
    0x4328C0: ("colour_pulse", 4, False, "pulse"),
    0x405480: ("time_split", 4, True, "split"),
    0x405A80: ("time_text", 2, True, None),
    0x405BA0: ("time_best_text", 2, True, None),
    0x42E6D0: ("text_lookup", 4, True, "text"),
    0x4979F0: ("sprintf", 3, True, None),
    0x432CD0: ("mode_start", 4, True, None),
    0x430AE0: ("player_count_text", 1, True, None),
    0x447650: ("menu_open", 1, True, None),
    0x448140: ("menu_add", 5, True, None),
    0x4481A0: ("menu_text", 4, True, "text"),
    0x4484A0: ("car_name_text", 2, True, "text2"),
    0x4486F0: ("world_name", 1, True, None),
    0x448A00: ("menu_title", 2, True, None),
    0x448A50: ("menu_label", 9, True, None),
    0x448AF0: ("menu_buttons", 4, True, None),
    0x448B50: ("menu_select", 1, True, None),
    0x44C9D0: ("menu_option", 6, True, None),
    0x44FFD0: ("menu_list", 2, True, None),
    0x48A6B0: ("net_close", 0, True, None),
    0x48ACE0: ("net_open", 3, True, None),
    0x48BC40: ("net_start", 0, True, None),
    0x476B80: ("heap_take", 1, True, None),
    0x476F50: ("mem_fill", 3, True, None),
}
ALL = set(STUBS)

# name: (address, argument kinds, compare the result)
TESTS = {
    "time_parts": (0x405590, "OII", True),
    "time_parts_text": (0x405BC0, "OII", True),
    "front_mode_start": (0x4329B0, "M", True),
    "menu_item_reset": (0x445650, "TBB", True),
    "menu_row_reset": (0x4455C0, "R", "row"),
    "menu_extra_row_reset": (0x4456E0, "", True),
    "menu_item_place": (0x445F50, "TXXJJ", True),
    "menu_loose_items_place": (0x446180, "", True),
    "menu_title_place": (0x4461B0, "", True),
    "menu_layout": (0x445790, "XXXXXX", True),
    "menu_back_to_main": (0x447ED0, "", True),
    "lap_title_text": (0x4483F0, "OL", True),
    "world_number": (0x448730, "W", True),
    "track_title_text": (0x4485B0, "OWK", True),
    "time_attack_report": (0x4488C0, "IIII", True),
    "front_fade_in": (0x44CE50, "", False),
    "driver_choose": (0x44FCD0, "IC", True),
    "driver_menu": (0x44FAE0, "PII", "player"),
    "driver_menu_1": (0x44FAC0, "II", True),
    "driver_menu_2": (0x44FD50, "II", True),
    "driver_menu_3": (0x44FD70, "II", True),
    "driver_menu_4": (0x44FD90, "II", True),
    "list_menu_open": (0x450150, "", True),
    "difficulty_option": (0x450460, "I", True),
    "menu_input_delay": (0x452940, "", False),
    "network_flag9_set": (0x45F790, "I", True),
    "network_buffer_reset": (0x48C990, "", True),
    "network_restart": (0x48A840, "", True),
    "serial_baud": (0x48CFF0, "S", True),
    "serial_stop_bits": (0x48D070, "S", True),
    "serial_parity": (0x48D090, "S", True),
    "serial_flow": (0x48D0E0, "S", True),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rand_int(rng):
    return pick(rng, rng.randrange(-0x100, 0x400), rng.randrange(-0x10000, 0x10000), rng.getrandbits(32) - 0x80000000)


def make_item(rng, item, colour_choices):
    struct.pack_into("<i", item, 0, pick(rng, -1, rng.randrange(0, 0x80), rng.randrange(0, 0x80)))
    item[4] = pick(rng, 0, 1)
    item[5] = pick(rng, 0, 1, 1, 1)
    item[6] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    for o in (8, 0xC):
        struct.pack_into("<i", item, o, pick(rng, -1, -1, -100, rng.randrange(-50, 640)))
    for o in (0x10, 0x14):
        struct.pack_into("<i", item, o, pick(rng, 0, rng.randrange(-40, 40)))
    item[0x18] = pick(rng, 0xFE, 0, 1, 2, 3, rng.getrandbits(8))
    item[0x19] = pick(rng, 0xFE, 0, 1, 2, 3, rng.getrandbits(8))
    struct.pack_into("<i", item, 0x3C, rand_int(rng))
    struct.pack_into("<i", item, 0x40, pick(rng, -1, -1, 0, 1, 2))
    flags = rng.getrandbits(32) if rng.randrange(3) else rng.getrandbits(32) & 0x0FFFFFFF
    struct.pack_into("<I", item, 0x44, flags)
    struct.pack_into("<I", item, 0x190, pick(rng, 0, 0, *colour_choices))


def make_state(rng):
    colours = [COLOURS + 4 * k for k in range(8)]
    count = pick(rng, rng.randrange(0, 15), rng.randrange(1, 15), rng.randrange(10, 30), 0)
    rows = bytearray(rng.getrandbits(8) for _ in range(0x10000))
    for i in range(0x10000 // ROW_SIZE):
        r = bytearray(rows[i * ROW_SIZE:(i + 1) * ROW_SIZE])
        r[1] = pick(rng, 0, 1)
        r[2] = pick(rng, 0, 1)
        for k in (4, 5, 6):
            r[k] = pick(rng, 0, 0, 0, 0, 0, 1, rng.getrandbits(8)) if k != 4 or rng.randrange(3) else 0
        r[7] = pick(rng, 1, 2, 3, 0, rng.getrandbits(8))
        for o in (0xC, 0x1AC, 0x34C):
            item = bytearray(r[o:o + 0x1A0])
            make_item(rng, item, colours)
            r[o:o + 0x1A0] = item
        struct.pack_into("<i", r, 0x4F4, rand_int(rng))
        rows[i * ROW_SIZE:(i + 1) * ROW_SIZE] = r
    data = {a: bytearray(rng.getrandbits(8) for _ in range(n)) for a, n in DATA}

    def put(address, fmt, value):
        for a, n in DATA:
            if a <= address < a + n:
                struct.pack_into(fmt.replace("i", "I"), data[a], address - a, value & 0xFFFFFFFF)
                return
        raise ValueError(hex(address))

    put(0x4AF5FC, "<i", pick(rng, 0, 1, 2, 3))
    put(0x4AF600, "<i", pick(rng, 0, 1, 2, 3))
    put(0x4AF5F4, "<i", pick(rng, -1, -1, rng.randrange(0, 700)))
    put(0x4AF5F8, "<i", pick(rng, -1, -1, rng.randrange(0, 500)))
    put(0x4AF604, "<i", pick(rng, -1, -1, rng.randrange(0, 14)))
    put(0x4AF608, "<i", pick(rng, -1, -1, 0, 1))
    put(0x4AF658, "<I", rng.getrandbits(32))
    for k in range(7):
        data[0x4AF500][0x4AF658 + k - 0x4AF500] = pick(rng, rng.randrange(0, 8), 6)
    put(0x4AF790, "<I", rng.getrandbits(32))
    put(0x4AC988, "<i", rand_int(rng))
    put(0x4AC98C, "<i", rand_int(rng))
    put(0x4AC990, "<i", rng.randrange(0, 0x101))
    put(0x4CB49C, "<i", count)
    put(0x4CB4B8, "<I", ROWS)
    put(0x4CB48C, "<i", pick(rng, 0, 1))
    for a in (0x4CB4BC, 0x4CB4C0):
        put(a, "<i", pick(rng, rng.randrange(0, 200), rng.randrange(-20, 400), rng.randrange(100, 700)))
    for a in (0x4CB4D4, 0x4CB4D8, 0x4CB4DC, 0x4CB4E0, 0x4CB4E4, 0x4CB4E8):
        put(a, "<i", pick(rng, rng.randrange(-50, 640), rand_int(rng)))
    put(0x4CB4FC, "<i", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    for a in (0x4CB4F4, 0x4CB4F8, 0x4CB504, 0x4CB590):
        put(a, "<i", pick(rng, 0, 1, rng.getrandbits(32)))
    cursor = pick(rng, rng.randrange(-2, count + 2), rng.randrange(0, max(count, 1)), -1, rng.getrandbits(8))
    first = pick(rng, rng.randrange(-2, count + 2), -1, 0, rng.getrandbits(8))
    data[0x4C2900][0x4CB588 - 0x4C2900] = cursor & 0xFF
    data[0x4C2900][0x4CB589 - 0x4C2900] = first & 0xFF
    # loose items and the title
    for base in [0x4C2920] + list(range(0x4C30E0, 0x4C98E0, 0x1A0)):
        item = bytearray(0x1A0)
        make_item(rng, item, colours)
        o = base - 0x4C2900
        data[0x4C2900][o:o + 0x1A0] = item
    put(0x5D611C, "<I", pick(rng, 0, 0x02000000 + rng.randrange(0, 0x1000)))
    item = bytearray(rng.getrandbits(8) for _ in range(0x1A0))
    make_item(rng, item, colours)
    config = bytes(rng.getrandbits(8) for _ in range(0x100))
    config = bytearray(config)
    for k in range(4):
        config[0x18 + k] = pick(rng, rng.randrange(0, 8), rng.getrandbits(8))
    race = bytearray(rng.getrandbits(8) for _ in range(0x800))
    script = {
        "string_place": [rand_int(rng)], "string_bounds": [rand_int(rng)],
        "mode_start": [pick(rng, 0, 1, rng.getrandbits(32))],
        "player_count_text": [rand_int(rng)],
        "menu_add": [rng.randrange(0, 0x80) for _ in range(8)],
        "menu_label": [rand_int(rng) for _ in range(12)],
        "menu_buttons": [rand_int(rng)], "menu_open": [rand_int(rng)], "menu_list": [rand_int(rng)],
        "menu_option": [rand_int(rng)], "time_text": [rand_int(rng)], "time_split": [rand_int(rng)],
        "sprintf": [rand_int(rng)],
        "world_name": [pick(rng, 0, 0x4B0000 + rng.randrange(0, 0x1000))],
        "net_open": [pick(rng, 0, 1, rng.getrandbits(32))], "net_start": [pick(rng, 0, 1, 1)],
        "heap_take": [0x02000000 + rng.randrange(0, 0x1000)], "mem_fill": [rand_int(rng)],
        "text_len": [rng.randrange(0, 40) for _ in range(16)],
    }
    return dict(rows=rows, data=data, item=item, config=config, race=race, script=script, count=count,
                colours=bytes(rng.getrandbits(8) for _ in range(0x40)))


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

    def _text(self, uc, address, args):
        n = self._next("text_len", 5)
        seed = sum(a if isinstance(a, int) else 0 for a in args) & 0xFF
        text = bytes(0x41 + (seed + k) % 26 for k in range(n)) + b"\0"
        uc.mem_write(address, text)

    def _hook(self, uc, address, size, _):
        name, nargs, returns, action = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        raw = list(args)
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                blob = bytes(uc.mem_read(a, 0x200))
                if name == "string_colours":
                    b = bytearray(blob[:16])
                    for k in (3, 7, 11, 15):
                        b[k] = 0
                    args[i] = ("colours", bytes(b))
                elif name in ("time_text", "time_best_text", "sprintf") and i == 0:
                    args[i] = ("buffer",)  # output: leftover stack until written
                elif name == "time_text":
                    args[i] = ("parts", blob[:16])
                elif action in ("text", "text2", "pulse", "split") and i == 0:
                    args[i] = ("buffer",)
                else:
                    args[i] = ("text", blob[:blob.index(b"\0")] if b"\0" in blob else blob)
        self.log.append((name, tuple(args)))
        if action in ("text", "text2"):
            self._text(uc, raw[0], args[1:])
        elif action == "pulse":
            uc.mem_write(raw[0], bytes((raw[1] + raw[3] * 7 + k) & 0xFF for k in range(16)))
        elif action == "split":
            uc.mem_write(raw[0], bytes((raw[1] ^ raw[2] + k) & 0xFF for k in range(16)))
        eax = self._next(name, 0) if returns else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds, st):
    args = []
    for k in kinds:
        if k == "O":
            args.append(OUT + rng.randrange(0, 4) * 0x200)
        elif k == "I":
            args.append(rand_int(rng) & 0xFFFFFFFF)
        elif k == "M":
            args.append(pick(rng, *range(0, 12), rng.getrandbits(32)))
        elif k == "T":
            args.append(pick(rng, ITEM, ITEM, 0))
        elif k == "B":
            args.append(pick(rng, 0, 1, 2, rng.getrandbits(32)))
        elif k == "R":
            args.append(pick(rng, ROWS + rng.randrange(0, 8) * ROW_SIZE, 0x4C9DF8, 0))
        elif k == "X":
            args.append(pick(rng, rng.randrange(-50, 640), rng.randrange(-50, 640), -1, -100) & 0xFFFFFFFF)
        elif k == "J":
            args.append(pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))
        elif k == "L":
            args.append(rng.randrange(0, 6))
        elif k == "W":
            args.append(pick(rng, *range(0, 8), rng.getrandbits(32)))
        elif k == "K":
            args.append(pick(rng, 100, 0x65, rng.randrange(0, 8), -1 & 0xFFFFFFFF, rng.getrandbits(32)))
        elif k == "C":
            args.append(pick(rng, *range(-1, 9), rng.getrandbits(32)))
        elif k == "P":
            args.append(pick(rng, 0, 1, 2, 3, 4, rng.getrandbits(32)))
        elif k == "S":
            args.append(pick(rng, *range(-1, 11), rng.getrandbits(32)))
        else:
            args.append(rng.getrandbits(32))
    return args


def run_case(m, stubs, name, address, kinds, compare, rng):
    st = make_state(rng)
    args = make_args(rng, kinds, st)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        m.write(ROWS, bytes(st["rows"]))
        m.write(CONFIG, bytes(st["config"]))
        m.write(RACE, bytes(st["race"]))
        m.write(ITEM, bytes(st["item"]))
        m.write(COLOURS, st["colours"])
        for a, content in st["data"].items():
            m.write(a, bytes(content))
        m.write(0x4B2198, struct.pack("<I", CONFIG))
        m.write(0x4B1960, struct.pack("<I", RACE))
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"])
        r = m.call(target, args)
        if r[0] == "ok":
            if compare is False or (compare == "row" and args[0] == 0) or (compare == "player" and args[0] > 3):
                r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in DATA) + m.read(0x4B2198, 4)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), struct.unpack_from("<i", a, i)[0], struct.unpack_from("<i", b, i)[0])
                                for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name} {[hex(x) for x in args]}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    m.uc.mem_map(0x02000000, 0x10000)
    stubs = Stubs(m)
    rng = random.Random(23)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, kinds, compare) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, compare, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
