#!/usr/bin/env python3
"""Differential test of post_h.c (weapons, pickups and their effects, the
0x47ab40 tree) against the original, in the Unicorn emulator.

Each case builds a random world (cars and their slots, pickups, droppers,
projectiles, track lines and markers) in scratch memory and the game's
globals, runs the original and ours from the same state, and compares all
of memory that either could touch plus the calls each made to functions
outside this group, which are stubbed: a stub logs its arguments (the
bytes behind stack pointers rather than the pointers) and returns a value
from the case's list.

    tests/emu_post_h.py EXE build/emu_h.elf [iterations] [test ...]
"""
import random
import struct
import sys

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x80000
CARS = SCRATCH                    # 6 x 0x2400
RACE = SCRATCH + 0xD800           # 0xb00
PICKUPS = SCRATCH + 0xE400        # 12 x 0x200
DROPPERS = SCRATCH + 0x11400      # 6 x 0x200
MISC = SCRATCH + 0x12000          # small records, 0x4000
PROJ = SCRATCH + 0x16000          # 6 x 0x2b00 projectile objects
TRACK = SCRATCH + 0x28000         # track header 0x200, lines 0x200, group masks 0x800, nodes
PARTICLE = SCRATCH + 0x2E000      # what the particle allocator hands out
MESH = SCRATCH + 0x2F000          # ripple mesh, links, colours
STACK_LO, STACK_HI = E.STACK, E.STACK + E.STACK_SIZE

# memory compared (and restored before each run)
REGIONS = [(0x4A7000, 0x4E0000 - 0x4A7000), (0x5D0000, 0x601000 - 0x5D0000), (0x6B8000, 0x6D2298 - 0x6B8000),
           (SCRATCH, SCRATCH_SIZE)]

CAR_SIZE, SLOT_SIZE, SLOTS = 0x2400, 0x274, 0x6BFB80
RACE_PTR = 0x4B1960

# address: (argument kinds, kind of return): i int, b byte, h int16,
# p pointer (logged with 16 bytes behind it if on the stack)
STUBS = {
    0x471C90: ("iiiip", "byte"),       # sound_play
    0x471D50: ("iiiipb", "byte"),      # car sound
    0x471E60: ("iiiiippb", "byte"),    # looped car sound
    0x471F40: ("b", None),             # sound stop
    0x471FC0: ("bhh", None),           # sound volume
    0x45C040: ("p", None),             # body release
    0x467490: ("p", None),             # give weapon
    0x467C90: ("pi", None),            # weapon slot clear
    0x47A860: ("pi", None),            # boost effect
    0x47D5A0: ("i", "int"),            # time warp palette
    0x460FB0: ("p", None),             # grid link
    0x4611D0: ("p", None),             # grid unlink
    0x45EF80: ("", "flag"),            # network game?
    0x45EED0: ("", "flag"),
    0x42B700: ("pppi", None),          # debris
    0x41B4D0: ("piiiii", None),        # rumble
    0x458F50: ("pp", None),            # matrix from direction
    0x42AC30: ("", "particle"),        # particle allocate
    0x42CD40: ("pii", None),
    0x460FF0: ("piii", None),
    0x4411C0: ("ii", None),
    0x441D00: ("ppiiiq", None),
    0x42AA30: ("ppiii", None),
    0x419300: ("p", None),
    0x4190A0: ("", "index"),
    0x415EA0: ("pSOi", "sweep"),       # track sweep: logs the slot, sets out[7]
    0x4600A0: ("iii", None),
    0x45E6F0: ("iiiDi", "int"),        # network send
    0x476EC0: ("i", None),
    0x48F430: ("p", None),
    0x467880: ("pi", None),
    0x48CE80: ("", "int"),
    0x419420: ("iiiO", "cell"),        # track cell (by address in ours too)
    0x40FE00: ("ppi", None),           # AI freeze
    0x40FEB0: ("ppi", None),
    0x48EEF0: ("p", None),
}


class Stubbed(E.Machine):
    def __init__(self, exe, elf, stubs):
        super().__init__(exe, elf)
        self.stubs = stubs
        self.log = []
        self.returns = {}
        self.uc.mem_map(SCRATCH, SCRATCH_SIZE)
        for address in stubs:
            self.uc.hook_add(UC_HOOK_CODE, self._stub, None, address, address)

    def _stub(self, uc, address, size, _):
        kinds, ret = self.stubs[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (len(kinds) + 1), bytes(uc.mem_read(esp, 4 * (len(kinds) + 1))))
        args = []
        for k, v in zip(kinds, words[1:]):
            if k == "b":
                v &= 0xFF
            elif k == "h":
                v &= 0xFFFF
            elif k == "p" and STACK_LO <= v < STACK_HI:
                v = ("stack", bytes(uc.mem_read(v, 12)))
            elif k == "q" and STACK_LO <= v < STACK_HI:
                v = ("stack", bytes(uc.mem_read(v, 4)))
            elif k == "D" and STACK_LO <= v < STACK_HI:
                # a launch record on the stack: the fields the weapons fill
                data = bytes(uc.mem_read(v, 0x60))
                if words[5] == 4:
                    v = ("stack", data[:4])
                elif words[3] == 1:
                    v = ("stack", data[:0x42] + data[0x44:0x45] + data[0x48:0x58] + data[0x5e:0x60])
                else:
                    v = ("stack", data[:0x3c] + data[0x44:0x45] + data[0x57:0x58])
            elif k == "S":
                v = (bytes(uc.mem_read(v + 8, 0x18)), bytes(uc.mem_read(v + 0x40, 4)))
            elif k == "O":
                out = v  # an output: not logged
                v = None
            args.append(v)
        self.log.append((address, tuple(args)))
        values = self.returns.get(ret, [0])
        value = values[len(self.log) % len(values)]
        if ret == "cell":
            uc.mem_write(out, struct.pack("<I", value >> 32))  # off track
            value &= 0xFFFFFFFF
        if ret == "sweep":
            uc.mem_write(out + 0x1C, struct.pack("<I", value >> 32))
            value &= 0xFFFFFFFF
        uc.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def s16(v):
    return struct.pack("<h", v)


class World:
    """Memory images of the compared regions, edited by the case."""

    def __init__(self, m, rng):
        self.rng = rng
        self.mem = {base: bytearray(m.pristine[base]) for base, _ in REGIONS}
        self.stack = rng.randbytes(STACK_HI - STACK_LO)  # uninitialised locals must not matter

    def _find(self, address, size):
        for base, length in REGIONS:
            if base <= address and address + size <= base + length:
                return self.mem[base], address - base
        raise SystemExit(f"{address:#x} not in a region")

    def write(self, address, data):
        buf, off = self._find(address, len(data))
        buf[off:off + len(data)] = data

    def read(self, address, size):
        buf, off = self._find(address, size)
        return bytes(buf[off:off + size])

    def u32(self, address, v):
        self.write(address, struct.pack("<I", v & 0xFFFFFFFF))

    def u16(self, address, v):
        self.write(address, struct.pack("<H", v & 0xFFFF))

    def u8(self, address, v):
        self.write(address, bytes([v & 0xFF]))

    def random(self, address, size):
        self.write(address, bytes(self.rng.getrandbits(8) for _ in range(size)))


