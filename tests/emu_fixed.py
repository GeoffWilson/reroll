#!/usr/bin/env python3
"""Differential test of src/game/fixed.c against the original machine code.

Both run in the Unicorn CPU emulator: Rollcage.exe is mapped at its own
addresses, and fixed.c is compiled for i386 and mapped next to it
(build/emu_fixed.elf, made by `make emu-test`). Every routine is called with
the same random and edge-case inputs both ways; results, output vectors and
the scratch globals must match.

    tests/emu_fixed.py EXE build/emu_fixed.elf [iterations]
"""
import random
import struct
import subprocess
import sys

from unicorn import UC_ARCH_X86, UC_HOOK_INTR, UC_MODE_32, Uc, UcError
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EFLAGS, UC_X86_REG_ESP, UC_X86_REG_EIP

STACK, STACK_SIZE = 0x00100000, 0x10000
BUF = 0x00200000  # argument vectors
RETURN = 0x00300000  # a hlt to return to
SCRATCH = [(0x6cccdc, 4), (0x6ccce0, 4), (0x6cccc8, 12)]
SEED = 0x4B11DC
AI_SEED = 0x4A7148


def pe_sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    table = pe + 24 + opt_size
    for i in range(count):
        vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, table + i * 40 + 8)
        yield base + va, vsize, data[raw_ptr:raw_ptr + min(raw_size, vsize)]


def elf_load(path):
    """PT_LOAD segments and function symbols of our i386 ELF."""
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", data, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    segments = []
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz = struct.unpack_from("<IIIIII", data, phoff + i * phentsize)
        if p_type == 1:
            segments.append((p_vaddr, p_memsz, data[p_offset:p_offset + p_filesz]))
    out = subprocess.run(["llvm-nm", path], capture_output=True, text=True, check=True).stdout
    symbols = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    return segments, symbols


def page_range(address, size):
    start = address & ~0xFFF
    end = (address + size + 0xFFF) & ~0xFFF
    return start, end - start


class Machine:
    def __init__(self, exe, elf):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        mapped = []

        def map_bytes(address, size, content):
            start, length = page_range(address, size)
            for s, l in mapped:
                if start < s + l and s < start + length:
                    raise SystemExit(f"overlap at {address:#x}")
            self.uc.mem_map(start, length)
            mapped.append((start, length))
            self.uc.mem_write(address, content)

        for va, size, content in pe_sections(open(exe, "rb").read()):
            map_bytes(va, size, content)
        segments, self.symbols = elf_load(elf)
        for va, size, content in segments:
            map_bytes(va, size, content)
        map_bytes(STACK, STACK_SIZE, b"")
        map_bytes(BUF, 0x1000, b"")
        map_bytes(RETURN, 0x1000, b"\xf4")
        self.uc.hook_add(UC_HOOK_INTR, self._interrupt)
        self.trapped = False

    def _interrupt(self, uc, number, _):
        self.trapped = True  # #DE from idiv, as the original would raise
        uc.emu_stop()

    def call(self, address, args):
        esp = STACK + STACK_SIZE - 0x100
        frame = struct.pack("<I", RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args)
        self.uc.mem_write(esp, frame)
        self.uc.reg_write(UC_X86_REG_ESP, esp)
        self.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)  # DF clear, as the calling convention promises (a faulted run can leave it set)
        self.trapped = False
        try:
            self.uc.emu_start(address, RETURN, count=20000000)
        except UcError as e:
            return ("error", str(e))
        if self.trapped:
            return ("trap",)
        if self.uc.reg_read(UC_X86_REG_EIP) != RETURN:
            return ("cut off",)  # ran out of instructions
        return ("ok", self.uc.reg_read(UC_X86_REG_EAX))

    def read(self, address, size):
        return bytes(self.uc.mem_read(address, size))

    def write(self, address, content):
        self.uc.mem_write(address, content)


def s32(v):
    return struct.pack("<i", v)


def rand_int(rng):
    kind = rng.randrange(6)
    if kind == 0:
        return rng.choice([0, 1, -1, 2, -2, 0x10000, -0x10000, 0x7FFFFFFF, -0x80000000, 0x40, 0x3F, 0x1000])
    if kind == 1:
        return rng.randrange(-0x100, 0x100)
    if kind == 2:
        return rng.randrange(-0x10000, 0x10000)
    if kind == 3:
        return rng.randrange(-0x1000000, 0x1000000)
    return rng.randrange(-0x80000000, 0x80000000)


