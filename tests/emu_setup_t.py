#!/usr/bin/env python3
"""Differential test of setup_t.c (race setup group T: the car setup
0x40e670 and the pools set up beside it) against the original, on a random
synthetic track, race and heap.

Functions outside the group (allocation, object creation and placement,
packet set-up, the message file reader, other groups' callees) are stubbed
in both runs: each stub logs its arguments (stack buffers by content) and
returns a scripted value, and the call logs are compared as well as memory.
The stack is filled with the same leftover bytes in both runs.

    tests/emu_setup_t.py EXE build/emu_t.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x162000
RACE = SCRATCH  # replay records reach +0xb8a38
RACE_SIZE = 0xBA000
CONFIG = SCRATCH + 0xBA000
TRACK = SCRATCH + 0xBA400
MODEL_PTRS = SCRATCH + 0xBA500  # 16 pointers
MODELS = SCRATCH + 0xBA600  # 16 models of 0x200
MODEL_SIZE = 0x200
SLOT_TABLE = SCRATCH + 0xBC600  # 0x80 int16 model indices
STARTS = SCRATCH + 0xBC700  # 32 start positions of 8
LINES = SCRATCH + 0xBC800  # 16 lines of 0xc
NODES = SCRATCH + 0xBC900  # 16 lines x 16 nodes x 0x10
CHECKS = SCRATCH + 0xBD900  # 16 checkpoints of 0x14
ORDER = SCRATCH + 0xBDB00  # 64 dwords
INFO = SCRATCH + 0xBDC00
TEXTURES = SCRATCH + 0xBDD00  # 0x110 entries of 0x10
PALETTE = SCRATCH + 0xBEE00  # 0x400 entries of 4
RADII = SCRATCH + 0xBFE00  # 256 x 0x48
SLOTS = SCRATCH + 0xC4600  # 2 slots of 0x274
POINTS = SCRATCH + 0xC4C00  # 4 points of 0x1c
HEAP = SCRATCH + 0xC5000  # bump allocations
HEAP_END = SCRATCH + SCRATCH_SIZE

POINTERS = {0x4B1960: RACE, 0x4B2198: CONFIG, 0x6C1E30: TRACK, 0x6C1EB0: INFO, 0x6C1E14: TEXTURES,
            0x6C1E20: PALETTE, 0x6CE740: RADII}
# game globals the group writes (compared), with random starting contents
GLOBALS = [(0x6D04A0, 8), (0x4D3DD8, 8), (0x600640, 0x24), (0x600664, 4), (0x600680, 0x90), (0x4D3DE4, 0x20),
           (0x6002D0, 0x10), (0x6002E0, 0x84 * 6), (0x4B1C68, 4), (0x4D44B8, 8), (0x6CE6FC, 0xC), (0x6CE738, 4),
           (0x6CCB54, 4), (0x6CC800, 4), (0x6CC960, 0x204), (0x4BE1A0, 0xC), (0x4BE158, 0x28), (0x4BE374, 0xC),
           (0x6C80F0, 0x80), (0x4BEB88, 4), (0x5F7220, 0x90), (0x5D9454, 0x10), (0x5D9480, 4), (0x5D9620, 4),
           (0x5D97C0, 4), (0x5D9960, 4), (0x5D92A0, 8), (0x4B2430, 4), (0x5D9288, 0xC), (0x4D1098, 0x100),
           (0x4A7A00, 0xC), (0x4B1948, 4), (0x6B8CD8, 8), (0x6CCCDC, 8), (0x4BE3F0, 4), (0x4ACC38, 4),
           (0x6C7DF0, 4), (0x6CCD84, 4)]
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))
ZERO = bytes(SCRATCH_SIZE)

# address: (name, number of args, returns)
STUBS = {
    0x4445E0: ("level_alloc", 1, True),
    0x460BA0: ("object_new", 4, True),
    0x460AF0: ("pieces_alloc", 1, True),
    0x465640: ("object_place", 2, False),
    0x4611D0: ("grid_unlink", 1, False),
    0x419150: ("body_spheres", 4, False),
    0x40EE60: ("quat_facing", 2, False),
    0x40DA70: ("car_model_set", 3, False),
    0x439910: ("slot_digits", 2, False),
    0x4270E0: ("packet_init", 2, False),
    0x427A40: ("marks_packet_init", 2, False),
    0x46E450: ("trail_init", 2, False),
    0x47BC80: ("glow_packets_init", 1, False),
    0x41C190: ("replay_check", 2, False),
    0x42E6D0: ("message_read", 4, True),
    0x460B20: ("piece_init", 2, False),
}

# name: (address, argument kinds, returns)
TESTS = {
    "crumble_queue_clear": (0x405EB0, "", False),
    "car_twin_wheels": (0x40DE00, "SM", False),
    "car_stats_set": (0x40EAE0, "CikB", False),
    "specials_reset": (0x41AA40, "", False),
    "replay_start": (0x41B8F0, "Cm", False),
    "car_trails_init": (0x421DD0, "C", False),
    "car_marks_init": (0x421E30, "C", False),
    "fix_sqrt_b": (0x4279D0, "q", True),
    "packet_tpage_set": (0x427AD0, "Pwww", True),
    "packet_blend_bits": (0x427B00, "w", True),
    "smoke_clouds_init": (0x42A8F0, "", False),
    "flash_packet_init": (0x42A9A0, "P", False),
    "flashes_init": (0x42A9E0, "w", False),
    "palette_ramp_init": (0x42C1D0, "", False),
    "flares_init": (0x42C200, "", False),
    "shock_waves_init": (0x42C2B0, "", False),
    "markers_init": (0x434380, "", False),
    "slot_find_line": (0x47AD30, "S", False),
    "slot_race_reset": (0x43D280, "S", False),
    "lights_reset": (0x441D70, "", False),
    "config_player_on": (0x449CD0, "j", True),
    "messages_load": (0x465270, "", False),
    "lineup_setup": (0x4698F0, "LwTU", False),
    "bomblets_setup": (0x46BA00, "", False),
    "car_shadow_init": (0x46C220, "H", False),
    "car_texture_anim_attach": (0x46E110, "S", False),
    "car_trail_init": (0x46E3E0, "C", False),
    "skid_marks_init": (0x46EAB0, "", False),
    "body_points_init": (0x475D50, "CwXr", False),
    "car_targets_reset": (0x478340, "", False),
    "slot_target_reset": (0x478350, "S", False),
    "car_target_add": (0x478400, "Cw", False),
    "text_lists_reset": (0x479AF0, "", False),
    "car_section_reset": (0x47AD00, "S", False),
    "glow_model_offset": (0x47B1E0, "Ok", False),
    "glow_first_low_face": (0x47B240, "G", True),
    "glow_pick_vertices": (0x47B280, "G", False),
    "glow_reset": (0x47BC60, "G", False),
    "car_glow_init": (0x47B160, "GMk", False),
    "debris_init": (0x47BD70, "", False),
    "car_setup": (0x40E670, "Si", False),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return bytearray(rng.getrandbits(8 * n).to_bytes(n, "little"))


def make_model(rng, small):
    """A model: +0x40 vertices (8 bytes, int16 x y z), +0x100 faces (8 bytes:
    four corner bytes, 1-based, then a dword whose low 10 bits are a signed
    value), +0x1f0 info (+6 face count); +0x1fc vertex count."""
    m = rbytes(rng, MODEL_SIZE)
    n = rng.randrange(0, 25)
    top = rng.randrange(-300, 300)
    for i in range(n):
        if small:
            # the original keeps its picks on the stack: at most 8 below the
            # top and 2 at the bottom fit
            z = top if i >= 6 else top - 20 if i < 2 else top - rng.randrange(1, 11)
        else:
            z = rng.randrange(-0x8000, 0x8000)
        struct.pack_into("<hhhh", m, 0x40 + i * 8, rng.randrange(-0x8000, 0x8000), rng.randrange(-0x8000, 0x8000),
                         z, rng.getrandbits(16) - 0x8000)
    faces = rng.randrange(0, 25)
    for j in range(faces):
        corners = [pick(rng, 0, rng.randrange(1, max(2, n + 2))) for _ in range(4)]
        struct.pack_into("<4BI", m, 0x100 + j * 8, *corners, pick(rng, rng.getrandbits(32),
                                                                   rng.randrange(0, 0x400), 0x3FF & -251))
    struct.pack_into("<H", m, 0x1F6, faces)
    m[0x1FC] = n
    return m, n


def make_state(rng, small=None):
    if small is None:
        small = rng.randrange(3) != 0
    st = {}
    race = bytearray(RACE_SIZE)
    race[0:0x1400] = rbytes(rng, 0x1400)
    for i in range(6):
        race[i] = pick(rng, 8, 10, 8, 10, rng.getrandbits(8))
        race[0x10 + i] = rng.randrange(0, 32)
        race[0x1C + i] = rng.randrange(0, 7)
    for o in (0x54E, 0x551, 0x587, 0x58C):
        race[o] = pick(rng, 0, 0, 1, 2, 4, rng.getrandbits(8))
    race[0x551] = pick(rng, 4, 4, race[0x551])  # line-up mode
    for i in range(6):
        struct.pack_into("<I", race, 0x1FB20 + i * 0x1E884, pick(rng, 0, 0, 0, 8, 5, rng.getrandbits(32)))
    st["race"] = race
    st["config"] = rbytes(rng, 0x400)
    st["config"][0x2C] = pick(rng, 2, 0, 1, rng.getrandbits(8))
    for i in range(4):
        st["config"][i * 0x90 + 0x1A2] = pick(rng, 0, 1, rng.getrandbits(8))
    models = []
    for _ in range(16):
        models.append(make_model(rng, small))
    st["models"] = models
    slot_table = b"".join(struct.pack("<h", rng.randrange(0, 16)) for _ in range(0x80))
    st["slot_table"] = slot_table
    lines = bytearray(16 * 0xC)
    nodes = bytearray(16 * 16 * 0x10)
    for i in range(16):
        n = pick(rng, 0, rng.randrange(0, 17), rng.randrange(0, 17))
        struct.pack_into("<HHII", lines, i * 0xC, n, rng.getrandbits(16), rng.getrandbits(32), NODES + i * 0x100)
        for k in range(16):
            struct.pack_into("<hhh", nodes, i * 0x100 + k * 0x10, *[pick(rng, rng.randrange(-0x400, 0x400),
                                                                            rng.getrandbits(16) - 0x8000)
                                                                       for _ in range(3)])
    st["lines"], st["nodes"] = lines, nodes
    checks = rbytes(rng, 16 * 0x14)
    order = b"".join(struct.pack("<I", pick(rng, rng.randrange(0, 16), rng.getrandbits(32) & 0xFFFFFF0F))
                     for _ in range(64))
    st["checks"], st["order"] = checks, order
    starts = b"".join(struct.pack("<hhhh", *[rng.getrandbits(16) - 0x8000 for _ in range(4)]) for _ in range(32))
    st["starts"] = starts
    track = rbytes(rng, 0x100)
    struct.pack_into("<I", track, 0x18, MODEL_PTRS)
    struct.pack_into("<I", track, 0x58, SLOT_TABLE)
    struct.pack_into("<I", track, 0x48, STARTS)
    struct.pack_into("<I", track, 0x60, LINES)
    struct.pack_into("<I", track, 0x70, CHECKS)
    struct.pack_into("<I", track, 0x90, ORDER)
    track[0x44] = rng.randrange(1, 64)
    track[0x46] = pick(rng, 0, 1, rng.randrange(2, 17))
    st["track"] = track
    info = rbytes(rng, 0x40)
    struct.pack_into("<H", info, 0x2E, rng.randrange(0, 0x60))
    st["info"] = info
    st["textures"] = rbytes(rng, 0x1100)
    st["palette"] = rbytes(rng, 0x1000)
    st["radii"] = rbytes(rng, 0x4800)
    slots = rbytes(rng, 0x274 * 2)
    st["slots"] = slots
    pts = bytearray(rbytes(rng, 0x70))
    for i in range(4):
        for k in (0, 1, 2, 4, 5, 6):
            struct.pack_into("<h", pts, i * 0x1C + k * 2, pick(rng, rng.randrange(-0x2000, 0x2000),
                                                                rng.getrandbits(16) - 0x8000))
    st["points"] = pts
    heap = rbytes(rng, HEAP_END - HEAP)
    for i in range(2):  # the slots' cars and twins have real indices
        for base in (0x40000, 0x48000):
            struct.pack_into("<H", heap, base + i * 0x2500 + 0x64E, pick(rng, 0, rng.randrange(0, 6),
                                                                         rng.randrange(6, 8)))
    st["heap"] = heap
    st["globals"] = {a: rbytes(rng, n) for a, n in GLOBALS}
    g = st["globals"]
    g[0x6D04A0][:3] = rbytes(rng, 3)
    g[0x4D3DE4][:4] = rbytes(rng, 4)
    struct.pack_into("<i", g[0x4D3DE4], 0, 0)  # not compared on its own: the count is 0x4d3dfc
    g[0x5D92A0][:] = bytes(pick(rng, 0, 0, 1) for _ in range(8))
    st["shadow_count"] = rng.randrange(0, 8)
    struct.pack_into("<i", g[0x5F7220], 0x80, rng.randrange(0, 16))
    struct.pack_into("<i", g[0x4ACC38], 0, pick(rng, 480, 600, 768, rng.randrange(-0x800000, 0x800000)))
    struct.pack_into("<I", g[0x4BE3F0], 0, pick(rng, 0, 1, 1, rng.getrandbits(32)))
    st["messages"] = [(pick(rng, 0, 0, 0, -1, 5), bytes(rng.randrange(0x20, 0x7F) for _ in range(
        pick(rng, 0, rng.randrange(0, 40), rng.randrange(0, 0xF0)))) + b"\0") for _ in range(43)]
    st["quat"] = [rng.getrandbits(32) for _ in range(4)]
    return st


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.brk = HEAP
        self.messages = []
        self.quat = []
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, st):
        self.log = []
        self.brk = HEAP
        self.messages = list(st["messages"])
        self.quat = st["quat"]

    def _alloc(self, size):
        p = self.brk
        self.brk = (self.brk + max(size, 4) + 0xF) & ~0xF
        if self.brk > HEAP_END - 0x30000:  # keep room for writes past the end
            self.brk = HEAP
        return p

    def _hook(self, uc, address, size, _):
        name, nargs, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        eax = 0
        if name in ("level_alloc", "pieces_alloc"):
            eax = self._alloc(args[0])
        elif name == "object_new":
            eax = self._alloc(max(args[2] & 0xFFFF, 0x300))
            model = args[0]
            if MODELS <= model < MODELS + 16 * MODEL_SIZE:  # an object of the model
                uc.mem_write(eax + 0x10, struct.pack("<I", model + 0x1F0))
                uc.mem_write(eax + 0x24, struct.pack("<II", model + 0x40, model + 0x100))
                uc.mem_write(eax + 0x10D, bytes(uc.mem_read(model + 0x1FC, 1)))
        elif name == "object_place":
            uc.mem_write(args[0] + 0x34, bytes(uc.mem_read(args[1], 12)))
        elif name == "quat_facing":
            d = bytes(uc.mem_read(args[1], 12))
            args[1] = ("dir", d)
            uc.mem_write(args[0], struct.pack("<4I", *[(q ^ x) & 0xFFFFFFFF for q, x in
                                                      zip(self.quat, struct.unpack("<3I", d) + (0,))]))
            args[0] = "stack"
        elif name == "message_read":
            result, text = self.messages.pop(0) if self.messages else (0, b"\0")
            uc.mem_write(args[0], text)
            args[0] = "stack"
            eax = result
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if isinstance(a, int) and E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, 12)))
        self.log.append((name, tuple(args)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args = []
    for k in kinds:
        if k == "S":
            args.append(SLOTS + pick(rng, 0, 0x274))
        elif k == "C":  # a car (heap object of 0x2474 bytes)
            args.append(HEAP + 0x40000 + rng.randrange(0, 0x40) * 4)
        elif k == "L":  # the first slot's car (a real index)
            args.append(HEAP + 0x40000)
        elif k == "H":  # a car's shadow
            args.append(HEAP + 0x40000 + 0x720)
        elif k == "G":  # a glow record with an object of a model
            args.append(HEAP + 0x50000)
        elif k == "O":  # an object of a model (as object_new makes)
            args.append(HEAP + 0x58000)
        elif k == "M":
            args.append(MODELS + rng.randrange(16) * MODEL_SIZE)
        elif k == "P":
            args.append(HEAP + 0x60000 + rng.randrange(0, 0x100))
        elif k == "i":
            args.append(rng.randrange(0, 6))
        elif k == "j":
            args.append(pick(rng, rng.randrange(-2, 6), rng.getrandbits(32)))
        elif k == "k":
            args.append(rng.randrange(0, 7))
        elif k == "B":
            args.append(pick(rng, 0, 1, rng.getrandbits(32)))
        elif k == "m":
            args.append(pick(rng, 4, 1, 4, rng.getrandbits(32)))
        elif k == "q":
            args.append(pick(rng, 0, rng.randrange(0, 0x100), rng.randrange(0, 0x10000), rng.getrandbits(32)))
        elif k == "T":
            args.append(0x4A7B68)
        elif k == "U":
            args.append(0x4A7B78)
        elif k == "X":
            args.append(pick(rng, POINTS, 0x4A7990))
        elif k == "r":
            args.append(pick(rng, 0x2C00, rng.randrange(-0x10000, 0x10000), rng.getrandbits(32)))
        else:
            args.append(rng.getrandbits(32))
    return args


def prepare_objects(m, st, rng_seed):
    """Glow and object arguments point at objects of a random model."""
    rng = random.Random(rng_seed)
    model = MODELS + rng.randrange(16) * MODEL_SIZE
    for obj in (HEAP + 0x50000 + 0x400, HEAP + 0x58000):
        m.write(obj + 0x10, struct.pack("<I", model + 0x1F0))
        m.write(obj + 0x24, struct.pack("<II", model + 0x40, model + 0x100))
        m.write(obj + 0x10D, bytes(m.read(model + 0x1FC, 1)))
    m.write(HEAP + 0x50000 + 4, struct.pack("<I", HEAP + 0x50000 + 0x400))


def load(m, st):
    m.write(SCRATCH, ZERO)
    m.write(RACE, bytes(st["race"]))
    m.write(CONFIG, bytes(st["config"]))
    m.write(TRACK, bytes(st["track"]))
    m.write(MODEL_PTRS, b"".join(struct.pack("<I", MODELS + i * MODEL_SIZE) for i in range(16)))
    for i, (model, _) in enumerate(st["models"]):
        m.write(MODELS + i * MODEL_SIZE, bytes(model))
    m.write(SLOT_TABLE, st["slot_table"])
    m.write(STARTS, st["starts"])
    m.write(LINES, bytes(st["lines"]))
    m.write(NODES, bytes(st["nodes"]))
    m.write(CHECKS, bytes(st["checks"]))
    m.write(ORDER, st["order"])
    m.write(INFO, bytes(st["info"]))
    m.write(TEXTURES, bytes(st["textures"]))
    m.write(PALETTE, bytes(st["palette"]))
    m.write(RADII, bytes(st["radii"]))
    slots = bytearray(st["slots"])
    for i in range(2):  # each slot's car
        struct.pack_into("<I", slots, i * 0x274, HEAP + 0x40000 + i * 0x2500)
        struct.pack_into("<I", slots, i * 0x274 + 0x260, HEAP + 0x48000 + i * 0x2500)
    m.write(SLOTS, bytes(slots))
    m.write(POINTS, bytes(st["points"]))
    m.write(HEAP, bytes(st["heap"]))
    for a, v in st["globals"].items():
        m.write(a, bytes(v))
    m.write(0x4D3DFC, struct.pack("<i", st["shadow_count"]))
    for a, p in POINTERS.items():
        m.write(a, struct.pack("<I", p))
    m.write(E.STACK, STACK_FILL)


COMPARED = GLOBALS + [(0x4D3DFC, 4)]


WELL_FORMED = {"car_setup", "lineup_setup"}  # whole-car tests: glow models the game can use


def run_case(m, stubs, name, address, kinds, returns, rng):
    st = make_state(rng, True if name in WELL_FORMED else None)
    args = make_args(rng, kinds)
    seed = rng.getrandbits(32)
    results = []
    for target in (address, m.symbols[name]):
        load(m, st)
        prepare_objects(m, st, seed)
        stubs.reset(st)
        # a faulted run can leave the direction flag set (it ran into
        # garbage); Windows has it clear at every call, and rep movs would
        # otherwise copy backwards
        m.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in COMPARED)
        results.append((r, mem, stubs.log))
    if results[0][0][0] != "ok":
        return "skip"  # the original faults (its stack lists overrun, or wild pointers)
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
    for name, (address, kinds, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = skipped = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, rng)
            if msg == "skip":
                skipped += 1
            elif msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({skipped} skipped: the original faulted)" if skipped else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