def small(rng, n):
    return rng.randrange(n) if rng.randrange(8) else rng.getrandbits(16)


def base_world(m, rng):
    w = World(m, rng)
    r = rng
    w.random(SCRATCH, SCRATCH_SIZE)
    count = r.randrange(1, 7)
    race = bytearray(r.getrandbits(8) for _ in range(0xB00))
    race[0x54D] = count
    race[0x551] = r.choice([0, 1, 2, 3, 5, 6, 7, 8, r.getrandbits(8)])
    race[0x585] = r.choice([0, 0, 0, 1])
    w.write(RACE, bytes(race))
    w.u32(RACE_PTR, RACE)
    for i in range(8):
        slot = SLOTS + i * SLOT_SIZE
        w.random(slot, SLOT_SIZE)
        car = CARS + (i % 6) * CAR_SIZE
        w.u32(slot, car)
        if i < 6:
            w.u16(car + 0x64E, i)
            w.u8(car + 0x70C, r.choice([8, 10, 8, 10, r.getrandbits(8)]))
            w.u32(car + 0x6AC, r.choice([0xFFFFFFFF, MESH + 0x800 + i * 0x84]))
            for k in (0x34, 0x38, 0x3C):
                w.u32(car + k, r.randrange(-0x400, 0x400) * 0x10000 + r.getrandbits(16))
            w.u8(car + 0x67C, r.choice([0xFF, 0, 1, 2, 3, 4, 5, 6, 7, 0x41, 0x60, r.getrandbits(8)]))
            w.u8(car + 0x67D, r.choice([0xFF, 0, 1, 2, 3, 4, 5, 6, 7, 0x45, 0x60, r.getrandbits(8)]))
    for name in ("seed", "fx", "ai"):
        pass
    w.u32(0x4B11DC, r.getrandbits(31))
    w.u32(0x4B11D8, r.getrandbits(31))
    w.u32(0x4A7148, r.getrandbits(31))
    w.u32(0x4A714C, r.choice([0, 1, 2, 3]))
    w.u32(0x6C0A50, r.getrandbits(32) if r.randrange(4) == 0 else r.randrange(0x1000))
    return w


def run(m, w, address, args, returns):
    for base, length in REGIONS:
        m.write(base, bytes(w.mem[base]))
    m.write(STACK_LO, w.stack)
    m.log = []
    m.returns = returns
    r = m.call(address, args)
    mem = [m.read(base, length) for base, length in REGIONS]
    gte = 0x6CCBE0 - REGIONS[2][0]  # GTE scratch: leftover stack in its padding, not compared
    mem[2] = mem[2][:gte] + bytes(0x104) + mem[2][gte + 0x104:]
    # a fired homing missile's +0x1b2 (never read) gets two bytes of the
    # original's uninitialised launch record
    scratch = bytearray(mem[3])
    for i in range(6):
        o = PROJ - SCRATCH + i * PROJ_SIZE + 0x1B2
        scratch[o:o + 2] = b"\0\0"
    mem[3] = bytes(scratch)
    return r, mem, m.log


# ---- case builders: (world, args, returns) ------------------------------

def default_returns(rng):
    return {"byte": [rng.getrandbits(8) for _ in range(5)], "int": [rng.getrandbits(32) for _ in range(5)],
            "flag": [rng.choice([0, 1]) for _ in range(5)], "particle": [PARTICLE],
            "index": [rng.randrange(0x20) for _ in range(5)],
            "sweep": [rng.randrange(0, 0x30000) | rng.choice([0, 1]) << 32 for _ in range(5)],
            "cell": [rng.choice(CELLS) | (rng.randrange(8) == 0) << 32 for _ in range(7)]}


def case_pickups(m, rng):
    w = base_world(m, rng)
    n = rng.choice([0, 1, 3, 12, rng.randrange(13)])
    w.u32(0x6B8CE4, n)
    w.u32(0x6B8CE0, MISC)
    for i in range(12):
        p = PICKUPS + i * 0x200
        w.u32(MISC + i * 4, p)
        w.u16(p + 0x182, rng.choice([0x81, 0x82, 0x83, 0x84, 0x01, 0x80, 0x181, 0xff81, rng.getrandbits(16)]))
        w.u16(p + 0x180, rng.choice([0, 1, 2, 100, 0xFFFF, rng.getrandbits(16)]))
        if rng.randrange(3):
            car = CARS + rng.randrange(6) * CAR_SIZE
            spread = rng.choice([0x30, 0x30, 0x90])
            for k in (0x34, 0x38, 0x3C):
                cv = struct.unpack("<i", w.read(car + k, 4))[0]
                w.u32(p + k, cv + rng.randrange(-spread, spread) * 0x10000)
            if rng.randrange(2):
                w.u8(car + 0x67C + rng.randrange(2), 0xFF)
    for i in range(6):
        w.u32(CARS + i * CAR_SIZE + 0x68, rng.choice([0, rng.randrange(-0x400000, 0x400000), rng.getrandbits(32)]))
    dropper_world(w, rng)
    return w, [], default_returns(rng)


def dropper_world(w, rng):
    nd = rng.choice([0, 1, 2, 4, 6])
    markers = MISC + 0x100
    w.u32(0x6C1EB0, markers)
    w.u8(markers + 0xF7, nd)
    w.u32(markers + 0xA8, MISC + 0x400)
    w.u32(0x6BFB28, MISC + 0x600)
    w.u32(0x6BFB34, MISC + 0x800)
    for i in range(6):
        d = DROPPERS + i * 0x200
        w.u32(0x6CD420 + i * 4, d)
        w.u32(d + 0x180, rng.choice([0, 1, 1, 2, 2, 3, rng.getrandbits(32)]))
        w.u16(d + 0x184, rng.choice([0, 1, 2, 9, 10, 11, rng.randrange(-3, 15), rng.getrandbits(16)]))
        w.u16(d + 0x186, rng.randrange(12))
        w.u16(d + 0x188, rng.choice([0, 1, 2, 3, 0xFFFF, rng.getrandbits(16)]))
        w.u8(d + 0x13F, rng.randrange(0x40))
    for i in range(12):
        w.u8(MISC + 0x600 + i * 0xC + 6, rng.randrange(0x10))
    w.u16(0x6CD450, rng.choice([0, 1, 2, 5, rng.randrange(0x18)]))
    w.u32(0x6B9B80, MISC + 0x2000)
    w.u32(0x6B9B84, rng.randrange(0x100) | rng.getrandbits(16) << 16)
    w.u32(0x6B8CE0, MISC)
    for i in range(12):
        w.u32(MISC + i * 4, PICKUPS + i * 0x200)


