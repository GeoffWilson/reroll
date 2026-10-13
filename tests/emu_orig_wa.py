#!/usr/bin/env python3
"""Differential test of orig_wa.c (group WA: the bodies of the originals our
widescreen wrappers ran through trampolines: the frustum 0x417ed0, the
viewport 0x41f2d0, the object pass 0x459b20, the countdown 0x46a9e0, the
visibility list manager 0x47a030, the flake spawner 0x485140, the sparkle
0x490430) against the original, on random state.

Callees outside the group are logging stubs in both runs (the call log
records as many arguments as the callee reads; out pointers are logged as
"stack or not" and filled with scripted values, the light records passed to
0x45a260 by content): the D3D viewport 0x41f210, the screen offset 0x426240,
the light ids 0x459cb0, the lit model 0x45a260, the track run steps
0x47a0d0 / 0x47a500 / 0x47c420 / 0x47c730. The grid record lookup 0x4613e0
and the C runtime's atan / sin / cos (0x497c24 / 0x497b74 / 0x497ac4) run
for real in both. The x87 control word is 0x27f, 0x37f or 0x07f; besides
memory (the whole data section and the scratch area), the return value,
the call log, ebx / esi / edi / ebp, the x87 stack top and tag word are
compared.

    tests/emu_orig_wa.py EXE build/emu_wa.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EFLAGS, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW,
                               UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

import emu_fixed as E  # noqa: E402

DATA, DATA_SIZE = 0x4A7000, 0x22C000  # the exe's data section, restored and compared whole
SCR = 0x01000000
SCR_SIZE = 0x40000
RACE = SCR + 0x0000  # 0x1000
TRACK = SCR + 0x1000  # 0x100
CELL = SCR + 0x1100  # 0x100
OTA = SCR + 0x2000  # 0x2000: list entries
TEX = SCR + 0x4000  # 0x2000: texture records (16 bytes)
FLAKES = SCR + 0x6000  # 32 x 0xa0
CAMERA = SCR + 0x8000  # 0x100
CAR = SCR + 0x8100  # 0x800
PKT = SCR + 0x9000  # 14 x 0x78 and more
GRID = SCR + 0xA000  # 400 records of 0x80 (0x6b9b90 base: record k at base + (k - 1) * 0x80)
GRID_N = 400


def S(name, kinds):
    return (name, kinds)


# argument kinds: v value, cN N bytes by content, P out word, L out lights (3 words)
STUBS = {
    0x41F210: S("d3d_viewport_set", ["v", "v", "v", "v"]),
    0x426240: S("screen_offset_get", ["P", "P"]),
    0x459CB0: S("light_colours_from_ids", ["v", "L"]),
    0x45A260: S("model_draw_lit", ["v", "c12"]),
    0x47A0D0: S("track_runs_visible", ["v", "v"]),
    0x47A500: S("track_runs_diff_texture", ["v", "v"]),
    0x47C420: S("track_runs_light_flags", ["v"]),
    0x47C730: S("track_runs_light", ["v", "v"]),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def mk_float(rng):
    k = rng.randrange(10)
    if k == 0:
        return fbits(rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 100.0, 640.0, 480.0, 3.0]))
    if k == 1:
        return rng.choice([0x7F800000, 0xFF800000, 0x7FC00000, 0xFFC00000, 1, 0x80000001, 0x00800000,
                           0x7F7FFFFF, 0xFF7FFFFF, 0x7F800001])
    if k == 2:
        return rng.getrandbits(32)
    return fbits(rng.uniform(-1000.0, 1000.0))


def pick(rng, *choices):
    return rng.choice(choices)


class State:
    data0 = None  # the data section as loaded

    def __init__(self, rng):
        self.rng = rng
        self.data = bytearray(State.data0)
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B1960, "<I", RACE)

    def put(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        if SCR <= address < SCR + SCR_SIZE:
            struct.pack_into(fmt, self.scr, address - SCR, value)
        else:
            assert DATA <= address < DATA + DATA_SIZE, hex(address)
            struct.pack_into(fmt, self.data, address - DATA, value)


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
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        outs = list(self._next(name + ".out", (0, 0, 0)))
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind[0] == "c":
                logged.append(rd(self.m, a, int(kind[1:])))
            elif kind == "P":
                logged.append("stack" if on_stack(a) else a)
                uc.mem_write(a, struct.pack("<I", outs.pop(0) & 0xFFFFFFFF))
            elif kind == "L":
                logged.append("stack" if on_stack(a) else a)
                uc.mem_write(a, struct.pack("<3I", *(o & 0xFFFFFFFF for o in outs[:3])))
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, self._next(name, 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def screen(st, rng):
    st.put(0x4ACC38, "<i", pick(rng, 640, 800, 1024, rng.randrange(-10, 2000)))
    st.put(0x4ACC3C, "<i", pick(rng, 480, 600, 768, rng.randrange(-10, 2000)))


def g_frustum(st, rng):
    screen(st, rng)
    sw = pick(rng, 640.0, 800.0, rng.uniform(100, 2000))
    sh = pick(rng, 480.0, 600.0, rng.uniform(100, 2000))
    if rng.randrange(3):
        st.put(0x4ACC38, "<i", int(sw))
        st.put(0x4ACC3C, "<i", int(sh))
    x = pick(rng, 0.0, 0.0, rng.uniform(-400, 400), -100.0, sw / 2)
    y = pick(rng, 0.0, 0.0, rng.uniform(-300, 300), sh / 2)
    w = pick(rng, sw, sw / 2, rng.uniform(1, 3000), sw - x)
    h = pick(rng, sh, sh / 2, rng.uniform(1, 2000), sh - y, w)
    focal = pick(rng, 400.0, 256.0, rng.uniform(50, 2000), rng.uniform(-100, 100))
    args = [fbits(v) for v in (x, y, w, h, focal)]
    if rng.randrange(10) == 0:
        args[rng.randrange(5)] = mk_float(rng)
    return args, None


def g_viewport(st, rng):
    screen(st, rng)
    sw = struct.unpack_from("<i", st.data, 0x4ACC38 - DATA)[0]
    sh = struct.unpack_from("<i", st.data, 0x4ACC3C - DATA)[0]
    x = pick(rng, 0, 0, rng.randrange(-500, 500), sw // 2, rng.randrange(-0x80000000, 0x80000000))
    y = pick(rng, 0, 0, rng.randrange(-500, 500), sh // 2, rng.randrange(-0x80000000, 0x80000000))
    w = pick(rng, sw, sw // 2, rng.randrange(0, 3000), rng.randrange(-0x80000000, 0x80000000))
    h = pick(rng, sh, sh // 2, rng.randrange(0, 3000), rng.randrange(-0x80000000, 0x80000000))
    return [x, y, w, h], None


def lights_script(st, rng, n):
    st.script["light_colours_from_ids.out"] = [tuple(rng.getrandbits(32) for _ in range(3)) for _ in range(n)]


def g_objects(st, rng):
    view = pick(rng, 0x6CD4A0, 0x6CD4A0, 0x6CDD10, rng.getrandbits(32))
    st.put(0x6CE60C, "<I", view)
    for a in (0x4BEB84, 0x6C1E34, 0x6C1E1C, 0x6C1E38):
        st.put(a, "<I", rng.getrandbits(32))
    st.put(0x6B9B90, "<I", GRID)
    st.put(0x6B9CB8, "<i", pick(rng, 0, rng.randrange(0, 71), 68, 69, 70, rng.randrange(0, 10)))
    st.put(0x4D1198, "<I", pick(rng, 0, 0, 0, 1))
    # records: kind, next link (to a higher index, acyclic), trigger word
    for k in range(1, GRID_N + 1):
        r = GRID + (k - 1) * 0x80
        kind = rng.getrandbits(32)
        if rng.randrange(3):
            kind = (kind & ~0x1FF) | pick(rng, 3, 3, 1, 2, 5, rng.randrange(0x200))
        st.put(r, "<I", kind)
        nxt = 0 if k == GRID_N or rng.randrange(4) == 0 else rng.randrange(k + 1, min(GRID_N, k + 20) + 1)
        st.put(r + 4, "<H", nxt)
        st.put(r + 0x16A, "<H", pick(rng, 0, 0, 1, rng.getrandbits(16)))
    n = pick(rng, 0, 1, 2, rng.randrange(1, 30), rng.randrange(1, 80))
    ids = rng.getrandbits(32)
    for k in range(n):
        st.put(0x5D7040 + 2 * k, "<H", rng.randrange(1, GRID_N + 1))
        if rng.randrange(3) == 0:
            ids = pick(rng, rng.getrandbits(32), ids ^ 0x80000000, 0)
        st.put(0x5D6AC0 + 4 * k, "<I", ids)
    st.put(0x5D7040 + 2 * n, "<H", 0)
    lights_script(st, rng, n + 1)
    return [], None


def ot_entry(rng):
    return (OTA + 4 * rng.randrange(0x800)) | pick(rng, 0, 0, 0x80000000)


def g_countdown(st, rng):
    st.put(RACE + 0xA82, "<H", pick(rng, 640, 512, 800, rng.getrandbits(16)))
    st.put(RACE + 0x57C, "<i", pick(rng, 0, 1, 2, 3, 3, 4, 5, rng.getrandbits(32)))
    st.put(0x4D3DA8, "<i", pick(rng, 0, 1, 2, 3, 4, 5, 6, rng.randrange(-10, 10), rng.getrandbits(32)))
    st.put(0x6C1EB0, "<I", TRACK)
    st.put(TRACK + 0x2E, "<H", rng.randrange(0, 0x60))
    st.put(0x6C1E14, "<I", TEX - 0x7E0)
    st.put(0x4D3DAC, "<i", pick(rng, 0, 1, 2, 3, 4, 5, 10, 11, 12, 20, 30, rng.randrange(-9, 60),
                                rng.randrange(-0x8000, 0x8000), -10))
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CDD10, rng.getrandbits(32)))
    for a in range(0x4D3CB8, 0x4D3DA8, 4):
        st.put(a, "<I", rng.getrandbits(32))
    st.put(0x6CCD84, "<B", rng.getrandbits(8))
    st.put(0x6CE608, "<I", OTA)
    st.put(OTA + 4, "<I", ot_entry(rng))
    st.put(0x6CCD64, "<I", fbits(pick(rng, 0.0, rng.uniform(-200, 300))) if rng.randrange(20) else mk_float(rng))
    st.put(0x6CCD6C, "<I", fbits(pick(rng, 0.0, rng.uniform(-200, 300))) if rng.randrange(20) else mk_float(rng))
    st.script["screen_offset_get.out"] = [(pick(rng, 320, rng.randrange(-1000, 1000), rng.getrandbits(32)),
                                           pick(rng, 240, rng.randrange(-1000, 1000), rng.getrandbits(32)))]
    return [], None


def g_visibility(st, rng):
    st.put(0x6CE60C, "<I", pick(rng, 0x6CD4A0, 0x6CDD10, rng.getrandbits(32)))
    for a in (0x5D9454, 0x5D9458, 0x5D945C, 0x5D9460):
        st.put(a, "<I", rng.getrandbits(32))
    return [pick(rng, CELL, 0, rng.getrandbits(32))], 0xFFFFFFFF


def g_flake(st, rng):
    st.put(0x4D5898, "<I", FLAKES)
    st.put(0x4B2C50, "<I", rng.getrandbits(31) if rng.randrange(8) else rng.getrandbits(32))
    st.put(RACE + 0xA82, "<H", pick(rng, 640, 512, 800, rng.randrange(0, 2048), rng.getrandbits(16)))
    st.put(RACE + 0xA80, "<H", pick(rng, 480, 384, 600, rng.randrange(2, 2048), rng.getrandbits(16), 1, 0))
    st.put(RACE + 0x553, "<B", pick(rng, 0, 0, 0, 1, 2, 3, rng.getrandbits(8)))
    return [pick(rng, *range(32))], None


def g_sparkle(st, rng):
    st.put(0x6CCE40, "<B", pick(rng, 0, 0, 0, 0x10, rng.getrandbits(8)))
    st.put(CAMERA + 4, "<I", CAR)
    st.put(CAR, "<I", (rng.getrandbits(32) & ~0x1FF) | pick(rng, 3, 3, 3, 3, 2, rng.randrange(0x200)))
    st.put(CAR + 0x689, "<B", pick(rng, 0, 1, 5, 15, 16, 36, 37, 255, rng.getrandbits(8)))
    st.put(CAR + 0x68C, "<I", PKT)
    st.put(0x6CCE84, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    st.put(0x6CDD84, "<I", ot_entry(rng))
    st.put(0x6CD514, "<I", ot_entry(rng))
    st.put(0x4B2D1C, "<I", rng.getrandbits(31) if rng.randrange(8) else rng.getrandbits(32))
    st.put(RACE + 0xA82, "<H", pick(rng, 640, 512, 800, rng.getrandbits(16), 0, 31))
    st.put(RACE + 0xA80, "<H", pick(rng, 480, 384, 600, rng.getrandbits(16), 0, 31))
    st.put(0x6CCD64, "<I", fbits(pick(rng, 0.0, rng.uniform(-200, 300))) if rng.randrange(20) else mk_float(rng))
    st.put(0x6CCD6C, "<I", fbits(pick(rng, 0.0, rng.uniform(-200, 300))) if rng.randrange(20) else mk_float(rng))
    return [CAMERA], None


# name: (address, generator)
TESTS = {
    "frustum_body": (0x417ED0, g_frustum),
    "viewport_body": (0x41F2D0, g_viewport),
    "objects_body": (0x459B20, g_objects),
    "countdown_body": (0x46A9E0, g_countdown),
    "visibility_body": (0x47A030, g_visibility),
    "flake_spawn_body": (0x485140, g_flake),
    "sparkle_body": (0x490430, g_sparkle),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    m.write(DATA, bytes(st.data))
    m.write(SCR, bytes(st.scr))
    m.uc.reg_write(UC_X86_REG_EBX, 0x11111111)
    m.uc.reg_write(UC_X86_REG_ESI, 0x22222222)
    m.uc.reg_write(UC_X86_REG_EDI, 0x33333333)
    m.uc.reg_write(UC_X86_REG_EBP, 0x44444444)


def call(m, address, args, count):
    esp = E.STACK + E.STACK_SIZE - 0x100
    frame = struct.pack("<I", E.RETURN) + b"".join(struct.pack("<I", a & 0xFFFFFFFF) for a in args)
    m.uc.mem_write(esp, frame)
    m.uc.reg_write(UC_X86_REG_ESP, esp)
    m.uc.reg_write(UC_X86_REG_EFLAGS, 0x2)
    m.trapped = False
    try:
        m.uc.emu_start(address, E.RETURN, count=count)
    except UcError as e:
        return ("error", str(e))
    if m.trapped:
        return ("trap",)
    if m.uc.reg_read(UC_X86_REG_EIP) != E.RETURN:
        return ("cut off",)
    return ("ok", m.uc.reg_read(UC_X86_REG_EAX))


SAVED = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, mask = gen(st, rng)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script)
        r = call(m, target, args, 20000000)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
            r = r + tuple(m.uc.reg_read(x) for x in SAVED)
            r = r + (m.uc.reg_read(UC_X86_REG_FPSW) & 0x3800, m.uc.reg_read(UC_X86_REG_FPTAG))
        mem = rd(m, SCR, SCR_SIZE) + rd(m, DATA, DATA_SIZE)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = None
        if first is not None:
            where = SCR + first if first < SCR_SIZE else DATA + first - SCR_SIZE
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            print("  differ:", [(hex(SCR + i if i < SCR_SIZE else DATA + i - SCR_SIZE), a[i:i + 4].hex(),
                                 b[i:i + 4].hex()) for i in diff[:8]])
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 2]}\n  ours:          {lb[k:k + 2]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True
    return None, results[0][0][0] != "ok"


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    State.data0 = rd(m, DATA, DATA_SIZE)
    stubs = Stubs(m)
    rng = random.Random(71)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, gen) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = faults = 0
        for _ in range(iterations):
            msg, fault = run_case(m, stubs, name, address, gen, rng)
            faults += fault and not msg
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        both = f" ({faults} fault the same way)" if faults else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
