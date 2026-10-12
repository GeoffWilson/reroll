#!/usr/bin/env python3
"""Differential test of orig_pj.c (group PJ: the front end's highlighted
item / second / car choice / mode models and the flame texture,
0x455e90-0x457f80) against the original, on random state.

Logging stubs (as many arguments as the callee reads; pointer arguments by
the content they point to; a light list by the 0x12 bytes of each light)
with scripted return values: fade_scale 0x4328a0, view_rect_set 0x428850,
ot_clear_reverse 0x427a70, draw_otag 0x428470, menu_model_draw 0x4538e0,
menu_model_corners_draw 0x453a00, fe_box_frame_place 0x431780, language_get
0x430120, charanim_upload 0x4114b0, car_stat_bar 0x456f70, the texture
surface lock / unlock / new / upload 0x428c30 / 0x428c60 / 0x428c80 /
0x428c90 (the lock writes its scripted pixels pointer, depth and pitch),
pixels_rgb_convert 0x41e1f0, texture_palettes_apply 0x429070, and for
flame_update / flame_init the group's own 0x457bf0 / 0x457d50. Pure code
runs for real in both: the menu model lookup 0x4539d0, matrix_from_angles
0x426f80, matrix_identity 0x4264f0, the GTE emulation 0x427fc0 / 0x428090,
charanim_texture_info 0x411740, flame_texture_info 0x457d10, mem_fill
0x476f50. The x87 control word is 0x27f, 0x37f or 0x07f; memory, the call
log, ebx / esi / edi / ebp and the x87 stack top / tag word are compared.

    tests/emu_orig_pj.py EXE build/emu_pj.elf [iterations] [names...] [-v] [-c]
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
SCR_SIZE = 0x40000
REC = SCR + 0x0000  # sprite records of *0x4be340 (and the bar colours at +0x210)
OPTIONS = SCR + 0x1000
BODY = SCR + 0x2000  # OT link targets
PIXELS = SCR + 0x10000  # the locked surface

REGIONS = [
    (0x4ACC38, 8),  # screen size
    (0x4AF610, 0x60),  # car choice ids / models, picture frame, items 0x4af660 / 0x4af664
    (0x4B0A20, 0x50),  # the car choice lights, 0x4b0a64
    (0x4B11D8, 4),  # flame generator
    (0x4B2198, 4),  # options pointer
    (0x4B6600, 8),  # picture size
    (0x4B7DA8, 4),  # picture texture handle
    (0x4BE334, 0x10),  # menu box position, sprite table pointer
    (0x4CB474, 0x10),  # panel position
    (0x4CB540, 4),
    (0x4CB800, 0x200),  # converted palette
    (0x4CBA00, 0x1CA0),  # highlighted sprite, flame handle, menu model records
    (0x4CD6A0, 0x1E0),  # the four bar quads
    (0x4CF750, 0x78),  # driver picture sprite
    (0x4CFA98, 0x400),  # flame palette
    (0x4D0710, 0x40),  # model angles, bar lengths
    (0x6B9D60, 0x5400),  # flame buffers
    (0x6CCB00, 0x388),  # GTE emulation state, 0x6cce84
    (0x6CD510, 0x200),  # ordering table entries
]

# argument kinds: v value, m<n> n bytes at a pointer, L a light list
STUBS = {
    0x4328A0: ("fade_scale", "v"),
    0x428850: ("view_rect_set", "vvvv"),
    0x427A70: ("ot_clear_reverse", "vv"),
    0x428470: ("draw_otag", "v"),
    0x4538E0: ("menu_model_draw", ["v", "m32", "m12", "L", "v"]),
    0x453A00: ("menu_model_corners_draw", ["v", "m32", "m12", "L", "m8", "m8"]),
    0x431780: ("fe_box_frame_place", ["v", "v", "m4", "v", "v", "v", "v"]),
    0x430120: ("language_get", ""),
    0x4114B0: ("charanim_upload", "vv"),
    0x456F70: ("car_stat_bar", "vv"),
    0x428C30: ("texture_surface_lock", "vv"),
    0x428C60: ("texture_surface_unlock", ["v", "v", "m1024"]),
    0x428C80: ("texture_handle_new", ""),
    0x428C90: ("texture_surface_upload", "vvvvvv"),
    0x41E1F0: ("pixels_rgb_convert", "vvvv"),
    0x429070: ("texture_palettes_apply", ""),
    0x457BF0: ("flame_step", ""),
    0x457D50: ("flame_upload", ""),
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
        self.lock = None
        self.put(0x4B2198, "<I", OPTIONS)
        self.put(0x4BE340, "<I", REC)
        # ordering table entries: link targets in scratch memory
        for i in range(0x80):
            self.put(0x6CD510 + 4 * i, "<I", BODY + 4 * rng.randrange(0x400) + pick(rng, 0, 0, 0x80000000))
        self.put(0x6CCE84, "<I", pick(rng, 0, 1, rng.getrandbits(32)))

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
        self.lock = None
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, active, lock):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.active = active
        self.lock = lock

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
            elif kind == "L":
                lights = struct.unpack("<3I", rd(self.m, a, 12))
                logged.append(tuple(rd(self.m, p, 0x12) if p else 0 for p in lights))
            else:
                logged.append(rd(self.m, a, int(kind[1:])))
        if address == 0x428C30 and self.lock is not None:
            pixels, bpp, pitch = self.lock
            uc.mem_write(words[3], struct.pack("<I", pixels))
            uc.mem_write(words[5], struct.pack("<I", bpp))
            uc.mem_write(words[6], struct.pack("<I", pitch))
        self.log.append((name, tuple(logged)))
        values = self.script.get(name)
        uc.reg_write(UC_X86_REG_EAX, (values.pop(0) if values else 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def set_screen(st, rng):
    st.put(0x4ACC38, "<I", pick(rng, 640, 800, 1024, 3440, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600, 768, 1440, rng.randrange(1, 4000), rng.getrandbits(32)))


def coord(rng):
    return pick(rng, rng.randrange(-100, 700), rng.randrange(-5000, 5000), rng.getrandbits(32))


def s16(rng):
    return pick(rng, rng.randrange(-0x1000, 0x1001), rng.randrange(-0x8000, 0x8000), 0, 0x1000)


def records(st, rng, ids):
    """Fill the 100 menu model records with other ids; place each of ids at
    a random slot with random angles and a rotation-ish matrix."""
    used = set(i & 0xFFFFFFFF for i in ids)
    for i in range(100):
        v = pick(rng, rng.randrange(-5, 200), rng.getrandbits(32), 0)
        while (v & 0xFFFFFFFF) in used:
            v = rng.getrandbits(32)
        st.put(0x4CBA80 + i * 0x48, "<I", v)
    free = list(range(100))
    rng.shuffle(free)
    for k, mid in enumerate(ids):
        r = 0x4CBA80 + free[k] * 0x48
        st.put(r, "<i", mid)
        for off in range(8, 0x48, 2):
            st.put(r + off, "<h", s16(rng))
        if rng.randrange(2):
            for i in range(9):
                st.put(r + 8 + 2 * i, "<h", pick(rng, 0x1000, 0, rng.randrange(-0x1000, 0x1001)))


def g_item(st, rng):
    set_screen(st, rng)
    kind = rng.randrange(6)
    if kind == 0:
        mid = -1
    elif kind < 3:
        mid = pick(rng, rng.randrange(0, 0x80), rng.randrange(-0x100, 0))
        if mid != -1 and rng.randrange(8):
            records(st, rng, [mid])
        else:
            records(st, rng, [])
    else:
        mid = pick(rng, rng.randrange(0x80, 0xC0), rng.randrange(0x80, 0xC0), 0x80)
        for off in range(0, 0x400, 4):
            if rng.randrange(4) == 0:
                st.put(REC + off, "<I", rng.getrandbits(32) & 0x3F3F3F3F)
    st.put(0x4AF660, "<i", mid)
    for a in (0x4BE334, 0x4BE338):
        st.put(a, "<i", coord(rng))
    st.script["fade_scale"] = [pick(rng, 0, 0x80, rng.randrange(0, 0x81), rng.getrandbits(32))]
    return []


def g_second(st, rng):
    set_screen(st, rng)
    mid = pick(rng, -1, rng.randrange(0, 0x80), rng.randrange(0, 0x80), rng.getrandbits(32))
    records(st, rng, [mid] if mid != -1 and rng.randrange(8) else [])
    st.put(0x4AF664, "<i", mid)
    for a in (0x4CB478, 0x4CB47C):
        st.put(a, "<i", coord(rng))
    return []


def g_car(st, rng):
    set_screen(st, rng)
    ids = [rng.getrandbits(32) for _ in range(7)]
    st.put(0x4AF660, "<I", pick(rng, rng.choice(ids), rng.choice(ids), rng.choice(ids), rng.getrandbits(32)))
    for i in range(7):
        st.put(0x4AF614 + 4 * i, "<I", ids[i])
    for a in (0x4CB474, 0x4CB478, 0x4CB47C, 0x4CB540):
        st.put(a, "<i", coord(rng))
    st.put(0x4AF654, "<i", pick(rng, rng.randrange(0, 0x30), rng.randrange(-4, 4), rng.getrandbits(32)))
    st.put(0x4D0730, "<I", pick(rng, 0x2000, 0x2800, 0x3000, 0, rng.getrandbits(32)))
    for k in range(3):
        for j in range(3):
            st.put(0x4D0718 + 8 * k + 2 * j, "<h", s16(rng))
            st.put(0x4B0A40 + 8 * k + 2 * j, "<h", pick(rng, rng.randrange(-40, 40), s16(rng)))
        st.put(0x4B0A28 + 8 * k, "<I", rng.getrandbits(32))
        st.put(0x4B0A2C + 8 * k, "<I", rng.getrandbits(32))
    # bar lengths near their targets
    for k in range(4):
        st.put(0x4D0734 + 4 * k, "<I", pick(rng, rng.randrange(0, 0x100), rng.randrange(0, 0x400),
                                             rng.getrandbits(32)))
    bar = REC + 0x210
    if rng.randrange(20) == 0:
        st.put(bar + 4, "<B", st.scr[bar - SCR])  # zero denominator: both fault
    st.script["language_get"] = [pick(rng, 0, 1, rng.getrandbits(32))]
    st.script["car_stat_bar"] = [pick(rng, rng.randrange(0, 0x100), rng.randrange(0, 0x100), rng.getrandbits(32))
                                 for _ in range(4)]
    return []


def g_mode(st, rng):
    set_screen(st, rng)
    st.put(0x4AF660, "<i", pick(rng, 3, 4, 3, 4, 0x39, 0x3A, 0x3B, 0x3C, rng.randrange(0, 0x40), rng.getrandbits(32)))
    st.put(OPTIONS + 0x1C, "<B", pick(rng, 1, 2, 3, 4, 0, 5, rng.getrandbits(8)))
    for a in (0x4CB478, 0x4CB47C):
        st.put(a, "<i", coord(rng))
    return []


def g_step(st, rng):
    return []


def g_info(st, rng):
    return [SCR + 0x100, SCR + 0x110, SCR + 0x120, SCR + 0x130, SCR + 0x140]


def g_upload(st, rng):
    bpp = pick(rng, 8, 8, 16, 16, 24, 0, rng.getrandbits(32))
    pitch = pick(rng, 0x80, 0x100, 0x200, rng.randrange(0x80, 0x200) & ~3)
    if bpp == 16:
        pitch = pick(rng, 0x100, 0x200, rng.randrange(0x100, 0x200) & ~3)
    st.lock = (PIXELS + rng.randrange(0x100), bpp, pitch)
    st.script["texture_surface_lock"] = [pick(rng, 1, 1, 1, 0, rng.getrandbits(32))]
    return []


def g_update(st, rng):
    return []


def g_init(st, rng):
    st.script["texture_handle_new"] = [rng.getrandbits(32)]
    return []


TESTS = {
    "fe_item_model_draw": (0x455E90, g_item, {0x4328A0, 0x428850, 0x427A70, 0x428470, 0x4538E0}),
    "fe_second_model_draw": (0x456800, g_second, {0x428850, 0x427A70, 0x428470, 0x4538E0}),
    "fe_car_choice_draw": (0x457020, g_car, {0x428850, 0x427A70, 0x428470, 0x453A00, 0x431780, 0x430120,
                                             0x4114B0, 0x456F70}),
    "fe_mode_model_draw": (0x4579A0, g_mode, {0x428850, 0x427A70, 0x428470, 0x453A00}),
    "flame_step": (0x457BF0, g_step, set()),
    "flame_texture_info": (0x457D10, g_info, set()),
    "flame_upload": (0x457D50, g_upload, {0x428C30, 0x428C60}),
    "flame_update": (0x457E90, g_update, {0x457BF0, 0x457D50}),
    "flame_init": (0x457EA0, g_init, {0x428C80, 0x428C90, 0x41E1F0, 0x429070, 0x457BF0, 0x457D50}),
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
    args = gen(st, rng)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script, active, st.lock)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) + tuple(m.uc.reg_read(x) for x in SAVED)
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
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs, True, None
    log = results[0][2]
    return None, results[0][0][0] != "ok", log


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pj_owned.txt"):
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
    rng = random.Random(61)
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
        seen = set()
        size = sizes.get(address, 0)
        if "-c" in sys.argv and size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        n = iterations if name not in ("flame_step",) else max(1, iterations // 5)
        for _ in range(n):
            msg, fault, log = run_case(m, stubs, name, address, gen, active, rng)
            faults += fault and not msg
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {n} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
