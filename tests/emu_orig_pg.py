#!/usr/bin/env python3
"""Differential test of orig_pg.c (group PG: the rest of the HUD file, the
archive / CPU helpers, the leader's hit callback, the light slots and the
lens flares, 0x438df0-0x442060) against the original, on random state.

Callees outside the group are logging stubs in both runs (each logs the
arguments it reads; strings and buffers by content, stack buffers as
"stack"): the HUD sprite set-up 0x4387d0 (returns a scratch sprite), text
create / free, the name list 0x45f650 and driver names (fill their
buffers), messages 0x4600a0, 0x469d60, the blast / missile end, the archive
calls (0x43b630 also logs the allocator hooks 0x4b4a84 / 88 it sees),
track_cell, the cell rows 0x440c60 (fills its buffer), light_set,
light_cells_add, mesh_rows_touch. Pure game code runs for real in both: the
GTE transform / projection behind 0x6ccd78 / 0x6ccd80 (0x426a20 /
0x426b00, x87), _ftol, vec_length / vec_div, direction_fit, matrix_look,
the int16 matrix helpers, and the group's own functions called inside the
group. The x87 control word is 0x27f, 0x37f or 0x07f. Compared: the whole
data section 0x4a7000-0x6d3000 (but the GTE vector's pad word 0x6ccc6a,
leftover stack in the original's 0x43a3e0), the scratch memory, the return
value, ebx / esi / edi / ebp / esp, the x87 top and tag word, the call log.

    tests/emu_orig_pg.py EXE build/emu_pg.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG)

DATA, DATA_SIZE = 0x4A7000, 0x6D3000 - 0x4A7000
SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
RACE = SCRATCH + 0x0000  # 0x1000
ARGS = SCRATCH + 0x1000  # argument blocks (0x1000)
STR = SCRATCH + 0x2000  # strings (0x1000)
SPR = SCRATCH + 0x3000  # the stub sprite (0x100)
TRACK = SCRATCH + 0x3800  # *0x6c1eb0 block (0x100)
TEX = SCRATCH + 0x4000  # texture records (0x4000)
CARS = SCRATCH + 0x8000  # 8 bodies of 0x800
CAR_SIZE = 0x800
OTS = SCRATCH + 0x10000  # OT heads (0x1000)
PKT = SCRATCH + 0x11000  # packets (0x1000)
ROWS = SCRATCH + 0x12000  # light rows (0x4000)
VIEW = SCRATCH + 0x16000  # view body (0x400)
STACK_FILL = b"\xcc" * E.STACK_SIZE

# address: (name, argument kinds: v value, s string, o out buffer, r 16 bytes, q 8 bytes)
STUBS = {
    0x4387D0: ("hud_sprite", "vvv"),
    0x479280: ("text_free", "v"),
    0x479060: ("text_create", "svvvvvv"),
    0x45F650: ("names_get", "ooo"),
    0x4484A0: ("driver_name", "ov"),
    0x469D60: ("f469d60", "v"),
    0x4600A0: ("message", "vvv"),
    0x4599C0: ("blast_hit", "vv"),
    0x459610: ("homer_end", "vv"),
    0x43B550: ("archive_find", "s"),
    0x465340: ("disk_load", "s"),
    0x43B630: ("archive_load", "v"),
    0x419420: ("track_cell", "vvvo"),
    0x440C60: ("cell_rows", "vov"),
    0x4412E0: ("light_set", "vvvvrqv"),
    0x441000: ("light_cells_add", "vr"),
    0x4411C0: ("mesh_rows_touch", "vv"),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def c_string(uc, a, limit=0x200):
    if on_stack(a):
        limit = min(limit, E.STACK + E.STACK_SIZE - a)
    return bytes(uc.mem_read(a, limit)).split(b"\0")[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def mk_float(rng):
    k = rng.randrange(8)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 10.0, 3500.0, 6000.0, 255.0]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FA00000, 1, 0x80000001, 0x7F7FFFFF])
    if k == 2:
        return r32(rng)
    return fbits(rng.uniform(-2000.0, 2000.0))


class Stubs:
    def __init__(self, m):
        self.m = m
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _stub(self, uc, address):
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<9I", bytes(uc.mem_read(esp, 36)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            if k == "o":
                logged.append("stack" if on_stack(a) else a)
            elif k == "s":
                logged.append(c_string(uc, a))
            elif k == "r":
                logged.append(bytes(uc.mem_read(a, 16)))
            elif k == "q":
                logged.append(bytes(uc.mem_read(a, 8)))
            else:
                logged.append(a)
        eax = self._next(name, 0)
        if name == "hud_sprite":
            eax = SPR
        elif name == "names_get":
            logged.append(struct.unpack("<i", bytes(uc.mem_read(args[0], 4)))[0])
            names = self._next("names", [0] * 6)
            uc.mem_write(args[2], struct.pack("<6I", *names))
            uc.mem_write(args[1], struct.pack("<6I", *self._next("info", list(range(6)))))
        elif name == "driver_name":
            uc.mem_write(args[0], b"DRV%d\0" % args[1])
        elif name == "archive_load":
            logged.append(bytes(uc.mem_read(0x4B4A84, 8)))
        elif name == "cell_rows":
            uc.mem_write(args[1], self._next("rows", b"\0" * 16))
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---- state builders: st = {address: bytes}, script = {stub name: [results]} ----

def w32(st, a, v):
    st[a] = struct.pack("<I", v & 0xFFFFFFFF)


def w16(st, a, v):
    st[a] = struct.pack("<H", v & 0xFFFF)


def w8(st, a, v):
    st[a] = bytes([v & 0xFF])


def base_state(rng):
    st = {}
    race = bytearray(rng.randbytes(0x1000))
    race[0x551] = pick(rng, *range(10), 8, 8, 2, rng.getrandbits(8))
    race[0x552] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    race[0x54D] = pick(rng, 0, 1, 2, 4, 6, 6, 8)
    race[0x586] = pick(rng, 0, 1, 3, 5, 9, 10, 12, rng.getrandbits(8))
    struct.pack_into("<H", race, 0xA80, pick(rng, 480, 600, 1440, rng.getrandbits(16)))
    struct.pack_into("<H", race, 0xA82, pick(rng, 640, 800, 3440, rng.getrandbits(16)))
    st[RACE] = bytes(race)
    w32(st, 0x4B1960, RACE)
    w32(st, 0x6C1EB0, TRACK)
    st[TRACK] = bytes(rng.randbytes(0x100))
    w16(st, TRACK + 0x2E, rng.randrange(0, 0x40))
    w32(st, 0x6C1E14, TEX + 0x100)
    st[TEX] = bytes(rng.randbytes(0x4000))
    w32(st, 0x4ACC38, pick(rng, 640, 800, 1024, 3440, rng.randrange(0, 5000), r32(rng)))
    w32(st, 0x4ACFF0, pick(rng, 640, 640, 512, 1, rng.randrange(1, 5000), r32(rng)) if rng.randrange(40)
        else 0)
    w32(st, 0x6C7FDC, r32(rng))
    w32(st, 0x6C0A50, r32(rng))
    w32(st, 0x6CD410, pick(rng, 0x100, 0x200, rng.randrange(0, 0x400), r32(rng)))
    w32(st, 0x6CD40C, pick(rng, 0x100, 0x200, rng.randrange(0, 0x400), r32(rng)))
    w8(st, 0x6CCD84, rng.getrandbits(8))
    # the HUD sprite records (0x43 of 0xf0) and the boxes after them
    spr = bytearray(rng.randbytes(0x6C8000 - 0x6C3EC0))
    for k in range(0x43):
        for off in (0x10, 0x14, 0x2C, 0x30, 0x48, 0x4C, 0x64, 0x68):
            if rng.randrange(3):
                struct.pack_into("<I", spr, k * 0xF0 + off, fbits(rng.uniform(-100, 900)))
    st[0x6C3EC0] = bytes(spr)
    st[SPR] = bytes(rng.randbytes(0x100))
    return st


def gte_state(rng, st, near_max=100.0):
    """The x87 transform / projection (0x426a20 / 0x426b00) and its state."""
    w32(st, 0x6CCD78, 0x426A20)
    w32(st, 0x6CCD80, 0x426B00)
    for a in (0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40, 0x6CCD44):
        w32(st, a, fbits(pick(rng, rng.uniform(-1, 1), rng.uniform(-1, 1), 0.0, 1.0, rng.uniform(-20, 20))))
    for a in (0x6CCD0C, 0x6CCD10):
        w32(st, a, fbits(pick(rng, rng.uniform(-300, 300), 0.0, rng.uniform(-1e5, 1e5))))
    w32(st, 0x6CCD14, fbits(pick(rng, rng.uniform(0, 4000), rng.uniform(0, 8000), rng.uniform(-1e4, 1e4))))
    w32(st, 0x6CCD00, fbits(pick(rng, 256.0, 320.0, rng.uniform(1, 1000))))
    w32(st, 0x6CCD04, fbits(min(near_max, pick(rng, 10.0, 1.0, 50.0, rng.uniform(0, 100)))))
    w32(st, 0x6CCD08, fbits(pick(rng, 20000.0, 65536.0, 1e9, rng.uniform(0, 30000))))
    for a in (0x6CCD54, 0x6CCD58, 0x6CCD5C, 0x6CCD60):
        w32(st, a, fbits(pick(rng, 320.0, 240.0, 0.0, rng.uniform(0, 2000))) if rng.randrange(20) else mk_float(rng))
    for a, v in ((0x6CCD64, 0.0), (0x6CCD68, 640.0), (0x6CCD6C, 0.0), (0x6CCD70, 480.0)):
        w32(st, a, fbits(pick(rng, v, v, rng.uniform(-100, 1000))))
    w32(st, 0x6CCE84, pick(rng, 0, 1, r32(rng)))
    for table in (0x6CD510, 0x6CDD80):  # OT heads: pointers into the scratch with the flag bit sometimes set
        st[table] = b"".join(struct.pack("<I", OTS + rng.randrange(0, 0x400) * 4 | pick(rng, 0, 0, 0x80000000))
                             for _ in range(0x200))
    st[OTS] = bytes(rng.randbytes(0x1000))


# car_screen_point (0x43a3e0) accepts depths 10..3500, but the projection only writes the screen x / y for
# depths past the near plane 0x6ccd04: with a near plane beyond 10 the original reads leftover stack.
NEAR_MAX = 10.0


def cars_state(rng, st, near=True):
    """Slots 0x6bfb80 (8 x 0x274) with bodies in the scratch."""
    slots = bytearray(rng.randbytes(8 * 0x274))
    n = 8
    places = list(range(1, n + 1))
    rng.shuffle(places)
    for i in range(8):
        struct.pack_into("<I", slots, i * 0x274, CARS + i * CAR_SIZE)
        slots[i * 0x274 + 0x2D] = places[i] if rng.randrange(12) else rng.getrandbits(8)
        slots[i * 0x274 + 0x2C] = pick(rng, 0, 1, 2, 3, 5, rng.getrandbits(8))
        struct.pack_into("<i", slots, i * 0x274 + 0x34, pick(rng, rng.randrange(0, 0x10000), r32(rng) - (1 << 31)))
        struct.pack_into("<I", slots, i * 0x274 + 0x270, pick(rng, 0, 1, r32(rng)))
        body = bytearray(rng.randbytes(CAR_SIZE))
        for k, off in enumerate((0x34, 0x38, 0x3C)):
            v = rng.randrange(-400, 400) if near else rng.randrange(-0x8000, 0x8000)
            if k == 2 and near:
                v = rng.randrange(-100, 4000)
            struct.pack_into("<I", body, off, (v << 16 | rng.getrandbits(16)) & 0xFFFFFFFF)
        struct.pack_into("<H", body, 0x64E, pick(rng, 0, 1, i, rng.randrange(0, 0x40)))
        st[CARS + i * CAR_SIZE] = bytes(body)
    st[0x6BFB80] = bytes(slots)


def slot_arg(rng):
    return 0x6BFB80 + 0x274 * pick(rng, 0, 0, 1, 2, 3, 5, 7)


# ---- HUD ----

def case_speed(rng):
    st = base_state(rng)
    return [pick(rng, rng.randrange(0, 1000), rng.randrange(-1000, 100000), r32(rng), 0x80000000)], st, {}


def case_lap_place(rng):
    st = base_state(rng)
    cars_state(rng, st)
    w8(st, RACE + 0x551, pick(rng, 0, 1, 2, 3, 5, 8, 8, 3))
    return [slot_arg(rng)], st, {}


def case_time_digits(rng):
    st = base_state(rng)
    cars_state(rng, st)
    s = slot_arg(rng)
    w8(st, CARS + (s - 0x6BFB80) // 0x274 * CAR_SIZE + 0x65C, pick(rng, 0, 1, 8, 9, rng.getrandbits(8)))
    return [s], st, {}


def case_warning(rng):
    st = base_state(rng)
    cars_state(rng, st)
    s = slot_arg(rng)
    w8(st, s + 0x2F, pick(rng, 0, 0x17, 0x18, 0x20, 0x28, rng.getrandbits(8)))
    return [s], st, {}


def case_weapons(rng):
    st = base_state(rng)
    cars_state(rng, st)
    s = slot_arg(rng)
    body = CARS + (s - 0x6BFB80) // 0x274 * CAR_SIZE
    for k in range(2):
        w8(st, body + 0x67C + k, pick(rng, 0xFF, 0x80, 0, 1, 5, 0x40, 0x41, 0x60, 0x67, rng.getrandbits(8)))
    w16(st, 0x6C7ED8, rng.randrange(-4, 64))
    w16(st, 0x6C7EDA, rng.randrange(-4, 64))
    return [s], st, {}


def case_lap_banner(rng):
    st = base_state(rng)
    cars_state(rng, st)
    s = slot_arg(rng)
    body = CARS + (s - 0x6BFB80) // 0x274 * CAR_SIZE
    w8(st, body + 0x65D, pick(rng, 0, 1, 8, 9, 0x10, rng.getrandbits(8)))
    if rng.randrange(2):
        w8(st, s + 0x2C, st[RACE][0x586])
    return [s, pick(rng, 0, 0, 1, r32(rng))], st, {}


def case_banner_set(rng):
    st = base_state(rng)
    w16(st, 0x6C7ED0, rng.getrandbits(16))
    w16(st, 0x6C7ED2, rng.getrandbits(16))
    return [pick(rng, 0, 1, 1, 2, r32(rng))], st, {}


def case_car_markers(rng):
    st = base_state(rng)
    gte_state(rng, st, NEAR_MAX)
    cars_state(rng, st)
    w8(st, RACE + 0x551, pick(rng, 8, 8, 8, 3, 5))
    w8(st, RACE + 0x58B, pick(rng, 0, 1))
    s = slot_arg(rng)
    body = CARS + (s - 0x6BFB80) // 0x274 * CAR_SIZE
    for off in (0xC8, 0xD4):
        w16(st, body + off, pick(rng, rng.randrange(-0x1000, 0x1000), rng.randrange(-40, 40), 0))
    w32(st, 0x6CD410, pick(rng, 0x100, rng.randrange(0, 0x400)))
    return [s], st, {}


def case_fade_set(rng):
    st = base_state(rng)
    return [rng.randrange(0, 4), r32(rng), r32(rng)], st, {}


def case_fade_apply(rng):
    st = base_state(rng)
    w8(st, RACE + 0x551, pick(rng, 4, 4, 0, 2, 8))
    for k in range(4):
        w8(st, 0x6C7FE1 + 4 * k, pick(rng, 0, 1, 2, 5, rng.getrandbits(8)))
    return [rng.randrange(0, 4), pick(rng, 0, 0x80, 0xFF, 0x100, rng.getrandbits(8), r32(rng)),
            pick(rng, 0, 1, 2, 3, r32(rng))], st, {}


def case_split_layout(rng):
    st = base_state(rng)
    cars_state(rng, st)
    return [slot_arg(rng), pick(rng, 0, 1, 1, 2, r32(rng))], st, {}


def case_name_texts(rng):
    st = base_state(rng)
    gte_state(rng, st, NEAR_MAX)
    cars_state(rng, st)
    w8(st, RACE + 0x551, pick(rng, 8, 8, 2, 3, 5, 6, 0, 1, 4, 9))
    w8(st, 0x6CCE40, pick(rng, 0, 0, 0, 0x10, 0x2, rng.getrandbits(8)))
    s = slot_arg(rng)
    if rng.randrange(4):
        w8(st, s + 0x2C, min(st[RACE][0x586], rng.randrange(0, 4)))
    lists = b"".join(struct.pack("<I", pick(rng, 0xFFFFFFFF, 0xFFFFFFFF, rng.randrange(0, 40))) for _ in range(12))
    st[0x4BEAF0] = lists
    strings = b""
    ptrs = []
    for k in range(6):
        ptrs.append(pick(rng, 0, STR + len(strings), STR + len(strings)))
        strings += pick(rng, b"", b"Name%d" % k, b"X") + b"\0"
    st[STR] = strings
    info = [pick(rng, k, 0xFFFFFFFF, STR) for k in range(6)]  # a 7th / 8th car's "name" (sic)
    sc = {"names": [ptrs], "info": [info], "text_create": [r32(rng) for _ in range(12)]}
    return [s, r32(rng)], st, sc


def case_screen_point(rng):
    st = base_state(rng)
    gte_state(rng, st, NEAR_MAX)
    cars_state(rng, st)
    return [slot_arg(rng), ARGS, ARGS + 0x10], st, {}


def case_gap_bars(rng):
    st = base_state(rng)
    cars_state(rng, st)
    w8(st, RACE + 0x551, pick(rng, 2, 3, 5, 6, 7, 2, 0, 8))
    w8(st, RACE + 0x552, pick(rng, 0, 0, 0, 1))
    w8(st, 0x6CCD84, pick(rng, 0, 1, 2, 4, rng.getrandbits(8)))
    n = st[RACE][0x54D]
    if n:  # a car placed first (the leader's progress is leftover stack in the original without one)
        w8(st, 0x6BFB80 + rng.randrange(0, n) * 0x274 + 0x2D, 1)
    for i in range(8):  # progress near the leader's
        w32(st, 0x6BFB80 + i * 0x274 + 0x34, pick(rng, rng.randrange(0, 0x10000), rng.randrange(-0x40000, 0x40000)))
    for a in (0x6CCD54, 0x6CCD58):
        w32(st, a, fbits(rng.uniform(0, 640)) if rng.randrange(10) else mk_float(rng))
    w32(st, 0x6CE608, ARGS)
    w32(st, ARGS + 4, OTS + 4 * rng.randrange(0, 0x400) | pick(rng, 0, 0x80000000))
    st[OTS] = bytes(rng.randbytes(0x1000))
    st[0x4BE4C0] = bytes(rng.randbytes(0x630))
    return [pick(rng, 0, 0x80, 0xFF, 0x100, rng.getrandbits(8), r32(rng)),
            pick(rng, 0x6BFB80, 0x6BFB80, 0x6BFB80 + 0x274)], st, {}


# ---- archive, CPU, leader ----

def case_archive(rng):
    st = base_state(rng)
    st[STR] = b"data\\track01.trk\0"
    w32(st, 0x4B4A84, r32(rng))
    w32(st, 0x4B4A88, r32(rng))
    sc = {"archive_find": [pick(rng, 0xFFFF, 0xFFFFFFFF, 0x1FFFF, 3, rng.getrandbits(16), r32(rng))],
          "disk_load": [r32(rng)], "archive_load": [r32(rng)]}
    return [STR], st, sc


def case_none(rng):
    return [], base_state(rng), {}


def case_cpu(rng):
    st = base_state(rng)
    st[0x4AF428] = pick(rng, b"GenuineIntel\0", b"GenuineIntel\0", b"AuthenticAMD\0", b"GenuineIntek\0",
                        b"GenuineInte\0", b"GenuineIntelX\0", b"\0", b"Fenuine\0", b"Henuine\0")
    return [], st, {}


def case_leader_hit(rng):
    st = base_state(rng)
    cars_state(rng, st)
    body = CARS + 7 * CAR_SIZE
    hit = CARS + rng.randrange(0, 7) * CAR_SIZE
    w32(st, hit, pick(rng, 3, 0x2003, 0x8003, 4, r32(rng)))
    w8(st, hit + 0x70C, pick(rng, 8, 0, 1, rng.getrandbits(8)))
    w32(st, body + 0x18C, pick(rng, 0, CARS + rng.randrange(0, 7) * CAR_SIZE))
    return [body, pick(rng, hit, hit, 0), r32(rng)], st, {}


# ---- lights ----

def light_cells_state(rng, st, n):
    """Light slot n's cell spans and the row tables they name."""
    spans = []
    for k in range(4):
        sec = rng.randrange(0, 8)
        first = rng.randrange(0, 8)
        last = pick(rng, first, first + rng.randrange(0, 4), rng.randrange(0, 8))
        spans.append(pick(rng, sec | first << 12 | last << 24, sec | first << 12 | last << 24, 0))
    st[0x5D8014 + n * 64] = struct.pack("<4I", *spans)
    st[0x5D7DE0] = struct.pack("<8I", *[ROWS + s * 0x400 for s in range(8)])
    rows = bytearray(0x2000)
    for i in range(0x800):
        v = pick(rng, 0, 0, (n << 22) | rng.getrandbits(22), (rng.randrange(0, 0x47) << 22) | rng.getrandbits(22))
        struct.pack_into("<I", rows, 4 * i, v)
    st[ROWS] = bytes(rows)