def case_droppers(m, rng):
    w = base_world(m, rng)
    dropper_world(w, rng)
    return w, [], default_returns(rng)


def case_drop(m, rng):
    w, _, ret = case_droppers(m, rng)
    return w, [DROPPERS + rng.randrange(6) * 0x200], ret


def case_slot(m, rng):
    w = base_world(m, rng)
    return w, [SLOTS + rng.randrange(6) * SLOT_SIZE], default_returns(rng)


def case_boost(m, rng):
    w = base_world(m, rng)
    i = rng.randrange(6)
    car = CARS + i * CAR_SIZE
    w.u16(car + 0x690, rng.choice([0, 0, 1, 2, rng.getrandbits(16)]))
    w.u16(car + 0x692, rng.choice([0, 1, 2, 3, 14, 15, 0x5A, 0x96, rng.getrandbits(16)]))
    w.u32(car + 0x698, rng.choice([0, 0x80000, 0xA0000, 0xC0000, 0xD0000, rng.getrandbits(32)]))
    return w, [SLOTS + i * SLOT_SIZE, rng.choice([0, 1, 2, 5, -1, rng.getrandbits(32)]),
               rng.choice([0, 0, 1, 3, rng.getrandbits(32)])], default_returns(rng)


def warp_world(w, rng):
    w.u32(0x5D9B20, rng.choice([0, 1, 2, 4, 3, rng.getrandbits(32)]))
    w.u32(0x5D9B24, CARS + rng.randrange(6) * CAR_SIZE)
    w.u32(0x5D9B28, rng.choice([1, 1, 1, 2, 0x1A, 0x19, 100, 10, 0x14, rng.getrandbits(32)]))
    w.u32(0x5D9B2C, rng.getrandbits(32))


def case_warp_start(m, rng):
    w = base_world(m, rng)
    warp_world(w, rng)
    return w, [SLOTS + rng.randrange(6) * SLOT_SIZE, rng.choice([0, 5, 9, 10, 11, 0x6e, rng.getrandbits(32)])], \
        default_returns(rng)


def case_warp(m, rng):
    w = base_world(m, rng)
    warp_world(w, rng)
    return w, [], default_returns(rng)


def case_car(m, rng):
    w = base_world(m, rng)
    return w, [CARS + rng.randrange(6) * CAR_SIZE], default_returns(rng)


def case_hop(m, rng):
    w = base_world(m, rng)
    car = CARS + rng.randrange(6) * CAR_SIZE
    w.u32(car + 0x2284, rng.choice([0, 0, 1, rng.getrandbits(32)]))
    w.u32(car + 0x654, rng.choice([0, 0x200000, 0x200001, rng.getrandbits(32)]))
    for k in range(4):
        w.u8(car + 0x324 + k * 0xC4, rng.choice([0, 0, 1, rng.getrandbits(8)]))
    return w, [car], default_returns(rng)


def ripple_world(w, rng):
    n = rng.choice([0, 1, 5, 20, rng.randrange(64)])
    mesh = MESH
    w.u32(0x4D44A4, mesh)
    w.u16(mesh + 4, n)
    w.u32(0x4D44A0, mesh + 0x100)
    for i in range(64):
        k = rng.randrange(8)
        w.write(mesh + 0x100 + i * 8, bytes([k] + [rng.randrange(max(n, 1)) for _ in range(7)]))
    w.u8(0x6CCD84, rng.getrandbits(8))
    for i in range(6):
        r = MESH + 0x800 + i * 0x84
        w.u8(r, rng.choice([0, 1, 2]))
        w.u8(r + 1, rng.choice([0, 1, 3, 5, 10, 0xFF, rng.getrandbits(8)]))
        w.u8(r + 3, rng.choice([0, 1, 5, 0x80, rng.getrandbits(8)]))
        w.u32(r + 0x70, MESH + 0x1000 + i * 0x100)


def case_ripple(m, rng):
    w = base_world(m, rng)
    ripple_world(w, rng)
    i = rng.randrange(6)
    w.u32(CARS + i * CAR_SIZE + 0x6AC, MESH + 0x800 + i * 0x84)
    return w, [SLOTS + i * SLOT_SIZE], default_returns(rng)


def case_shield(m, rng):
    w = base_world(m, rng)
    ripple_world(w, rng)
    i = rng.randrange(6)
    w.u32(CARS + i * CAR_SIZE + 0x6AC, rng.choice([0xFFFFFFFF, MESH + 0x800 + i * 0x84]))
    w.write(0x6002E0, bytes(rng.choice([0, 0, 1]) for _ in range(6 * 0x84)))
    ret = default_returns(rng)
    if rng.randrange(2):
        ret["flag"] = [0]  # (a network game with no free ripple returns; otherwise it writes to -1)
    else:
        w.write(0x6002E0, bytes(1 for _ in range(6 * 0x84)))
        ret["flag"] = [1]
    return w, [SLOTS + i * SLOT_SIZE], ret


def case_ripple_alloc(m, rng):
    w = base_world(m, rng)
    for i in range(6):
        w.u8(0x6002E0 + i * 0x84, rng.choice([0, 1, 1, 2]))
    return w, [SLOTS + rng.randrange(6) * SLOT_SIZE], default_returns(rng)


def track_world(w, rng):
    """Racing lines at *(0x6c1e30)+0x60 (0xc each) with 0x10-byte nodes."""
    w.u32(0x6C1E30, TRACK)
    w.u32(TRACK + 0x60, TRACK + 0x200)
    for i in range(8):
        w.u16(TRACK + 0x200 + i * 0xC, rng.randrange(2, 9))
        w.u32(TRACK + 0x200 + i * 0xC + 8, TRACK + 0x400 + i * 0x100)
        for j in range(16):
            node = TRACK + 0x400 + i * 0x100 + j * 0x10
            w.write(node, b"".join(s16(rng.randrange(-0x400, 0x400)) for _ in range(3)))