def rand_s16(rng):
    return rng.randrange(-0x8000, 0x8000)


# name: (original address, argument kinds, returns a value?)
# kinds: i = int, v = pointer to an input vector of 3 ints (in BUF),
#        q = pointer to a quaternion (4 ints), m = pointer to 9 ints,
#        o = pointer to an output vector, M = 9-int output;
#        for the sweep tests: S = contact slot, P = point near it,
#        h = int16 vertex near it, n = direction, O = 8-int result,
#        d = step time, e = small int;
#        A = 3 int16 angles, B = 32-byte output, R = 0x100-byte wheel record,
#        C = car (only its "upside down" byte 0x63f), T = car slot with 4 times,
#        U = unit vector, N = about the negation of the last U, K = car
#        (velocity only), z = null, Z = null and a random seed at 0x4b11dc,
#        L = racing line (with up to 8 nodes), k = node index, w = pointer to
#        a node index, Q = position near the nodes, Y = null and a random AI
#        seed at 0x4a7148
TESTS = {
    "fix_mul8": (0x4291A0, "ii", True),
    "fix_div16": (0x42DF70, "ii", True),
    "fix_div8": (0x444490, "ii", True),
    "fix_sqrt_table": (0x427960, "i", True),
    "fix_isqrt": (0x40D9D0, "i", True),
    "count_leading_zeros_of": (0x429170, "i", True),
    "vec_length": (0x480CD0, "v", True),
    "vec_dot": (0x40D950, "vv", True),
    "vec_cross": (0x4806A0, "vvo", False),
    "vec_scale": (0x480090, "ivo", False),
    "vec_scale8": (0x4444B0, "oiv", False),
    "vec_div": (0x486E90, "vi", False),
    "vec_normalise": (0x480C70, "v", False),
    "mat_mul_vec": (0x486D70, "mvo", False),
    "gte_rotate": (0x428090, "", False),
    "quat_normalise": (0x480430, "q", False),
    "quat_normalise_small": (0x480490, "q", False),
    "quat_normalise_large": (0x480580, "q", False),
    "quat_to_matrix": (0x480780, "qo", False),
    "quat_mul": (0x4809D0, "qqo", False),
    "vec_split": (0x4661A0, "vvoo", False),
    "vec_set_length": (0x480220, "vi", False),
    "sin_table": (0x480B10, "i", True),
    "mat_mul": (0x4800F0, "mmM", False),
    "mat_diag_mul_transpose": (0x4801A0, "vmM", False),
    "vec_set_length_small": (0x480360, "vi", False),
    "vec_set_length_large": (0x480270, "vi", False),
    "gte_outer_product": (0x427F50, "", False),
    "quad_root": (0x413590, "iii", True),
    "vec_normalise_shifted": (0x413BE0, "vo", True),
    "sphere_sweep_point": (0x413190, "SPOd", True),
    "sphere_sweep_edge": (0x413630, "ShhnOed", True),
    "sphere_pair_bounds": (0x413F70, "SSOd", True),
    "sphere_pair_contact": (0x4142C0, "SSOOOOd", True),
    "face_edges_test": (0x415940, "FfnP", True),
    "vec_set_length_shifted": (0x486F00, "vi", True),
    "wheel_force_torque": (0x486C10, "voWW", False),
    "vec_step_diff": (0x463810, "ovvi", False),
    "spline_velocity": (0x463610, "ovvvvtt", False),
    "fix_sin": (0x4270A0, "i", True),
    "fix_cos": (0x4270C0, "i", True),
    "matrix_from_angles": (0x426F80, "AB", False),
    "gte_rotate_short": (0x427FC0, "", False),
    "wheel_bounce": (0x40E1A0, "RC", False),
    "slot_time_digits": (0x439910, "iT", False),
    "quat_rotate": (0x480940, "qv", False),
    "matrix_look": (0x407220, "UUB", True),
    "vec_blend": (0x407CF0, "oUNeeZ", False),
    "vec_blend_car": (0x407CF0, "oUNeeK", False),
    "vec_bend_by_velocity": (0x407E00, "oUK", False),
    "vec_blend_scaled": (0x407BD0, "vveeZ", False),
    "bit27_clear": (0x47F390, "i", True),
    "svec_length_sq": (0x47F400, "h", True),
    "svec_length": (0x47F3D0, "h", True),
    "vec_turn_about": (0x47F640, "vvv", True),
    "line_next": (0x47F470, "kLB", True),
    "line_prev": (0x47F4E0, "kLB", True),
    "node_near": (0x47F6E0, "kLQi", True),
    "node_passed": (0x47F760, "kLQ", True),
    "line_advance": (0x47F880, "LwBQ", False),
    "ai_chance": (0x410670, "iY", True),
    "ai_roll_aggression": (0x410300, "RY", False),
    "ai_roll_flags80": (0x410C10, "RY", False),
    "ai_roll_flag4": (0x410A20, "RY", False),
    "ai_roll_flag8": (0x410AB0, "RY", False),
    "ai_roll_flag10": (0x410B40, "RY", False),
    "ai_roll_flag40": (0x410BD0, "RY", False),
    "ai_roll_flag800": (0x4106C0, "RY", False),
}
SYMBOL = {"vec_blend_car": "vec_blend"}  # tests that share a function
GTE_REGS = (0x6CCBE0, 0x6CCD00)  # matrix .. results, randomised for the GTE tests