def case_cells_remove(rng):
    st = base_state(rng)
    n = rng.randrange(0, 0x47)
    light_cells_state(rng, st, n)
    return [n], st, {}


def case_slot_new(rng):
    st = base_state(rng)
    slots = bytearray(rng.randbytes(0x47 * 64))
    for k in range(0x47):
        struct.pack_into("<H", slots, k * 64 + 0x24, pick(rng, 0, 1, 1, 1, rng.getrandbits(16)))
    st[0x5D8000] = bytes(slots)
    w32(st, 0x5D7FE8, pick(rng, 0, 1, 5, 0x40, 0x46, 0x47, rng.randrange(0, 0x47)))
    w32(st, 0x5D7FE0, pick(rng, 0, 1, 10, 0x47, rng.randrange(0, 0x48)))
    st[ARGS] = struct.pack("<3I", r32(rng), r32(rng), r32(rng))
    sc = {"track_cell": [r32(rng)], "rows": [bytes(rng.randbytes(16))]}
    return [ARGS, r32(rng), r32(rng)], st, sc


def case_slot_free(rng):
    st = base_state(rng)
    n = rng.randrange(0, 0x47)
    light_cells_state(rng, st, n)
    w32(st, 0x5D7FE0, pick(rng, n + 1, n + 1, n, 0x47, rng.randrange(0, 0x48)))
    return [n], st, {}