def case_markers(m, rng):
    w = base_world(m, rng)
    track_world(w, rng)
    markers = MISC + 0x100
    w.u32(0x6C1EB0, markers)
    n = rng.choice([0, 1, 4, 8])
    w.u8(markers + 0xF6, n)
    w.u32(markers + 0xF8, MISC + 0x300)
    i = rng.randrange(6)
    car = CARS + i * CAR_SIZE
    for k in range(8):
        e = MISC + 0x300 + k * 8
        w.write(e, struct.pack("<HHBB", rng.randrange(8), rng.randrange(16), rng.choice([4, 4, 2]),
                               rng.choice([1, 1, 0])))
        if rng.randrange(2):
            line, node = struct.unpack("<HH", w.read(e, 4))
            nd = TRACK + 0x400 + line * 0x100 + node * 0x10
            for c, off in enumerate((0x34, 0x38, 0x3C)):
                v = struct.unpack("<h", w.read(nd + c * 2, 2))[0]
                w.u32(car + off, (v + rng.randrange(-0x600, 0x600)) * 0x10000 + rng.getrandbits(16))
    return w, [SLOTS + i * SLOT_SIZE], default_returns(rng)


def case_hit_timer(m, rng):
    w = base_world(m, rng)
    car = CARS + rng.randrange(6) * CAR_SIZE
    w.u8(car + 0x689, rng.choice([0, 1, 2, 0xFF]))
    return w, [car], default_returns(rng)


def case_slot_spent(m, rng):
    w = base_world(m, rng)
    return w, [CARS + rng.randrange(6) * CAR_SIZE, rng.choice([0, 1, 3, 5, 7, rng.getrandbits(32)])], \
        default_returns(rng)


def case_body_by_id(m, rng):
    w = base_world(m, rng)
    w.u32(0x4D1088, rng.randrange(0x10))
    for i in range(0x10):
        b = MISC + 0x1000 + i * 0x120
        w.u32(0x4D0880 + i * 4, b)
        w.u16(b + 0x10E, rng.choice([i, i, rng.randrange(0x10)]))
    return w, [rng.randrange(0x14) | rng.getrandbits(16) << 16], default_returns(rng)


def case_shape_add(m, rng):
    w = base_world(m, rng)
    w.u32(0x6B9B80, MISC + 0x2000)
    w.u32(0x6B9B84, rng.randrange(0x100) | rng.getrandbits(16) << 16)
    return w, [DROPPERS + rng.randrange(6) * 0x200], default_returns(rng)


def case_path_start(m, rng):
    w = base_world(m, rng)
    w.u32(0x6BFB34, MISC + 0x800)
    return w, [DROPPERS + rng.randrange(6) * 0x200, rng.randrange(0x20), rng.getrandbits(32)], \
        default_returns(rng)


def case_beam_add(m, rng):
    w = base_world(m, rng)
    for i in range(4):
        w.u16(0x6C8100 + i * 0x18, rng.choice([0, 1, 5]))
    return w, [MISC + 0x10, MISC + 0x20, rng.getrandbits(32)], default_returns(rng)


def case_beam_move(m, rng):
    w = base_world(m, rng)
    return w, [rng.choice([0, MISC + 0x10]), rng.choice([0, MISC + 0x20]), rng.choice([-1, 0, 1, 2, 3, -5])], \
        default_returns(rng)


def case_beam_remove(m, rng):
    w = base_world(m, rng)
    return w, [rng.choice([-1, 0, 1, 2, 3, -5])], default_returns(rng)


def case_drop_push(m, rng):
    w = base_world(m, rng)
    w.u16(0x6CD450, rng.randrange(0x18))
    return w, [rng.getrandbits(32)], default_returns(rng)


LINES, GROUPS, NODES = TRACK + 0x200, TRACK + 0x400, TRACK + 0xC00
PROJ_SIZE = 0x2B00


def line_world(w, rng):
    """8 racing lines in 4..7 groups, linked; group neighbour masks at
    track +0x94 such that every group reaches every other in a few steps."""
    w.u32(0x6C1E30, TRACK)
    w.u32(TRACK + 0x60, LINES)
    w.u32(TRACK + 0x94, GROUPS)
    groups = rng.randrange(2, 8)
    counts = []
    for i in range(8):
        count = rng.randrange(2, 9)
        counts.append(count)
        w.write(LINES + i * 0xC, struct.pack("<HBBBBH", count, rng.choice([0, 1]), 0, rng.choice([0, 0, 1]),
                                             rng.randrange(groups), 0))
        w.u32(LINES + i * 0xC + 8, NODES + i * 0x100)
    for i in range(8):
        for j in range(16):
            node = NODES + i * 0x100 + j * 0x10
            w.write(node, b"".join(s16(rng.randrange(-0x400, 0x400)) for _ in range(3)))
            w.u8(node + 7, rng.getrandbits(8))
            w.u32(node + 8, rng.getrandbits(32))
            link = rng.getrandbits(4) << 28
            if rng.randrange(3):
                a = rng.randrange(8)
                link |= a | rng.randrange(counts[a]) << 5
            if rng.randrange(3):
                b = rng.randrange(8)
                link |= b << 14 | rng.randrange(counts[b]) << 19
            w.u32(node + 0xC, link)
    # neighbour masks: a ring plus random chords; groups past `groups` link to 0
    for g in range(64):
        mask = 0
        if g < groups:
            mask |= 1 << ((g + 1) % groups) | 1 << ((g - 1) % groups)
            for h in range(groups):
                if h != g and rng.randrange(3) == 0:
                    mask |= 1 << h
        else:
            mask = 1
        w.write(GROUPS + g * 8, struct.pack("<Q", mask))
    return counts


