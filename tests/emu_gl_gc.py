#!/usr/bin/env python3
"""Differential test of gl_gc.c (group GC: dynamic lights 0x441450 tree,
wheel dust 0x4219e0, skid marks 0x46ecb0-0x46efa0, effect triggers, the
trigger queue, rumble, the step clock, the pause screen 0x463b70 /
0x464030 / 0x463ca0, the snatch mesh 0x48f930, the object light point
0x442c90) against the original, on random game state.

Outside callees are logging stubs in both runs (stack-pointer arguments
logged by content), except the pure maths that run for real (original code
in both runs): gte_rotate 0x428090, the x87 float transform 0x426620,
fix_mul8 0x4291a0, vec_length / vec_div / vec_cross / vec_dot. Memory
compared: the touched globals (data section regions below, the GTE scratch
and float matrix 0x6ccbe0-0x6ccd50 included) and the whole scratch area.
The stack is zeroed before each run.

    tests/emu_gl_gc.py EXE build/emu_gc.elf [iterations] [names...] [-v] [--cov]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x20000
CARS = [SCRATCH + i * 0x3000 for i in range(4)]
CAMS = SCRATCH + 0xC000  # 0x1300
SETS = SCRATCH + 0xE000  # 6 x 0x200
SURF = SCRATCH + 0x10000  # 8 x 0x10
DMG = SCRATCH + 0x10200  # 0x40
DMGTAB = SCRATCH + 0x10300  # 0x40
WHEEL = SCRATCH + 0x10400  # 0x100
VEC = SCRATCH + 0x10600  # vectors
CARREF = SCRATCH + 0x10F00
SKID = SCRATCH + 0x11000  # 8 x 0x124
TRACK = SCRATCH + 0x12000  # 0x200
PTS1 = SCRATCH + 0x12200  # 64 x 12
PTS2 = SCRATCH + 0x12600
TEX = SCRATCH + 0x13000  # 0x2000
GLINT = SCRATCH + 0x15000
RECTAB = SCRATCH + 0x15100  # *0x6c1e30
RECS = SCRATCH + 0x15200  # 32 x 12
OBJS = [SCRATCH + 0x16000 + i * 0x200 for i in range(8)]
SHAPE = SCRATCH + 0x17000
CFG = SCRATCH + 0x18000
RACE = SCRATCH + 0x19000

NLIGHTS = 64
REGIONS = [
    (0x4B11D8, 8), (0x4B1960, 4), (0x4B2198, 4), (0x4B5E58, 0x150), (0x4BCD54, 4), (0x4BE390, 4), (0x4BE3E0, 4),
    (0x4D1198, 4), (0x4D44A8, 4), (0x5D6E00, 0x100), (0x5D71E0, 0x600), (0x5D7FE0, 8), (0x5D8000, 0x40 * NLIGHTS),
    (0x5F7218, 0x90), (0x6002D0, 0xC), (0x6B8D60, 0x8A0), (0x6B9838, 4), (0x6B9B94, 0x128), (0x6BFB80, 0x274 * 4),
    (0x6C1B88, 0x200), (0x6C1E14, 4), (0x6C1E30, 4), (0x6C1EB0, 4), (0x6CCBE0, 0x170), (0x6CCD84, 4), (0x6CD410, 4),
]

# address: (name, number of args logged, masks)
STUBS = {
    0x43692A: ("game_time", 0, None),
    0x40D6F0: ("surface_kind", 1, None),
    0x489AB0: ("rumble_a", 2, None),
    0x489BC0: ("rumble_b", 2, None),
    0x441220: ("light_spawn_queued", 3, None),
    0x4413A0: ("light_move", 2, None),
    0x441370: ("light_free", 1, None),
    0x462E80: ("body_script_step", 1, None),
    0x462690: ("particle_spawn", 3, None),
    0x442F70: ("glint_spawn", 5, None),
    0x42AA30: ("explosion_effects", 5, None),
    0x471D50: ("sound_play_flagged", 6, {5: 0xFF}),
    0x471C90: ("sound_play", 5, None),
    0x472A40: ("sound_play_volume", 6, None),
    0x471B30: ("sound_cd_volume_keep", 2, {0: 0xFFFF, 1: 0xFFFF}),
    0x471B80: ("sound_volume_set", 1, {0: 0xFFFF}),
    0x471C30: ("sound_cd_pause", 1, None),
    0x46FE90: ("snd_resume_all", 0, None),
    0x472040: ("mixer", 0, None),
    0x46F080: ("snd_box_merge", 2, None),
    0x40EAE0: ("car_stats_set", 4, None),
    0x48CEF0: ("local_car_index", 0, None),
    0x48CF20: ("net_local_slot", 0, None),
    0x45EF80: ("net_state_game", 0, None),
    0x45EED0: ("net_state_host", 0, None),
    0x48CFD0: ("net_player_active", 1, None),
    0x460A20: ("net_frame", 0, None),
    0x45FDE0: ("net_pause_state", 0, None),
    0x45F990: ("net_pause_set", 1, None),
    0x45D170: ("net_race_abort", 0, None),
    0x48BED0: ("net_unpause", 1, None),
    0x4600A0: ("net_message", 3, None),
    0x48BF70: ("net_drop_player", 1, None),
    0x45E8C0: ("controls_fetch", 3, None),
    0x4977B0: ("cd_present", 0, None),
    0x443B50: ("race_draw", 0, None),
    0x423F20: ("present", 0, None),
    0x463F90: ("pause_draw", 0, None),
    0x464C10: ("hud_slider", 3, None),
    0x464650: ("menu_page", 2, {1: 0xFFFF}),
    0x463990: ("replay_state_clear", 0, None),
}

# name: (address, argument kinds, return mask (None = void))
TESTS = {
    "pair_users_release": (0x403950, "k", None),
    "object_trigger_1a": (0x41AC20, "Ona", None),
    "object_trigger_set": (0x426060, "Ona", None),
    "rumble_update": (0x41C2C0, "", None),
    "flag_4bcd54_clear": (0x4219C0, "", 0xFFFFFFFF),
    "pause_pad_bits": (0x427D90, "a", 0xFFFFFFFF),
    "sim_clock_start": (0x43693F, "", None),
    "sim_steps_due": (0x436958, "", 0xFFFFFFFF),
    "particles_update": (0x441450, "aaf", None),
    "light_shape_step": (0x4416A0, "L", None),
    "light_fade_step": (0x441720, "Lim", None),
    "light_object_step": (0x4417E0, "Li", None),
    "light_object_step2": (0x441840, "Li", None),
    "light_head_step": (0x4418A0, "Li", None),
    "trigger_queue_process": (0x442890, "", None),
    "trigger_fire": (0x442C20, "O", None),
    "trigger_light_colour": (0x442E40, "Oc", None),
    "trigger_glint": (0x442EB0, "c", None),
    "trigger_glint2": (0x442F20, "c", None),
    "car_blast_hit": (0x4599C0, "CO", None),
    "body_sparks": (0x45BD30, "Cs", None),
    "contact_particle": (0x45BE10, "COa", None),
    "pause_is_local": (0x463970, "", 0xFFFFFFFF),
    "pause_frame": (0x463B70, "", 0xFFFFFFFF),
    "pause_menu_input": (0x464030, "", None),
    "camera_cycle": (0x469BB0, "Cd", None),
    "camera_setting_copy": (0x469C50, "CO", None),
    "camera_view_copy": (0x469CD0, "CO", None),
    "body_velocity_gap": (0x46C1A0, "CP", 0xFFFFFFFF),
    "blast_mark": (0x46D760, "CVa", 0xFF),
    "wheel_skid_marks": (0x46ECB0, "Wpu", None),
    "skid_mark_start": (0x46ED80, "WS", 0xFFFFFFFF),
    "skid_mark_point": (0x46EE30, "W", 0xFFFFFFFF),
    "skid_mark_moved": (0x46EFA0, "W", 0xFFFFFFFF),
    "pair_list_remove": (0x478430, "K", 0xFFFFFFFF),
    "car_wheel_dust": (0x4219E0, "C", None),
    "body_point_world": (0x442C90, "CVB", 0xFFFFFFFF),
    "snatch_mesh_update": (0x48F930, "C", None),
    "pause_open": (0x463CA0, "R", None),
}


def pick(rng, *choices):
    return rng.choice(choices)


class State:
    def __init__(self, rng):
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scratch = bytearray(rng.randbytes(SCRATCH_SIZE))

    def put(self, addr, fmt, value):
        if fmt[-1] in "bhiBHI" and isinstance(value, int):
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt.upper()
        if SCRATCH <= addr < SCRATCH + SCRATCH_SIZE:
            struct.pack_into(fmt, self.scratch, addr - SCRATCH, value)
            return
        for a, n in REGIONS:
            if a <= addr < a + n:
                struct.pack_into(fmt, self.mem[a], addr - a, value)
                return
        raise KeyError(hex(addr))

    def putb(self, addr, data):
        if SCRATCH <= addr < SCRATCH + SCRATCH_SIZE:
            self.scratch[addr - SCRATCH:addr - SCRATCH + len(data)] = data
            return
        for a, n in REGIONS:
            if a <= addr < a + n:
                self.mem[a][addr - a:addr - a + len(data)] = data
                return
        raise KeyError(hex(addr))


def rand_float(rng):
    k = rng.randrange(4)
    if k == 0:
        return rng.choice([0.0, 4.0, 3.9999998, 4.0000005, -1.0, 8.0, float("nan")])
    if k == 1:
        return rng.uniform(0.0, 8.0)
    return rng.uniform(-100.0, 100.0)


def big(rng):
    return pick(rng, rng.randrange(-0x100000, 0x100000), rng.randrange(-0x1000000, 0x1000000),
                rng.getrandbits(32), rng.randrange(-0x40000000, 0x40000000))


def make_car(st, rng, car):
    for o in (0x34, 0x38, 0x3C):
        st.put(car + o, "<i", pick(rng, rng.randrange(-0x10000000, 0x10000000), rng.getrandbits(32)))
    for o in (0x40, 0x44, 0x48, 0x68, 0x6C, 0x70, 0x80, 0x84, 0x88):
        st.put(car + o, "<i", pick(rng, rng.randrange(-0x80000, 0x80000), big(rng)))
    for k in range(9):
        st.put(car + 0xC4 + k * 2, "<h", pick(rng, rng.randrange(-4096, 4097), 4096, 0, rng.getrandbits(16)))
    st.put(car, "<I", pick(rng, 3, 3, 0x1A, 0x1A, rng.getrandbits(32), 0x203))
    for w in range(4):
        st.put(car + 0x198 + w * 0x1C, "<I", pick(rng, 0, SURF + rng.randrange(8) * 0x10,
                                                    SURF + rng.randrange(8) * 0x10))
        wheel = car + 0x324 + w * 0xC4
        st.put(wheel, "<B", pick(rng, 0, 1, 3, rng.getrandbits(8)))
        for o in (0x60, 0x64, 0x68):
            st.put(wheel + o, "<i", pick(rng, rng.randrange(-0x80000, 0x80000), big(rng)))
    st.put(car + 0x63F, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    st.put(car + 0x654, "<i", pick(rng, rng.randrange(0, 0x800000), 0x1FFFF, 0x20000, 0x3FFFFF, 0x400000,
                                   rng.getrandbits(32), rng.randrange(-0x100000, 0x100000)))
    st.put(car + 0x658, "<h", pick(rng, rng.randrange(-50, 300), rng.getrandbits(16)))
    st.put(car + 0x64E, "<H", pick(rng, 0, 1, 2, 3, rng.getrandbits(16)))
    st.put(car + 0x6AC, "<I", DMG if rng.randrange(8) else 0xFFFFFFFF)
    st.put(car + 0x188, "<i", pick(rng, 0, 0x10, 0x40, 0x80, rng.getrandbits(32)))
    st.put(car + 0x2470, "<I", CAMS)
    st.put(car + 0x33C, "<i", pick(rng, rng.randrange(0, 0x400), rng.randrange(-0x10000, 0x10000), big(rng)))
    st.put(car + 0x2A34, "<h", pick(rng, 0, 0x1000, 0xF80, rng.randrange(0, 0x1001), rng.getrandbits(16)))
    st.put(car + 0x13FD, "<B", pick(rng, 0, 1, 2, rng.getrandbits(8)))


def make_state(rng):
    st = State(rng)
    put = st.put
    put(0x4B1960, "<I", RACE)
    put(0x4B2198, "<I", CFG)
    put(0x6C1E14, "<I", TEX)
    put(0x6C1E30, "<I", RECTAB)
    put(0x6C1EB0, "<I", TRACK)
    put(0x4D44A8, "<I", DMGTAB)
    put(0x6CD410, "<i", pick(rng, 0x100, 0x200, 0x155, rng.randrange(0, 0x1000), rng.getrandbits(32)))
    put(0x6CCD84, "<I", pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))
    put(0x4BCD54, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    put(RECTAB + 0x60, "<I", RECS)
    for i in range(32):
        put(RECS + i * 12 + 3, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    # pair table
    n = pick(rng, 0, 1, 2, rng.randrange(0, 0x2A), 0x29)
    put(0x4B5FA0, "<I", n)
    keys = [rng.randrange(1, 6) for _ in range(0x29)]
    for i in range(0x29):
        put(0x4B5E58 + i * 8, "<I", keys[i])
        put(0x4B5E5C + i * 8, "<I", rng.randrange(32))
    # pair list
    put(0x5F72A0, "<I", pick(rng, 0, 1, rng.randrange(0, 17), 16))
    for i in range(17):
        put(0x5F7218 + i * 8, "<I", rng.randrange(1, 6))
    # race, settings
    race = RACE
    put(race + 0x54D, "<B", pick(rng, 0, 1, 2, 4, rng.randrange(0, 5)))
    put(race + 0x552, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    put(race + 0x551, "<B", pick(rng, 0, 1, 2, 3, 4, 5, 9, 10, rng.getrandbits(8)))
    put(race + 0x57C, "<i", pick(rng, 0, 6, 7, 8, rng.getrandbits(32)))
    put(CFG + 0x2C, "<B", pick(rng, 0, 1, 1))
    put(CFG + 0x68, "<f", rand_float(rng))
    # cars, slots
    for i, car in enumerate(CARS):
        make_car(st, rng, car)
        slot = 0x6BFB80 + i * 0x274
        put(slot, "<I", car)
        put(slot + 0x268, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
        put(slot + 0x26C, "<h", rng.randrange(-2, 5))
    put(CARREF, "<I", CARS[0])
    put(0x6B8D80, "<I", CARREF)
    for s in range(8):
        put(SURF + s * 0x10 + 0xC, "<I", pick(rng, rng.randrange(0, 16), rng.getrandbits(32)))
    # cameras
    put(CAMS + 0x1218, "<B", pick(rng, 0, 3, 5, rng.randrange(0, 6), rng.getrandbits(8)))
    for k in range(6):
        put(CAMS + k * 4, "<I", SETS + k * 0x200)
    # damage
    put(DMG, "<B", pick(rng, 0, 0, 1, rng.getrandbits(8)))
    put(DMG + 3, "<B", pick(rng, 0, 0, 10))
    for i in range(64):
        put(DMGTAB + i, "<B", rng.randrange(0, 0x3C))
    # track points
    put(TRACK + 0x2E, "<H", rng.randrange(0, 0x100))
    put(TRACK + 0x108, "<I", PTS1)
    put(TRACK + 0x10C, "<I", PTS2)
    for i in range(64):
        put(PTS1 + i * 12 + 6, "<h", pick(rng, 0, 1, 2, 3, rng.getrandbits(16)))
    # objects with trigger actions
    for o in OBJS:
        for k in range(5):
            kind = pick(rng, 0, 1, 2, 3, 4, 5, rng.randrange(16))
            put(o + 0x16A + k * 2, "<H", kind << 12 | rng.randrange(48))
        put(o + 0x10C, "<B", pick(rng, 0, 1))
    for i in range(64):
        put(0x5D6E00 + i * 4, "<I", rng.choice(OBJS))
    # trigger queue
    cnt = pick(rng, 0, 1, 2, rng.randrange(0, 12), rng.randrange(0, 70))
    put(0x6B9CB8, "<I", cnt)
    for i in range(70):
        put(0x6B9BA0 + i * 4, "<I", rng.choice(OBJS[:pick(rng, 2, 8)]))
    # lights
    nl = pick(rng, 0, 1, 2, rng.randrange(1, 20), rng.randrange(1, NLIGHTS))
    put(0x5D7FE0, "<I", nl)
    for i in range(NLIGHTS):
        l = 0x5D8000 + i * 0x40
        kind = pick(rng, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, rng.getrandbits(16))
        put(l + 0x24, "<H", kind)
        put(l + 0x26, "<h", pick(rng, 0, 1, 2, 0x10, 0x40, rng.getrandbits(16)))
        put(l + 0x12, "<H", pick(rng, 0, 1, rng.getrandbits(16)))
        put(l + 0x28, "<I", pick(rng, 0, SHAPE))
        put(l + 0x2E, "<H", rng.randrange(0, 0x40))
        put(l + 0x2C, "<H", pick(rng, 0, rng.randrange(0, 0x40), rng.getrandbits(16)))
        if rng.randrange(30):
            put(l + 0x34, "<I", rng.choice(CARS))
        put(l + 0x30, "<I", pick(rng, 0, 0, rng.choice(OBJS)))
    for i in range(64):
        put(0x6C1B88 + i * 8 + 6, "<H", pick(rng, 0, 1, rng.getrandbits(16)))
    na = pick(rng, 0, 1, rng.randrange(0, 20), rng.randrange(0, 65))
    put(0x5D7FE4, "<I", na)
    for i in range(64):
        put(0x5D71E0 + i * 4, "<I", pick(rng, rng.getrandbits(29), rng.getrandbits(32), 0x20000000 | rng.getrandbits(29)))
    nq = pick(rng, 0, 1, rng.randrange(0, 8))
    put(0x5D72E0, "<I", (rng.getrandbits(16) << 16) * pick(rng, 0, 0, 1) | nq)
    put(0x5D72E0, "<I", nq)
    for i in range(8):
        put(0x5D7300 + i * 0x24 + 0x12, "<H", pick(rng, 0, 1, 2, 3))
    # pause
    item = pick(rng, 0, 1, 2, 3, 4, 0x10, 0x11, 0x12, 0x14, 0x20, 0x21, 0x22, 0x23, 0x24, 0xA0, 0xA1, 0xB0, 0xB1,
                rng.randrange(0x140, 0x146), rng.randrange(0x140, 0x146), rng.randrange(-0x20, 0x200))
    put(0x6B8D64, "<h", item >> 4)
    put(0x6B8D66, "<h", item & 0xF if rng.randrange(8) else rng.randrange(-2, 20))
    items = rng.randrange(1, 12)
    put(0x6B9584, "<h", items)
    on = [rng.randrange(2) for _ in range(items)]
    on[rng.randrange(items)] = 1
    for i in range(24):
        put(0x6B9586 + i, "<B", on[i] if i < items else rng.randrange(2))
    put(0x6B8D84, "<i", pick(rng, 0, 0x100, 0x3F00, 0x3FFF, rng.randrange(-0x200, 0x4200), rng.getrandbits(32)))
    put(0x6B8D88, "<i", pick(rng, 0, 0x100, 0x3F00, 0x3FFF, rng.randrange(-0x200, 0x4200), rng.getrandbits(32)))
    put(0x6B8D68, "<i", rng.randrange(-10, 100))
    put(0x6B8D60, "<I", pick(rng, 0, 1, 1))
    put(0x6B8D74, "<I", pick(rng, 0, 0, 0, rng.getrandbits(16), rng.getrandbits(32)))
    # skid marks
    w = WHEEL
    put(w + 0x9C, "<B", rng.randrange(4))
    put(w + 0x9E, "<B", pick(rng, 0, 1))
    put(w + 0xA8, "<I", pick(rng, 0, 0x8000, rng.getrandbits(32)))
    put(w + 0xB8, "<I", pick(rng, 0xFFFFFFFF, SKID + rng.randrange(8) * 0x124))
    for o in (0x10, 0x14, 0x18):
        put(w + o, "<i", pick(rng, rng.randrange(-0x10000, 0x10000), rng.randrange(-0x200, 0x200), big(rng)))
    for o in (0x40, 0x44, 0x48, 0xAC, 0xB0, 0xB4):
        put(w + o, "<i", pick(rng, rng.randrange(-0x8000, 0x8000), rng.randrange(-0x100, 0x100), big(rng)))
    for o in (0xA0, 0xA2, 0xA4):
        put(w + o, "<h", rng.getrandbits(16))
    put(w + 0x58, "<i", pick(rng, rng.randrange(-0x200000, 0x200000), big(rng)))
    if rng.randrange(2):  # near its last mark point
        car = CARS[rng.randrange(4)]
        put(w + 0x9C, "<B", CARS.index(car))
        for o in (0x34, 0x38, 0x3C, 0x68, 0x6C, 0x70):
            put(car + o, "<i", rng.randrange(-0x10000, 0x10000))
        for o in (0x1C, 0x20, 0x24):
            put(w + o, "<i", rng.randrange(-0x10000, 0x10000))
        for o in (0xA0, 0xA2, 0xA4):
            put(w + o, "<h", rng.randrange(-3, 3))
        if rng.randrange(4) == 0:
            put(w + 0xA4, "<h", rng.getrandbits(16))
        for o in (0x40, 0x44, 0x48):
            v = rng.randrange(-0x40000, 0x40000)
            put(w + o, "<i", v)
            put(w + o + 0x6C, "<i", v + rng.randrange(-8, 8))
    k = rng.randrange(1, 9)
    put(0x6002D4, "<I", SKID)
    put(0x6002D0, "<I", SKID + k * 0x124)
    put(0x6002D8, "<I", SKID + rng.randrange(k) * 0x124)
    me = struct.unpack_from("<H", st.scratch, CARS[0] + 0x64E - SCRATCH)[0]
    script = {
        "game_time": [rng.getrandbits(32) for _ in range(2)],
        "surface_kind": [pick(rng, 0, 0, 1, 2) for _ in range(4)],
        "light_spawn_queued": [pick(rng, 0, rng.randrange(1, max(nl, 2))) for _ in range(8)],
        "snd_box_merge": [pick(rng, 0, 1) for _ in range(2)],
        "local_car_index": [pick(rng, me, me, me, 0, 1, rng.getrandbits(32)) for _ in range(6)],
        "net_state_game": [pick(rng, 0, 0, 1, 2) for _ in range(6)],
        "net_state_host": [pick(rng, 0, 1, 1) for _ in range(6)],
        "net_player_active": [pick(rng, 0, 1, 1)],
        "net_pause_state": [pick(rng, 0, 1, 2, 3)],
        "cd_present": [pick(rng, 0, 1, 1)],
        "controls_fetch": [rng.randbytes(8) if rng.randrange(3) == 0 else bytes(4) + bytes([pick(rng, 1, 2, 4, 8, 0x10, 0x20, 0x40)]) + bytes(3)],
    }
    return st, script


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
        name, nargs, masks = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        args = list(words[1:1 + nargs])
        for i, mask in (masks or {}).items():
            args[i] &= mask
        eax = 0
        if name == "controls_fetch":
            uc.mem_write(args[1], self._next(name, bytes(8)))
            args[1] = "raw"
        elif name == "glint_spawn":
            eax = GLINT
        else:
            eax = self._next(name, 0)
        for i, a in enumerate(args):
            if isinstance(a, int) and E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args = []
    for k in kinds:
        if k == "C":
            args.append(rng.choice(CARS))
        elif k == "O":
            args.append(rng.choice(OBJS + CARS[:1]))
        elif k == "P":
            args.append(pick(rng, 0, rng.choice(CARS)))
        elif k == "L":
            i = rng.randrange(1, NLIGHTS)
            args.append(0x5D8000 + i * 0x40)
        elif k == "i":
            args.append(rng.randrange(1, NLIGHTS))
        elif k == "m":
            args.append(pick(rng, 2, 3, 2, rng.getrandbits(32)))
        elif k == "f":
            args.append(pick(rng, 0, 1, 0, rng.getrandbits(32)))
        elif k == "n":
            args.append(pick(rng, 2, 2, 0, 1, 99, 100, rng.getrandbits(32)))
        elif k == "k" or k == "K":
            args.append(pick(rng, rng.randrange(1, 6), rng.randrange(1, 6), rng.getrandbits(32)))
        elif k == "c":
            args.append(rng.getrandbits(20) << 12 | pick(rng, 0x2000, 0x3000, 0x4000, 0) | rng.randrange(48))
        elif k == "s":
            args.append(pick(rng, 0, 1, 3, rng.randrange(-3, 8), -0x80000000))
        elif k == "d":
            args.append(pick(rng, 1, 0xFF, 0, 2, rng.getrandbits(8), rng.getrandbits(32)))
        elif k == "V":
            args.append(VEC)
        elif k == "B":
            args.append(VEC + 0x40)
        elif k == "W":
            args.append(WHEEL)
        elif k == "S":
            args.append(pick(rng, 0xFFFFFFFF, SKID + rng.randrange(8) * 0x124))
        elif k == "p":
            args.append(pick(rng, 0x1B0000, 0x1B0001, 0xE0000, 0xE0001, rng.randrange(0, 0x400000), rng.getrandbits(32)))
        elif k == "u":
            args.append(pick(rng, 0, 1, 2, 3, -1, rng.getrandbits(32)))
        elif k == "R":
            args.append(CARREF)
        else:
            args.append(pick(rng, 0, 1, rng.randrange(0, 0x4000), rng.getrandbits(16), rng.getrandbits(32)))
    return args


def fill_vec(st, rng):
    for o in range(0, 0x40, 4):
        st.put(VEC + o, "<i", pick(rng, rng.randrange(-0x2000, 0x2000), rng.randrange(-0x100000, 0x100000), big(rng)))
    for o in range(0, 8, 2):
        st.put(VEC + 0x40 + o, "<h", rng.getrandbits(16))


def run_case(m, stubs, name, address, kinds, mask, rng):
    st, script = make_state(rng)
    fill_vec(st, rng)
    args = make_args(rng, kinds)
    results = []
    for target in (address, m.symbols[name]):
        m.write(E.STACK, bytes(E.STACK_SIZE))
        m.write(SCRATCH, bytes(st.scratch))
        for a, data in st.mem.items():
            m.write(a, bytes(data))
        stubs.reset(script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            if first < SCRATCH_SIZE:
                where = f"scratch +{first:#x}"
            else:
                off = first - SCRATCH_SIZE
                for ra, rn in REGIONS:
                    if off < rn:
                        where = f"{ra + off:#x}"
                        break
                    off -= rn
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at {where})") + logs
    if results[0][0][0] != "ok":
        return "fault"
    return None


class Coverage:
    """Instructions of the original function executed (straight-line spans)."""

    def __init__(self, m, start, end):
        self.seen = set()
        self.hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: self.seen.add(a), begin=start, end=end - 1)


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    rng = random.Random(23)
    failed = 0
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    sizes = {}
    if "--cov" in sys.argv:
        for line in open("build/functions.txt"):
            p = line.split()
            if len(p) >= 2:
                sizes[int(p[0], 16)] = int(p[1])
    for name, (address, kinds, mask) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        cov = Coverage(m, address, address + sizes[address]) if sizes else None
        bad = faults = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, mask, rng)
            if msg == "fault":
                faults += 1
            elif msg:
                if bad < 3:
                    print(msg)
                bad += 1
        extra = f" ({faults} both faulted)" if faults else ""
        if cov:
            m.uc.hook_del(cov.hook)
            all_ins = instructions(address, sizes[address])
            total = len(all_ins)
            if "--cov-detail" in sys.argv:
                print("  not run:", " ".join(hex(a) for a in sorted(all_ins - cov.seen)))
            extra += f" coverage {len(cov.seen)}/{total}"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{extra}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


def instructions(address, size):
    found = set()
    for line in open("build/rollcage.asm"):
        p = line.split(":", 1)
        try:
            a = int(p[0], 16)
        except ValueError:
            continue
        if address <= a < address + size:
            found.add(a)
    return found


if __name__ == "__main__":
    main()