def case_reposition(rng):
    st = base_state(rng)
    n = rng.randrange(0, 0x47)
    light_cells_state(rng, st, n)
    rec = bytearray(rng.randbytes(0x14))
    pos = [r32(rng) for _ in range(3)]
    for k in range(3):
        old = struct.unpack_from("<I", rec, 4 * k)[0]
        struct.pack_into("<I", rec, 4 * k, pick(rng, (pos[k] & 0xFF000000) | (old & 0xFFFFFF), pos[k], old))
    st[0x5D8000 + n * 64] = bytes(rec)
    st[ARGS] = struct.pack("<3I", *pos)
    sc = {"track_cell": [r32(rng)], "rows": [bytes(rng.randbytes(16))]}
    return [n, ARGS], st, sc


# ---- flares ----

def case_flare_add(rng):
    st = base_state(rng)
    w32(st, 0x4BEB88, pick(rng, 0, 1, 5, 9, 10, 11, -1, r32(rng)))
    st[ARGS] = struct.pack("<3I", r32(rng), r32(rng), r32(rng))
    return [ARGS, pick(rng, 0, 1, 2, 4, r32(rng)), r32(rng)], st, {}


def case_flare_quad(rng):
    st = base_state(rng)
    gte_state(rng, st)
    st[PKT] = bytes(rng.randbytes(0x100))
    z = pick(rng, rng.randrange(-100, 20000), rng.randrange(0, 0x8000), r32(rng), 10, 0x2000 + 10, 0x3000 + 10)
    return [pick(rng, rng.randrange(-100, 800), r32(rng)), pick(rng, rng.randrange(-100, 600), r32(rng)), z,
            pick(rng, 0xFF, 0x80, 0, rng.getrandbits(8), r32(rng)), pick(rng, 0, 1, 2, 3, 0x20, 0x40, r32(rng)),
            PKT, pick(rng, 0, 1, 2, 2, 3, r32(rng))], st, {}