def proj_world(w, rng, counts):
    for i in range(6):
        o = PROJ + i * PROJ_SIZE
        w.u32(o + 0x184, rng.choice([0, 1, 2, 0, 1, 2, rng.getrandbits(32)]))
        w.u32(o + 0x188, rng.choice([0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 3, rng.getrandbits(32)]))
        w.u32(o + 0x180, rng.choice([0, 1, 2, 10, 0x2c, 0x95, rng.randrange(-5, 0x100), rng.getrandbits(32)]))
        w.u32(o + 0x18C, rng.choice([CARS + rng.randrange(6) * CAR_SIZE, PROJ + rng.randrange(6) * PROJ_SIZE]))
        w.u32(o + 0x190, rng.choice([CARS + rng.randrange(6) * CAR_SIZE, 0]))
        for k in (0x184, 0x188):
            pass
        for k in range(3):
            w.u32(o + 0x184 + 0 if False else o + 0x1E0 + k * 4, 0)
        w.u16(o + 0x23DE, rng.choice([0, 1, 2, 5, 0x1F, 0x20, rng.randrange(-3, 0x30)]))
        w.u8(o + 0x23DC, rng.choice([0, 1, 0x1F, rng.randrange(0x20), rng.getrandbits(8)]))
        w.u8(o + 0x23DD, rng.getrandbits(8))
        line = rng.randrange(8)
        w.u32(o + 0x23FC, LINES + line * 0xC)
        w.u16(o + 0x2400, rng.randrange(counts[line]))
        line2 = rng.randrange(8)
        w.u32(o + 0x2404, LINES + line2 * 0xC)
        w.u16(o + 0x2408, rng.randrange(counts[line2]))
        w.u16(o + 0x2402, rng.choice([1, 0xFFFF]))
        route = [rng.randrange(8) for _ in range(rng.randrange(4))] + [0xFF]
        w.write(o + 0x23EC, bytes(route))
        w.u32(o + 0x23E0, rng.choice([0, 1, 2, 3]))
        for k in (0x34, 0x38, 0x3C):
            w.u32(o + k, rng.randrange(-0x400, 0x400) * 0x10000 + rng.getrandbits(16))
        for k in (0x40, 0x44, 0x48, 0x68, 0x6C, 0x70, 0x1A0, 0x1A4, 0x1A8):
            w.u32(o + k, rng.choice([rng.randrange(-0x100000, 0x100000), rng.getrandbits(32)]))
        w.u32(o + 0x194, rng.choice([0, 1, 2, 0x10, 3, rng.getrandbits(32)]))
        w.u32(o + 0xA8, rng.choice([0x10000, 0x8000, rng.randrange(1, 0x40000)]))
        w.u32(o + 0xAC, rng.choice([0x10000, 0x20000, rng.randrange(1, 0x40000)]))
        w.u32(o + 0x198, rng.choice([1, 2, 5, 9, rng.randrange(10)]))
    # drillers: a head with three parts (each part's +0x18c is the head)
    head = PROJ + rng.randrange(6) * PROJ_SIZE
    for k in range(3):
        part = PROJ + rng.randrange(6) * PROJ_SIZE
        w.u32(head + 0x184 + k * 4, part)
    for i in range(0x18):
        w.u32(0x6B9CE0 + i * 4, PROJ + rng.randrange(6) * PROJ_SIZE)
    for i in range(6):
        slot = SLOTS + i * SLOT_SIZE
        w.u8(slot + 0x2D, rng.choice([0, 0, 1, 2]))
        w.u32(slot + 0x64, MISC + 0x3000 + i * 0x10)
        w.u8(MISC + 0x3000 + i * 0x10 + 5, rng.randrange(8))
    w.u32(0x5D9B20, rng.choice([0, 0, 1, 2]))


def case_proj(m, rng):
    w = base_world(m, rng)
    counts = line_world(w, rng)
    proj_world(w, rng, counts)
    return w, [], default_returns(rng)


def case_proj_obj(m, rng):
    w, _, ret = case_proj(m, rng)
    return w, [PROJ + rng.randrange(6) * PROJ_SIZE], ret


def case_proj_mode(m, rng):
    w, _, ret = case_proj(m, rng)
    return w, [PROJ + rng.randrange(6) * PROJ_SIZE, rng.choice([0, 1, 2, 3, rng.getrandbits(32)])], ret


