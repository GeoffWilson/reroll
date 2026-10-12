#!/usr/bin/env python3
"""Differential test of orig_pe.c (group PE: the front end's text helpers,
sprite quads, box frame, colour fades and a mode's race set-up,
0x42fc40-0x432cd0) against the original, on random state.

Callees outside the group that look up fonts, print, link lists or belong to
the race set-up are logging stubs in both runs (the call log records as many
arguments as the callee reads; the debug printf's format by content) with
scripted return values: font_find 0x42e050, the debug printf 0x476ec0, the
list link 0x429080, and 0x433cd0 / 0x433670 / 0x433550 / 0x4335c0 /
0x436935 / 0x432b70 / 0x432bd0 / 0x432c80 / 0x433ab0 / 0x435d43. Pure code
runs for real in both: the texture sizes 0x431670 / 0x4316a0, the packet
flag 0x4270e0, the link store 0x4290a0, and the group's own 0x4310f0 (from
0x4316d0). The x87 control word is 0x27f, 0x37f or 0x07f; memory, the
return value, the call log, ebx / esi / edi / ebp and the x87 stack top /
tag word are compared.

    tests/emu_orig_pe.py EXE build/emu_pe.elf [iterations] [names...] [-v] [-c]
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
OPTIONS = SCR + 0x0000  # 0x200
RACE = SCR + 0x1000  # 0x1000
ARG = SCR + 0x2000  # small argument blocks (0x1000)
STR = SCR + 0x3000
INFO = SCR + 0x4000  # front-end image info (+0x2e)
TEX = SCR + 0x5000  # texture records (0x2000)
FONT = SCR + 0x8000  # 0x1200
TEXT = SCR + 0xA000  # 128 x 0x14c text records
PKT = SCR + 0x16000  # packets (0x1000)
PKTA = SCR + 0x18000  # glyph packets (16 x 0x84)
PKTB = SCR + 0x19000
WORDS = SCR + 0x1A000  # glyph vertex words (16 x 32)

REGIONS = [
    (0x4A7148, 4),
    (0x4AC850, 0x10),
    (0x4AC990, 4),
    (0x4ACA7C, 0x10),
    (0x4ACC38, 8),
    (0x4B11D8, 4),
    (0x4B1960, 4),
    (0x4B2198, 4),
    (0x4BE2C0, 0x1A0),
]

# argument kinds: v value, s string
STUBS = {
    0x42E050: ("font_find", "v"),
    0x476EC0: ("debug_printf", "sv"),
    0x429080: ("list_link", "vvv"),
    0x433CD0: ("race_option_clamp", ""),
    0x433670: ("race_track_choose", "vv"),
    0x433550: ("track_world", "v"),
    0x4335C0: ("track_world_place", "v"),
    0x436935: ("fe_time", ""),
    0x432B70: ("start_kind_set", ""),
    0x432BD0: ("weapons_flags_set", "vv"),
    0x432C80: ("race_option554_set", ""),
    0x433AB0: ("race_slots_init", "vvvvv"),
    0x435D43: ("network_session_poll", "v"),
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
        self.put(0x4B1960, "<I", RACE)
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

    def putb(self, address, data):
        if SCR <= address < SCR + SCR_SIZE:
            self.scr[address - SCR:address - SCR + len(data)] = data
            return
        a = self.find(address)
        self.mem[a][address - a:address - a + len(data)] = data


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

    def _hook(self, uc, address, size, _):
        name, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        logged = []
        for kind, a in zip(kinds, words[1:]):
            if kind == "v":
                logged.append(a)
            elif kind == "s":
                logged.append(rd(self.m, a, 0x40).split(b"\0")[0])
        self.log.append((name, tuple(logged)))
        values = self.script.get(name)
        uc.reg_write(UC_X86_REG_EAX, (values.pop(0) if values else 0) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


# ---------------------------------------------------------------- generators

def coord(rng):
    return pick(rng, rng.randrange(-100, 700), rng.randrange(-100, 700), rng.randrange(-5000, 5000),
                rng.getrandbits(32))


def size(rng):
    return pick(rng, -1, -1, rng.randrange(0, 300), rng.randrange(-50, 300), rng.getrandbits(32))


def set_screen(st, rng):
    st.put(0x4ACC38, "<I", pick(rng, 640, 800, 1024, 3440, rng.randrange(1, 4000), rng.getrandbits(32)))
    st.put(0x4ACC3C, "<I", pick(rng, 480, 600, 768, 1440, rng.randrange(1, 4000), rng.getrandbits(32)))


def set_textures(st, rng):
    st.put(0x4BE344, "<I", INFO)
    st.put(INFO + 0x2E, "<H", rng.randrange(0, 0x60))
    st.put(0x4BE340, "<I", TEX)


def g_format(st, rng, m):
    return [ARG, pick(rng, 0, rng.randrange(0, 1000), rng.randrange(0, 999999999), rng.getrandbits(32),
                      0xFFFFFFFF, 999999999, 1000000000)], None


def g_parse(st, rng, m):
    n = rng.randrange(0, 12)
    s = bytes(rng.choice(b"0123456789") for _ in range(n)) + bytes([rng.choice(b"\0 /:a9\xff")])
    st.putb(STR, s + b"\0")
    return [STR, ARG], 0xFFFFFFFF


def g_slot(st, rng, m):
    st.put(0x4BE310, "<I", TEXT)
    st.put(0x4BE2F8, "<i", pick(rng, rng.randrange(0, 128), rng.randrange(-5, 140), rng.getrandbits(32)))
    full = rng.randrange(3) == 0
    for i in range(128):
        b = rng.getrandbits(8)
        if full or rng.randrange(4):
            b |= 0x40
        else:
            b &= ~0x40
        st.put(TEXT + i * 0x14C + 1, "<B", b)
    return [], 0xFFFFFFFF


def g_tab(st, rng, m):
    stops = sorted(rng.randrange(0, 0x10000) for _ in range(8)) if rng.randrange(3) else \
        [rng.getrandbits(16) for _ in range(8)]
    for i, v in enumerate(stops):
        st.put(0x4AC850 + 2 * i, "<H", v)
    return [pick(rng, rng.getrandbits(16), rng.getrandbits(32), stops[rng.randrange(8)])], 0xFFFFFFFF


def g_glyph(st, rng, m):
    st.script["font_find"] = [pick(rng, FONT, FONT, FONT, 0)]
    st.put(FONT + 4, "<I", pick(rng, 0x100, rng.randrange(0, 0x400), rng.getrandbits(32)))
    st.put(FONT + 8, "<I", rng.randrange(0, 0x40))
    st.put(FONT + 0x14, "<I", pick(rng, 0, rng.randrange(0, 0x20), rng.getrandbits(32)))
    st.put(FONT + 0x18, "<I", pick(rng, 0, 1, rng.getrandbits(32)))
    for c in range(256):
        st.put(FONT + 0x101C + c, "<B", 0xFF if rng.randrange(5) == 0 else rng.getrandbits(8))
    st.put(0x4BE2FC, "<H", pick(rng, rng.randrange(0, 16), rng.randrange(0, 16), 0x800, 0x801, 0xFFFF))
    st.put(0x4BE2E8, "<I", PKTA + 0x84 * rng.randrange(16))
    st.put(0x4BE308, "<I", PKTA)
    st.put(0x4BE30C, "<I", PKTB)
    st.put(0x4BE304, "<I", WORDS)
    code = pick(rng, rng.randrange(0, 256), rng.randrange(0, 256), 0xD6, 0x100, rng.getrandbits(32))
    colours = pick(rng, 0, ARG + 0x100)
    flags = pick(rng, 0, 0x10, rng.getrandbits(32))
    return [rng.getrandbits(32), code, colours, coord(rng), ARG, ARG + 0x40, flags], 0xFFFFFFFF


def g_none(st, rng, m):
    return [], 0xFFFFFFFF


def g_sprite(st, rng, m):
    set_screen(st, rng)
    set_textures(st, rng)
    return [PKT, rng.randrange(0, 0xA0), pick(rng, 0, ARG), coord(rng), coord(rng), size(rng), size(rng),
            pick(rng, 0x44FA0000, rng.getrandbits(32))], None


def g_quad(st, rng, m):
    set_screen(st, rng)
    return [PKT, pick(rng, 0, ARG), coord(rng), coord(rng), size(rng), size(rng),
            pick(rng, 0x44FA0000, rng.getrandbits(32))], None


def g_frame_init(st, rng, m):
    set_screen(st, rng)
    set_textures(st, rng)
    return [PKT], None


def g_frame_place(st, rng, m):
    set_screen(st, rng)
    set_textures(st, rng)
    return [rng.getrandbits(32), PKT, pick(rng, 0, ARG), coord(rng), coord(rng), coord(rng), coord(rng)], None


def fade(st, rng):
    st.put(0x4AC990, "<i", pick(rng, 0, 0x100, rng.randrange(0, 0x101), rng.randrange(-300, 600), rng.getrandbits(32)))


def g_fade_apply(st, rng, m):
    fade(st, rng)
    return [ARG, pick(rng, ARG + 0x10, ARG + 1, ARG)], None


def g_fade_scale(st, rng, m):
    fade(st, rng)
    return [pick(rng, rng.randrange(-0x200, 0x200), rng.getrandbits(32))], 0xFFFFFFFF


def g_pulse(st, rng, m):
    st.put(0x4BE320, "<I", pick(rng, rng.randrange(0, 1000), rng.getrandbits(32)))
    return [pick(rng, PKT, PKT, 0), pick(rng, ARG, ARG, 0), pick(rng, 4, 1, 0, 3, -1, rng.getrandbits(32)),
            pick(rng, 0, 2, 4, 6, rng.getrandbits(32))], None


def g_race_setup(st, rng, m):
    st.script["race_track_choose"] = [pick(rng, -1, rng.randrange(0, 30), rng.getrandbits(32))]
    st.script["track_world"] = [rng.getrandbits(32)]
    st.script["track_world_place"] = [rng.getrandbits(32)]
    st.script["fe_time"] = [rng.getrandbits(32)]
    st.script["network_session_poll"] = [pick(rng, 0, 1, 2, 3)]
    st.put(OPTIONS + 0xB, "<B", pick(rng, 0, 1, 20, 21, rng.getrandbits(8)))
    st.put(OPTIONS + 0x87, "<B", pick(rng, 0, 1, 2, 3, 4, rng.getrandbits(8)))
    st.put(OPTIONS + 0x43, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    st.put(OPTIONS + 0x83, "<B", pick(rng, 0, 1, rng.getrandbits(8)))
    return [pick(rng, *range(11), 7, 7, 7, rng.getrandbits(32)), rng.getrandbits(32), rng.getrandbits(32)], \
        0xFFFFFFFF


TESTS = {
    "decimal9_format": (0x42FC40, g_format),
    "decimal_parse": (0x42FCD0, g_parse),
    "text_slot_free": (0x42FD10, g_slot),
    "text_tab_next": (0x42FD60, g_tab),
    "glyph_place": (0x42FD90, g_glyph),
    "language_get": (0x430120, g_none),
    "fe_sprite_gouraud_set": (0x430EA0, g_sprite),
    "fe_sprite_set": (0x4310F0, g_sprite),
    "fe_sprite_set_512": (0x431300, g_sprite),
    "fe_quad_flat_set": (0x4314D0, g_quad),
    "fe_frame_sprites_init": (0x4316D0, g_frame_init),
    "fe_box_frame_place": (0x431780, g_frame_place),
    "colour_fade_apply": (0x432820, g_fade_apply),
    "fade_scale": (0x4328A0, g_fade_scale),
    "colour_pulse_scale": (0x4328C0, g_pulse),
    "front_mode_race_setup": (0x432CD0, g_race_setup),
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


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, mask = gen(st, rng, m)
    cw = rng.choice((0x27F, 0x27F, 0x37F, 0x07F))
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok",) if mask is None else ("ok", r[1] & mask)
            r = r + tuple(m.uc.reg_read(x) for x in SAVED)
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


def func_sizes():
    sizes = {}
    try:
        for line in open("build/groups/pe_owned.txt"):
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
    rng = random.Random(53)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-c")]
    sizes = func_sizes()
    for name, (address, gen) in TESTS.items():
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
        for _ in range(iterations):
            msg, fault = run_case(m, stubs, name, address, gen, rng)
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
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{both}{cov}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