def case_flare_colour(rng):
    st = base_state(rng)
    st[PKT] = bytes(rng.randbytes(0x100))
    return [PKT, pick(rng, 0x3F, 0x10, rng.randrange(0, 0x100)),
            pick(rng, 0x80, 0xFF, 0, rng.getrandbits(8), rng.getrandbits(12), r32(rng)), r32(rng)], st, {}


def case_flares_draw(rng):
    st = base_state(rng)
    gte_state(rng, st)
    count = pick(rng, 0, 1, 2, 4, 10, rng.randrange(0, 11), -1)
    w32(st, 0x4BEB88, count)
    recs = bytearray(rng.randbytes(10 * 0x4C0))
    for k in range(10):
        struct.pack_into("<I", recs, k * 0x4C0, pick(rng, 0, 1, 2, 2, 4, 4, 3, 5, r32(rng)))
        struct.pack_into("<3h", recs, k * 0x4C0 + 4, rng.randrange(-400, 400), rng.randrange(-400, 400),
                         rng.randrange(-100, 6000))
    st[0x4BEB90] = bytes(recs)
    struct.pack_into  # noqa
    race = bytearray(st[RACE])
    struct.pack_into("<i", race, 0x57C, pick(rng, 0, 1, 4, 6, 7, 8, -1))
    struct.pack_into("<H", race, 0xA82, pick(rng, 640, 800, 3440, 1, 0, rng.getrandbits(16)) if rng.randrange(30)
                     else 0)
    st[RACE] = bytes(race)
    w32(st, 0x4B2C58, pick(rng, 0, 0x40, 0x80, 0xFF, rng.randrange(0, 0x100), 0x100 if not rng.randrange(40) else 0))
    w32(st, 0x4B2C60, pick(rng, 0, 0x80, 0xFF, rng.randrange(0, 0x100), r32(rng)))
    w8(st, 0x6CCD84, pick(rng, 0, 1, 4, 5, rng.getrandbits(8)))
    view = bytearray(rng.randbytes(0x400))
    for k in range(9):
        struct.pack_into("<h", view, 0xC4 + 2 * k, pick(rng, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000)))
    for k, off in enumerate((0x34, 0x38, 0x3C)):
        struct.pack_into("<i", view, off, (rng.randrange(-400, 400) << 16) | rng.getrandbits(16))
    st[VIEW] = bytes(view)
    return [VIEW], st, {}