def case_node_offset(m, rng):
    w, _, ret = case_proj(m, rng)
    o = PROJ + rng.randrange(6) * PROJ_SIZE
    return w, [NODES + rng.randrange(0x800) // 0x10 * 0x10, o + 0x34, MISC + 0x40, rng.choice([0, 1, 2])], ret


def case_push(m, rng):
    w, _, ret = case_proj(m, rng)
    o = PROJ + rng.randrange(6) * PROJ_SIZE
    for k in range(3):
        w.u32(MISC + 0x40 + k * 4, rng.choice([rng.randrange(-0x400, 0x400), rng.getrandbits(32)]))
    return w, [o, MISC + 0x40, rng.choice([0, NODES + rng.randrange(0x80) * 0x10]),
               rng.choice([0, 0, 1, rng.randrange(0x10000)])], ret


def case_puff(m, rng):
    w = base_world(m, rng)
    return w, [CARS + 0x34], default_returns(rng)


def case_bits(m, rng):
    return base_world(m, rng), [rng.getrandbits(32), rng.getrandbits(32)], default_returns(rng)


SECTIONS, SECTION_TABLE, LINKS = TRACK + 0x2000, TRACK + 0x1F00, TRACK + 0x2800
CELLS = [MISC + 0x3800 + i * 8 for i in range(4)]
SECTION_VERTS = TRACK + 0x3000


def section_world(w, rng):
    """4 track sections (+8 width - 1, +0xc last row, +0x20 vertices, +0x50
    polygons) whose ends often meet; the cell links between them."""
    w.u32(0x6BFAC0, SECTION_TABLE)
    w.u32(TRACK, SECTION_TABLE)
    w.u32(0x6BFB40, LINKS)
    w.u32(TRACK + 0x80, LINKS)
    w.u32(TRACK + 0x1C, MISC + 0x3C00)
    for i in range(0x40):
        w.u32(MISC + 0x3C00 + i * 4, rng.getrandbits(32))
    shapes = []
    for i in range(4):
        sec = SECTIONS + i * 0x60
        width, rows = rng.randrange(2, 5), rng.randrange(2, 7)
        shapes.append((width, rows))
        w.u32(SECTION_TABLE + i * 4, sec)
        w.u32(sec + 8, width - 1)
        w.u32(sec + 0xC, rows - 1)
        w.u32(sec + 0x20, SECTION_VERTS + i * 0x400)
        w.u32(sec + 0x50, SECTION_VERTS + 0x1000 + i * 0x400)
        for k in range(0x80):
            w.write(SECTION_VERTS + i * 0x400 + k * 8, b"".join(s16(rng.randrange(-0x200, 0x200)) for _ in range(3)) +
                    struct.pack("<H", rng.getrandbits(16)))
            w.write(SECTION_VERTS + 0x1000 + i * 0x400 + k * 8, struct.pack("<HHI", 0, rng.randrange(0x40), 0))
    # make section i's first row a near copy of section i-1's last
    for i in range(1, 4):
        if rng.randrange(3):
            (pw, pr), (cw, cr) = shapes[i - 1], shapes[i]
            for k in range(min(pw, cw) + 1):
                src = SECTION_VERTS + (i - 1) * 0x400 + ((pr - 1) * pw + k) * 8
                dst = SECTION_VERTS + i * 0x400 + k * 8
                w.write(dst, b"".join(s16(c + rng.randrange(-0x60, 0x60)) for c in struct.unpack("<3h", w.read(src, 6))))
    for c, cell in enumerate(CELLS):
        w.u32(cell + 4, (c * 4) << 16)
    for e in range(16):
        sec = rng.randrange(4)
        width, rows = shapes[sec]
        first = rng.randrange(rows)
        word0 = first | rng.randrange(width) << 10
        word1 = (rng.randrange(4) << 9 if e % 4 == 0 else 0) | sec
        word2 = rng.randrange(4) << 8 | rng.randrange(1, 4) << 12
        w.write(LINKS + e * 6, struct.pack("<HHH", word0, word1, word2))
    return shapes


def freeze_world(w, rng, shapes):
    w.u32(0x6C3080, MISC + 0x1000)
    w.random(MISC + 0x1000, 0x800)
    for i in range(0x40):
        if rng.randrange(2):
            w.u16(MISC + 0x1000 + rng.randrange(0x400) * 2, 0xFFFF)
    w.u32(TRACK + 0x30, MISC + 0x2400)
    w.u16(TRACK + 0x5C, rng.choice([0x3C0, 0x3CE, 0x3D0]))
    w.u16(0x6C3E94, rng.choice([0x3CF, 0x3C5, 0x3C0]))
    w.u32(0x6C30A0, rng.choice([1, 2, 3, 4, 0, 5]))
    slot = SLOTS + rng.randrange(6) * SLOT_SIZE
    w.u32(0x6C30A4, slot)
    w.u32(0x6C30A8, rng.choice([0, 1, 0x95, 0x96, 0x97, 0xAE, 0xAF, 0xC8, 0xC9, rng.randrange(0x200)]))
    length = rng.randrange(1, 15)
    w.u32(0x6C30AC, rng.choice([length, 0xD, 0xE]))
    w.u32(0x6C30B0, rng.choice([rng.randrange(length + 1), length - 1, 4]))
    w.u32(0x6C30B4, rng.choice([1, 0xFFFFFFFF]))
    car = struct.unpack("<I", w.read(slot, 4))[0]
    w.u32(0x6C30B8, car + 0x11C)
    sec = rng.randrange(4)
    width, rows = shapes[sec]
    w.write(car + 0x11C, struct.pack("<Bbh", sec, rng.randrange(width), rng.randrange(rows)))
    w.u8(0x4BEB24, rng.choice([0x7F, 3, 0x10]))
    for p in range(2):
        n = rng.randrange(0x10)
        w.u16(0x6C30BC + p * 2, n)
        for k in range(0x14):
            s_ = rng.randrange(4)
            w.write(0x6C30C0 + p * 0x50 + k * 4, struct.pack("<hBB", rng.randrange(shapes[s_][1]), s_, 1))
    nsaved = rng.randrange(6)
    w.u32(0x6C3E90, nsaved)
    for k in range(8):
        s_ = rng.randrange(4)
        rec = 0x6C31D0 + k * 0x88
        w.write(rec, struct.pack("<hBB", rng.randrange(shapes[s_][1]), s_, 1))
        w.u32(rec + 4, SECTION_VERTS + s_ * 0x400 + rng.randrange(8) * 8)
    for k in range(2):
        w.u16(0x6C3E96 + k * 2, rng.choice([0xFFFF, rng.randrange(4)]))
    w.u32(0x6C3EAC, rng.randrange(6))


def case_freeze(m, rng):
    w = base_world(m, rng)
    line_world(w, rng)
    shapes = section_world(w, rng)
    freeze_world(w, rng, shapes)
    for i in range(4):
        w.u16(0x6C8100 + i * 0x18, rng.choice([0, 0, 1]))
    return w, [], default_returns(rng)


def case_freeze_row(m, rng):
    w, _, ret = case_freeze(m, rng)
    k = rng.randrange(0x14)
    return w, [rng.choice([0x6C30C0 + k * 4, 0x6C31D0 + rng.randrange(6) * 0x88])], ret


def case_freeze_vertex(m, rng):
    w, _, ret = case_freeze(m, rng)
    return w, [SECTION_VERTS + rng.randrange(0x80) * 8, rng.getrandbits(16)], ret


def case_freeze_next(m, rng):
    w, _, ret = case_freeze(m, rng)
    return w, [rng.randrange(4), rng.choice([-1, 1])], ret


def case_fire(m, rng):
    w = base_world(m, rng)
    counts = line_world(w, rng)
    shapes = section_world(w, rng)
    proj_world(w, rng, counts)
    freeze_world(w, rng, shapes)
    ripple_world(w, rng)
    warp_world(w, rng)
    w.write(0x6002E0, bytes(rng.choice([0, 0, 1]) for _ in range(6 * 0x84)))
    for i in range(2):
        w.u32(0x6B9D38 + i * 4, PROJ + rng.randrange(6) * PROJ_SIZE)
    for i in range(6):
        w.u32(0x6B9CC0 + i * 4, PROJ + rng.randrange(6) * PROJ_SIZE)
    w.u32(0x5D1FC0, PROJ + rng.randrange(6) * PROJ_SIZE)
    w.u32(0x6CE740, MISC + 0x2800)
    for i in range(6):
        o = PROJ + i * PROJ_SIZE
        for k in range(0x25):
            w.u32(o + 0x1E0 + k * 4, PARTICLE + rng.randrange(8) * 0x20)
    w.u32(0x6BFDF4, CARS + rng.randrange(6) * CAR_SIZE)
    w.u32(0x6B9B80, MISC + 0x2000)
    w.u32(0x6B9B84, rng.randrange(0x40))
    w.u32(0x4D1088, rng.randrange(0x10))
    for i in range(0x10):
        b = MISC + 0x1800 + i * 0x40
        w.u32(0x4D0880 + i * 4, b)
        w.u16(b + 0x10E, rng.choice([i, i, rng.randrange(0x10)]))
        w.u32(b + 0x10, MISC + 0x2C00)
    i = rng.randrange(6)
    slot = SLOTS + i * SLOT_SIZE
    car = CARS + i * CAR_SIZE
    w.u8(slot + 0x7A, rng.choice([0, 1, 2]))
    w.u16(slot + 0x8C, rng.choice([0, 2, 4]))
    w.u32(slot + 0x80, rng.choice([MISC + 0x1800, CARS]))
    w.u32(slot + 0x84, MISC + 0x1840)
    w.u32(slot + 0x64, LINES + rng.randrange(8) * 0xC)
    w.u16(slot + 0x68, rng.randrange(2))
    sec = rng.randrange(4)
    width, rows = shapes[sec]
    w.write(car + 0x11C, struct.pack("<Bbh", sec, rng.randrange(width), rng.randrange(rows)))
    data = MISC + 0x3400
    w.random(data, 0x60)
    w.u8(data + 0x57, rng.choice([0, 1, 2, 3, 7, 9]))
    w.u16(data + 0x54, rng.choice([0, 2, 4]))
    w.u8(data + 0x56, rng.choice([0, 1, 2]))
    w.u16(data + 0x5E, rng.randrange(0x14))
    w.u16(data + 0x5C, rng.randrange(4))
    for k in range(3):
        w.u32(data + k * 4, rng.randrange(-0x400, 0x400) * 0x10000)
    local = rng.choice([1, 1, 0])
    return w, slot, local, data, i


def case_fire_kind(m, rng):
    w, slot, local, data, i = case_fire(m, rng)
    return w, [slot, local, rng.getrandbits(32), rng.choice([data, 0]), rng.choice([0, 1])], default_returns(rng)


def case_weapon_fire(m, rng):
    w, slot, local, data, i = case_fire(m, rng)
    car = CARS + i * CAR_SIZE
    which = rng.choice([0, 1])
    w.u8(car + 0x67C + which, rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 0xFF, 0x41, 8]))
    return w, [slot, which, local, rng.getrandbits(32), rng.choice([data, 0])], default_returns(rng)


