#!/usr/bin/env python3
"""Differential test of orig_wb.c (group WB: the bodies of the five
functions src/subunit.c wraps: the camera matrix 0x423d30, the float
transforms 0x426a20 / 0x426be0, the view's HUD 0x438310 and draw_view
0x479be0) against the original at those addresses, on random state.

Every callee of 0x423d30 / 0x438310 / 0x479be0 is a logging stub in both
runs (as many arguments as it reads; the viewport rectangles passed by
pointer are logged by content, 8 bytes), with scripted return values for
the two whose result is used (0x458760 the missile count, 0x47a030 the
level of detail); 0x428bd0 (fld the far plane) runs for real. The
transforms run from a trampoline that stores eax / ecx / edx and st(0)
(80 bits) after the call. The x87 control word is 0x27f, 0x37f or 0x07f;
besides memory, eax (transforms), ebx / esi / edi / ebp, the x87 stack top
and tag word are compared.

    tests/emu_orig_wb.py EXE build/emu_wb.elf [iterations] [names...] [-v] [-c]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402

SCR = 0x01000000
SCR_SIZE = 0x10000
RACE = SCR + 0x0000  # 0x1000
ARG = SCR + 0x1000  # the transforms' record
RES = SCR + 0x1100  # what the trampolines store
VIEW = SCR + 0x2000  # 0x100
CAMP = SCR + 0x2100  # the view's +0x28: pointer to the camera object
CAMERA = SCR + 0x3000  # 0x1000
CAR = SCR + 0x4000  # 0x1000 (the HUD view's *(view) object, word +0x64e)
OTS = SCR + 0x5000  # 0x100: the OT block *0x6ce608 (+4 an OT entry)
OT = SCR + 0x5100  # OT entries
TRAMP = 0x00800000

REGIONS = [
    (0x4ACC38, 8),
    (0x4AF4B8, 4),
    (0x4B1960, 4),
    (0x4BE454, 4),
    (0x4D1198, 4),
    (0x6BFB80, 0xEE0),  # the four views (0x274 each), the slot table 0x6c0a40, 0x6c0a50
    (0x6C3E00, 0x4200),  # HUD rings 0x6c3ec0 (0x43 x 0xf0), 0x6c7d90, 0x6c7f5a, 0x6c7fa4, 0x6c7fdc..
    (0x6CC900, 0x1E00),
]


def S(name, kinds):
    return (name, kinds)


# argument kinds: v value, r 8-byte rectangle
STUBS = {
    0x423D70: S("camera_build", "vvv"),
    0x423DF0: S("camera_load", "v"),
    0x428770: S("viewport_apply", "rv"),
    0x4284B0: S("border_draw", "rrv"),
    0x40CBE0: S("view_begin", "v"),
    0x40CD00: S("view_end", "v"),
    0x466F70: S("camera_shake", "vv"),
    0x423D10: S("view_matrix", "v"),
    0x479940: S("time_warp", "v"),
    0x423D30: S("camera_matrix", ""),
    0x47BD20: S("f47bd20", ""),
    0x478DF0: S("f478df0", ""),
    0x490430: S("f490430", "v"),
    0x42BE20: S("f42be20", "v"),
    0x47EA00: S("f47ea00", "v"),
    0x479B30: S("f479b30", "v"),
    0x42C0A0: S("f42c0a0", "v"),
    0x484980: S("f484980", "v"),
    0x438760: S("f438760", "v"),
    0x433F00: S("f433f00", ""),
    0x42B9A0: S("f42b9a0", ""),
    0x42C950: S("f42c950", ""),
    0x42C420: S("f42c420", ""),
    0x4345C0: S("f4345c0", ""),
    0x467EC0: S("f467ec0", ""),
    0x483210: S("f483210", ""),
    0x48FDD0: S("f48fdd0", ""),
    0x46C840: S("f46c840", ""),
    0x438310: S("hud", "v"),
    0x47A030: S("visibility", "v"),
    0x477940: S("f477940", "v"),
    0x479F60: S("f479f60", ""),
    0x428B90: S("far_plane_set", "v"),
    0x404F50: S("sky_quads", ""),
    0x4291F0: S("noop", ""),
    0x4049B0: S("sky_dome", "v"),
    0x47DD70: S("f47dd70", "v"),
    0x459B20: S("f459b20", ""),
    0x46DC50: S("f46dc50", ""),
    0x421EF0: S("f421ef0", ""),
    0x4821D0: S("f4821d0", ""),
    0x442060: S("f442060", "v"),
    0x46F230: S("f46f230", ""),
    0x47DB20: S("f47db20", ""),
    0x46A9E0: S("f46a9e0", ""),
    0x41B2F0: S("f41b2f0", ""),
    0x45CFE0: S("f45cfe0", "vv"),
    0x43A4A0: S("panel", "vv"),
    0x4792A0: S("messages", "vv"),
    0x483D20: S("lock_on", "v"),
    0x458760: S("missiles", "v"),
    0x439D50: S("flash", "vvv"),
    0x471C90: S("sound_play", "vvvvv"),
    0x439D70: S("icons", "vvv"),
    0x469F80: S("ot_entry", "vv"),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def mk_float(rng):
    k = rng.randrange(10)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 100.0, 640.0, 480.0, 3.0]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 0x7FA00000, 1, 0x80000001, 0x00800000,
                           0x7F7FFFFF, 0xFF7FFFFF])
    if k == 2:
        return rng.getrandbits(32)
    return fbits(rng.uniform(-1000.0, 1000.0))


def pick(rng, *choices):
    return rng.choice(choices)


def s16(rng, scale=0x8000):
    return rng.randrange(-scale, scale) & 0xFFFF


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B1960, "<I", RACE)

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
        self.disabled = None  # the function under test, when it is itself a stub of the others
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address == self.disabled:
            return
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind == "r":
                logged.append(rd(self.m, a, 8))
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def set_gte_float(st, rng):
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
    if rng.randrange(8) == 0:
        for a in [r + 4 * k for r in rows for k in range(3)] + [0x6CCD0C, 0x6CCD10, 0x6CCD14, 0x6CCD00, 0x6CCD04,
                                                                0x6CCD08]:
            if rng.randrange(3) == 0:
                st.put(a, "<I", mk_float(rng))
    scale = pick(rng, 0x8000, 3000, 500)
    for k in range(3):
        st.put(0x6CCC64 + 2 * k, "<H", s16(rng, scale))


def g_transform(st, rng):
    set_gte_float(st, rng)
    return [ARG]


def g_camera_matrix(st, rng):
    st.put(0x6CCE94, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    return []


def set_race(st, rng):
    st.put(RACE + 0x57C, "<i", pick(rng, rng.randrange(0, 10), rng.randrange(0, 10), rng.getrandbits(32)))
    st.put(RACE + 0x580, "<i", pick(rng, rng.randrange(0, 0x40), rng.randrange(0, 0x30), rng.getrandbits(32)))
    for off in (0x54C, 0x54D, 0x551, 0x552, 0x586, 0x58C, 0xA84):
        st.put(RACE + off, "<B", pick(rng, 0, 1, rng.randrange(0, 8), rng.getrandbits(8)))
    st.put(RACE + 0x54D, "<B", rng.randrange(0, 7))
    st.put(RACE + 0x551, "<B", rng.randrange(0, 6))
    st.put(0x6CCE40, "<I", rng.getrandbits(8) & pick(rng, 0xFF, 0x00, 0x24, 0x12, 0x04))


def g_hud(st, rng):
    set_race(st, rng)
    view = VIEW
    st.put(view, "<I", CAR)
    car = rng.randrange(0, 8)
    st.put(CAR + 0x64E, "<H", car)
    st.put(view + 0x2C, "<B", rng.getrandbits(8))
    st.put(0x4D1198, "<I", pick(rng, 0, 0, 1))
    st.put(0x4BE454, "<I", pick(rng, 0, 0, 1))
    st.put(0x6C0A50, "<I", pick(rng, 0, 0x10, rng.getrandbits(32)))
    st.put(0x6C7FDC, "<I", pick(rng, rng.getrandbits(32), rng.randrange(0, 0x20)))
    st.put(0x6C7FE0 + 4 * car, "<B", pick(rng, 0, rng.getrandbits(8)))
    st.put(0x6C7FE2 + 4 * car, "<B", pick(rng, 0, 0, 1, rng.getrandbits(8)))
    st.put(0x6CE608, "<I", OTS)
    st.put(OTS + 4, "<I", (OT + 4 * rng.randrange(0, 8)) | pick(rng, 0, 0x80000000))
    for a in (0x6CCD54, 0x6CCD58):
        st.put(a, "<I", fbits(rng.uniform(0, 400)) if rng.randrange(10) else mk_float(rng))
    for i in range(0x43):
        rec = 0x6C3EC0 + i * 0xF0
        st.put(rec + 0x4C, "<I", mk_float(rng))
        st.put(rec + 0x14, "<I", mk_float(rng) if rng.randrange(3) else fbits(0.0))
        st.put(0x6C7F5A + i, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    st.script["missiles"] = [pick(rng, 0, 0, 1, rng.randrange(1, 6), rng.getrandbits(32))]
    return [view]


def g_draw_view(st, rng):
    set_race(st, rng)
    if rng.randrange(3) == 0:
        view = 0x6BFB80 + 0x274 * rng.randrange(0, 4)
    else:
        view = VIEW
    for k in range(4):
        st.put(0x6C0A40 + 4 * k, "<I", rng.randrange(0, 4))
    st.put(view + 0x28, "<I", CAMP)
    st.put(CAMP, "<I", CAMERA)
    for off in (8, 0xC, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24):
        st.put(view + off, "<H", pick(rng, rng.randrange(0, 1500), s16(rng)))
    st.put(CAMERA + 0xC, "<I", pick(rng, 0, 1, 2, 3, rng.getrandbits(32)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 1440, rng.randrange(1, 2000)))
    st.put(0x6CCE94, "<I", pick(rng, 0, 1))
    st.script["visibility"] = [rng.getrandbits(32)]
    return [view]


# mode: "c" plain call; "t" the transforms' trampoline
TESTS = {
    "camera_matrix_body": (0x423D30, g_camera_matrix, "c"),
    "subunit_transform_body": (0x426A20, g_transform, "t"),
    "subunit_transform_only_body": (0x426BE0, g_transform, "t"),
    "hud_body": (0x438310, g_hud, "c"),
    "draw_view_body": (0x479BE0, g_draw_view, "c"),
}


def make_trampoline(target, slot):
    code = b"\xb8\x11\x00\xaa\x55\xb9\x22\x00\xaa\x55\xba\x33\x00\xaa\x55"
    code += b"\xff\x74\x24\x04"  # push dword [esp + 4]
    rel = target - (slot + len(code) + 5)
    code += b"\xe8" + struct.pack("<i", rel)  # call target
    code += b"\x83\xc4\x04"  # add esp, 4
    code += b"\xa3" + struct.pack("<I", RES + 0x10)  # mov [RES+0x10], eax
    code += b"\x89\x0d" + struct.pack("<I", RES + 0x14)  # mov [RES+0x14], ecx
    code += b"\x89\x15" + struct.pack("<I", RES + 0x18)  # mov [RES+0x18], edx
    code += b"\xdb\x3d" + struct.pack("<I", RES)  # fstp tbyte [RES]
    return code + b"\xc3"


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


SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, slots, name, address, gen, mode, rng):
    st = State(rng)
    args = gen(st, rng)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for k, target in enumerate((address, m.symbols[name])):
        apply(m, st, cw)
        stubs.reset(st.script)
        stubs.disabled = address
        if mode == "t":
            target = slots[(name, k)]
        r = m.call(target, args)
        if r[0] == "ok":
            r = (("ok", r[1]) if mode == "t" else ("ok",)) + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
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
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True
    return None, results[0][0][0] != "ok"


SIZES = {0x423D30: 59, 0x426A20: 211, 0x426BE0: 227, 0x438310: 782, 0x479BE0: 886}


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
    m.uc.mem_map(TRAMP, 0x1000)
    stubs = Stubs(m)
    slots = {}
    n = 0
    for name, (address, _, mode) in TESTS.items():
        if mode == "t" and name in m.symbols:
            for k, target in enumerate((address, m.symbols[name])):
                slot = TRAMP + n * 0x80
                m.write(slot, make_trampoline(target, slot))
                slots[(name, k)] = slot
                n += 1
    rng = random.Random(53)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    for name, (address, gen, mode) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        seen = set()
        size = SIZES[address]
        if "-c" in sys.argv:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg, fault = run_case(m, stubs, slots, name, address, gen, mode, rng)
            faults += fault and not msg
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        cov = ""
        if "-c" in sys.argv:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            cov = f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:12]}" if missed else "") + "]"
        both = f" ({faults} fault the same way)" if faults else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