# name: (original address, case builder, returns a value)
TESTS = {
    "hud_speed_digits": (0x438DF0, case_speed, False),
    "hud_lap_place": (0x438E60, case_lap_place, False),
    "hud_time_digits": (0x438FB0, case_time_digits, False),
    "hud_warning_sprite": (0x439050, case_warning, True),
    "hud_weapon_icons": (0x4390C0, case_weapons, False),
    "hud_lap_banner": (0x439270, case_lap_banner, False),
    "hud_banner_set": (0x4392E0, case_banner_set, False),
    "hud_car_markers": (0x439400, case_car_markers, False),
    "hud_fade_set": (0x439D50, case_fade_set, False),
    "hud_fade_apply": (0x439D70, case_fade_apply, False),
    "hud_split_layout": (0x439F20, case_split_layout, False),
    "hud_name_texts": (0x43A000, case_name_texts, False),
    "car_screen_point": (0x43A3E0, case_screen_point, True),
    "hud_gap_bars": (0x43A4A0, case_gap_bars, False),
    "archive_load_by_name": (0x43B660, case_archive, True),
    "cpu_is_intel": (0x43C370, case_cpu, True),
    "cpu_feature_none": (0x43C3E0, case_none, True),
    "leader_hit": (0x43F040, case_leader_hit, True),
    "light_cells_remove": (0x4410F0, case_cells_remove, False),
    "light_slot_new": (0x441220, case_slot_new, True),
    "light_slot_free": (0x441370, case_slot_free, False),
    "light_reposition": (0x4413A0, case_reposition, False),
    "flare_add": (0x441D80, case_flare_add, False),
    "flare_quad": (0x441DE0, case_flare_quad, True),
    "flare_quad_colour": (0x441FA0, case_flare_colour, False),
    "flares_draw": (0x442060, case_flares_draw, False),
}
REGS = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, base, name, address, builder, returns, rng):
    args, st, script = builder(rng)
    cw = pick(rng, 0x27F, 0x37F, 0x07F)
    data = bytearray(base)
    scratch = bytearray(SCRATCH_SIZE)
    for a, v in st.items():
        if DATA <= a < DATA + DATA_SIZE:
            data[a - DATA:a - DATA + len(v)] = v
        else:
            scratch[a - SCRATCH:a - SCRATCH + len(v)] = v
    results = []
    for target in (address, m.symbols[name]):
        m.write(DATA, bytes(data))
        m.write(SCRATCH, bytes(scratch))
        m.write(E.STACK, STACK_FILL)
        for i, reg in enumerate(REGS):
            m.uc.reg_write(reg, 0x11111111 * (i + 1))
        m.uc.reg_write(UC_X86_REG_FPCW, cw)
        m.uc.reg_write(UC_X86_REG_FPSW, 0)
        m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        stubs.reset(script)
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        regs = tuple(m.uc.reg_read(reg) for reg in REGS) + (m.uc.reg_read(UC_X86_REG_ESP),
                                                              (m.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7,
                                                              m.uc.reg_read(UC_X86_REG_FPTAG))
        if r[0] != "ok":
            regs = ()  # faulted: the registers at the fault differ
        mem = bytearray(m.read(DATA, DATA_SIZE) + m.read(SCRATCH, SCRATCH_SIZE))
        mem[0x6CCC6A - DATA:0x6CCC6C - DATA] = b"\0\0"  # the GTE vector's pad: an unwritten stack word in the original
        if on_stack(struct.unpack_from("<I", mem, 0x6CCD74 - DATA)[0]):
            mem[0x6CCD74 - DATA:0x6CCD78 - DATA] = b"STAK"  # the transform's record pointer: a stack address
        results.append((r, regs, stubs.log, bytes(mem)))
    a, b = results
    if a[:3] == b[:3] and a[3] == b[3]:
        return None, a[0]
    msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {a[0]} ours {b[0]}"
    if a[1] != b[1]:
        msg += f"\n  registers / esp / x87 differ: {[hex(x) for x in a[1]]} vs {[hex(x) for x in b[1]]}"
    if a[3] != b[3]:
        x, y = a[3], b[3]
        diff = [i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]]
        where = [hex(DATA + i if i < DATA_SIZE else SCRATCH + i - DATA_SIZE) for i in diff[:8]]
        msg += f"\n  memory differs at {where} ({len(diff)} words)"
        if "-v" in sys.argv:
            msg += f"\n  orig {[x[i:i + 4].hex() for i in diff[:8]]}\n  ours {[y[i:i + 4].hex() for i in diff[:8]]}"
    if a[2] != b[2]:
        la, lb = a[2], b[2]
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        msg += f"\n  calls from #{k}: orig {la[k:k + 3]}\n                ours {lb[k:k + 3]}"
    return msg, a[0]


def owned_sizes():
    sizes = {}
    for line in open("build/groups/pg_owned.txt"):
        if not line.startswith("#"):
            parts = line.split()
            sizes[int(parts[0], 16)] = int(parts[2])
    return sizes


def instruction_addresses(address, size):
    out = set()
    for line in open("build/rollcage.asm"):
        parts = line.split(":", 1)
        if len(parts) == 2:
            try:
                a = int(parts[0], 16)
            except ValueError:
                continue
            if address <= a < address + size:
                out.add(a)
    return out


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    base = m.read(DATA, DATA_SIZE)
    rng = random.Random(0x96)
    failed = 0
    sizes = owned_sizes() if "-c" in sys.argv else {}
    for name, (address, builder, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        seen = set()
        hook = None
        if address in sizes:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address,
                                 end=address + sizes[address] - 1)
        for _ in range(iterations):
            msg, r = run_case(m, stubs, base, name, address, builder, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs faulted the same way in both)" if errors else ""
        cov = ""
        if hook is not None:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, sizes[address])
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:10]}" if missed else "") + "]"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