def case_fire_buttons(m, rng):
    w, slot, local, data, i = case_fire(m, rng)
    car = CARS + i * CAR_SIZE
    w.u32(car + 0x704, rng.choice([0, 4, 8, 12, rng.getrandbits(32)]))
    w.u32(car + 0x700, rng.getrandbits(32))
    return w, [slot], default_returns(rng)


def case_spheres(m, rng):
    w, _, _, _, _ = case_fire(m, rng)
    src = MISC + 0x3500
    for k in range(4):
        w.write(src + k * 0x14, struct.pack("<iiiIH", rng.randrange(-0x800, 0x800), rng.randrange(-0x800, 0x800),
                                            rng.randrange(-0x800, 0x800), rng.getrandbits(16), rng.getrandbits(16)))
    body = PROJ + rng.randrange(6) * PROJ_SIZE
    w.u32(body, rng.choice([0x11, 0x8011, 3, rng.getrandbits(32)]))
    return w, [body, rng.choice([0, 1, 2, 4]), src, rng.getrandbits(32)], default_returns(rng)


def case_freezer_hit(m, rng):
    w = base_world(m, rng)
    for i in range(6):
        slot = SLOTS + i * SLOT_SIZE
        w.u8(slot + 0x2C, rng.randrange(3))
        w.u32(slot + 0x34, rng.randrange(0x10000))
        w.u8(CARS + i * CAR_SIZE + 0x11C, rng.randrange(4))
    return w, [SLOTS + rng.randrange(6) * SLOT_SIZE, rng.choice([1, -1, 0])], default_returns(rng)


def target_world(w, rng):
    shapes = section_world(w, rng)
    markers = MISC + 0x100
    w.u32(0x6C1EB0, markers)
    w.u32(markers + 0xEC, MISC + 0x500)
    n = rng.randrange(5)
    w.u32(0x5F72A4, n)
    for k in range(4):
        e = MISC + 0x500 + k * 0x14
        item, obj = MISC + 0x700 + k * 0x20, PROJ + rng.randrange(6) * PROJ_SIZE
        w.u32(e, item)
        w.u32(e + 4, obj)
        w.u32(item, rng.getrandbits(32) & 0x03FF03FF)
        w.u32(item + 4, rng.getrandbits(32) & 0x3FF)
        w.u32(item + 0xC, rng.getrandbits(32))
        w.u32(obj + 0x194, MISC + 0x900)
        w.u8(obj + 0x199, rng.getrandbits(8))
    for k in range(16):
        w.u32(MISC + 0x900 + k * 4, rng.choice([0x5000000, 0, rng.getrandbits(32)]))
    nc = rng.randrange(6)
    w.u32(0x5F72A0, nc)
    for k in range(6):
        w.u32(0x5F7220 + k * 8, CARS + rng.randrange(6) * CAR_SIZE)
        w.u32(0x5F7220 + k * 8 + 4, rng.choice([0, 1, 2]))
    for i in range(6):
        car = CARS + i * CAR_SIZE
        for k in (0x34, 0x38, 0x3C):
            w.u32(car + k, rng.randrange(-0x300, 0x300) * 0x10000 + rng.getrandbits(16))
        w.u32(car + 0xC, rng.choice(CELLS) | rng.getrandbits(1))
        if rng.randrange(3):
            w.u8(car + 0x67C + rng.randrange(2), 1)
        slot = SLOTS + i * SLOT_SIZE
        w.u16(slot + 0x7A, rng.choice([0, 1, 2]))
        w.u32(slot + 0x8C, rng.choice([0, 2, 4]))
        w.u32(slot + 0x80, rng.choice([MISC + 0x700, CARS + rng.randrange(6) * CAR_SIZE]))
        w.u32(slot + 0x84, PROJ)
        w.u32(slot + 0x88, rng.choice([0, 1000, 10000, rng.getrandbits(32)]))
        w.u16(slot + 0x78, rng.choice([1, 2, 6]))


def case_targets(m, rng):
    w = base_world(m, rng)
    target_world(w, rng)
    return w, [], default_returns(rng)


def case_target_slot(m, rng):
    w = base_world(m, rng)
    target_world(w, rng)
    return w, [SLOTS + rng.randrange(6) * SLOT_SIZE], default_returns(rng)


def case_target_visible(m, rng):
    w = base_world(m, rng)
    target_world(w, rng)
    i = rng.randrange(6)
    car = CARS + i * CAR_SIZE
    t = MISC + 0x3600
    w.write(t, b"".join(s16((struct.unpack("<i", w.read(car + k, 4))[0] >> 16) + rng.randrange(-0x400, 0x400))
                        for k in (0x34, 0x38, 0x3C)))
    w.u32(MISC + 0x3610, rng.choice([0xFFFFFFFF, 0, 1000, rng.randrange(0x10000)]))
    return w, [SLOTS + i * SLOT_SIZE, rng.choice([0, 1]), t, MISC + 0x3610], default_returns(rng)


def case_target_blocked(m, rng):
    w = base_world(m, rng)
    target_world(w, rng)
    n, p, t = MISC + 0x3600, MISC + 0x3610, MISC + 0x3620
    w.write(n, struct.pack("<3i", *[rng.randrange(-0x1000, 0x1000) for _ in range(3)]))
    w.write(p, struct.pack("<3i", *[rng.randrange(-0x300, 0x300) for _ in range(3)]))
    w.write(t, b"".join(s16(rng.randrange(-0x300, 0x300)) for _ in range(3)))
    return w, [rng.choice(CELLS), n, p, t], default_returns(rng)


def case_wheel_sparks(m, rng):
    w = base_world(m, rng)
    warp_world(w, rng)
    return w, [CARS + rng.randrange(6) * CAR_SIZE, MISC + 0x10, MISC + 0x20], default_returns(rng)


def case_wormhole_init(m, rng):
    w, _, _, _, _ = case_fire(m, rng)
    return w, [PROJ + rng.randrange(6) * PROJ_SIZE], default_returns(rng)


