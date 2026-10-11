#!/usr/bin/env python3
"""Differential test of setup_w.c (race setup pieces: the shield ripple,
the views, track water, the chase camera records, the weather tint, pads,
tick_sync and the start messages) against the original, on random track,
mesh, palette and race state.

Functions outside the group (the heap allocator, texture locks, the x87
normalise, viewport / D3D state, bodies, text, the empty waits) are stubbed
in both runs: each stub logs its arguments (what stack pointers point at,
not where) and returns a scripted value, and the call logs are compared as
well as memory.

    tests/emu_setup_w.py EXE build/emu_w.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
RACE = SCRATCH
CONFIG = SCRATCH + 0x1000
TRACK = SCRATCH + 0x1400
INFO = SCRATCH + 0x1500
SECTION_LIST = SCRATCH + 0x1700  # 16 pointers
SECTIONS = SCRATCH + 0x1800  # 16 section models of 0x40
GRIDS = SCRATCH + 0x2000  # 16 vertex grids of 0x400
MESH = SCRATCH + 0x6000
MESH_VERTS = SCRATCH + 0x6100
MESH_POLYS = SCRATCH + 0x6500
MODELS = SCRATCH + 0x6900
TRACK58 = SCRATCH + 0x6A00
CELLS = SCRATCH + 0x6A80
COLOURS = SCRATCH + 0x6B00
PALETTE = SCRATCH + 0x8000
TRACK_PALETTE = SCRATCH + 0x9000
SKY = SCRATCH + 0xA000  # 64 * 0x78
LIGHTS1 = SCRATCH + 0xC000
LIGHTS2 = SCRATCH + 0xC800
EFFECTS = SCRATCH + 0xD000  # 32 * 0x60
SPOTS = SCRATCH + 0xD900
CAMERA = SCRATCH + 0xDA00
PLAIN = SCRATCH + 0xDB00  # argument buffers
UVS = SCRATCH + 0xE000  # 6 records' polygon lists, 2 * 0xC00 each
ALLOC = SCRATCH + 0x20000  # heap_alloc hands out from here
BODIES = SCRATCH + 0x3C000
POINTERS = {0x4B1960: RACE, 0x4B2198: CONFIG, 0x6C1E30: TRACK, 0x6C1EB0: INFO, 0x6BFAC0: SECTION_LIST,
            0x6BFAF0: PALETTE, 0x6C1E14: COLOURS, 0x6BFB08: SPOTS, 0x4B5FB0: SKY, 0x4B5FC8: LIGHTS1,
            0x4B5FCC: LIGHTS2, 0x4B5FDC: EFFECTS}
COMPARED = [(0x4A76F0, 0x20), (0x4ACC38, 8), (0x4B11D8, 8), (0x4B24A0, 8), (0x4B5FB0, 0x30), (0x4BDE00, 0x100),
            (0x4BE118, 4), (0x4BE3E8, 4), (0x4D1130, 0x14), (0x4D44A0, 0x10), (0x5D6378, 8), (0x5D92A0, 8),
            (0x6002E0, 0x320), (0x6C1E10, 8), (0x6C1EB0, 0x10), (0x6CD400, 0x1240), (0x6CE680, 0x80),
            (0x6CF6A0, 0x540), (0x6CCE94, 4)]
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))

# address: (name, number of args, masks for args, returns, bytes logged behind stack pointers)
STUBS = {
    0x4445E0: ("heap_alloc", 1, None, True, 0),
    0x444500: ("heap_init", 0, None, False, 0),
    0x46D0C0: ("shield_texture_draw", 0, None, False, 0),
    0x429340: ("textures_unusable", 0, None, True, 0),
    0x4290B0: ("normalise", 2, None, False, 12),
    0x4291F0: ("wait_frame", 0, None, False, 0),
    0x479AE0: ("screen_idle", 0, None, True, 0),
    0x428060: ("screen_ready", 0, None, True, 0),
    0x423F00: ("view_reset", 1, None, False, 0),
    0x41F2D0: ("viewport_set", 4, None, False, 0),
    0x423CE0: ("view_set_position", 1, None, False, 16),
    0x423D10: ("view_set_matrix", 1, None, False, 18),
    0x423D30: ("view_set_lights", 0, None, False, 0),
    0x427A70: ("list_clear", 2, None, False, 0),
    0x428390: ("clear_colour_set", 3, {0: 0xFF, 1: 0xFF, 2: 0xFF}, False, 0),
    0x460AF0: ("body_new", 1, None, True, 0),
    0x460B40: ("body_set_kind", 2, None, False, 0),
    0x465640: ("body_place", 2, None, False, 0),
    0x419150: ("body_spheres_set", 4, None, False, 0),
    0x40AFB0: ("camera_fade", 1, None, False, 0),
    0x479060: ("text_create", 7, None, False, 0),
}

# name: (address, argument kinds, returns)
TESTS = {
    "shield_uv_corners": (0x46D270, "PPPPP", False),
    "shield_link_add": (0x46D590, "bbb", False),
    "shield_mesh_links": (0x46D320, "", False),
    "shield_record_init": (0x46D600, "R", False),
    "shield_uvs": (0x46E160, "", False),
    "shield_textures_draw": (0x46D250, "", True),
    "shield_setup": (0x46D2B0, "", False),
    "rect_set": (0x4291C0, "Pwwww", True),
    "viewport_apply": (0x4265D0, "P", True),
    "view_keep": (0x426600, "P", True),
    "view_angles_reset": (0x423A90, "", False),
    "views_setup": (0x4238C0, "", True),
    "views_init": (0x423880, "", False),
    "screen_waits": (0x43C300, "", False),
    "heap_and_views_init": (0x46A040, "", False),
    "ripple_vertices_setup": (0x43E050, "", False),
    "water_wave": (0x482AE0, "www", True),
    "water_meshes_setup": (0x482B20, "", False),
    "track_water_setup": (0x41AA60, "", False),
    "camera_view_setup": (0x406BE0, "icC", False),
    "rgb_scale": (0x404940, "Pwww", True),
    "tint_colour_tables": (0x4047E0, "www", False),
    "weather_tint_setup": (0x485070, "", False),
    "pad_state_clear": (0x41B8E0, "P", False),
    "tick_sync": (0x429220, "t", True),
    "pads_reset": (0x41B370, "", True),
    "race_start_messages": (0x463860, "m", False),
    "player_car_choice": (0x449CA0, "p", True),
    "glow_flash_reset": (0x47B140, "", False),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rbytes(rng, n):
    return bytearray(rng.getrandbits(8) for _ in range(n))


def make_state(rng):
    st = {}
    w = {}  # address: bytes, written in both runs

    # the shield mesh
    nverts = pick(rng, rng.randrange(2, 41), rng.randrange(2, 8), rng.randrange(2, 41), rng.randrange(1, 41))
    npolys = pick(rng, rng.randrange(1, 31), rng.randrange(1, 5), 0)
    mesh = rbytes(rng, 0x80)
    struct.pack_into("<HH", mesh, 4, nverts, npolys)
    struct.pack_into("<I", mesh, 0xC, MESH_POLYS)
    struct.pack_into("<I", mesh, 0x40, MESH_VERTS)
    verts = rbytes(rng, 0x400)
    span = pick(rng, 0x400, 0x40, 0x7FFF, 4)
    for i in range(nverts):
        struct.pack_into("<hhh", verts, i * 8, *[rng.randrange(-span, span + 1) for _ in range(3)])
    if nverts >= 2 and rng.randrange(16):  # nonzero x and z extents, different minima
        struct.pack_into("<h", verts, 0, -span - 1)
        struct.pack_into("<h", verts, 12, min(span + 2, 0x7FFF))
    polys = rbytes(rng, 0x400)
    for i in range(npolys):
        idx = [pick(rng, rng.randrange(1, nverts + 1), rng.randrange(1, nverts + 1), rng.randrange(0, 0x30))
               for _ in range(3)]
        if rng.randrange(6):
            idx = [min(max(x, 1), nverts) for x in idx]
        polys[i * 8:i * 8 + 3] = bytes(idx)
    w[MESH] = mesh
    w[MESH_VERTS] = verts
    w[MESH_POLYS] = polys
    models = b"".join(struct.pack("<I", MESH) for _ in range(4))
    w[MODELS] = models
    t58 = rbytes(rng, 0x40)
    struct.pack_into("<h", t58, 0xE, rng.randrange(0, 4))
    w[TRACK58] = t58

    # track sections and their vertex grids
    nsec = pick(rng, rng.randrange(0, 9), rng.randrange(1, 4), 0)
    secs = bytearray()
    for i in range(16):
        s = rbytes(rng, 0x40)
        width = rng.randrange(1, 7)
        rows = rng.randrange(1, 7)
        band = pick(rng, 0, 0, rng.randrange(1, width + 1), rng.randrange(0, width + 2))
        flags = band | pick(rng, 0, 0x40, 0x80, 0xC0)
        struct.pack_into("<H", s, 6, flags | (rng.getrandbits(8) << 8))
        struct.pack_into("<I", s, 8, width)
        struct.pack_into("<I", s, 0xC, rows)
        struct.pack_into("<I", s, 0x20, GRIDS + i * 0x400 + pick(rng, 0, 0x40))
        secs += s
    w[SECTIONS] = secs
    w[SECTION_LIST] = b"".join(struct.pack("<I", SECTIONS + i * 0x40) for i in range(16))
    grids = bytearray()
    for i in range(16 * 0x400 // 8):
        grids += struct.pack("<hhhH", rng.randrange(-0x1000, 0x1000), rng.randrange(-0x1000, 0x1000),
                             rng.randrange(-0x1000, 0x1000), rng.getrandbits(16))
    w[GRIDS] = grids
    track = rbytes(rng, 0x100)
    struct.pack_into("<I", track, 0, SECTION_LIST)
    struct.pack_into("<I", track, 0x10, nsec)
    struct.pack_into("<I", track, 0x18, MODELS)
    struct.pack_into("<I", track, 0x58, TRACK58)
    w[TRACK] = track

    # track info: ripple cells, colours, fog, palette
    info = rbytes(rng, 0x200)
    ncells = pick(rng, 0, rng.randrange(1, 7), rng.randrange(1, 3))
    info[0xFC] = ncells
    struct.pack_into("<I", info, 0x100, CELLS)
    struct.pack_into("<H", info, 0x2E, rng.randrange(0, 5))
    struct.pack_into("<I", info, 0x4C, TRACK_PALETTE)
    cells = bytearray()
    for i in range(8):
        sec = rng.randrange(0, 16)
        width = struct.unpack_from("<I", secs, sec * 0x40 + 8)[0]
        rows = struct.unpack_from("<H", secs, sec * 0x40 + 0xC)[0]
        cells += struct.pack("<HBB", rng.randrange(0, rows), sec, rng.randrange(0, width))
    if rng.randrange(4) == 0:  # shared corners
        cells[4:8] = cells[0:4]
    w[CELLS] = cells
    w[INFO] = info
    w[COLOURS] = rbytes(rng, 0x1000)
    w[PALETTE] = rbytes(rng, 0x1000)
    w[TRACK_PALETTE] = rbytes(rng, 0x1000)
    w[SKY] = rbytes(rng, 0x2000)
    w[LIGHTS1] = rbytes(rng, 0x800)
    w[LIGHTS2] = rbytes(rng, 0x800)
    w[EFFECTS] = rbytes(rng, 0xC00)
    w[SPOTS] = b"".join(struct.pack("<hhhh", *[rng.randrange(-0x8000, 0x8000) for _ in range(4)])
                        for _ in range(8))
    w[CAMERA] = rbytes(rng, 0x100)
    w[PLAIN] = rbytes(rng, 0x200)
    w[UVS] = rbytes(rng, 0x12000)
    w[ALLOC] = rbytes(rng, 0x1C000)
    w[BODIES] = rbytes(rng, 0x4000)

    race = rbytes(rng, 0xC00)
    race[0x54D] = pick(rng, rng.randrange(0, 7), 0, 6)
    race[0x551] = pick(rng, rng.randrange(0, 10), 5, 8)
    race[0x553] = pick(rng, 0, 1, 2, rng.getrandbits(8))
    for o in (0xA90, 0xA91, 0xA92, 0xA93):
        race[o] = pick(rng, rng.randrange(0, 4), rng.randrange(0, 4), rng.getrandbits(8))
    w[RACE] = race
    config = rbytes(rng, 0x400)
    config[0x2A] = pick(rng, 0, 1, 1, rng.getrandbits(8))
    w[CONFIG] = config

    # globals in the game's data
    g = {
        0x4B24A0: struct.pack("<i", pick(rng, -1, rng.randrange(0, 0x40), rng.getrandbits(16))),
        0x4ACC38: struct.pack("<iI", pick(rng, 640, 320, 1920, rng.getrandbits(32) - 0x80000000),
                              pick(rng, 480, 240, 1080, rng.getrandbits(32))),
        0x4B11DC: struct.pack("<I", rng.getrandbits(31)),
        0x6CCE94: struct.pack("<I", pick(rng, 0, 1, rng.getrandbits(32))),
        0x6D0A78: struct.pack("<I", pick(rng, 0, rng.randrange(1, 9))),
        0x6D0A00: struct.pack("<I", pick(rng, 0, rng.randrange(1, 9))),
        0x4D1134: rbytes(rng, 0x10),
        0x6CD400: rbytes(rng, 0x1240),
        0x4BDE00: rbytes(rng, 0x100),
        0x6CE680: rbytes(rng, 0x80),
        0x6CF6A0: rbytes(rng, 0x540),
        0x4A76F0: rbytes(rng, 0x20),
        0x5D92A0: rbytes(rng, 8),
        0x5D6378: rbytes(rng, 8),
        0x6C1EB4: rbytes(rng, 8),
        0x4D44A0: rbytes(rng, 0x10),
        0x4BE118: struct.pack("<I", rng.getrandbits(32)),
        0x4BE3E8: struct.pack("<I", rng.getrandbits(32)),
    }
    pads = bytearray(g[0x6CE680])
    if rng.randrange(2):
        pads[0x41] = 0x80
    if rng.randrange(2):
        pads[0x01] = 0x80
    g[0x6CE680] = bytes(pads)
    # the shield records' polygon lists for shield_uvs
    shields = rbytes(rng, 0x320)
    for r in range(6):
        struct.pack_into("<II", shields, r * 0x84 + 0x68, UVS + r * 0x3000, UVS + r * 0x3000 + 0xC00)
    g[0x6002E0] = bytes(shields)
    # links (for shield_link_add alone), the mesh, directions, vertex count
    g[0x4D44A0] = struct.pack("<IIIi", ALLOC + 0x18000, MESH, ALLOC + 0x1A000, rng.randrange(0, 41))
    g[0x4B5FB0] = rbytes(rng, 0x30)  # the colour table pointers are set over this; 0x4b5fd0 is a colour
    st["writes"] = w
    st["globals"] = g
    st["script"] = {
        "textures_unusable": [pick(rng, 0, 0, 1)],
        "screen_idle": [0, 0],
        "screen_ready": [1, 1],
    }
    return st


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.alloc = ALLOC
        self.bodies = BODIES
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.alloc = ALLOC
        self.bodies = BODIES

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        name, nargs, masks, returns, behind = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, mask in (masks or {}).items():
            args[i] &= mask
        for i, a in enumerate(args):  # locals: compare what they hold, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = ("stack", bytes(uc.mem_read(a, behind or 12)))
        self.log.append((name, tuple(args)))
        eax = 0
        if name == "heap_alloc":
            eax = self.alloc
            self.alloc += (words[1] + 0xF) & ~0xF
            if self.alloc > ALLOC + 0x18000:
                self.alloc = ALLOC + 0x10000
        elif name == "body_new":
            eax = self.bodies
            self.bodies += 0x200
        elif name == "normalise":
            v = struct.unpack("<3i", bytes(uc.mem_read(words[1], 12)))
            big = max(abs(x) for x in v) or 1
            out = [(x * 0x1000 // big) for x in v]
            uc.mem_write(words[2], struct.pack("<3i", *out))
        elif returns:
            eax = self._next(name, 0)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def make_args(rng, kinds):
    args = []
    for k in kinds:
        if k == "P":
            args.append(PLAIN + rng.randrange(0, 0x100) & ~1)
        elif k == "R":
            args.append(pick(rng, 0x6002E0 + rng.randrange(0, 6) * 0x84, PLAIN))
        elif k == "C":
            args.append(CAMERA)
        elif k == "b":
            args.append(pick(rng, rng.randrange(0, 40), rng.getrandbits(32), rng.randrange(0, 8)))
        elif k == "w":
            args.append(pick(rng, rng.randrange(-0x200, 0x200), rng.getrandbits(32), rng.randrange(0, 0x100)))
        elif k == "i":
            args.append(pick(rng, 0, 0, 1, rng.randrange(0, 6)))
        elif k == "c":
            args.append(rng.getrandbits(32))
        elif k == "t":
            args.append(pick(rng, 0, 1, 1, 2, 3, -1, rng.randrange(-5, 6)))
        elif k == "m":
            args.append(pick(rng, 1, 1, 0, 2, rng.getrandbits(32)))
        elif k == "p":
            args.append(pick(rng, 0, 1, 2, 3, 4, -1, rng.getrandbits(32)))
        else:
            args.append(rng.getrandbits(32))
    return args


def run_case(m, stubs, name, address, kinds, returns, rng):
    st = make_state(rng)
    args = make_args(rng, kinds)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        for a, v in st["writes"].items():
            m.write(a, bytes(v))
        for a, v in st["globals"].items():
            m.write(a, bytes(v))
        for a, p in POINTERS.items():
            m.write(a, struct.pack("<I", p))
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"])
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCRATCH, SCRATCH_SIZE) + b"".join(m.read(a, n) for a, n in COMPARED)
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
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else f" (memory differs at +{first:#x})") + logs
    return results[0][0][0]


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
        outcomes = {}
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, kinds, returns, rng)
            if msg in ("ok", "trap", "error", "cut off"):
                outcomes[msg] = outcomes.get(msg, 0) + 1
                continue
            if bad < 3:
                print(msg)
            bad += 1
        extra = "" if set(outcomes) <= {"ok"} else f" {outcomes}"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{extra}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
