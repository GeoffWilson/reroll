#!/usr/bin/env python3
"""Differential test of orig_pi.c (group PI: the front end's menu box quads,
side panel, text box frames' set-up and the 3D menu models,
0x453220-0x455e90) against the original, on random state.

Logging stubs (as many arguments as the callee reads; pointer arguments by
the content they point to) with scripted return values: fade_scale 0x4328a0,
fe_attract_left 0x448030, word_exchange 0x429080, ot_add_prim 0x427ab0,
fe_quad_flat_set 0x4314d0, fe_frame_sprites_init 0x4316d0, the model's
depth / lighting / drawing 0x45a2a0 / 0x45b200 / 0x45b400 / 0x4226d0 /
0x45a430, and, for 0x453a00, the group's own 0x4538e0 (the matrix by its
0x20 bytes, the position by its 12). Pure code runs for real in both: the
menu model lookup 0x4539d0, matrix_from_angles 0x426f80, the GTE emulation
0x427fc0 / 0x428090 / 0x426620, the transform / projection pair through
0x6ccd78 / 0x6ccd80 (the x87 pair 0x426a20 / 0x426b00) and the facing value
0x4278f0. The x87 control word is 0x27f, 0x37f or 0x07f; memory, the call
log, ebx / esi / edi / ebp and the x87 stack top / tag word are compared.

    tests/emu_orig_pi.py EXE build/emu_pi.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x10000
ARG = SCR + 0x0000  # argument blocks (0x400)
BODY = SCR + 0x1000  # 8 bodies x 0x100
TOP = SCR + 0x2000  # the top point
OPTIONS = SCR + 0x3000

REGIONS = [
    (0x4AC990, 0x10),  # fade, backdrop colour 0x4ac99c
    (0x4ACC38, 8),  # screen size
    (0x4B0608, 8),  # menu box base
    (0x4B09F0, 0x38),  # the seven ids and their corner sets
    (0x4B2198, 4),
    (0x4BDF00, 4),
    (0x4BE334, 8),  # menu box position
    (0x4BEB84, 4),
    (0x4CBA80, 0x1C20),  # menu model records
    (0x4CD880, 0xD0),  # side panel quads
    (0x4CF7C8, 0x2D0),  # menu box quads
    (0x4CFE98, 4),  # top point pointer
    (0x6C1E18, 0x20),
    (0x6CCB00, 0x380),  # GTE emulation state
    (0x6CE5D0, 0x20),  # camera matrix
]

# argument kinds: v value, m<n> n bytes at a pointer
STUBS = {
    0x4328A0: ("fade_scale", "v"),
    0x448030: ("fe_attract_left", "v"),
    0x429080: ("word_exchange", "vvv"),
    0x427AB0: ("ot_add_prim", "vv"),
    0x4314D0: ("fe_quad_flat_set", "vvvvvvv"),
    0x4316D0: ("fe_frame_sprites_init", "v"),
    0x45A2A0: ("model_depth", "v"),
    0x45B200: ("light_set", "v"),
    0x45B400: ("model_light", "vv"),
    0x4226D0: ("model_draw_lod", "vvv"),
    0x45A430: ("model_draw", "vv"),
    0x4538E0: ("menu_model_draw", ["v", "m32", "m12", "v", "v"]),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def pick(rng, *choices):
    return rng.choice(choices)


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B2198, "<I", OPTIONS)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        if SCR <= address < SCR + SCR_SIZE:
            struct.pack_into(fmt, self.scr, address - SCR, value)
            return
        a = self.find(address)
        struct.pack_into(fmt, self.mem[a], address - a, value)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.active = set()
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.active = active

    def _hook(self, uc, address, size, _):
        if address not in self.active:
            return
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            else:
                logged.append(rd(self.m, a, int(kind[1:])))
        self.log.append((name, tuple(logged)))
        values = self.script.get(name)
        uc.reg_write(UC_X86_REG_EAX, (values.pop(0) if values else 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def coord(rng):
    return pick(rng, rng.randrange(-100, 700), rng.randrange(-100, 700), rng.randrange(-5000, 5000),
                rng.getrandbits(32))


def set_screen(st, rng):
    st.put(0x4ACC38, "<I", pick(rng, 640, 800, 1024, 3440, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600, 768, 1440, rng.randrange(1, 4000), rng.getrandbits(32)))


def g_box(st, rng, m):
    set_screen(st, rng)
    for a in (0x4B0608, 0x4B060C, 0x4BE334, 0x4BE338):
        st.put(a, "<i", coord(rng))
    st.script["fade_scale"] = [pick(rng, 0, 0x80, rng.randrange(0, 0x81), rng.getrandbits(32))]
    page = pick(rng, 0, 1, 2, rng.getrandbits(32))
    return [rng.getrandbits(32), page], (0x453220, "menu_box_quads_draw")


def g_panel_init(st, rng, m):
    return [], None


def g_panel(st, rng, m):
    set_screen(st, rng)
    st.script["fe_attract_left"] = [pick(rng, 0, rng.randrange(0, 0x100), rng.getrandbits(32))]
    return [rng.getrandbits(32), pick(rng, 0, 1, 2, rng.getrandbits(32)), coord(rng), coord(rng)], None


def records(st, rng, ids):
    """Fill the 100 menu model records with other ids; place each of ids at
    a random slot. Returns the slot of each."""
    used = set(ids)
    for i in range(100):
        v = pick(rng, rng.randrange(-5, 200), rng.getrandbits(32), 0)
        while v in used or (v - (1 << 32)) in used:
            v = rng.getrandbits(32)
        st.put(0x4CBA80 + i * 0x48, "<I", v)
    free = list(range(100))
    rng.shuffle(free)
    slots = {}
    for k, mid in enumerate(ids):
        st.put(0x4CBA80 + free[k] * 0x48, "<i", mid)
        slots[mid] = free[k]
    return slots


def g_draw(st, rng, m):
    mid = pick(rng, rng.randrange(0, 60), rng.getrandbits(32))
    slots = records(st, rng, [mid] if rng.randrange(8) else [])
    for mid2, slot in slots.items():
        st.put(0x4CBA80 + slot * 0x48 + 4, "<I", pick(rng, BODY + 0x100 * rng.randrange(8), BODY, 0))
    st.put(0x6CCE7C, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    st.script["model_depth"] = [pick(rng, rng.randrange(-100, 0x2000), rng.randrange(-0x40, 0x40), rng.getrandbits(32))]
    flags = pick(rng, 0, 1, 2, 3, rng.getrandbits(32))
    return [mid, ARG, ARG + 0x40, rng.getrandbits(32), flags], None


def s16(rng):
    return pick(rng, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), 0, 0x1000)


def g_corners(st, rng, m):
    mid = rng.randrange(1, 40)
    ids = [mid]
    first = 0
    corner = rng.randrange(4)  # 0 not listed, 1 listed without corners, 2 / 3 with
    for i in range(7):
        st.put(0x4B09F0 + 4 * i, "<i", pick(rng, rng.randrange(40, 100), rng.getrandbits(32)))
        st.put(0x4B0A0C + 4 * i, "<i", pick(rng, 0, rng.getrandbits(32)))
    if corner:
        slot = rng.randrange(7)
        if corner > 1:
            first = rng.randrange(100, 200)
            ids += [first + k for k in range(4)]
        st.put(0x4B09F0 + 4 * slot, "<i", mid)
        st.put(0x4B0A0C + 4 * slot, "<i", first)
    slots = records(st, rng, ids)
    for mid2, slot in slots.items():
        r = 0x4CBA80 + slot * 0x48
        for off in range(8, 0x48, 2):
            st.put(r + off, "<h", s16(rng))
        if rng.randrange(2):  # a rotation-ish matrix at +8
            for i in range(9):
                st.put(r + 8 + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001)))
        st.put(r + 0x28, "<h", rng.randrange(-300, 300))
        st.put(r + 0x2A, "<h", rng.randrange(-300, 300))
        st.put(r + 0x2C, "<h", pick(rng, rng.randrange(-300, 300), 0))
    # camera matrix and translation
    for i in range(9):
        st.put(0x6CE5D0 + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001)))
    for i in range(3):
        st.put(0x6CE5E4 + 4 * i, "<i", pick(rng, rng.randrange(-2000, 2000), rng.randrange(1000, 20000),
                                              rng.getrandbits(32)))
    st.put(0x4CFE98, "<I", TOP)
    for i in range(3):
        st.put(TOP + 2 * i, "<h", pick(rng, rng.randrange(-500, 500), s16(rng)))
    # the GTE: x87 transform / projection, projection constants
    st.put(0x6CCD78, "<I", 0x426A20)
    st.put(0x6CCD80, "<I", 0x426B00)
    st.put(0x6CCD74, "<I", ARG + 0x200)
    st.put(0x6CCD5C, "<f", rng.uniform(-400, 400))
    st.put(0x6CCD60, "<f", rng.uniform(-400, 400))
    for a in (0x6CCD64, 0x6CCD68, 0x6CCD6C, 0x6CCD70):
        st.put(a, "<f", rng.uniform(-1000, 1000))
    for a in range(0x6CCD5C - 0x30, 0x6CCD5C, 4):  # H / projection scale
        if rng.randrange(2):
            st.put(a, "<f", rng.uniform(-2, 600))
    args = [mid, ARG, ARG + 0x40, rng.getrandbits(32), ARG + 0x60, ARG + 0x70]
    for i in range(5):
        st.put(ARG + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001)))
    for i in range(3):
        st.put(ARG + 0x40 + 4 * i, "<i", rng.getrandbits(32))
    for i in range(4):
        st.put(ARG + 0x60 + 2 * i, "<h", s16(rng))
        st.put(ARG + 0x70 + 2 * i, "<h", s16(rng))
    return args, None


TESTS = {
    "menu_box_quads_draw": (0x453220, g_box, {0x4328A0, 0x429080}),
    "fe_side_panel_init": (0x453570, g_panel_init, {0x4314D0}),
    "fe_side_panel_draw": (0x4535B0, g_panel, {0x448030, 0x427AB0}),
    "fe_corner_frames_init": (0x453710, g_panel_init, {0x4316D0}),
    "menu_model_draw": (0x4538E0, g_draw, {0x45A2A0, 0x45B200, 0x45B400, 0x4226D0, 0x45A430}),
    "menu_model_corners_draw": (0x453A00, g_corners, {0x4538E0}),
}

SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))
    m.uc.reg_write(UC_X86_REG_EBX, 0x11111111)
    m.uc.reg_write(UC_X86_REG_ESI, 0x22222222)
    m.uc.reg_write(UC_X86_REG_EDI, 0x33333333)
    m.uc.reg_write(UC_X86_REG_EBP, 0x44444444)


def run_case(m, stubs, name, address, gen, active, rng):
    st = State(rng)
    args, _ = gen(st, rng, m)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script, active)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        # 0x6ccd74, the transform's current record: a stack address in 0x453a00
        record = struct.unpack("<I", rd(m, 0x6CCD74, 4))[0]
        if E.STACK <= record < E.STACK + E.STACK_SIZE:
            m.write(0x6CCD74, b"STAK")
        mem = rd(m, SCR, SCR_SIZE) + b"".join(rd(m, a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = None
        if first is not None:
            if first < SCR_SIZE:
                where = SCR + first
            else:
                off = first - SCR_SIZE
                for ra, n in REGIONS:
                    if off < n:
                        where = ra + off
                        break
                    off -= n
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(i), a[i:i + 4].hex(), b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True, None
    log = results[0][2]
    return None, results[0][0][0] != "ok", log


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pi_owned.txt"):
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
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(59)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen, active) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        flags = {}
        seen = set()
        size = sizes.get(address, 0)
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg, fault, log = run_case(m, stubs, name, address, gen, active, rng)
            faults += fault and not msg
            if log:
                for call in log:
                    if call[0] == "menu_model_draw":
                        flags[call[1][4]] = flags.get(call[1][4], 0) + 1
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if "-c" in sys.argv and size:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:12]}" if missed else "") + "]"
        both = f" ({faults} fault the same way)" if faults else ""
        drawn = f" draws by flags {flags}" if flags else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{drawn}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