def case_top(m, rng):
    w = base_world(m, rng)
    counts = line_world(w, rng)
    shapes = section_world(w, rng)
    proj_world(w, rng, counts)
    freeze_world(w, rng, shapes)
    ripple_world(w, rng)
    warp_world(w, rng)
    dropper_world(w, rng)
    target_world(w, rng)
    w.u32(0x6B8CE4, rng.randrange(4))
    for i in range(6):
        car = CARS + i * CAR_SIZE
        w.u32(car + 0x704, rng.choice([0, 0, 0x20, rng.getrandbits(32)]))
        w.u16(car + 0x690, rng.choice([0, 1, 2]))
        w.u16(car + 0x692, rng.choice([0, 2, 0xd, 0xe, 0xf, 0x5a, 0x96]))
        w.u32(car + 0x698, rng.randrange(0x100000))
        w.u32(car + 0x69C, rng.randrange(0x100000))
    return w, [], default_returns(rng)


def case_debris(m, rng):
    w = base_world(m, rng)
    return w, [MISC + 0x10, MISC + 0x20, rng.getrandbits(32)], default_returns(rng)


def case_none(m, rng):
    return base_world(m, rng), [], default_returns(rng)


# name: (original, case builder, returns a value?)
TESTS = {
    "car_hit_timer_tick": (0x4906A0, case_hit_timer, False),
    "warp_palette_off": (0x47D7E0, case_none, False),
    "weapon_slot_spent": (0x467CC0, case_slot_spent, False),
    "slot_car_index": (0x45CA20, case_slot, True),
    "body_by_id": (0x460F80, case_body_by_id, True),
    "ripple_stop": (0x46E150, case_drop, False),
    "ripple_alloc": (0x46E0E0, case_ripple_alloc, True),
    "drop_queue_push": (0x426040, case_drop_push, False),
    "shape_list_add": (0x45C010, case_shape_add, False),
    "body_path_start": (0x462E30, case_path_start, False),
    "beam_add": (0x4344E0, case_beam_add, True),
    "beam_move": (0x434540, case_beam_move, False),
    "beam_remove": (0x4345A0, case_beam_remove, False),
    "pickup_drop": (0x4682B0, case_drop, False),
    "pickup_droppers_update": (0x425D70, case_droppers, False),
    "pickups_update": (0x4671F0, case_pickups, False),
    "boost_start": (0x47AA60, case_boost, True),
    "time_warp_start": (0x479890, case_warp_start, True),
    "warp_end_ai_burst": (0x424CB0, case_car, False),
    "time_warp_update": (0x479740, case_warp, False),
    "shield_start": (0x46D6E0, case_shield, True),
    "shield_ripple_update": (0x46D950, case_ripple, False),
    "car_hop_start": (0x40EEE0, case_hop, False),
    "player_markers_pass": (0x42A410, case_markers, False),
    "smoke_puff": (0x42B650, case_puff, False),
    "debris_spawn": (0x42B8D0, case_debris, False),
    "trail_update": (0x458A20, case_proj_mode, False),
    "homer_steer": (0x459770, case_proj_obj, False),
    "homer_launch": (0x4598F0, case_proj_obj, False),
    "homer_end": (0x459610, case_proj_mode, False),
    "homer_update": (0x458810, case_proj_obj, False),
    "bit_count64": (0x43F360, case_bits, True),
    "leader_route": (0x43F0B0, case_proj_obj, True),
    "leader_node_offset": (0x43F9C0, case_node_offset, False),
    "leader_push": (0x43F790, case_push, False),
    "leader_advance": (0x43F390, case_proj_mode, True),
    "leader_update": (0x43EBC0, case_proj_obj, False),
    "driller_follow_ground": (0x424B90, case_proj_obj, False),
    "driller_head_update": (0x424380, case_proj_obj, False),
    "driller_update": (0x423FF0, case_proj_obj, False),
    "projectiles_update": (0x458710, case_proj, False),
    "body_spheres_set": (0x419150, case_spheres, False),
    "leader_fire": (0x43E880, case_fire_kind, True),
    "homer_fire": (0x459050, case_fire_kind, True),
    "driller_fire": (0x424550, case_fire_kind, True),
    "wormhole_init": (0x48EDA0, case_wormhole_init, False),
    "wormhole_fire": (0x48D920, case_fire_kind, True),
    "freezer_hit_cars": (0x410160, case_freezer_hit, False),
    "freezer_fire": (0x43A910, case_fire_kind, True),
    "weapon_fire": (0x467900, case_weapon_fire, True),
    "car_fire_buttons": (0x47B0E0, case_fire_buttons, False),
    "wheel_sparks": (0x42B4D0, case_wheel_sparks, False),
    "freeze_next_section": (0x43B380, case_freeze_next, True),
    "freeze_vertex_colour": (0x43B2A0, case_freeze_vertex, False),
    "freeze_row_toggle": (0x43B190, case_freeze_row, False),
    "freezer_update": (0x43ABA0, case_freeze, False),
    "target_blocked": (0x478BB0, case_target_blocked, True),
    "target_visible": (0x4789B0, case_target_visible, True),
    "homer_choose_target": (0x478530, case_target_slot, False),
    "homers_choose_targets": (0x478470, case_targets, False),
    "weapons_update": (0x47AB40, case_top, False),
}


def describe(results, regions=REGIONS):
    (r1, m1, l1), (r2, m2, l2) = results
    out = [f"original {r1} ours {r2}"]
    for (base, _), a, b in zip(regions, m1, m2):
        if a != b:
            first = next(i for i in range(len(a)) if a[i] != b[i])
            out.append(f"memory differs at {base + first:#x}: {a[first:first + 8].hex()} vs {b[first:first + 8].hex()}")
    if l1 != l2:
        for i, (x, y) in enumerate(zip(l1, l2)):
            if x != y:
                out.append(f"call {i}: {x} vs {y}")
                break
        else:
            out.append(f"{len(l1)} vs {len(l2)} calls")
    return "; ".join(out)


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    only = set(sys.argv[4:])
    m = Stubbed(exe, elf, STUBS)
    m.pristine = {base: m.read(base, length) for base, length in REGIONS}
    failed = 0
    for name, (address, build, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        rng = random.Random(name)
        bad = skipped = 0
        for _ in range(iterations):
            w, args, rets = build(m, rng)
            results = [run(m, w, address, args, rets)]
            if results[0][0][0] == "error" and "FETCH_UNMAPPED" in results[0][0][1]:
                # the original's own crash (the leader search smashing its
                # return address when no route is found within 6 groups): a
                # state the game never reaches
                skipped += 1
                continue
            results.append(run(m, w, m.symbols[name], args, rets))
            if results[0][0] == results[1][0] and results[0][0][0] in ("trap", "cut off"):
                continue  # both crash (divide by zero) or run away: nothing to compare
            if not returns:
                results = [(("ok",) if r[0] == "ok" else r, mem, log) for r, mem, log in results]
            if results[0] != results[1]:
                if bad < 3:
                    print(f"{name}: {describe(results)}")
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}" +
              (f" ({skipped} skipped: the original crashed)" if skipped else ""))
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
