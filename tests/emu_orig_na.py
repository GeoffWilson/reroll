#!/usr/bin/env python3
"""Differential test of src/game/orig_na.c (group NA: the CPU checks, the sky
dome and sky quads, blast / collapse helpers, the event camera cut, the TV
camera swap, the camera blend, the screenshot dump, the character animation
bitmaps and the float polygon clippers) against the original, on random
state.

Every callee outside the group is a logging stub in both runs (stack buffers
compared by content, never by address; a callee's out-parameters written by
the stub); the pure ones (vec_dot 0x40d950, fix_sqrt_table 0x427960, _ftol
0x497a80) run for real. The GTE function pointers 0x6ccd78 / 0x6ccd80 and
the CreateDirectoryA import slot point at stubs. Each run starts from
fninit and a random x87 control word (mostly the game's single precision).
Return value, the scratch pages and the touched globals are compared.

    tests/emu_orig_na.py EXE build/emu_na.elf [iterations] [names...] [-v] [--cov]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

S, SIZE = 0x01000000, 0x20000
STUB_PAGE = 0x00340000
TRAMP = 0x00350000
VERBOSE = "-v" in sys.argv

OBJS = S + 0x0000          # 8 objects of 0x200
MODELS = S + 0x1000        # 8 models of 0x40
PARTS = S + 0x1400         # 8 part tables of 64 words
BODIES = S + 0x2000        # 4 bodies of 0x100
POINTS = S + 0x2400
SPOTS = S + 0x2500
CAR, CAMREC, CAMBODY = S + 0x3000, S + 0x3800, S + 0x3A00
RACE = S + 0x4000
DOME, RULES, CELLS = S + 0x5000, S + 0x5200, S + 0x5300
TRI, QUAD, SKY, SKYV = S + 0x6000, S + 0x7000, S + 0x9000, S + 0x9800
LISTS = S + 0x9C00
PAL, PIX = S + 0xA000, S + 0xA400
VERTS, INLIST, OUTLIST, PLANE = S + 0xB000, S + 0xB400, S + 0xB500, S + 0xB600
SURFACE, INFO = S + 0xC000, S + 0xE000
ARGS = S + 0xF000          # small argument buffers
NAMEBUF = S + 0xF800

GLOBALS = [(0x4B5E44, 8), (0x6CD400, 4), (0x6CCC64, 8), (0x6CCD78, 12), (0x6CCE84, 4), (0x6CD6F8, 12),
           (0x6CDD08, 8), (0x4B5FAC, 0x20), (0x6D0A40, 4), (0x6D0A00, 4), (0x6D0A38, 4), (0x6CCD84, 4),
           (0x6C1EB0, 4), (0x6C1E30, 4), (0x4B1960, 4), (0x6D06E0, 0x100), (0x6D04A0, 0x228), (0x4B65F4, 8),
           (0x6CF6A0, 0x150 * 4 + 0x10), (0x6CCE40, 4), (0x6CCE6C, 4), (0x4B6244, 4), (0x4B616C, 0xD8),
           (0x6CD408, 4), (0x6BFB80, 4 * 0x274), (0x4B60E0, 12), (0x4B6020, 0x20), (0x4B6160, 4),
           (0x4B63BC, 4), (0x4ACC38, 8), (0x4D5F4C, 0x18), (0x4B5FE8, 0x20), (0x4B6600, 8),
           (0x4B6608, 7 * 24 * 0x24), (0x4B7DA8, 4), (0x6CCE98, 0x30 * 16), (0x6CCD04, 8), (0x4B8000, 4),
           (0x4A3140, 4)]

# name: (argument count, stdcall, per-argument log kinds)
#  v value, w low word, b low byte, mN N bytes at the pointer, c C string, s value or "stack" (default)
STUBS = {
    0x419300: ("body_retire", 1, False, "v"),
    0x4427A0: ("crumble_start", 1, False, "v"),
    0x478430: ("pair_list_remove", 1, False, "v"),
    0x4611D0: ("grid_unlink", 1, False, "v"),
    0x460FB0: ("grid_link", 1, False, "v"),
    0x40A570: ("spot_nearest", 4, False, ["m12", "v", "s", "v"]),
    0x40ADD0: ("zoom_for", 4, False, ["m12", "m12", "v", "v"]),
    0x46BB50: ("face_blast", 3, False, ["v", "v", "m12"]),
    0x486240: ("sky_level", 0, False, ""),
    0x486310: ("sun_height", 0, False, ""),
    0x428390: ("fog_colour", 3, False, "bbb"),
    0x441D80: ("flare_add", 3, False, ["m12", "v", "v"]),
    STUB_PAGE + 0x00: ("gte_project", 1, False, "s"),
    STUB_PAGE + 0x10: ("gte_finish", 0, False, ""),
    STUB_PAGE + 0x20: ("CreateDirectoryA", 2, True, ["c", "v"]),
    0x476AE0: ("alloc", 2, False, "vv"),
    0x4979F0: ("sprintf", 4, False, ["s", "c", "v", "v"]),
    0x476F00: ("file_exists", 1, False, "c"),
    0x476D10: ("file_create", 1, False, "c"),
    0x476E50: ("file_write", 3, False, "vWv"),
    0x4875A0: ("surface_lock", 3, False, "vvs"),
    0x487610: ("surface_unlock", 1, False, "v"),
    0x43B760: ("file_close", 1, False, "v"),
    0x423F00: ("fov_set", 1, False, "w"),
    0x472050: ("listener_set", 4, False, "vvvv"),
    0x407220: ("matrix_look", 3, False, ["m12", "m12", "v"]),
    0x428C30: ("palette_lock", 6, False, "vvssss"),
    0x41E1F0: ("palette_convert", 4, False, ["m1024", "v", "s", "v"]),
    0x477860: ("blit", 8, False, ["v", "v", "v", "v", "v", "v", "v", "m512"]),
    0x428C60: ("palette_unlock", 3, False, ["v", "v", "m1024"]),
    0x477530: ("bmp_load", 3, False, "cvv"),
    0x428C80: ("texture_new", 0, False, ""),
    0x428C90: ("texture_setup", 6, False, "vvvvvv"),
}

# name: (address, returns, state builder)
TESTS = {}
OUTCOMES = {}


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def p16(v):
    return struct.pack("<H", v & 0xFFFF)


def pf(x):
    return struct.pack("<f", x)


def pick(rng, *c):
    return rng.choice(c)


def rbytes(rng, n):
    return rng.randbytes(n)


def rfloat(rng, scale=1000.0, wild=True):
    """A float; `wild` adds any bit pattern and tiny values. The clippers get
    tame ones only: ours runs on SSE, whose float exponent range differs from
    the x87's registers (products underflowing to a signed zero or overflowing,
    and NaN propagation with two NaN operands), see the notes."""
    k = rng.randrange(12)
    if k == 0:
        return pick(rng, 0.0, -0.0, 1.0, -1.0, scale, -scale)
    if k == 1:
        if wild:
            return struct.unpack("<f", rbytes(rng, 4))[0] if rng.randrange(2) else rng.uniform(-1e-30, 1e-30)
        return pick(rng, 1, -1) * rng.uniform(1.0, 2.0) * 2.0 ** rng.randrange(-30, 30)
    return rng.uniform(-scale, scale)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.gte_calls = 0
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.gte_calls = 0
        self.seed = script.get("_seed", [0])[0]

    def _next(self, name, default=0):
        v = self.script.get(name)
        return v.pop(0) if v else default

    def _hook(self, uc, address, size, _):
        name, nargs, stdcall, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        raw = list(words[1:])
        args = []
        for i, a in enumerate(raw):
            k = kinds[i] if i < len(kinds) else "s"
            if k == "v":
                args.append(a)
            elif k == "w":
                args.append(a & 0xFFFF)
            elif k == "b":
                args.append(a & 0xFF)
            elif k.startswith("m"):
                args.append(bytes(uc.mem_read(a, int(k[1:]))))
            elif k == "c":
                args.append(bytes(uc.mem_read(a, 0x80)).split(b"\0", 1)[0])
            elif k == "W":  # file_write's buffer: its content, length from the next argument
                n = raw[i + 1]
                args.append(bytes(uc.mem_read(a, n)) if n <= 0x2000 else ("long",))
            else:
                args.append(("stack",) if E.STACK <= a < E.STACK + E.STACK_SIZE else a)
        if name == "sprintf" and b"%s" not in args[1]:
            args = args[:3]  # one value argument: the 4th is the caller's stack
        self.log.append((name, tuple(args)))
        eax = self._act(uc, name, raw)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * nargs if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, words[0])

    def _act(self, uc, name, a):
        if name == "gte_project":
            vec = bytes(uc.mem_read(0x6CCC64, 6))
            r = random.Random(int.from_bytes(vec, "little") * 977 + self.gte_calls * 31 + self.seed)
            self.gte_calls += 1
            far = struct.unpack("<f", bytes(uc.mem_read(0x4B5FAC, 4)))[0]
            depth = pick(r, far, far * 0.5, far * 2, -far, r.uniform(-2 * abs(far) - 1, 2 * abs(far) + 1))
            code = pick(r, 0, 0, 0, 0, 1, 2, 4, 8, 3, r.getrandbits(32))
            uc.mem_write(a[0], p32(code) + pf(depth) + r.randbytes(16))
            return 0
        if name == "spot_nearest":
            uc.mem_write(a[2], p32(SPOTS + 0x10 * self._next(name)))
            return 0
        if name == "surface_lock":
            ok = self._next(name)
            if ok:
                desc = bytearray(rbytes(random.Random(self.seed), 0x6C))
                struct.pack_into("<i", desc, 0x10, self._next("pitch"))
                struct.pack_into("<I", desc, 0x24, SURFACE)
                uc.mem_write(a[2], bytes(desc))
            return ok
        if name == "palette_lock":
            ok = self._next(name)
            if ok:
                uc.mem_write(a[2], p32(self._next("pitch")))
                uc.mem_write(a[4], p32(self._next("bits")))
                uc.mem_write(a[5], p32(self._next("height")))
                uc.mem_write(a[3], rbytes(random.Random(self.seed + 1), 0x400))
            return ok
        if name == "palette_convert":
            pal = bytes(uc.mem_read(a[0], 0x400))
            uc.mem_write(a[2], bytes((pal[i] * 7 + i) & 0xFF for i in range(0x200)))
            return 0
        if name == "sprintf":
            fmt = bytes(uc.mem_read(a[1], 0x80)).split(b"\0", 1)[0]
            out = fmt.replace(b"%s", b"S").replace(b"%04d", b"%d").replace(b"%d", str(a[2] & 0xFFFF).encode())
            uc.mem_write(a[0], out[:0xF0] + b"\0")
            return len(out)
        if name == "bmp_load":
            ok = self._next(name)
            if ok:
                r = random.Random(self.seed + a[1])
                uc.mem_write(a[1] + 2, p16(r.randrange(-5, 300)) + p16(r.randrange(-5, 300)) + p16(r.randrange(-5, 300)))
                uc.mem_write(a[1] + 0x10, p32(PAL))
            return ok
        return self._next(name)


def call_fpu(m, target, args, cw):
    """fninit; fldcw [cw]; jmp target (one trampoline per target)."""
    slots = call_fpu.slots
    if target not in slots:
        at = TRAMP + 0x10 * len(slots)
        code = b"\xdb\xe3" + b"\xd9\x2d" + p32(TRAMP + 0xF00)
        code += b"\xe9" + p32(target - (at + len(code) + 5))
        m.write(at, code)
        slots[target] = at
    m.write(TRAMP + 0xF00, p16(cw))
    return m.call(slots[target], args)


call_fpu.slots = {}


# ---------------------------------------------------------------- world

def base_world(rng, w):
    """Random values for every global the group touches and the scratch
    structures, as (address, bytes) writes."""
    for a, n in GLOBALS:
        w.append((a, rbytes(rng, n)))
    w.append((0x6CCD78, p32(STUB_PAGE) + p32(0) + p32(STUB_PAGE + 0x10)))
    w.append((0x4A3140, p32(STUB_PAGE + 0x20)))
    race = bytearray(rbytes(rng, 0x1000))
    race[0x58C] = pick(rng, 0, 1, 2, 3)
    race[0x553] = pick(rng, 0, 0, 1, 2, 4, 8)
    w.append((RACE, bytes(race)))
    w.append((0x4B1960, p32(RACE)))
    w.append((0x6CCE40, p32(rng.getrandbits(32) & pick(rng, 0xFFFFFFFF, ~0x12, ~0x10, ~0x2))))
    w.append((0x6CCE84, p32(pick(rng, 0, 1))))
    for k, a in enumerate((0x6CD6F8, 0x6CDD08, 0x6CD700, 0x6CDD0C)):
        w.append((a, p32(LISTS + 4 * k + pick(rng, 0, 0x80000000))))
    w.append((LISTS, rbytes(rng, 0x40)))
    w.append((0x4B65F4, p32(pick(rng, 0, 1, 2, 3, -1))))
    w.append((0x6CF6D2, p16(pick(rng, 0, 0x2C, 0x2D, 0x100, -5))))


def make_object(rng, w, i, nparts=None, root=None, chain=None):
    obj = bytearray(rbytes(rng, 0x200))
    n = nparts if nparts is not None else rng.randrange(0, 20)
    struct.pack_into("<I", obj, 0x10, MODELS + i * 0x40)
    struct.pack_into("<I", obj, 0x194, PARTS + i * 0x100)
    obj[0x19B] = rng.randrange(0, 16)
    obj[0x198] = pick(rng, 0, 1, 1, 2, 5, rng.getrandbits(8))
    struct.pack_into("<I", obj, 0x1B0, root or 0)
    struct.pack_into("<I", obj, 0x19C, chain or 0)
    w.append((OBJS + i * 0x200, bytes(obj)))
    model = bytearray(rbytes(rng, 0x40))
    model[0x27] = n
    w.append((MODELS + i * 0x40, bytes(model)))
    parts = b"".join(p32((rng.getrandbits(32) & 0xF0FFFFFF) | (pick(rng, 4, 4, 4, 5, 0, rng.randrange(16)) << 24))
                     for _ in range(64))
    w.append((PARTS + i * 0x100, parts))
    return OBJS + i * 0x200


def st_none(rng, w):
    return [], {}


def st_obj(rng, w):
    o = make_object(rng, w, 0)
    return [o], {}


def st_event_cut(rng, w):
    w.append((POINTS, rbytes(rng, 12)))
    w.append((SPOTS, rbytes(rng, 0x100)))
    frm = pick(rng, 0, OBJS)
    w.append((OBJS, rbytes(rng, 16)))
    kind = pick(rng, 10, 15, 15, 15, 0, 1, 5, 0x10, 0x11, rng.getrandbits(32))
    return [frm, POINTS, kind, rng.getrandbits(16)], {"spot_nearest": [rng.randrange(16)],
                                                     "zoom_for": [rng.getrandbits(32)]}


def st_crumble_queue(rng, w):
    n = rng.randrange(1, 5)
    objs = []
    for i in range(n):
        objs.append(make_object(rng, w, i, chain=OBJS + (i + 1) * 0x200 if i + 1 < n else 0))
    head = pick(rng, 0, 0x3F, 0x3E, rng.randrange(0x40), rng.getrandbits(8))
    w.append((0x6D04A0, bytes([head, 0, rng.getrandbits(8)])))
    w.append((SPOTS, rbytes(rng, 0x100)))
    return [objs[0]], {"spot_nearest": [rng.randrange(16)], "zoom_for": [rng.getrandbits(32)]}


def st_part_hit(rng, w):
    depth = rng.randrange(0, 3)
    root = None
    objs = []
    for i in range(depth + 1):
        objs.append(make_object(rng, w, i, root=OBJS + (i + 1) * 0x200 if i < depth else 0))
    obj = objs[0]
    body = bytearray(rbytes(rng, 0x100))
    struct.pack_into("<I", body, 0, (rng.getrandbits(32) & ~0x1FF) | pick(rng, 0xC, 0x11, 3, rng.getrandbits(9)))
    struct.pack_into("<i", body, 0xA8, pick(rng, 0, 1, 0x10, 0x100, 0x10000, rng.getrandbits(32) - 2 ** 31))
    w.append((BODIES, bytes(body)))
    pt = b"".join(p32(pick(rng, rng.randrange(-0x400000, 0x400000), rng.getrandbits(32))) for _ in range(3))
    w.append((POINTS, pt))
    w.append((SPOTS, rbytes(rng, 0x100)))
    w.append((0x6D04A0, bytes([rng.randrange(0x40), 0, rng.getrandbits(8)])))
    part = pick(rng, 0, 1, 5, rng.randrange(20), rng.randrange(40))
    return [obj, BODIES, part, POINTS], {"spot_nearest": [rng.randrange(16)], "zoom_for": [rng.getrandbits(32)]}


def st_dome_scale(rng, w):
    w.append((ARGS, rbytes(rng, 4)))
    w.append((0x6CD400, p32(pick(rng, 0x100, 0x200, 0x1000, rng.getrandbits(32)))))
    return [ARGS], {}


def far_value(rng):
    return pick(rng, 100.0, 1000.0, 0.5, -10.0, rfloat(rng))


def st_dome_vertex(rng, w):
    w.append((ARGS, rbytes(rng, 8)))
    w.append((0x4B5FAC, pf(far_value(rng))))
    return [ARGS, ARGS + 0x10, ARGS + 0x40, struct.unpack("<I", pf(far_value(rng)))[0]], {"_seed": [rng.getrandbits(16)]}


def vertex_words(rng):
    return p32(pick(rng, 0, 0, 1, 2, 4, rng.getrandbits(32))) + rbytes(rng, 20)


def st_quad(rng, w):
    for k in range(4):
        w.append((ARGS + 0x20 * k, vertex_words(rng)))
    w.append((QUAD, rbytes(rng, 0x78)))
    return [QUAD, ARGS, ARGS + 0x20, ARGS + 0x40, ARGS + 0x60] + [rng.getrandbits(32) for _ in range(5)], {}


def st_tri(rng, w):
    for k in range(3):
        w.append((ARGS + 0x20 * k, vertex_words(rng)))
    w.append((TRI, rbytes(rng, 0x60)))
    return [TRI, ARGS, ARGS + 0x20, ARGS + 0x40] + [rng.getrandbits(32) for _ in range(4)], {}


def st_dome(rng, w):
    w.append((0x4B5FAC, pf(far_value(rng))))
    w.append((0x4B5FB0, p32(QUAD)))
    w.append((0x4B5FBC, p32(TRI)))
    w.append((0x6D0A40, p32(DOME)))
    w.append((DOME, b"".join(p16(pick(rng, rng.randrange(-0x400, 0x400), rng.getrandbits(16))) for _ in range(0x100))))
    rules = bytearray(rbytes(rng, 0x40))
    rules[4] = pick(rng, 0, 0, 4, rng.getrandbits(8))
    w.append((RULES, bytes(rules)))
    w.append((0x6C1EB0, p32(RULES)))
    w.append((0x6C1E30, p32(CELLS)))
    w.append((CELLS, p32(CELLS + 0x10)))
    w.append((CELLS + 0x10, b"".join(p32(CELLS + 0x500 + 8 * k) for k in range(256))))
    w.append((CELLS + 0x500, bytes(pick(rng, 0, 8, rng.getrandbits(8)) for _ in range(0x800))))
    view = bytearray(rbytes(rng, 0x200))
    struct.pack_into("<I", view, 0x11C, pick(rng, 0xFFFFFFFF, rng.randrange(256), rng.getrandbits(32)))
    w.append((OBJS, bytes(view)))
    w.append((0x6CD400, p32(pick(rng, 0x100, 0x200, rng.getrandbits(32)))))
    w.append((TRI, rbytes(rng, 0xC00)))
    w.append((QUAD, rbytes(rng, 0x1E00)))
    return [OBJS], {"sky_level": [pick(rng, 0, 0x100, -0x100, rng.getrandbits(32))],
                    "sun_height": [rng.getrandbits(32)], "_seed": [rng.getrandbits(16)]}


def st_sky(rng, w):
    n = pick(rng, 0, 1, 2, 5, 16)
    w.append((0x6D0A00, p32(n)))
    w.append((0x6CCD84, p32(pick(rng, 0, 0, 4, rng.getrandbits(32)))))
    w.append((0x6D0A38, p32(SKYV)))
    w.append((SKYV, rbytes(rng, 16 * 32)))
    w.append((0x4B5FC0, p32(SKY)))
    w.append((SKY, rbytes(rng, 16 * 0x78)))
    w.append((0x4B5FAC, pf(far_value(rng))))
    return [], {"_seed": [rng.getrandbits(16)]}


def st_screenshot(rng, w):
    width, height = pick(rng, 0, 1, 3, 8, 16, -2), pick(rng, 0, 1, 2, 5, 12, -1)
    w.append((0x4ACC38, p32(width) + p32(height)))
    w.append((SURFACE, rbytes(rng, 0x1000)))
    w.append((INFO, rbytes(rng, 0x40)))
    w.append((0x4D5F4C, rbytes(rng, 0x18)))
    for a in (0x4D5F50, 0x4D5F58, 0x4D5F60):
        w.append((a, p32(pick(rng, 0x0003, 0x0B03, 0x0502, 0x0A03, 0x1008, rng.getrandbits(32)))))
    exists = [1] * rng.randrange(0, 4) + [0]
    return [], {"alloc": [pick(rng, INFO, INFO, 0)], "file_exists": exists,
                "file_create": [pick(rng, 0x1234, 0x1234, 0)], "surface_lock": [pick(rng, 1, 1, 0)],
                "pitch": [pick(rng, 0x20, 0x40, 0x22)], "_seed": [rng.getrandbits(16)]}


def camera_slots(rng, w):
    slots = bytearray(rbytes(rng, 4 * 0x274))
    for v in range(4):
        struct.pack_into("<I", slots, v * 0x274 + 0x28, CAMREC + 0x10 * v)
        w.append((CAMREC + 0x10 * v, p32(CAMBODY)))
    w.append((0x6BFB80, bytes(slots)))
    w.append((CAMBODY, rbytes(rng, 0x200)))


def st_tv_begin(rng, w):
    camera_slots(rng, w)
    w.append((0x6CCE6C, pf(pick(rng, 60.0, 90.5, -3.7, 1e10, rfloat(rng, 300)))))
    return [rng.randrange(4)], {}


def st_tv_end(rng, w):
    camera_slots(rng, w)
    w.append((0x6CD408, p32(rng.randrange(4))))
    return [], {}


def st_lerp(rng, w):
    w.append((ARGS, b"".join(p32(pick(rng, rng.randrange(-0x10000, 0x10000), rng.getrandbits(32))) for _ in range(6))))
    return [ARGS, ARGS + 12, ARGS + 0x20, pick(rng, 0, 1, 4, 8, rng.getrandbits(32))], {}


def blend_rec(rng, w):
    car = bytearray(rbytes(rng, 0x700))
    car[0x63F] = pick(rng, 0, 1)
    w.append((CAR, bytes(car)))
    cam = bytearray(rbytes(rng, 0x60))
    struct.pack_into("<I", cam, 0, CAMBODY)
    for o in (0x4C, 0x4D, 0x50):
        cam[o] = pick(rng, 0, 1)
    struct.pack_into("<H", cam, 0x5A, pick(rng, 0, 1, 4, 8, rng.getrandbits(16)))
    w.append((CAMREC, bytes(cam)))
    body = bytearray(rbytes(rng, 0x100))
    if rng.randrange(2):
        for k in range(3):
            struct.pack_into("<i", body, 0x34 + 4 * k, struct.unpack_from("<i", car, 0x34 + 4 * k)[0]
                             + rng.randrange(-0x8000, 0x8000))
    w.append((CAMBODY, bytes(body)))
    rec = bytearray(rbytes(rng, 0x30))
    struct.pack_into("<I", rec, 0, CAR)
    struct.pack_into("<I", rec, 0x28, CAMREC)
    w.append((ARGS + 0x100, bytes(rec)))
    w.append((0x4B65F8, p32(pick(rng, 0, 1))))
    return ARGS + 0x100


def st_blend(rng, w):
    return [blend_rec(rng, w)], {}


def st_charanim(rng, w):
    w.append((PAL, rbytes(rng, 0x400) if rng.randrange(2) else bytes(rng.randrange(12) for _ in range(0x400))))
    table = bytearray(rbytes(rng, 7 * 24 * 0x24))
    for k in range(7 * 24):
        struct.pack_into("<I", table, k * 0x24 + 0x10, PAL)
    w.append((0x4B6608, bytes(table)))
    c = pick(rng, 0, 3, 6, 7, rng.getrandbits(32))
    f = pick(rng, 0, 0x15, 0x16, rng.randrange(22), rng.getrandbits(32))
    return [c, f], {"palette_lock": [pick(rng, 0, 1, 1)], "pitch": [rng.getrandbits(32)],
                    "bits": [rng.getrandbits(32)], "height": [rng.getrandbits(32)], "bmp_load": [pick(rng, 0, 1, 1)],
                    "_seed": [rng.getrandbits(16)]}


def st_charanim_all(rng, w):
    args, script = st_charanim(rng, w)
    script["bmp_load"] = [pick(rng, 0, 1) for _ in range(7 * 22)]
    script["texture_new"] = [rng.getrandbits(32)]
    return [], script


def st_texinfo(rng, w):
    w.append((0x4B6600, rbytes(rng, 8)))
    return [ARGS + 2 * k for k in range(5)], {}


def clip_world(rng, w):
    n = pick(rng, 0, 1, 2, 3, 3, 4, 4, 5, 8, -1)
    verts = bytearray()
    scale = pick(rng, 1.0, 100.0, 1000.0)
    shared = [rfloat(rng, scale, False) for _ in range(3)]
    for k in range(10):
        v = [rfloat(rng, scale, False) for _ in range(12)]
        if rng.randrange(4) == 0:
            v[1] = pick(rng, *shared)
        verts += b"".join(pf(x) for x in v)
    w.append((VERTS, bytes(verts)))
    ids = [rng.randrange(10) for _ in range(max(n, 1))]  # a valid first vertex even when empty: it is read
    w.append((INLIST, p32(n) + b"".join(p32(VERTS + 0x30 * i) for i in ids)))
    w.append((OUTLIST, rbytes(rng, 0x40)))
    nxt = rng.randrange(0, 5)
    w.append((ARGS, p32(nxt)))
    w.append((PLANE, b"".join(pf(x) for x in [pick(rng, *shared, rfloat(rng, scale, False))] + [rfloat(rng, 2.0, False) for _ in range(3)])))
    w.append((0x4B8000, pf(pick(rng, 0.0, rfloat(rng, scale, False)))))
    w.append((0x6CCD04, pf(pick(rng, *shared, rfloat(rng, scale, False), 1.0, 0.0))))
    w.append((0x6CCD08, pf(pick(rng, *shared, rfloat(rng, scale, False), 1000.0, -0.0))))
    w.append((0x6CCE98, rbytes(rng, 0x30 * 16)))


def st_clip4(rng, w):
    clip_world(rng, w)
    return [INLIST, PLANE, OUTLIST, ARGS], {}


def st_clip3(rng, w):
    clip_world(rng, w)
    return [INLIST, OUTLIST, ARGS], {}


TESTS = {
    "cpu_has_3dnow": (0x4010C0, True, st_none),
    "cpu_ext_level": (0x4010F0, True, st_none),
    "cpu_3dnow_get": (0x401110, True, st_none),
    "track_object_vanish": (0x401230, True, st_obj),
    "blast_object_wreck": (0x4052B0, False, st_obj),
    "blast_object_hit": (0x405280, True, st_obj),
    "camera_event_cut": (0x40AE50, False, st_event_cut),
    "crumble_queue_add": (0x405ED0, True, st_crumble_queue),
    "object_part_hit": (0x412BF0, True, st_part_hit),
    "dome_scale": (0x404980, False, st_dome_scale),
    "dome_vertex": (0x404E00, False, st_dome_vertex),
    "dome_quad_add": (0x404E50, False, st_quad),
    "dome_tri_add": (0x404EE0, False, st_tri),
    "sky_dome_draw": (0x4049B0, False, st_dome),
    "sky_quads_draw": (0x404F50, False, st_sky),
    "screenshot_dump": (0x405C10, True, st_screenshot),
    "tv_camera_begin": (0x408270, False, st_tv_begin),
    "tv_camera_end": (0x40AD60, False, st_tv_end),
    "vec_lerp8": (0x40CFA0, False, st_lerp),
    "camera_blend_apply": (0x40CD90, False, st_blend),
    "camera_blend_begin": (0x40CBE0, False, st_blend),
    "camera_blend_end": (0x40CD00, False, st_blend),
    "charanim_upload": (0x4114B0, True, st_charanim),
    "charanim_load": (0x4115E0, True, st_charanim),
    "charanim_load_all": (0x4116F0, True, st_charanim_all),
    "charanim_texture_info": (0x411740, False, st_texinfo),
    "clip_plane": (0x417060, True, st_clip4),
    "clip_plane_flat": (0x417270, True, st_clip4),
    "clip_near": (0x417450, True, st_clip3),
    "clip_far": (0x417600, True, st_clip3),
    "clip_near_list": (0x4177B0, True, st_clip3),
}

CONTROL_WORDS = (0x007F,)  # the game runs the x87 at single precision, round to nearest; ours is SSE
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))
PAD = 0x6CCC6A  # the GTE vector's pad word: leftover stack in the original's sky_dome_draw


def run_case(m, stubs, name, rng):
    address, returns, builder = TESTS[name]
    w = []
    base_world(rng, w)
    args, script = builder(rng, w)
    cw = pick(rng, *CONTROL_WORDS)
    results = []
    for target in (address, m.symbols[name]):
        m.write(S, bytes(SIZE))
        for a, data in w:
            m.write(a, data)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(script)
        r = call_fpu(m, target, args, cw)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = bytearray(m.read(S, SIZE) + b"".join(m.read(a, n) for a, n in GLOBALS))
        if name == "sky_dome_draw":
            off = SIZE + 8 + 4 + (PAD - 0x6CCC64)
            mem[off:off + 2] = b"\0\0"
        results.append((r, bytes(mem), stubs.log))
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            if first < SIZE:
                where = f" (memory differs at scratch +{first:#x})"
            else:
                off = first - SIZE
                for ga, gn in GLOBALS:
                    if off < gn:
                        where = f" (memory differs at {ga + off:#x})"
                        break
                    off -= gn
        if VERBOSE and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]], "cw", hex(cw))
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {str(la[k:k + 2])[:400]}\n  ours:          {str(lb[k:k + 2])[:400]}"
        return f"{name}: args {[hex(x) for x in args]} original {results[0][0]} ours {results[1][0]}" + where + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 500
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, SIZE)
    m.uc.mem_map(STUB_PAGE, 0x1000)
    m.write(STUB_PAGE, b"\xc3" * 0x1000)
    m.uc.mem_map(TRAMP, 0x1000)
    stubs = Stubs(m)
    rng = random.Random(0x4A)
    covered = set()
    if "--cov" in sys.argv:
        m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, size, _: covered.add(a), begin=0x401000, end=0x418000)
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    failed = 0
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {OUTCOMES}")
        failed += bad != 0
    if covered:
        for name in TESTS:
            if only and name not in only:
                continue
            address = TESTS[name][0]
            size = next(int(l.split()[2]) for l in open(__file__.rsplit("/", 2)[0] + "/build/groups/na_owned.txt")
                        if l.startswith(hex(address)))
            ins = [a for a in instruction_addresses(address, size)]
            missed = sorted(a for a in ins if a not in covered)
            print(f"coverage {name}: {len(ins) - len(missed)}/{len(ins)}" + (
                f" missed {[hex(a) for a in missed[:12]]}" if missed else ""))
    sys.exit(1 if failed else 0)


def instruction_addresses(start, size):
    out = set()
    asm = __file__.rsplit("/", 2)[0] + "/build/rollcage.asm"
    for line in open(asm):
        head = line.split(":", 1)[0].strip()
        try:
            a = int(head, 16)
        except ValueError:
            continue
        if start <= a < start + size:
            if not line.rstrip().endswith(("nop", "int3")):
                out.add(a)
    return out


if __name__ == "__main__":
    main()
