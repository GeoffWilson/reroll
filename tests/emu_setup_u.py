#!/usr/bin/env python3
"""Differential test of setup_u.c (race setup group U: weapon bodies,
pickups, viewports, primitive pools, tables, pad bindings and the body
allocator's helpers) against the original, on random worlds.

Calls out to other code (the allocators, body shape / grid, sprites,
0x4270e0, 0x4291f0, the screen centre and clear) are stubbed and logged,
so both sides must make the same calls with the same arguments. The whole
data section, the scratch world and the call logs are compared.

    tests/emu_setup_u.py EXE build/emu_u.elf [iterations]
"""
import random
import re
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

DATA, DATA_SIZE = 0x4A7000, 0x22B298
SCR, SCR_SIZE = 0x01000000, 0x100000
RACE, CONFIG, WORLD, MODEL_PTRS, MODELS = SCR, SCR + 0x1000, SCR + 0x1400, SCR + 0x1500, SCR + 0x1600
MODEL_LIST, INDEX, TRACK, TEX = SCR + 0x2600, SCR + 0x2A00, SCR + 0x2C00, SCR + 0x3000
TABLE, PADS, PAD_COUNTS, NMODEL, OUT = SCR + 0x5000, SCR + 0x5200, SCR + 0x5600, SCR + 0x6000, SCR + 0x8000
BODY = SCR + 0xA000
HEAP, HEAP_END = SCR + 0x10000, SCR + SCR_SIZE
STACK_FILL = (E.STACK, E.STACK_SIZE - 0x200)

# address: argument logging kinds -- v value, r 8-byte rectangle behind a pointer
STUBS = {
    0x4445E0: "v",              # allocate
    0x460BA0: "v v v v",        # new body
    0x465640: "v v",            # body shape
    0x4611D0: "v",              # grid unlink
    0x4612A0: "v",              # sprite grid unlink
    0x47DCC0: "v v v v v v",    # new sprite
    0x4270E0: "v v",
    0x4291F0: "v v v v v",
    0x426200: "v v",            # screen centre
    0x479AE0: "r v v v",        # clear (empty in the game)
    0x48F430: "v",              # snatch spark reset
}
ALLOCATORS = {0x4445E0: 0, 0x460BA0: 2, 0x47DCC0: None}

# name: (original, case maker, returns a value)
TESTS = {}


def test(address, returns=False):
    def wrap(f):
        TESTS[f.__name__] = (address, f, returns)
        return f
    return wrap


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.heap = HEAP
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self.hit, begin=address, end=address)

    def u32(self, address):
        return struct.unpack("<I", self.m.read(address, 4))[0]

    def hit(self, uc, address, size, _):
        esp = uc.reg_read(UC_X86_REG_ESP)
        args = []
        for i, kind in enumerate(STUBS[address].split()):
            a = self.u32(esp + 4 + i * 4)
            args.append(self.m.read(a, 8) if kind == "r" else a)
        self.log.append((address, tuple(args)))
        eax = 0
        if address in ALLOCATORS:
            which = ALLOCATORS[address]
            n = args[which] if which is not None else 0x200
            eax = self.heap
            self.heap = (self.heap + min(n, 0x8000) + 0x7F) & ~0x7F
            if self.heap >= HEAP_END:
                self.heap = HEAP
        uc.reg_write(UC_X86_REG_EAX, eax)
        uc.reg_write(UC_X86_REG_EIP, self.u32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4)


def pick(rng, common, bits=32):
    return rng.choice(common) if rng.randrange(5) else rng.getrandbits(bits)


class World:
    """Random memory for one case: the scratch block and global values."""

    def __init__(self, rng):
        self.rng = rng
        self.scr = bytearray(rng.randbytes(0x60000)) + bytearray(SCR_SIZE - 0x60000)
        self.globals = {}
        self.data = {}

    def put(self, address, fmt, *values):
        fmt = fmt.translate(str.maketrans("ihb", "IHB"))
        codes = "".join(c * int(n or 1) for n, c in re.findall(r"(\d*)([A-Za-z])", fmt))
        sizes = [struct.calcsize("<" + c) for c in codes]
        values = [v & ((1 << 8 * n) - 1) for v, n in zip(values, sizes)]
        if SCR <= address < SCR + SCR_SIZE:
            struct.pack_into(fmt, self.scr, address - SCR, *values)
        else:
            self.data[address] = struct.pack(fmt, *values)

    def common(self):
        rng = self.rng
        self.put(0x4B1960, "<I", RACE)
        self.put(0x4B2198, "<I", CONFIG)
        self.put(0x6C1E30, "<I", WORLD)
        self.put(0x6C1EB0, "<I", TRACK)
        self.put(0x6C1E14, "<I", TEX)
        self.put(TRACK + 0x2E, "<H", pick(rng, [0, 1, 0x10, 0x40, 0x60], 7))
        self.put(WORLD + 0x18, "<I", MODEL_LIST)
        self.put(WORLD + 0x58, "<I", INDEX)
        for i in range(0x60):
            self.put(INDEX + i * 2, "<h", rng.choice([rng.randrange(0, 0x100), rng.randrange(-4, 4)]))
        self.put(0x4B11D8, "<I", rng.getrandbits(31))
        self.put(0x4B24A0, "<I", rng.getrandbits(32))


