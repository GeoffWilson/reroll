#!/usr/bin/env python3
"""Differential test of orig_ps.c (group PS: the projectile trails, the
homing lock marker, the weather's drops and screen flakes, the weather's
light level and palette, the snatch texture; 0x483210-0x4867a0) against the
original, on random state.

Callees outside the group that queue other drawing, touch DirectDraw or the
network, or are big unrelated code are logging stubs in both runs (the call
log records as many arguments as the callee reads; vectors passed by
pointer are logged by content, out pointers as "stack or not" and filled
with scripted values): the flare queue 0x441d80, the sprite quad 0x47e5f0,
the network slot 0x48cf20, the flake spawner 0x485140, the surface lock /
unlock 0x428c30 / 0x428c60, the pixel converter 0x41e1f0, the low arena
0x476b80. Pure game code runs for real in both: the transform pointers'
targets (x87 0x426a20 / 0x426be0 / 0x426b00 or 3DNow! 0x4268c0 / 0x4291f0),
0x4266d0, fixed sin 0x4270a0, the C runtime's _ftol, sqrt, sin and cos. The
emulated CPU is a Phenom (3DNow!). The x87 control word is 0x27f, 0x37f or
0x07f; besides memory (the whole data section, the scratch areas), the
return value, the call log, ebx / esi / edi / ebp, the x87 stack top and tag
word are compared. Our trail ring (ps_trail_ring, the original's stack
leftovers) starts zeroed like the original's stack.

    tests/emu_orig_ps.py EXE build/emu_ps.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import unicorn  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_CPU_X86_PHENOM, UC_X86_REG_CR4, UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402


def _phenom_uc(arch, mode):
    uc = unicorn.Uc(arch, mode)
    uc.ctl_set_cpu_model(UC_CPU_X86_PHENOM)  # 3DNow!
    return uc


E.Uc = _phenom_uc

DATA, DATA_SIZE = 0x4A7000, 0x22C000  # the exe's data section, restored and compared whole
SCR = 0x01000000
SCR_SIZE = 0x20000
RACE = SCR + 0x0000  # 0x1000
CONFIG = SCR + 0x1000  # 0x100
ARG = SCR + 0x1100  # 0x100
OTA = SCR + 0x2000  # 0x2000: ordering-table entries
TRACK = SCR + 0x4000  # 0x100
PALSRC = SCR + 0x4100  # 0x1000
PALBUF = SCR + 0x5100  # 0x1000
CARS = SCR + 0x6200  # 8 x 0x700
FLAKES = SCR + 0xA000  # 32 x 0xa0
SPR_RAIN = SCR + 0xB400  # 0xa0 x 0x3c
SPR_SNOW = SCR + 0xDA00  # 0xaf x 0x78
EMIT = SCR + 0x13000  # 6 x 0xd00
BODY = SCR + 0x18000  # 0x100
PROJ = SCR + 0x18100  # 3 x 0x2600
PROJ_SIZE = 0x2600
LOCK = SCR + 0x1F400  # 0x200
LOCKOT = SCR + 0x1F600  # 0x10
ZREC = SCR + 0x1F800  # 0x400, zero: a dead projectile
BIG = 0x02000000
BIG_SIZE = 0x80000
SURF = BIG
PLASMA = BIG + 0x20000
TABLE = BIG + 0x50000


def S(name, kinds):
    return (name, kinds)


# argument kinds: v value, cN N bytes by content, P out pointer (logged as "stack or not", filled from the script)
STUBS = {
    0x441D80: S("flare_add", ["c12", "v", "v"]),
    0x47E5F0: S("sprite_quad", ["c8", "v", "v", "v", "v", "v"]),
    0x48CF20: S("net_slot", []),
    0x485140: S("flake_spawn", ["v"]),
    0x428C30: S("surface_lock", ["v", "v", "P", "v", "P", "P"]),
    0x428C60: S("surface_unlock", ["v", "v", "v"]),
    0x41E1F0: S("pixels_convert", ["v", "v", "v", "v"]),
    0x476B80: S("arena_alloc", ["v"]),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def mk_float(rng):
    k = rng.randrange(10)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 100.0, 640.0, 480.0, 3.0]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 1, 0x80000001, 0x00800000,
                           0x7F7FFFFF, 0xFF7FFFFF])
    if k == 2:
        return rng.getrandbits(32)
    return fbits(rng.uniform(-1000.0, 1000.0))


def pick(rng, *choices):
    return rng.choice(choices)


def s16(rng, scale=0x8000):
    return rng.randrange(-scale, scale) & 0xFFFF


class State:
    data0 = None  # the data section as loaded

    def __init__(self, rng):
        self.rng = rng
        self.data = bytearray(State.data0)
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.scr[ZREC - SCR:ZREC - SCR + 0x400] = bytes(0x400)
        self.big = None
        self.script = {}
        self.put(0x4B1960, "<I", RACE)
        self.put(0x4B2198, "<I", CONFIG)

    def put(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        if SCR <= address < SCR + SCR_SIZE:
            struct.pack_into(fmt, self.scr, address - SCR, value)
        elif BIG <= address < BIG + BIG_SIZE:
            struct.pack_into(fmt, self.big, address - BIG, value)
        else:
            assert DATA <= address < DATA + DATA_SIZE, hex(address)
            struct.pack_into(fmt, self.data, address - DATA, value)

    def use_big(self):
        self.big = bytearray(self.rng.randbytes(BIG_SIZE))


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
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        outs = list(self._next(name + ".out", (0, 0, 0)))
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind[0] == "c":
                logged.append(rd(self.m, a, int(kind[1:])))
            elif kind == "P":
                logged.append("stack" if on_stack(a) else a)
                uc.mem_write(a, struct.pack("<I", outs.pop(0) & 0xFFFFFFFF))
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- state helpers

def set_gte_float(st, rng):
    """Float matrix rows, translation, projection and clip parameters."""
    rows = (0x6CCD1C, 0x6CCD2C, 0x6CCD3C)
    for r in rows:
        for k in range(3):
            st.put(r + 4 * k, "<I", fbits(rng.uniform(-1.0, 1.0)))
    st.put(0x6CCD0C, "<I", fbits(rng.uniform(-2000, 2000)))
    st.put(0x6CCD10, "<I", fbits(rng.uniform(-2000, 2000)))
    st.put(0x6CCD14, "<I", fbits(rng.uniform(-500, 3000)))
    st.put(0x6CCD00, "<I", fbits(pick(rng, 400.0, rng.uniform(100, 800))))
    st.put(0x6CCD04, "<I", fbits(pick(rng, 1.0, 16.0, rng.uniform(0.5, 50))))
    st.put(0x6CCD08, "<I", fbits(pick(rng, 20000.0, rng.uniform(500, 30000))))
    for a in (0x6CCD4C, 0x6CCD50, 0x6CCD5C, 0x6CCD60):
        st.put(a, "<I", fbits(rng.uniform(0, 400)))
    st.put(0x6CCD64, "<I", fbits(pick(rng, 0.0, rng.uniform(-10, 100))))
    st.put(0x6CCD68, "<I", fbits(pick(rng, 640.0, rng.uniform(300, 800))))
    st.put(0x6CCD6C, "<I", fbits(pick(rng, 0.0, rng.uniform(-10, 100))))
    st.put(0x6CCD70, "<I", fbits(pick(rng, 480.0, rng.uniform(200, 600))))
    st.put(0x6CCC40, "<I", rng.randrange(0, 400) << 16 | rng.getrandbits(16))
    st.put(0x6CCC44, "<I", rng.randrange(0, 300) << 16 | rng.getrandbits(16))
    if rng.randrange(12) == 0:
        for a in [r + 4 * k for r in rows for k in range(3)] + [0x6CCD0C, 0x6CCD10, 0x6CCD14, 0x6CCD00, 0x6CCD04,
                                                                0x6CCD08, 0x6CCD64, 0x6CCD68]:
            if rng.randrange(3) == 0:
                st.put(a, "<I", mk_float(rng))


def set_paths(st, rng):
    path = pick(rng, (0x426A20, 0x426BE0, 0x426B00), (0x426A20, 0x426BE0, 0x426B00), (0x4268C0, 0x4268C0, 0x4291F0))
    st.put(0x6CCD78, "<I", path[0])
    st.put(0x6CCD7C, "<I", path[1])
    st.put(0x6CCD80, "<I", path[2])


def ot_entry(rng):
    return (OTA + 4 * rng.randrange(0x800)) | pick(rng, 0, 0, 0x80000000)


def set_ots(st, rng):
    for base in (0x6CD510, 0x6CDD80):
        for k in range(0x200):
            st.put(base + 4 * k, "<I", ot_entry(rng))
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(0x6CCD84, "<B", rng.getrandbits(8))
    st.put(0x6CE60C, "<I", OTA - 0x70 + 4 * rng.randrange(0x100))
    st.put(0x6CE584, "<I", ot_entry(rng))
    st.put(0x6CE608, "<I", LOCKOT)
    st.put(LOCKOT + 4, "<I", ot_entry(rng))


def base(st, rng):
    set_gte_float(st, rng)
    set_paths(st, rng)
    set_ots(st, rng)


# ---------------------------------------------------------------- generators

def rand_proj(st, rng, rec):
    st.put(rec + 0x180, "<I", pick(rng, rng.randrange(0, 11), rng.randrange(0, 11), 0))
    st.put(rec + 0x184, "<I", pick(rng, 0, 1, 2, 2, 1, 3))
    st.put(rec + 0x188, "<I", pick(rng, 1, 2, 3, 0x40, 0x80, 0x100, 4, 1, rng.getrandbits(32)))
    cx, cy, cz = (rng.randrange(-3000, 3000) for _ in range(3))
    spread = pick(rng, 50, 300, 2000)
    for k in range(32 * 4 + 1):
        a = rec + 0x1B8 + 8 * k
        st.put(a, "<h", cx + rng.randrange(-spread, spread))
        st.put(a + 2, "<h", cy + rng.randrange(-spread, spread))
        st.put(a + 4, "<h", cz + rng.randrange(-spread, spread))
    st.put(rec + 0x23DC, "<b", pick(rng, 0, 1, rng.randrange(-40, 40)))
    st.put(rec + 0x23DE, "<h", pick(rng, 0, 1, 2, 3, rng.randrange(2, 33), rng.randrange(2, 33), 32, 33, -1))


def trail_globals(st, rng):
    base(st, rng)
    st.put(0x4D1198, "<I", pick(rng, 0, 0, 0, 1))
    st.put(0x4B11D8, "<I", rng.getrandbits(31))
    st.put(CONFIG + 0x68, "<I", fbits(pick(rng, 2.0, 4.0, 5.0, 3.99)))


def g_trails(st, rng):
    trail_globals(st, rng)
    for k in range(3):
        rand_proj(st, rng, PROJ + k * PROJ_SIZE)
    for k in range(24):
        st.put(0x6B9CE0 + 4 * k, "<I", pick(rng, ZREC, ZREC, ZREC, ZREC, PROJ, PROJ + PROJ_SIZE, PROJ + 2 * PROJ_SIZE))
    return [], None


def g_trail(st, rng):
    trail_globals(st, rng)
    rand_proj(st, rng, PROJ)
    return [PROJ], None


def lock_state(st, rng):
    base(st, rng)
    for off in (0x6C, 0x70, 0x74):
        st.put(LOCK + off, "<I", (rng.randrange(-3000, 3000) << 16 | rng.getrandbits(16)) & 0xFFFFFFFF)
    st.put(LOCK + 0x8C, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(LOCK + 0x78, "<H", pick(rng, 0, rng.randrange(0, 20), rng.getrandbits(16)))
    st.put(0x6CD40C, "<i", pick(rng, 0x100, rng.randrange(0x80, 0x300), rng.getrandbits(32)))


def g_lock(st, rng):
    lock_state(st, rng)
    return [LOCK], None


def g_lock_quads(st, rng):
    lock_state(st, rng)
    return [LOCK + 0x6C, pick(rng, rng.randrange(0, 640), rng.getrandbits(32)),
            pick(rng, rng.randrange(0, 480), rng.getrandbits(32))], None


def rand_flakes(st, rng):
    st.put(0x4D5898, "<I", FLAKES)
    for i in range(32):
        f = FLAKES + i * 0xA0
        st.put(f, "<I", pick(rng, 0, 1, 1, rng.getrandbits(32)))
        st.put(f + 0x7C, "<I", fbits(rng.uniform(-50, 700)) if rng.randrange(20) else mk_float(rng))
        st.put(f + 0x80, "<I", fbits(rng.uniform(-50, 700)) if rng.randrange(20) else mk_float(rng))
        st.put(f + 0x84, "<I", fbits(rng.uniform(0, 30)) if rng.randrange(20) else mk_float(rng))
        st.put(f + 0x88, "<i", pick(rng, rng.randrange(-5, 5), rng.randrange(0x3D0, 0x420), rng.randrange(0, 0x400)))
        st.put(f + 0x8C, "<i", pick(rng, rng.randrange(0, 4), rng.randrange(-5, 10)))
        st.put(f + 0x90, "<i", pick(rng, rng.randrange(0, 12), rng.randrange(-3, 3)))
        st.put(f + 0x94, "<I", rng.getrandbits(32))
        st.put(f + 0x98, "<i", pick(rng, rng.randrange(-5, 30), rng.randrange(0, 300)))
        st.put(f + 0x9C, "<I", pick(rng, rng.randrange(0, 0x200), rng.getrandbits(32)))


def weather_state(st, rng):
    base(st, rng)
    st.put(RACE + 0x553, "<B", pick(rng, 0, 1, 2, 1, 2, 3))
    st.put(RACE + 0x552, "<B", pick(rng, 0, 0, 0, 1))
    st.put(0x6CCE40, "<I", rng.getrandbits(32))
    st.put(0x5D6248, "<i", pick(rng, 2, 3, 4, 5, 6, 1))
    st.put(0x5D6340, "<I", SPR_RAIN)
    st.put(0x5D6244, "<I", SPR_SNOW)
    st.put(0x6CD418, "<I", fbits(pick(rng, 1.5, rng.uniform(0, 20))))
    st.put(0x6CCE48, "<B", pick(rng, 0, 1, 2, rng.getrandbits(8)))
    st.put(0x6CD478, "<I", pick(rng, 480 << 16 | 640, 240 << 16 | 320, rng.randrange(1, 0x7FFF) << 16 | rng.randrange(1, 0x7FFF)))
    st.put(0x6CD40C, "<i", pick(rng, 0x100, rng.randrange(0x80, 0x300)))
    for k in range(8):
        st.put(0x5D6358 + 4 * k, "<I", EMIT + rng.randrange(6) * 0xD00)
    for k in range(6):
        e = EMIT + k * 0xD00
        st.put(e + 0x10, "<I", BODY + pick(rng, 0, 0x10) - 0x68 + 0x68)
        bx, by, bz = (rng.randrange(-2000, 2000) for _ in range(3))
        for j, v in enumerate((bx, by, bz)):
            st.put(BODY + 0x68 + 4 * j, "<i", v << 16 | rng.getrandbits(16))
            st.put(BODY + 0x78 + 4 * j, "<i", v << 16 | rng.getrandbits(16))
            st.put(e + 0xC98 + 2 * j, "<h", v + rng.randrange(-40, 40))
        cx, cy, cz = (rng.randrange(-3000, 3000) for _ in range(3))
        spread = pick(rng, 200, 1500, 5000)
        dense = pick(rng, 0.1, 0.5, 0.9)
        for d in range(406):
            a = e + 0x14 + 8 * d
            st.put(a, "<h", cx + rng.randrange(-spread, spread))
            st.put(a + 2, "<h", cy + rng.randrange(-spread, spread))
            st.put(a + 4, "<h", cz + rng.randrange(-spread, spread))
            st.put(a + 6, "<H", (pick(rng, 1, rng.getrandbits(16)) if rng.random() < dense else 0))
    rand_flakes(st, rng)
    slot = 0x6BFB80
    for k in range(8):
        st.put(slot + k * 0x274, "<I", CARS + k * 0x700)
        st.put(CARS + k * 0x700 + 0x658, "<h", pick(rng, 0, rng.randrange(-50, 400), rng.randrange(-0x8000, 0x8000)))


def g_weather(st, rng):
    weather_state(st, rng)
    return [pick(rng, *[0x6BFB80 + k * 0x274 for k in range(8)], ARG)], None


def g_emitter(st, rng):
    weather_state(st, rng)
    return [rng.randrange(0, 6)], None


def g_flakes_draw(st, rng):
    weather_state(st, rng)
    return [], None


def g_flakes_update(st, rng):
    weather_state(st, rng)
    st.put(0x4ACC38, "<i", pick(rng, 640, 800, rng.randrange(0, 2000)))
    st.put(0x4ACC3C, "<i", pick(rng, 480, 600, rng.randrange(-100, 2000)))
    st.put(0x4B2C50, "<I", rng.getrandbits(31))
    st.put(0x4D590C, "<i", pick(rng, 0, 0, 1, 1, 2))
    st.put(0x6C7FF8, "<I", rng.getrandbits(32))
    st.put(0x4D5910, "<I", pick(rng, rng.getrandbits(32), 0))
    st.put(0x4D58F0, "<I", pick(rng, 0, 1))
    if rng.randrange(3) == 0:  # a seed whose next values are small: the 1-in-1024 / 1-in-2048 glint draws
        st.put(0x4B2C50, "<I", rng.randrange(pick(rng, 4000, 4000, 1 << 21)) * pow(16807, -1, 1 << 31) % (1 << 31))
        st.put(0x4D58F0, "<I", 1)
        st.put(0x6CCE40, "<I", rng.getrandbits(32) & ~4)
        for k in range(8):
            st.put(CARS + k * 0x700 + 0x658, "<h", pick(rng, 251, 300, 1000, 250))
    st.script["net_slot"] = [rng.randrange(0, 8)]
    return [pick(rng, 0, 1, rng.getrandbits(32))], None


def g_tint(st, rng):
    st.put(0x6CCB64, "<i", pick(rng, rng.randrange(-5, 20), rng.getrandbits(32)))
    return [ARG], 0xFFFFFFFF


def g_free(st, rng):
    rand_flakes(st, rng)
    for i in range(32):
        st.put(FLAKES + i * 0xA0, "<I", pick(rng, 1, 1, 1, 0, rng.getrandbits(32)))
    if rng.randrange(4) == 0:
        for i in range(32):
            st.put(FLAKES + i * 0xA0, "<I", 1)
    return [], 0xFFFFFFFF


def levels(st, rng):
    st.put(0x4B2C5C, "<i", pick(rng, rng.randrange(0, 0x101), rng.randrange(-0x200, 0x400), rng.getrandbits(32)))
    st.put(0x4B2C60, "<i", pick(rng, rng.randrange(0, 0x101), rng.randrange(-0x200, 0x400), rng.getrandbits(32)))
    st.put(0x4D58EC, "<i", pick(rng, rng.randrange(-100, 100), rng.getrandbits(32)))


def g_level(st, rng):
    levels(st, rng)
    return [], 0xFFFFFFFF


def g_palette(st, rng):
    levels(st, rng)
    st.put(0x6C1EB0, "<I", TRACK)
    st.put(TRACK + 0x4C, "<I", PALSRC)
    st.put(0x4D58FC, "<I", pick(rng, PALBUF, PALBUF, PALBUF, 0))
    st.put(0x5D6338, "<i", pick(rng, 0, 1, 2))
    st.put(0x4B2C64, "<I", pick(rng, 1, 1, 0))
    st.put(0x4D58F4, "<I", pick(rng, 0, 0, 1))
    return [], 0xFFFFFFFF


def lock_script(st, rng):
    st.script["surface_lock"] = [pick(rng, 1, 1, 0, rng.getrandbits(32)) for _ in range(2)]
    st.script["surface_lock.out"] = [(SURF, pick(rng, 8, 16, 16, 8, 32, 0), pick(rng, 0x80, 0x100, 0x200, 0x180)) for _ in range(2)]
    st.script["surface_unlock"] = [rng.getrandbits(32)]


def g_snatch_pal(st, rng):
    st.put(0x4B24A0, "<I", pick(rng, 3, rng.getrandbits(32)))
    lock_script(st, rng)
    return [], 0xFFFFFFFF


def blit_state(st, rng):
    st.use_big()
    st.put(0x4D5F14, "<I", TABLE)
    st.put(0x4D5F1C, "<I", PLASMA)
    st.put(0x4D5F20, "<i", pick(rng, rng.randrange(0, 0xE10), rng.randrange(0xE00, 0xE10), rng.randrange(0, 0x4000)))


def g_snatch_anim(st, rng):
    blit_state(st, rng)
    st.put(CONFIG + 0x28, "<B", pick(rng, 1, 1, 0, rng.getrandbits(8)))
    st.put(0x4B24A0, "<I", pick(rng, 3, 3, 0xFFFFFFFF, rng.getrandbits(32)))
    last = pick(rng, 0xFFFFFFFF, rng.getrandbits(32), 100)
    st.put(0x4B2C88, "<I", last)
    st.put(0x4BE3E8, "<I", pick(rng, (last + rng.randrange(-2, 8)) & 0xFFFFFFFF, rng.getrandbits(32)))
    lock_script(st, rng)
    return [], 0xFFFFFFFF


def g_blit(st, rng):
    blit_state(st, rng)
    pitch = pick(rng, 0x80, 0x100, 0x200, 0x180)
    return [pick(rng, 1, 2, 3, 4, 5, rng.randrange(-10, 1000)), SURF, pitch, pick(rng, 8, 16, 16, 8, 32, 0)], None


def g_setup(st, rng):
    g_snatch_anim(st, rng)
    st.put(0x4B24A0, "<I", pick(rng, 3, 3, 0xFFFFFFFF))
    st.script["arena_alloc"] = [0]  # the plasma itself: snatch_plasma_build
    return [], 0xFFFFFFFF


def g_plasma(st, rng):
    st.use_big()
    st.script["arena_alloc"] = [pick(rng, BIG, BIG, 0)]
    return [], None


# name: (address, generator, iteration cap)
TESTS = {
    "projectile_trails_draw": (0x483210, g_trails, None),
    "projectile_trail_draw": (0x483240, g_trail, None),
    "lock_marker_quads": (0x483A80, g_lock_quads, None),
    "lock_marker_draw": (0x483D20, g_lock, None),
    "weather_draw": (0x484980, g_weather, None),
    "rain_drops_draw": (0x484A10, g_emitter, None),
    "snow_drops_draw": (0x484C20, g_emitter, None),
    "net_tint_level_get": (0x485050, g_tint, None),
    "flake_free_find": (0x485680, g_free, None),
    "screen_flakes_update": (0x4857F0, g_flakes_update, None),
    "screen_flakes_draw": (0x485D00, g_flakes_draw, None),
    "weather_brightness": (0x486240, g_level, None),
    "weather_level_lower": (0x486270, g_level, None),
    "weather_sky_offset": (0x486310, g_level, None),
    "weather_palette_get": (0x486350, g_palette, None),
    "snatch_palette_build": (0x486450, g_snatch_pal, None),
    "snatch_texture_animate": (0x486540, g_snatch_anim, None),
    "snatch_texture_blit": (0x486600, g_blit, None),
    "snatch_texture_setup": (0x486780, g_setup, None),
    "snatch_plasma_build": (0x4867A0, g_plasma, 6),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    m.write(DATA, bytes(st.data))
    m.write(SCR, bytes(st.scr))
    if st.big is not None:
        m.write(BIG, bytes(st.big))
    m.uc.reg_write(UC_X86_REG_EBX, 0x11111111)
    m.uc.reg_write(UC_X86_REG_ESI, 0x22222222)
    m.uc.reg_write(UC_X86_REG_EDI, 0x33333333)
    m.uc.reg_write(UC_X86_REG_EBP, 0x44444444)


def call(m, address, args, count):
    esp = E.STACK + E.STACK_SIZE - 0x100
    frame = struct.pack("<I", E.RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args)
    m.uc.mem_write(esp, frame)
    m.uc.reg_write(UC_X86_REG_ESP, esp)
    m.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
    m.trapped = False
    try:
        m.uc.emu_start(address, E.RETURN, count=count)
    except UcError as e:
        return ("error", str(e))
    if m.trapped:
        return ("trap",)
    if m.uc.reg_read(UC_X86_REG_EIP) != E.RETURN:
        return ("cut off",)
    return ("ok", m.uc.reg_read(UC_X86_REG_EAX))


SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, mask = gen(st, rng)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    count = 400000000 if name in ("snatch_plasma_build", "snatch_texture_setup") else 20000000
    results = []
    for k, target in enumerate((address, m.symbols[name])):
        apply(m, st, cw)
        m.write(m.symbols["ps_trail_ring"], bytes(96))
        stubs.reset(st.script)
        r = call(m, target, args, count)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
            r = r + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        last = struct.unpack("<I", rd(m, 0x6CCD74, 4))[0]
        ring = m.symbols["ps_trail_ring"]
        if on_stack(last) or ring <= last < ring + 96:  # the transforms' record pointer: the stack / our ring
            m.write(0x6CCD74, b"STK!")
        mem = rd(m, SCR, SCR_SIZE) + rd(m, DATA, DATA_SIZE) + (rd(m, BIG, BIG_SIZE) if st.big is not None else b"")
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = None
        if first is not None:
            if first < SCR_SIZE:
                where = SCR + first
            elif first < SCR_SIZE + DATA_SIZE:
                where = DATA + first - SCR_SIZE
            else:
                where = BIG + first - SCR_SIZE - DATA_SIZE
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True
    return None, results[0][0][0] != "ok"


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/ps_owned.txt"):
            if not line.startswith("#"):
                parts = line.split()
                sizes[int(parts[0], 16)] = int(parts[2])
    except OSError:
        pass
    return sizes


def instruction_addresses(address, size):
    out = set()
    try:
        for line in open("build/rollcage.asm"):
            parts = line.split(":", 1)
            if len(parts) == 2:
                try:
                    a = int(parts[0], 16)
                except ValueError:
                    continue
                if address <= a < address + size:
                    out.add(a)
    except OSError:
        pass
    return out


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.reg_write(UC_X86_REG_CR4, m.uc.reg_read(UC_X86_REG_CR4) | 0x600)  # SSE on (OSFXSR / OSXMMEXCPT)
    m.uc.mem_map(SCR, SCR_SIZE)
    m.uc.mem_map(BIG, BIG_SIZE)
    State.data0 = rd(m, DATA, DATA_SIZE)
    stubs = Stubs(m)
    rng = random.Random(53)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, cap) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        seen = set()
        size = sizes.get(address, 0)
        n = min(iterations, cap) if cap else iterations
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(n):
            msg, fault = run_case(m, stubs, name, address, gen, rng)
            faults += fault and not msg
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if "-c" in sys.argv and size:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)  # 0x4836ee-0x4837e9 (0x483240's dead interpolation block) not listed
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed if not 0x4836ee <= a < 0x4837ea][:16]}" if missed else "") + "]"
        both = f" ({faults} fault the same way)" if faults else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {n} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
