#!/usr/bin/env python3
"""Differential test of gl_gb.c (race and car setup, group GB: the track
loader, the sky, the HUD layout, the effect pools, the car's wheels, the car
hit callback and helpers, the projection set-up, the object face blast, the
clock read and the shock-wave fall-off) against the original, on random
state and random x87 control words.

Every run starts from fninit + a random FPU control word (the game's single
precision 0x007f, 0x027f, 0x037f, rounding modes); the x87 stack top after
the call is compared with the memory, the return value and the call log.
Functions outside the group are stubbed in both runs (each stub logs the
arguments its callee reads and returns a scripted value); pure fixed-point
helpers (vec_dot, fix_isqrt, fix_div16, the sqrt table, vec_length,
vec_div, gte_rotate, mem_fill, shock_falloff) run for real.

    tests/emu_gl_gb.py EXE build/emu_gb.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_FPSW  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
ARGS = SCRATCH            # small argument records
CAR = SCRATCH + 0x1000    # 0x800
OTHER = SCRATCH + 0x1800  # 0x800
HIT = SCRATCH + 0x2000    # 0x100
OBJ = SCRATCH + 0x2100    # hit object, 0x200
MODEL = SCRATCH + 0x2300  # 0x200
TABLE = SCRATCH + 0x2500  # 0x100
CONFIG = SCRATCH + 0x2600  # 0x100
CAR2 = SCRATCH + 0x2800  # 0x800
RACE = SCRATCH + 0x3000   # 0x1000
TEX = SCRATCH + 0x4000    # 0x2000 texture entries
INFO_SMALL = SCRATCH + 0x6000  # 0x200 track header for the HUD / smoke tests
POOL = SCRATCH + 0x8000   # 0x8000
OBJS = SCRATCH + 0x10000  # created objects, 8 of 0x200
HEAP = SCRATCH + 0x12000  # allocations, 4 of 0x1000
INFO = SCRATCH + 0x20000  # track file, 0x10000
ARC2 = SCRATCH + 0x30000  # texture file, 0x4000
TRACK = SCRATCH + 0x34000  # track record
SKY = SCRATCH + 0x35000   # sky archive 0x800, vertices 2 x 0x200, packets 2 x 0x800
TRAMP = 0x00310000        # fninit; fldcw; jmp target

STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))
OUTCOMES = {}
VERBOSE = "-v" in sys.argv

# address: (name, args the callee reads, returns, arg masks or "sN" = stack/pointer contents of N bytes)
STUBS = {
    0x43692A: ("game_time", 0, True, None),
    0x40FEB0: ("ai_hit", 3, False, None),
    0x40FE00: ("human_hit", 3, False, None),
    0x407E90: ("vec_bend", 1, False, ["s12"]),
    0x45BE10: ("hit_sound", 3, False, None),
    0x46D760: ("sparks", 3, False, [None, "s12", None]),
    0x460BA0: ("object_new", 4, True, None),
    0x40DFF0: ("wheel_place", 3, False, None),
    0x42AA30: ("blast", 5, False, ["s12", "s12", None, None, None]),
    0x460AF0: ("pool_take", 1, True, None),
    0x460B20: ("header_init", 2, False, None),
    0x42A8F0: ("pool_a", 0, False, None),
    0x42C2B0: ("pool_b", 0, False, None),
    0x42A9E0: ("pool_c", 1, False, None),
    0x42C1D0: ("pool_d", 0, False, None),
    0x42C200: ("pool_e", 0, False, None),
    0x4283D0: ("screen_set", 3, False, None),
    0x401110: ("has_3dnow", 0, True, None),
    0x4270E0: ("prim_flag", 2, False, None),
    0x404980: ("horizon", 1, False, ["s4"]),
    0x427A70: ("list_init", 2, False, None),
    0x428070: ("list_push", 2, False, None),
    0x4387D0: ("hud_slot", 3, False, None),
    0x439D70: ("hud_reset", 3, False, None),
    0x478FF0: ("text_reset", 0, False, None),
    0x429340: ("is_6", 0, True, None),
    0x43B660: ("archive_load", 1, True, ["str"]),
    0x460AA0: ("pool_size", 1, False, None),
    0x428350: ("fog_colour", 3, False, [0xFF, 0xFF, 0xFF]),
    0x428390: ("back_colour", 3, False, [0xFF, 0xFF, 0xFF]),
    0x4445E0: ("heap_take", 1, True, None),
    0x404480: ("sky_set", 1, False, None),
    0x4044C0: ("sky_size", 2, False, None),
    0x428130: ("textures_load", 7, True, None),
    0x404550: ("sky_packets", 1, False, None),
    0x480A80: ("quat_from_angles", 4, False, None),
    0x480780: ("quat_to_matrix", 2, False, None),
    0x442680: ("object_lights", 3, False, [None, None, "s4"]),
    0x460FB0: ("grid_link", 1, False, None),
    0x405070: ("object_special", 2, False, None),
    0x478370: ("object_start", 1, False, None),
    0x403A20: ("object_static", 1, False, None),
    0x47DBC0: ("flare_add", 6, False, [0xFFFF, 0xFFFF, 0xFFFF, None, 0xFF, 0xFF]),
    0x47DC30: ("glow_add", 9, False, [0xFFFF, 0xFFFF, 0xFFFF, None, 0xFF, 0xFF, 0xFF, 0xFF, None]),
    0x41AA70: ("pickup_new", 1, True, None),
    0x4039D0: ("pickup_place", 2, False, None),
    0x478F80: ("records_done", 2, False, None),
    0x444570: ("heap_free", 1, False, None),
    0x40B5B0: ("track_ready", 0, False, None),
    # our own functions, stubbed when another is under test
    0x410050: ("car_hit_ai_hard", 3, False, None),
    0x410090: ("car_hit_ai_object", 3, False, None),
    0x4100E0: ("car_hit_human", 3, False, None),
    0x410110: ("car_hit_human_object", 3, False, None),
    0x40FD40: ("car_hit_timer_start", 2, False, None),
    0x42A710: ("smoke_pool_init", 0, False, None),
}

GTE = (0x6CCBE0, 0x6CCD00)

# name: (address, returns, stubs active, fixed regions compared)
TESTS = {
    "shock_falloff": (0x405210, True, [], []),
    "shock_falloff_apply": (0x4050F0, False, [], [(0x6D06C0, 4), (0x6D06E0, 0x400)]),
    "game_clock_read": (0x428290, True, ["game_time"], []),
    "car_hit_timer_start": (0x40FD40, False, [], [(0x4A7148, 8)]),
    "car_hit_ai_hard": (0x410050, False, ["ai_hit"], []),
    "car_hit_ai_object": (0x410090, False, ["ai_hit"], []),
    "car_hit_human": (0x4100E0, False, ["human_hit"], []),
    "car_hit_human_object": (0x410110, False, ["human_hit"], []),
    "car_hit_callback": (0x40EB60, True, ["vec_bend", "hit_sound", "sparks", "car_hit_ai_hard", "car_hit_ai_object",
                                          "car_hit_human", "car_hit_human_object", "car_hit_timer_start"], []),
    "car_wheels_setup": (0x40DA70, False, ["object_new", "wheel_place"], [(GTE[0], GTE[1] - GTE[0] + 0x50)]),
    "object_face_blast": (0x46BB50, False, ["blast"], [(GTE[0], GTE[1] - GTE[0] + 0x50)]),
    "smoke_pool_init": (0x42A710, False, ["pool_take", "header_init"],
                        [(0x4AC728, 4), (0x6CCB50, 0x60), (0x6CCA50, 4), (0x6CC804, 8), (0x6CC940, 4),
                         (0x6C1E10, 4)]),
    "effects_init": (0x42A610, False, ["smoke_pool_init", "pool_a", "pool_b", "pool_c", "pool_d", "pool_e"],
                     [(0x4AC724, 8), (0x6CCB5C, 0x54), (0x6CCA54, 4), (0x4BE128, 0xC)]),
    "projection_init": (0x426090, False, ["screen_set", "has_3dnow"], [(0x6CCBE0, 0x840)]),
    "sky_set": (0x404480, False, [], [(0x6D0A40, 0x40), (0x4B5FAC, 4)]),
    "sky_packets_build": (0x404550, False, ["prim_flag", "horizon"],
                          [(0x6D0A00, 0x80), (0x4B5FC8, 8), (0x4B5FE0, 4), (0x6CD400, 4)]),
    "hud_layout": (0x4377C0, False, ["list_init", "list_push", "hud_slot", "hud_reset", "text_reset"],
                   [(0x6C3EC0, 0x1F80), (0x6C7DA0, 0x260), (0x6CE5F0, 0x20), (0x4ACFF0, 4)]),
    "track_load": (0x43FA60, True, ["is_6", "archive_load", "pool_size", "fog_colour", "back_colour", "heap_take",
                                    "sky_set", "sky_size", "textures_load", "sky_packets", "object_new",
                                    "quat_from_angles", "quat_to_matrix", "object_lights", "grid_link",
                                    "object_special", "object_start", "object_static", "flare_add", "glow_add",
                                    "pickup_new", "pickup_place", "records_done", "heap_free", "track_ready"],
                   [(0x6C1DC0, 0x100), (0x4AF4B8, 4), (0x6CD4B8, 4), (0x6CDD28, 4), (0x4BEB84, 4),
                    (0x6CFBE0, 0x8C0), (0x4B65F4, 4)]),
}

CONTROL_WORDS = [0x007F, 0x027F, 0x037F, 0x0C7F, 0x047F, 0x087F, 0x0E7F]


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return bytes(rng.getrandbits(8) for _ in range(n))


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def p16(v):
    return struct.pack("<H", v & 0xFFFF)


def f32(v):
    return struct.pack("<f", v)


def rand_float(rng, lo, hi):
    """A float in lo..hi, or an edge value now and then."""
    k = rng.randrange(12)
    if k == 0:
        return pick(rng, f32(lo), f32(hi), f32(0.0), b"\0\0\0\x80", f32(1.0), f32(-1.0))
    if k == 1:
        return pick(rng, b"\0\0\xc0\x7f", b"\0\0\x80\x7f", b"\0\0\x80\xff", rbytes(rng, 4))
    return f32(rng.uniform(lo, hi))


# ---------------------------------------------------------------- states

def st_shock(rng, w):
    for a in (0x6CCBA0, 0x6CCBA4, 0x6CCBA8):
        w.append((a, p32(pick(rng, rng.randrange(-0x4000000, 0x4000000), rng.getrandbits(32)))))
    w.append((0x6CCBB0, p32(pick(rng, rng.randrange(0, 0x800), rng.getrandbits(32), 0))))
    w.append((0x6CCBAC, p32(pick(rng, 0, 1, 1, 1))))
    n = pick(rng, 0, rng.randrange(1, 64), 64)
    w.append((0x6D06C0, p32(n)))
    recs = bytearray(rbytes(rng, 0x400))
    for i in range(64):
        for k in range(3):
            struct.pack_into("<h", recs, i * 16 + k * 2, pick(rng, rng.randrange(-0x400, 0x400),
                                                             rng.randrange(-0x8000, 0x8000)))
        recs[i * 16 + 6] = pick(rng, 0, rng.randrange(1, 0x80), rng.getrandbits(8))
    w.append((0x6D06E0, bytes(recs)))
    return [ARGS], {}


def st_shock_one(rng, w):
    st_shock(rng, w)
    w.append((ARGS, rbytes(rng, 6)))
    return [ARGS], {}


def st_clock(rng, w):
    return [], {"game_time": [pick(rng, rng.randrange(0, 100000), rng.getrandbits(32), 0, 0xFFFFFFFF,
                                   0x80000000)]}


def make_car(rng, human=None):
    car = bytearray(rbytes(rng, 0x800))
    car[0x70C] = pick(rng, 8, 8, 0, 1, rng.getrandbits(8)) if human is None else human
    car[0x6B9] = pick(rng, 1, 0, rng.getrandbits(8))
    struct.pack_into("<H", car, 0x6B4, pick(rng, 0, 0x400, 0x100, rng.getrandbits(16)))
    car[0x65F] = pick(rng, 0, rng.randrange(0, 0x80), rng.getrandbits(8))
    car[0x63F] = pick(rng, 0, 1)
    struct.pack_into("<h", car, 0x658, pick(rng, 0, 3, 4, rng.randrange(-0x8000, 0x8000)))
    struct.pack_into("<I", car, 0x6AC, pick(rng, 0xFFFFFFFF, TABLE, TABLE + 1))
    for off in (0x34, 0x38, 0x3C, 0x664, 0x668, 0x66C):
        struct.pack_into("<i", car, off, rng.randrange(-0x10000000, 0x10000000))
    for off in (0xC6, 0xCC, 0xD2):
        struct.pack_into("<h", car, off, rng.randrange(-0x1000, 0x1001))
    return car


def st_timer(rng, w):
    car = make_car(rng)
    w.append((CAR, bytes(car)))
    w.append((0x4A7148, rbytes(rng, 4) + p32(pick(rng, 0, 1, 2, rng.getrandbits(32)))))
    w.append((TABLE, rbytes(rng, 0x100)))
    w.append((0x6CF654, p32(TABLE)))
    return [CAR, pick(rng, 0x13, 0x14, 0x7F, rng.randrange(-0x100, 0x100), rng.getrandbits(32))], {}


def st_hit_helper(rng, w):
    w.append((CAR, bytes(make_car(rng))))
    w.append((OTHER, bytes(make_car(rng))))
    return [CAR, pick(rng, OTHER, OTHER, 0), pick(rng, 0xC, 0x11, 0x13, 0x14, 0x30, rng.getrandbits(32))], {}


def st_hit_callback(rng, w):
    car = make_car(rng)
    scale = pick(rng, 0x10000, 0x10000, 0x1000000)
    for off in (0x40, 0x44, 0x48):
        struct.pack_into("<i", car, off, rng.randrange(-scale, scale))
    w.append((CAR, bytes(car)))
    w.append((TABLE, rbytes(rng, 0x100)))
    obj = make_car(rng)  # the hit thing: an object (kind 0xc / 0x11) or another car (kind 3)
    struct.pack_into("<I", obj, 0, pick(rng, 3, 0xC, 0x11, 0x203, rng.getrandbits(32)))
    struct.pack_into("<I", obj, 0x18C, pick(rng, CAR2, CAR2, 0))
    struct.pack_into("<I", obj, 0x180, pick(rng, CAR2, CAR2, 0))
    w.append((OTHER, bytes(obj)))
    w.append((CAR2, bytes(make_car(rng))))
    hit = bytearray(rbytes(rng, 0x100))
    struct.pack_into("<I", hit, 4, pick(rng, CAR, OTHER))
    for off in (0x2C, 0x30, 0x34):
        struct.pack_into("<i", hit, off, rng.randrange(-0x100000, 0x100000))
    for off in (0x38, 0x3C, 0x40):
        struct.pack_into("<i", hit, off, pick(rng, rng.randrange(-0x10000, 0x10000),
                                               rng.randrange(-0x200000, 0x200000)))
    for off in (0x5C, 0x60, 0x64):
        struct.pack_into("<I", hit, off, pick(rng, rng.randrange(-0x100000, 0x100000), rng.getrandbits(32)) & 0xFFFFFFFF)
    w.append((HIT, bytes(hit)))
    return [CAR, pick(rng, 0, OTHER, OTHER), HIT], {}


def st_wheels(rng, w):
    car = bytearray(rbytes(rng, 0x800))
    for i in range(4):
        for k in range(3):
            struct.pack_into("<h", car, 0x180 + i * 0x1C + k * 2,
                             pick(rng, rng.randrange(-0x200, 0x200), rng.randrange(-0x8000, 0x8000),
                                  0 if rng.randrange(4) == 0 else 0x40))
    w.append((CAR, bytes(car)))
    w.append((OBJS, rbytes(rng, 0x800)))
    return [CAR, rng.getrandbits(32), rng.getrandbits(32)], {"object_new": [OBJS + i * 0x200 for i in range(4)]}


def st_face_blast(rng, w):
    obj = bytearray(rbytes(rng, 0x200))
    struct.pack_into("<I", obj, 0x10, MODEL)
    w.append((OBJ, bytes(obj)))
    model = bytearray(rbytes(rng, 0x200))
    struct.pack_into("<H", model, 0x34, rng.randrange(0x40, 0x80))
    w.append((MODEL, bytes(model)))
    w.append((ARGS, b"".join(p32(rng.getrandbits(32)) for _ in range(3))))
    w.append((GTE[0], rbytes(rng, GTE[1] - GTE[0])))
    return [OBJ, rng.randrange(0, 4), ARGS], {}


def st_config(rng, w, amount):
    cfg = bytearray(rbytes(rng, 0x100))
    cfg[0x68:0x6C] = amount
    cfg[0x33] = pick(rng, 0, 1, 2, rng.getrandbits(8))
    w.append((CONFIG, bytes(cfg)))
    w.append((0x4B2198, p32(CONFIG)))


def st_textures(rng, w):
    w.append((TEX, rbytes(rng, 0x2000)))
    w.append((0x6C1E14, p32(TEX)))
    info = bytearray(rbytes(rng, 0x200))
    struct.pack_into("<H", info, 0x2E, pick(rng, 0, rng.randrange(0, 0x40)))
    w.append((INFO_SMALL, bytes(info)))
    w.append((0x6C1EB0, p32(INFO_SMALL)))


def st_smoke(rng, w):
    st_config(rng, w, pick(rng, f32(rng.uniform(0, 0.5)), f32(rng.choice([0.0, 0.25, 0.5, 0.1])),
                           f32(rng.uniform(0, 0.53))))
    st_textures(rng, w)
    w.append((0x6C1E10, p32(rng.randrange(0, 0x80))))
    w.append((0x6CCD84, rbytes(rng, 4)))
    w.append((POOL, rbytes(rng, 0x8000)))
    return [], {"pool_take": [POOL]}


def st_effects(rng, w):
    st_config(rng, w, rand_float(rng, -2.0, 14.0))
    n = pick(rng, 0, rng.randrange(1, 64), 64)
    w.append((0x4AC728, p32(n)))
    w.append((0x6CCB5C, p32(POOL)))
    w.append((POOL, rbytes(rng, 0x8000)))
    return [], {}


def st_projection(rng, w):
    w.append((0x4ACC38, p32(pick(rng, 320, 512, 640, 800, 1024, 1280, 3440, rng.randrange(0, 4000),
                                 rng.getrandbits(32)))))
    w.append((0x6CCBE0, rbytes(rng, 0x840)))
    return [], {"has_3dnow": [pick(rng, 0, 1)]}


def st_sky(rng, w):
    rec = bytearray(rbytes(rng, 8))
    w.append((ARGS, bytes(rec)))
    return [ARGS], {}


def st_sky_packets(rng, w):
    st_config(rng, w, rbytes(rng, 4))
    st_textures(rng, w)
    n1 = pick(rng, 0, rng.randrange(1, 9), 8)
    n2 = pick(rng, 0, rng.randrange(1, 9), 8)
    w.append((0x6D0A78, p32(n1)))
    w.append((0x6D0A00, p32(n2)))
    arc = bytearray(rbytes(rng, 0x800))
    struct.pack_into("<I", arc, 0x1C, 0x40)
    for i in range(16):
        r = 0x40 + i * 0x22
        for k in range(12):
            struct.pack_into("<h", arc, r + k * 2, pick(rng, rng.randrange(-0x400, 0x100), rng.randrange(-0x8000, 0x8000)))
        struct.pack_into("<H", arc, r + 0x20, rng.randrange(0, 0x200))
    w.append((SKY, bytes(arc)))
    w.append((0x6D0A3C, p32(SKY + 0x800)))
    w.append((0x6D0A38, p32(SKY + 0xA00)))
    w.append((0x4B5FC8, p32(SKY + 0xC00)))
    w.append((0x4B5FCC, p32(SKY + 0x1400)))
    w.append((SKY + 0x800, rbytes(rng, 0x1400)))
    w.append((0x6CD400, rbytes(rng, 4)))
    horizon = pick(rng, rng.randrange(-0x400, 0x400), rng.randrange(-0x400, 0x400), rng.randrange(-0x8000, 0x8000),
                   -1, 0xFFFFF448, 0 if rng.randrange(4) == 0 else 0x1234)
    return [SKY], {"horizon": [horizon]}


def st_hud(rng, w):
    st_textures(rng, w)
    race = bytearray(rbytes(rng, 0x1000))
    struct.pack_into("<HH", race, 0xA80, pick(rng, 320, 512, 640, 800, 1024, rng.getrandbits(16)),
                     pick(rng, 240, 384, 480, 600, 768, rng.getrandbits(16)))
    race[0x552] = pick(rng, 0, 0, 1, 2, 3, rng.getrandbits(8))
    race[0x551] = pick(rng, *range(10), rng.getrandbits(8))
    race[0xA86] = pick(rng, 0, 1)
    w.append((RACE, bytes(race)))
    w.append((0x4B1960, p32(RACE)))
    w.append((0x4ACC38, p32(pick(rng, 320, 512, 640, 800, 1024, rng.randrange(1, 4000), rng.getrandbits(32)))))
    w.append((0x4ACFF0, p32(pick(rng, 0x140, 0x200, 0x280, 0x226, rng.randrange(1, 0x1000), rng.getrandbits(32)))))
    w.append((0x6CCD84, rbytes(rng, 4)))
    for a, n in [(0x6C3EC0, 0x1F80), (0x6C7DA0, 0x260), (0x6CE5F0, 0x20)]:
        w.append((a, rbytes(rng, n)))
    return [], {}


def st_track(rng, w):
    """A track file (INFO) and a texture file (ARC2) with in-range offsets."""
    info = bytearray(rbytes(rng, 0x10000))
    base = INFO
    n = pick(rng, 0, rng.randrange(1, 5), 4)
    struct.pack_into("<i", info, 8, n)
    nk = pick(rng, 0, rng.randrange(1, 5), 4)
    struct.pack_into("<i", info, 0x10, nk)
    struct.pack_into("<I", info, 0x14, 0x200)
    for j in range(4):
        struct.pack_into("<I", info, 0x200 + j * 4, 0x300 + j * 0x60)
        struct.pack_into("<I", info, 0x300 + j * 0x60 + 0x4C, rng.randrange(0, 0x10))
    nm = pick(rng, 0, rng.randrange(1, 5), 4)
    struct.pack_into("<H", info, 0x2C, nm)
    struct.pack_into("<I", info, 0x18, 0x500)
    for j in range(16):
        struct.pack_into("<I", info, 0x500 + j * 4, pick(rng, 0, 0x600 + (j % 4) * 0x100))
    for j in range(4):
        m = 0x600 + j * 0x100
        struct.pack_into("<HH", info, m + 0x1C, pick(rng, 0, 0x50), pick(rng, 0, 0xA0))
        for s in (m, m + 0x50, m + 0xA0):
            struct.pack_into("<I", info, s + 0x44, pick(rng, 0, rng.getrandbits(32)))
    struct.pack_into("<I", info, 0x24, 0x1000)  # spots, 0x100 of 0x10
    for j in range(0x100):
        info[0x1000 + j * 16] = pick(rng, 0xFF, 0xFF, rng.getrandbits(8))
        info[0x1001 + j * 16] = pick(rng, 0, 0, rng.getrandbits(8))
    nc = pick(rng, 0, rng.randrange(1, 9), 8)
    struct.pack_into("<H", info, 0x66, nc)
    struct.pack_into("<I", info, 0x4C, 0xA00)
    struct.pack_into("<I", info, 0x80, 0xB00)
    struct.pack_into("<B", info, 0x79, pick(rng, 0, rng.randrange(1, 5), 4))
    struct.pack_into("<I", info, 0x8C, 0xC00)
    struct.pack_into("<I", info, 0x98, rng.randrange(0, 0x100))
    nslots = pick(rng, 0, rng.randrange(1, 5), 4)
    struct.pack_into("<H", info, 0x96, nslots)
    struct.pack_into("<I", info, 0xAC, 0xD00)
    for j in range(4):
        struct.pack_into("<I", info, 0xD00 + j * 4, pick(rng, rng.randrange(0, 6), rng.getrandbits(32)))
    struct.pack_into("<B", info, 0xB6, pick(rng, 0, rng.randrange(1, 7), 6, rng.getrandbits(8) % 50))
    struct.pack_into("<I", info, 0xF0, 0x2000)
    for j in range(50):
        info[0x2000 + j * 16 + 6] = pick(rng, 1, 0, 2, rng.getrandbits(8))
    struct.pack_into("<I", info, 0x110, 0xF00)
    struct.pack_into("<H", info, 0xC8, rng.randrange(0, 0x100))
    struct.pack_into("<I", info, 0xC4, 0x3000)
    w.append((INFO, bytes(info)))
    arc = bytearray(rbytes(rng, 0x4000))
    cnt = pick(rng, 0, rng.randrange(1, 7), 6)
    struct.pack_into("<i", arc, 0x14, cnt)
    struct.pack_into("<I", arc, 0x28, 0x200)
    struct.pack_into("<I", arc, 0x24, 0x800)
    struct.pack_into("<I", arc, 0x38, 0x900)
    struct.pack_into("<I", arc, 0x3C, 0xA00)
    for i in range(6):
        r = 0x200 + i * 0x38
        kind = pick(rng, 0, 0, 2, 0xD, 0x18, rng.randrange(0, 0x19), rng.getrandbits(10))
        struct.pack_into("<H", arc, r, kind | (pick(rng, 0, rng.getrandbits(6)) << 10))
        arc[r + 3] = rng.randrange(0, 16)
        struct.pack_into("<H", arc, r + 6, pick(rng, 0xFFFF, rng.randrange(0, 16)))
        arc[r + 0x16] = pick(rng, 0, 0, 1)
        arc[r + 0x1F] = pick(rng, 0, 2, 1, rng.getrandbits(8))
        struct.pack_into("<H", arc, r + 0x14, rng.randrange(0, 0x40))
    w.append((ARC2, bytes(arc)))
    w.append((TRACK, rbytes(rng, 0x100)))
    w.append((ARGS, b"TRACK" + bytes([rng.randrange(0x30, 0x3A)]) + b"\0"))
    w.append((0x4BEB84, p32(pick(rng, 0, HEAP + 0x3000))))
    w.append((0x6CCDC0, pick(rng, f32(0.5), f32(0.5), f32(1.0), rand_float(rng, 0.0, 2.0))))
    w.append((HEAP, rbytes(rng, 0x4000)))
    w.append((OBJS, rbytes(rng, 0x1000)))
    w.append((0x6CFBE0, rbytes(rng, 0x8C0)))
    six = pick(rng, 0, 1)
    script = {
        "is_6": [six, six],
        "archive_load": [base, ARC2],
        "heap_take": [HEAP, HEAP + 0x1000, HEAP + 0x2000, HEAP + 0x3000],
        "textures_load": [pick(rng, 1, 1, 1, 0)],
        "object_new": [OBJS + i * 0x200 for i in range(8)],
        "pickup_new": [rng.getrandbits(32) for _ in range(6)],
    }
    return [ARGS, TRACK], script


STATES = {
    "shock_falloff": st_shock_one,
    "shock_falloff_apply": st_shock,
    "game_clock_read": st_clock,
    "car_hit_timer_start": st_timer,
    "car_hit_ai_hard": st_hit_helper,
    "car_hit_ai_object": st_hit_helper,
    "car_hit_human": st_hit_helper,
    "car_hit_human_object": st_hit_helper,
    "car_hit_callback": st_hit_callback,
    "car_wheels_setup": st_wheels,
    "object_face_blast": st_face_blast,
    "smoke_pool_init": st_smoke,
    "effects_init": st_effects,
    "projection_init": st_projection,
    "sky_set": st_sky,
    "sky_packets_build": st_sky_packets,
    "hud_layout": st_hud,
    "track_load": st_track,
}


class Stubs:
    def __init__(self, m):
        self.log = []
        self.script = {}
        self.active = set()
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.active = set(active)

    def _hook(self, uc, address, size, _):
        name, nargs, returns, masks = STUBS[address]
        if name not in self.active:
            return
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, a in enumerate(args):
            mk = masks[i] if masks else None
            if isinstance(mk, int):
                args[i] = a & mk
            elif mk == "str":
                args[i] = bytes(uc.mem_read(a, 16)).split(b"\0")[0]
            elif isinstance(mk, str):
                args[i] = ("mem", bytes(uc.mem_read(a, int(mk[1:]))))
            elif E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        values = self.script.get(name)
        eax = values.pop(0) if values else 0
        if name == "horizon":
            uc.mem_write(words[1], p32(eax))
            eax = 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF if returns else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


TRAMP_SLOTS = {}


def call_fpu(m, target, args, cw):
    """Runs target after fninit and fldcw cw. One trampoline per target:
    Unicorn keeps translated code, so a trampoline is never rewritten."""
    if target not in TRAMP_SLOTS:
        at = TRAMP + 0x10 * len(TRAMP_SLOTS)
        code = b"\xdb\xe3" + b"\xd9\x2d" + p32(TRAMP + 0x800)
        code += b"\xe9" + p32(target - (at + len(code) + 5))
        m.write(at, code)
        TRAMP_SLOTS[target] = at
    m.write(TRAMP + 0x800, p16(cw))
    return m.call(TRAMP_SLOTS[target], args)


def run_case(m, stubs, name, rng):
    address, returns, active, regions = TESTS[name]
    w = []
    args, script = STATES[name](rng, w)
    cw = pick(rng, *CONTROL_WORDS)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        for a, data in w:
            m.write(a, data)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(script, active)
        r = call_fpu(m, target, args, cw)
        if not returns and r[0] == "ok":
            r = ("ok",)
        top = (m.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in regions)
        if name == "object_face_blast":  # 0x6ccc6a: the original's leftover stack halfword
            off = SCRATCH_SIZE + 0x6CCC6A - GTE[0]
            mem = mem[:off] + b"\0\0" + mem[off + 2:]
        results.append((r, top, mem, stubs.log))
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        a, b = results[0][2], results[1][2]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if VERBOSE:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), hex(struct.unpack_from("<I", a, i)[0]), hex(struct.unpack_from("<I", b, i)[0]))
                                for i in diff[:8]], "cw", hex(cw))
        logs = ""
        if results[0][3] != results[1][3]:
            la, lb = results[0][3], results[1][3]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        tops = "" if results[0][1] == results[1][1] else f" (x87 top {results[0][1]} vs {results[1][1]})"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + tops + logs
    return None


def instruction_addresses(start, size):
    """The original's instruction addresses in start..start+size (from build/rollcage.asm)."""
    out = set()
    asm = __file__.rsplit("/", 2)[0] + "/build/rollcage.asm"
    for line in open(asm):
        head = line.split(":", 1)[0].strip()
        try:
            a = int(head, 16)
        except ValueError:
            continue
        if start <= a < start + size:
            out.add(a)
    return out


SIZES = {0x43FA60: 4545, 0x4377C0: 2693, 0x404550: 651, 0x40DA70: 905, 0x40EB60: 762, 0x426090: 366,
         0x42A710: 469, 0x42A610: 241, 0x46BB50: 524}


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 500
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    m.uc.mem_map(TRAMP, 0x1000)
    stubs = Stubs(m)
    rng = random.Random(0x6B)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "--cov")]
    covered = set()
    if "--cov" in sys.argv:
        m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, _: covered.add(a), begin=0x401000, end=0x4A0000)
    for name in TESTS:
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        OUTCOMES.clear()
        for _ in range(iterations):
            msg = run_case(m, stubs, name, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if "--cov" in sys.argv and TESTS[name][0] in SIZES:
            want = instruction_addresses(TESTS[name][0], SIZES[TESTS[name][0]])
            missed = sorted(want - covered)
            cov = f" coverage {len(want) - len(missed)}/{len(want)}" + (
                f" missed {[hex(a) for a in missed[:40]]}" if missed else "")
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {OUTCOMES}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