def run(m, stubs, address, world, args, returns):
    results = []
    stack = world.rng.randbytes(0x400) * (STACK_FILL[1] // 0x400)
    for target in address:
        m.write(DATA, m.base_data)
        for a, v in world.data.items():
            m.write(a, v)
        m.write(SCR, bytes(world.scr))
        m.write(STACK_FILL[0], stack)
        stubs.log = []
        stubs.heap = HEAP
        r = m.call(target, args)
        if r[0] == "ok" and not returns:
            r = ("ok",)
        results.append((r, m.read(DATA, DATA_SIZE), m.read(SCR, SCR_SIZE), stubs.log))
    return results


def describe(results):
    (r1, d1, s1, l1), (r2, d2, s2, l2) = results
    msg = f"original {r1} ours {r2}"
    if d1 != d2:
        i = next(i for i in range(len(d1)) if d1[i] != d2[i])
        msg += f"; data differs at {DATA + i:#x} ({d1[i]:#x} vs {d2[i]:#x})"
    if s1 != s2:
        i = next(i for i in range(len(s1)) if s1[i] != s2[i])
        msg += f"; scratch differs at {SCR + i:#x} ({s1[i]:#x} vs {s2[i]:#x})"
    if l1 != l2:
        k = next((i for i in range(min(len(l1), len(l2))) if l1[i] != l2[i]), min(len(l1), len(l2)))
        msg += f"; calls differ at #{k}: {l1[k:k + 2]} vs {l2[k:k + 2]}"
    return msg


# --- cases ---

def body_ptr(rng):
    return rng.choice([BODY, HEAP + 0x40000, HEAP + rng.randrange(0, 0x40) * 0x80])


@test(0x483100)
def projectile_trail_init(w):
    b = body_ptr(w.rng)
    w.put(b + 0x184, "<i", pick(w.rng, [0, 1, 2, 3, -1]))
    return [b]


@test(0x486A40)
def snatch_texture_corners(w):
    rng = w.rng
    return [OUT + rng.randrange(0, 12) * 2 for _ in range(5)]


@test(0x48F780)
def snatch_prims_init(w):
    return [body_ptr(w.rng)]


@test(0x48EC30)
def snatch_sprites_create(w):
    return [body_ptr(w.rng)]


@test(0x48D880)
def snatch_create(w):
    return []


@test(0x4583E0)
def weapon_bodies_create(w):
    return []


@test(0x468150)
def pickup_prims_init(w):
    return [body_ptr(w.rng)]


@test(0x425CE0)
def droppers_create(w):
    w.put(TRACK + 0xF7, "<B", pick(w.rng, [0, 1, 2, 6], 8))
    return []


@test(0x467120)
def pickups_create(w):
    rng = w.rng
    w.put(RACE + 0xA86, "<B", rng.choice([0, 0, 1, rng.getrandbits(8)]))
    w.put(RACE + 0x551, "<B", rng.choice([0, 1, 3, 8, rng.getrandbits(8)]))
    w.put(WORLD + 0x36, "<B", pick(rng, [0, 1, 4, 10], 8))
    w.put(TRACK + 0xF7, "<B", pick(rng, [0, 1, 2, 6], 8))
    return []


@test(0x428B50)
def viewport_border(w):
    rng = w.rng
    w.put(RACE + 0x552, "<B", rng.choice([0, 1, 4, rng.getrandbits(8)]))
    a = OUT + rng.randrange(0, 4) * 4
    return [a, rng.choice([a, OUT + 0x10, OUT + rng.randrange(0, 4) * 4])]


def players(w):
    rng = w.rng
    w.put(0x5D622C, "<B", rng.getrandbits(8))
    w.put(0x5D535C, "<i", rng.choice([0, 1, 2, 3, 4, 5, 5, rng.randrange(0, 6)]))
    for i in range(6):
        w.put(0x5D2CD0 + i * 0x66C, "<i", rng.choice([0, 1, 1, rng.getrandbits(32)]))
        w.put(0x5D2CE8 + i * 0x66C, "<i", rng.choice([rng.randrange(0, 6), rng.randrange(0, 8), i,
                                                       rng.randrange(-2, 10)]))


@test(0x45ED40)
def viewport_player_order(w):
    players(w)
    return [OUT + w.rng.randrange(0, 4) * 4]


def screen(w):
    rng = w.rng
    w.put(RACE + 0x552, "<B", rng.choice([0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, rng.getrandbits(8)]))
    w.put(0x6CD462, "<H", pick(rng, [320, 512, 640, 800, 1024, 1280, 3440], 16))
    w.put(0x6CD464, "<H", pick(rng, [240, 256, 480, 600, 768, 1440], 16))
    w.put(0x6CD46E, "<I", rng.getrandbits(32))
    w.put(0x6CD470, "<I", rng.getrandbits(32))


@test(0x423360)
def viewports_layout(w):
    screen(w)
    rng = w.rng
    return [rng.choice([rng.randrange(0, 6), rng.randrange(0, 4), 0]) for _ in range(4)]


@test(0x444080)
def viewports_setup(w):
    screen(w)
    players(w)
    if w.rng.randrange(3) == 0:
        w.put(0x5D622C, "<B", 0)
    return []


@test(0x4040B0)
def uv_midpoint(w):
    rng = w.rng
    return [OUT + rng.randrange(0, 8), TEX + rng.randrange(0, 0x100), TEX + rng.randrange(0, 0x100)]


@test(0x4040F0)
def quarter_prims_init(w):
    w.put(0x6D0A40, "<I", TABLE)
    return []


@test(0x405050)
def explosions_reset(w):
    return []


@test(0x442660)
def attached_points_reset(w):
    return []


@test(0x46C210)
def shadows_reset(w):
    return []


@test(0x433D30)
def trail_prims_init(w):
    return []


@test(0x47C2C0)
def reciprocal_table_init(w):
    w.put(0x4D4B9C, "<I", w.rng.choice([0, 0, 0, HEAP + 0x80000]))
    return []


@test(0x47C330)
def model_buffers_init(w):
    rng = w.rng
    count = pick(rng, [0, 1, 2, 5, 0x20, 0x3F], 6)
    w.put(WORLD, "<I", MODEL_PTRS)
    w.put(WORLD + 0x10, "<I", count)
    for i in range(0x40):
        w.put(MODEL_PTRS + i * 4, "<I", MODELS + i * 0x40)
        w.put(MODELS + i * 0x40 + 0xC, "<I", pick(rng, [0, 1, 0xF, 0x10, 0x11, 0x100], 12))
    w.put(0x4D4B9C, "<I", w.rng.choice([0, HEAP + 0x80000]))
    return []


def normals_model(w, at, count_vertices=None):
    """A model at `at` (+4 vertices, +6 faces, +0xc face pointer)."""
    rng = w.rng
    nv = count_vertices if count_vertices is not None else pick(rng, [0, 1, 4, 8, 12], 4)
    nf = pick(rng, [0, 1, 6, 12], 4)
    faces = at + 0x80
    w.put(at + 4, "<HH", nv, nf)
    w.put(at + 0xC, "<I", faces)
    w.put(at + 0x48, "<I", rng.choice([0, 0, 0, 1]))
    for f in range(nf):
        corners = [rng.choice([0, rng.randrange(1, nv + 2)]) for _ in range(4)]
        n = [rng.choice([0, rng.randrange(-512, 512)]) & 0x3FF for _ in range(3)]
        packed = n[0] << 20 | n[1] << 10 | n[2] if rng.randrange(6) else rng.getrandbits(32)
        w.put(faces + f * 8, "<4BI", *corners, packed)


@test(0x40F2A0)
def model_vertex_normals(w):
    normals_model(w, NMODEL)
    return [NMODEL, OUT]


@test(0x40F1E0)
def model_normals_make(w):
    rng = w.rng
    normals_model(w, NMODEL)
    first = rng.choice([0, 0x200])
    second = rng.choice([0, 0x400]) if first else 0
    w.put(NMODEL + 0x1C, "<HH", first, second)
    if first:
        normals_model(w, NMODEL + first)
    if second:
        normals_model(w, NMODEL + second)
    return [NMODEL]


def bindings(w):
    rng = w.rng
    for base in (0x6BF180, 0x6BF340, 0x6BF500, 0x6BF6C0):
        for n in range(0x1B):
            w.put(base + n * 0x10 + 4, "<i", rng.choice([0, 1, 1, 2, rng.getrandbits(32)]))


@test(0x449BF0, returns=True)
def binding_get(w):
    rng = w.rng
    return [pick(rng, [0, 1, 2, 3, 4], 32), pick(rng, [0, 5, 0x1A, 0x1B, -1], 32)]


@test(0x449D80, returns=True)
def binding_is_button(w):
    bindings(w)
    rng = w.rng
    return [pick(rng, [0, 1, 2, 3, 4], 32), pick(rng, [0, 1, 2, 4, 0x1A, 0x1B, -1], 32)]


@test(0x449D00, returns=True)
def pad_button_flags(w):
    bindings(w)
    rng = w.rng
    for p in range(4):
        w.put(CONFIG + p * 0x90 + 0x1A3, "<B", rng.choice([0, 1, rng.getrandbits(8)]))
    return [pick(rng, [0, 1, 2, 3, 4, -1], 32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32),
            rng.getrandbits(32)]


def devices(w):
    rng = w.rng
    w.put(0x4D9554, "<i", rng.choice([0, 1, 2, 4, 8, rng.randrange(-2, 9)]))
    for n in range(8):
        w.put(0x4D75C0 + n * 0x43C, "<II", rng.getrandbits(32), rng.choice([0x400, 0x401, rng.getrandbits(32)]))


@test(0x489FF0, returns=True)
def device_flag(w):
    devices(w)
    return [w.rng.randrange(0, 8)]


@test(0x489FA0, returns=True)
def device_usable(w):
    devices(w)
    return [w.rng.choice([w.rng.randrange(0, 8), w.rng.randrange(-3, 8)])]


@test(0x428CC0, returns=True)
def pad_device_bound(w):
    devices(w)
    rng = w.rng
    for s in range(4):
        w.put(0x4AC6FC + s * 4, "<I", PADS + s * 0x100)
        w.put(0x4AC70C + s * 4, "<I", PAD_COUNTS + s * 4)
        w.put(PAD_COUNTS + s * 4, "<i", rng.choice([0, 1, 4, 8, rng.randrange(-2, 15)]))
        for r in range(15):
            w.put(PADS + s * 0x100 + r * 0x10 + 4, "<ii", rng.choice([0, 1, 1, 2]), rng.randrange(0, 8))
    w.put(OUT, "<h", rng.randrange(0, 8))
    return [rng.choice([0, 1, 2, 3, 3, 4, -1]), OUT]


@test(0x460AF0, returns=True)
def link_pool_take(w):
    rng = w.rng
    w.put(0x4D087C, "<I", pick(rng, [0, 0x80, 0x1000, 3], 20))
    w.put(0x6B9B90, "<I", rng.getrandbits(32))
    return [pick(rng, [0, 1, 0x7F, 0x80, 0x81, 0x1000], 32)]


@test(0x460B20)
def grid_link_init(w):
    return [OUT + w.rng.randrange(0, 4) * 2, w.rng.getrandbits(32)]


@test(0x460B40)
def body_header_init(w):
    return [body_ptr(w.rng), w.rng.getrandbits(32)]


def face_model(w, at):
    rng = w.rng
    start = rng.choice([0x40, 0x80])
    n = pick(rng, [0, 1, 3, 8], 4)
    w.put(at + 0x10, "<ii", start, start + n * 0x28 + rng.choice([0, 0, 0, rng.randrange(-0x28, 0x28)]))


@test(0x460DC0)
def model_instance_init(w):
    face_model(w, NMODEL)
    return [OUT, NMODEL, HEAP + 0x1000, w.rng.getrandbits(32)]


@test(0x460E40, returns=True)
def model_faces_carve(w):
    rng = w.rng
    face_model(w, NMODEL)
    w.put(NMODEL + 0x1C, "<HH", rng.choice([0, 0x100]), rng.choice([0, 0x200]))
    face_model(w, NMODEL + 0x100)
    face_model(w, NMODEL + 0x200)
    model = rng.choice([NMODEL, NMODEL, NMODEL, 0])
    outs = [OUT + i * 4 for i in range(6)]
    if rng.randrange(4) == 0:
        rng.shuffle(outs)
    return [model, rng.getrandbits(32), rng.getrandbits(1), rng.getrandbits(1), rng.choice([0, 1, 5])] + outs


@test(0x460F50)
def body_list_add(w):
    w.put(0x4D1088, "<i", w.rng.choice([0, 1, 0x1FF, 0x200, 0x201, w.rng.randrange(-2, 0x200)]))
    return [body_ptr(w.rng)]


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 3000
    only = sys.argv[4:]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    m.base_data = m.read(DATA, DATA_SIZE)
    stubs = Stubs(m)
    rng = random.Random(21)
    failed = 0
    for name, (address, make, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        outcomes = {}
        for _ in range(iterations):
            w = World(rng)
            w.common()
            args = make(w)
            results = run(m, stubs, (address, m.symbols[name]), w, args, returns)
            outcomes[results[0][0][0]] = outcomes.get(results[0][0][0], 0) + 1
            if results[0] != results[1]:
                if bad < 5:
                    print(f"{name}: {describe(results)}")
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {outcomes}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