def near(rng, scale):
    return rand_int(rng) if rng.randrange(20) == 0 else rng.randrange(-scale, scale)


def rand_slot(rng):
    v = [rand_int(rng) for _ in range(18)]
    v[2:5] = [near(rng, 0x1000) for _ in range(3)]  # position
    v[5:8] = [near(rng, 0x1000) for _ in range(3)]  # velocity per step
    v[16] = rng.randrange(0x100, 0x1000) if rng.randrange(20) else rand_int(rng)  # radius
    return v


def rand_direction(rng):
    if rng.randrange(10) == 0:
        return [rand_int(rng) for _ in range(3)]
    scale = rng.choice([0x1FF, 0x1000, 0x10000])
    v = [rng.gauss(0, 1) for _ in range(3)]
    length = sum(c * c for c in v) ** 0.5 or 1
    return [int(c / length * scale) for c in v]


def run_case(m, name, address, kinds, returns, rng):
    args, layout, offset = [], [], 0
    init = bytearray(0x800)
    seed, seed_at, last_unit, line_count = None, SEED, [0x10000, 0, 0], 1
    for k in kinds:
        if k == "i":
            args.append(rand_int(rng))
        elif k == "d":
            args.append(rng.choice([0x10000, 0x8000, 1, 0]) if rng.randrange(4) == 0 else
                        near(rng, 0x20000))
        elif k == "e":
            args.append(near(rng, 0x200))
        elif k == "t":
            args.append(rng.randrange(0, 0x101))
        elif k == "f":
            args.append(rng.choice([0x80000, 0x90000, 0x90123, rand_int(rng)]))
        elif k == "F":
            values = [rng.randrange(-0x10, 0x10) for _ in range(16)]
            struct.pack_into("<16h", init, offset, *values)
            args.append(BUF + offset)
            offset += 32
        elif k in "SPnO":
            values = {"S": rand_slot, "P": lambda r: [near(r, 0x1000) for _ in range(3)],
                      "n": rand_direction, "O": lambda r: [rand_int(r) for _ in range(8)]}[k](rng)
            struct.pack_into(f"<{len(values)}i", init, offset, *values)
            args.append(BUF + offset)
            offset += len(values) * 4
        elif k == "W":
            values = [near(rng, 0x40000) for _ in range(64)]
            struct.pack_into("<64i", init, offset, *values)
            args.append(BUF + offset)
            offset += 256
        elif k in "UN":
            if k == "U":
                values = last_unit = rand_direction(rng)
            else:
                values = [(-c + rng.randrange(-0x400, 0x400) + 2**31) % 2**32 - 2**31 for c in last_unit]
            struct.pack_into("<3i", init, offset, *values)
            args.append(BUF + offset)
            offset += 12
        elif k == "K":
            struct.pack_into("<3i", init, offset + 0x68, *[near(rng, 0x100000) for _ in range(3)])
            args.append(BUF + offset)
            offset += 0x80
        elif k in "zZY":
            args.append(0)
            if k == "Z":
                seed = rng.randrange(0, 0x80000000)
            if k == "Y":
                seed, seed_at = rng.randrange(0, 0x80000000), AI_SEED
        elif k == "L":
            count = rng.randrange(2, 9)
            struct.pack_into("<HBxBxxxI", init, offset, count, rng.choice([0, 1]), rng.choice([0, 1]),
                             BUF + offset + 0x10)
            for j in range(8):
                struct.pack_into("<3h", init, offset + 0x10 + j * 16, *[rng.randrange(-0x60, 0x60) for _ in range(3)])
                init[offset + 0x10 + j * 16 + 7] = rng.getrandbits(8)
            line_count = count
            args.append(BUF + offset)
            offset += 0x90
        elif k == "k":
            args.append(rng.randrange(0, 9) | (rng.getrandbits(16) << 16 if rng.randrange(2) else 0))
        elif k == "w":
            struct.pack_into("<H", init, offset, rng.randrange(0, line_count))
            args.append(BUF + offset)
            offset += 4
        elif k == "Q":
            struct.pack_into("<3i", init, offset, *[rng.randrange(-0x600000, 0x600000) for _ in range(3)])
            args.append(BUF + offset)
            offset += 12
        elif k == "A":
            struct.pack_into("<3h", init, offset, *[rand_s16(rng) for _ in range(3)])
            args.append(BUF + offset)
            offset += 8
        elif k == "B":
            args.append(BUF + offset)
            offset += 32
        elif k == "R":
            init[offset:offset + 0x100] = bytes(rng.getrandbits(8) for _ in range(0x100))
            struct.pack_into("<h", init, offset + 2, rng.randrange(-0x200, 0x200))
            struct.pack_into("<i", init, offset + 0x80, near(rng, 0x400000))
            args.append(BUF + offset)
            offset += 0x100
        elif k == "C":
            init[offset] = rng.choice([0, 1, 2, 0xff])
            args.append(BUF + offset - 0x63F)
            offset += 4
        elif k == "T":
            init[offset:offset + 0x60] = bytes(rng.getrandbits(8) for _ in range(0x60))
            for j in range(4):
                t = rng.choice([-1, near(rng, 0x8000000), rand_int(rng), rng.randrange(0, 0x40000000)])
                struct.pack_into("<i", init, offset + 0x3C + j * 4, t)
            args.append(BUF + offset)
            offset += 0x60
        elif k == "h":
            values = [rng.randrange(-0x10, 0x10) if rng.randrange(10) else rand_s16(rng) for _ in range(3)]
            struct.pack_into("<3h", init, offset, *values)
            args.append(BUF + offset)
            offset += 8
        else:
            n = {"m": 9, "M": 9, "q": 4, "o": 4}.get(k, 3)
            values = [rand_int(rng) for _ in range(n)]
            struct.pack_into(f"<{n}i", init, offset, *values)
            args.append(BUF + offset)
            layout.append((offset, n * 4))
            offset += n * 4
    gte = bytes(rng.getrandbits(8) for _ in range(GTE_REGS[1] - GTE_REGS[0])) if not kinds else \
        bytes(GTE_REGS[1] - GTE_REGS[0])

    results = []
    for target in (address, m.symbols[SYMBOL.get(name, name)]):
        m.write(BUF, bytes(init))
        for a, size in SCRATCH:
            m.write(a, b"\0" * size)
        if gte:
            m.write(GTE_REGS[0], gte)
        if seed is not None:
            m.write(seed_at, struct.pack("<I", seed))
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        state = m.read(BUF, 0x800) + b"".join(m.read(a, s) for a, s in SCRATCH)
        if gte:
            state += m.read(GTE_REGS[0], GTE_REGS[1] - GTE_REGS[0])
        if seed is not None:
            state += m.read(seed_at, 4)
        results.append((r, state))
    if results[0] != results[1]:
        return f"{name}{tuple(hex(a & 0xFFFFFFFF) for a in args)}: original {results[0][0]} ours {results[1][0]}" + (
            "" if results[0][1] == results[1][1] else " (memory differs)")
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 20000
    m = Machine(exe, elf)
    rng = random.Random(1)
    failed = 0
    for name, (address, kinds, returns) in TESTS.items():
        if SYMBOL.get(name, name) not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, name, address, kinds, returns, rng)
            if msg:
                if bad < 5:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
