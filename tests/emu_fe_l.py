#!/usr/bin/env python3
"""Differential test of fe_l.c (the front end's frame 0x4302d0 and the menu
screen builder 0x44a2a0) against the original.

Every function they call is a stub in both runs: it logs its arguments
(strings and colours behind stack pointers by content), returns scripted
values, fills output buffers with scripted text, and at scripted calls
changes a global, so values read after a call must be read after it. The
call logs and memory (the data section and the config/race records) must
match.

    tests/emu_fe_l.py EXE build/emu_l.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCRATCH, SCRATCH_SIZE = 0x01000000, 0x8000
CONFIG = SCRATCH            # 0x1200 bytes
RACE = SCRATCH + 0x2000     # 0x800 bytes
NETREC = SCRATCH + 0x3000   # *0x4cb4b8 record
DATA = (0x4A7000, 0x6D3000 - 0x4A7000)
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))

CALLEES = [
    0x427A70, 0x428240, 0x428470, 0x42DE50, 0x42DE70, 0x42E2A0, 0x42E6D0, 0x42F3C0, 0x432820, 0x4328A0, 0x4329B0,
    0x4368F0, 0x43692A, 0x436B7E, 0x4447D0, 0x445340, 0x445480, 0x445790, 0x446200, 0x446300, 0x446EA0, 0x4479D0,
    0x447ED0, 0x447EF0, 0x447F00, 0x447F10, 0x448000, 0x448070, 0x4480D0, 0x448140, 0x4481A0, 0x4484A0, 0x4485B0,
    0x4488C0, 0x448A00, 0x448A50, 0x448B50, 0x448BE0, 0x448C00, 0x448D70, 0x448EC0, 0x449060, 0x449FD0, 0x44A260,
    0x44A2A0, 0x44C6D0, 0x44C820, 0x44C950, 0x44C980, 0x44C9D0, 0x44CB10, 0x44CC50, 0x44CE50, 0x44DFD0, 0x44E2A0,
    0x44E320, 0x44E3C0, 0x44E410, 0x44E430, 0x44E470, 0x44E5B0, 0x44E640, 0x44E800, 0x44E8D0, 0x44EBD0, 0x44EFA0,
    0x44EFC0, 0x44EFE0, 0x44F000, 0x44F030, 0x44F2D0, 0x44F560, 0x44F5A0, 0x44F860, 0x44FAC0, 0x44FD50, 0x44FD70,
    0x44FD90, 0x44FDB0, 0x44FED0, 0x44FFB0, 0x4500F0, 0x450150, 0x4501A0, 0x450210, 0x450460, 0x450560, 0x4505C0,
    0x450620, 0x450680, 0x4506E0, 0x450740, 0x450810, 0x450870, 0x4508D0, 0x450930, 0x4509A0, 0x450A10, 0x450BA0,
    0x450E30, 0x450E90, 0x450EF0, 0x450F50, 0x451040, 0x451110, 0x451800, 0x4518A0, 0x451900, 0x451C00, 0x452940,
    0x452FF0, 0x4530D0, 0x453220, 0x4535B0, 0x455E90, 0x456800, 0x457020, 0x4579A0, 0x457E90, 0x45F500, 0x45F570,
    0x45F710, 0x45F790, 0x45F7B0, 0x462AE0, 0x462BB0, 0x472040, 0x472A90, 0x48A6B0, 0x48A840, 0x48B780, 0x48B7E0,
    0x48B940, 0x48D110, 0x48D1C0, 0x48D630, 0x490EC0, 0x490ED0, 0x4979F0,
]
NARGS = {0x427A70: 2, 0x428470: 1, 0x42E6D0: 4, 0x42F3C0: 5, 0x432820: 2, 0x4328A0: 1, 0x4329B0: 1, 0x445340: 5,
         0x445790: 6, 0x446200: 3, 0x446300: 8, 0x446EA0: 2, 0x4479D0: 2, 0x4480D0: 5, 0x448140: 5, 0x4481A0: 4,
         0x4484A0: 2, 0x4485B0: 3, 0x4488C0: 4, 0x448A00: 2, 0x448A50: 9, 0x448B50: 1, 0x448BE0: 2, 0x448C00: 7,
         0x448D70: 4, 0x448EC0: 4, 0x449060: 7, 0x44C6D0: 6, 0x44C820: 4, 0x44C950: 4, 0x44C980: 2, 0x44C9D0: 6,
         0x44CB10: 9, 0x44DFD0: 2, 0x44E2A0: 2, 0x44E320: 1, 0x44E410: 4, 0x44E430: 4, 0x44E470: 2, 0x44E5B0: 4,
         0x44E640: 2, 0x44E800: 1, 0x44EBD0: 2, 0x44EFA0: 2, 0x44EFC0: 2, 0x44EFE0: 2, 0x44F000: 3, 0x44F030: 5,
         0x44F2D0: 4, 0x44F560: 2, 0x44F5A0: 4, 0x44F860: 4, 0x44FAC0: 2, 0x44FD50: 2, 0x44FD70: 2, 0x44FD90: 2,
         0x44FDB0: 2, 0x44FFB0: 1, 0x4500F0: 1, 0x4501A0: 1, 0x450210: 2, 0x450460: 1, 0x451900: 4, 0x451C00: 3,
         0x4530D0: 3, 0x453220: 4, 0x4535B0: 4, 0x45F500: 1, 0x45F570: 2, 0x45F710: 5, 0x45F790: 1, 0x45F7B0: 1,
         0x462AE0: 3, 0x462BB0: 1, 0x472A90: 3, 0x48A840: 2, 0x48B780: 4, 0x48B7E0: 2, 0x48B940: 2, 0x48D630: 2,
         0x490EC0: 1, 0x4979F0: 4}
RETURNS = {0x452FF0, 0x448070, 0x436B7E, 0x4328A0, 0x448140, 0x44F560, 0x48A840, 0x48B780, 0x48B7E0, 0x43692A,
           0x44E410}
# stack-pointer arguments: output buffers (the stub writes text; content at
# the call is not compared), colours (3 or 4 bytes)
OUT_TEXT = {(0x4481A0, 0): 0x200, (0x42E6D0, 0): 0x40, (0x445340, 0): 0x40, (0x44E320, 0): 0x100,
            (0x4979F0, 0): 0x200}
OUT_COLOUR = {(0x432820, 0)}
COLOUR_BYTES = {(0x4530D0, 2): 3, (0x446200, 2): 4}
# globals a stub may change (at a scripted call), and how
MUTABLE = [(0x4AF64C, "screen"), (0x4B2198, None), (0x4AC990, "fade"), (0x4BE330, "dir"), (0x4BE334, "int"),
           (0x4BE338, "int"), (0x4BE328, "attract"), (0x6C8160, "level"), (0x4BE450, "quit"), (0x4CB48C, "flag"),
           (0x4CB590, "flag"), (0x4CB0D8, "count"), (0x4AC980, "int"), (0x4CB4C0, "int")]

# menu globals the callees read: their values at each call are logged too
WATCHED = [(0x4C9DF0, 0x30), (0x4CA2F0, 0x14), (0x4CB470, 0x138), (0x4AF5F0, 0x80), (0x4BE320, 0x20),
           (0x4AC980, 0x18), (0x6C8160, 4), (0x4AF798, 4), (0x4AF708, 4), (0x4AF88C, 0x14), (CONFIG, 0x200)]
# callees after which a global is read again, and what they may change
TARGETED = {0x436B7E: (0x4BE328, "attract"), 0x4328A0: (0x4BE334, "int"), 0x448140: (0x4B2198, None),
            0x44F560: (0x4AF64C, "screen"), 0x44FAC0: (0x4AF64C, "screen"), 0x48A840: (0x4CB48C, "flag"),
            0x45F710: (0x4B2198, None), 0x452940: (0x4CB4B8, None), 0x448A00: (0x4AC98C, "int")}

TESTS = {
    "menu_screen_build": (0x44A2A0, False),
    "frontend_frame": (0x4302D0, True),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rint(rng):
    return pick(rng, 0, 1, -1, rng.randrange(-0x400, 0x400), rng.getrandbits(32) - 0x80000000)


def mutant(rng, kind):
    if kind == "screen":
        return rng.randrange(0, 0x82)
    if kind == "fade":
        return pick(rng, 0, 0x100, rng.randrange(-5, 0x105))
    if kind == "dir":
        return pick(rng, 0, 1, 2, 3)
    if kind == "attract":
        return pick(rng, 5, 11, 0x81, 0x82, 0x7e, 0x259, 0x5dc, 0x1000, rng.getrandbits(16))
    if kind == "level":
        return pick(rng, 0, 400, 0xffff, 0xfff0, 0x10000, rng.getrandbits(32))
    if kind == "quit":
        return pick(rng, -1, 0, 1, 2, 5)
    if kind == "flag":
        return pick(rng, 0, 1, 2)
    if kind == "count":
        return rng.randrange(-1, 6)
    return rint(rng) & 0xFFFFFFFF


CASE = [0]


def make_state(rng, name):
    CASE[0] += 1
    config = bytearray(rng.getrandbits(8) for _ in range(0x1200))
    for o in (0x74, 0x80, 0x87, 0x88, 0x9c, 0x9d, 0x7, 0x11, 0x20, 0x1c, 0x1d, 0x85, 0x89, 0x8b):
        config[o] = pick(rng, 0, 1, 2, 3, 3, 4, rng.getrandbits(8))
    struct.pack_into("<i", config, 0x48, pick(rng, 0, 5, 0x7f, 0x80, 0x100, -1, rng.getrandbits(32) - 0x80000000))
    # the network strings live in the config: keep them terminated
    for o in range(0, 0x1200, 0x37):
        config[o] = 0
    race = bytearray(rng.getrandbits(8) for _ in range(0x800))
    race[0x551] = pick(rng, 7, 0, 1, rng.getrandbits(8))
    netrec = bytes(rng.getrandbits(8) for _ in range(0x600))
    screen = pick(rng, CASE[0] % 0x82, CASE[0] % 0x82, CASE[0] % 0x82, rng.randrange(0, 0x82),
                  rng.randrange(0, 0x82), rng.randrange(0, 0x82), -1, 0x82, 0x1000, rng.getrandbits(32) - 0x80000000)
    if screen == 0x2C:  # the multiplayer screen greys out "Networked" on purpose (fe_l.c): not compared
        screen = 0x2B
    nxt = pick(rng, -1, -1, 6, 9, 0xB, 0x11, 0x16, 0x1F, 0x28, 0x3C, 0x53, 0x5B, 0x5F, 0x71, 0x72,
               rng.randrange(0, 0x82), rng.randrange(-8, 0x100))
    if nxt == 0x2C:
        nxt = 0x2B
    g = {}
    for a, n in WATCHED:  # leftovers from earlier screens
        if a != CONFIG:
            g[a] = bytes(pick(rng, 0, rng.getrandbits(8)) for _ in range(n))
    g.update({
        0x4AF64C: struct.pack("<i", screen if name == "menu_screen_build" or rng.randrange(3) else
                              pick(rng, -1, 4, rng.randrange(0, 0x82))),
        0x4AF650: struct.pack("<i", nxt),
        0x4AC990: struct.pack("<i", pick(rng, 0, 0, 0x100, 0x100, rng.randrange(-4, 0x104))),
        0x4BE330: struct.pack("<i", pick(rng, 0, 1, 2, 1, 2, rng.randrange(-2, 5))),
        0x4CB58C: struct.pack("<I", pick(rng, 0, 1)),
        0x4CB590: struct.pack("<I", pick(rng, 0, 1)),
        0x4BE450: struct.pack("<i", pick(rng, -1, 0, 0, 1, 2, 7)),
        0x4AC994: struct.pack("<I", pick(rng, 0, 1, 1)),
        0x6CCD84: bytes([pick(rng, 0, 4, rng.getrandbits(8))]),
        0x4BE328: struct.pack("<I", pick(rng, 5, 10, 11, 0x81, 0x82, 0x7d, 0x7e, 0x258, 0x259, 0x5db, 0x5dc,
                                         0x1000, 0x17ff, rng.getrandbits(16), rng.getrandbits(32))),
        0x6C8160: struct.pack("<I", pick(rng, 0, 400, 0xffff, 0xfffe, 0xff00, 0x10000, rng.getrandbits(16),
                                         rng.getrandbits(32))),
        0x4BE32C: struct.pack("<i", pick(rng, 0, 1, 2, 3)),
        0x4BE334: struct.pack("<i", rint(rng)),
        0x4BE338: struct.pack("<i", rint(rng)),
        0x4ACC38: struct.pack("<i", pick(rng, 640, 1280, 3440, rint(rng))),
        0x4ACC3C: struct.pack("<i", pick(rng, 480, 1440, rint(rng))),
        0x4CB474: struct.pack("<i", rint(rng)), 0x4CB478: struct.pack("<i", rint(rng)),
        0x4CB47C: struct.pack("<i", rint(rng)), 0x4CB480: struct.pack("<i", rint(rng)),
        0x4ACC54: struct.pack("<i", pick(rng, 2, 1, 0)),
        0x4CB48C: struct.pack("<I", pick(rng, 0, 1, 2)),
        0x4CB59C: struct.pack("<I", pick(rng, 0, 1)),
        0x4CB598: struct.pack("<I", pick(rng, 0, 1)),
        0x4CB55C: struct.pack("<I", pick(rng, 0, 1)),
        0x4CB558: struct.pack("<I", pick(rng, 0, 1)),
        0x4CB0D8: struct.pack("<i", pick(rng, 0, 1, 3, 5, -1)),
        0x4AF88C: struct.pack("<I", pick(rng, config[0x1C], rng.getrandbits(8))),
        0x4AF890: struct.pack("<I", pick(rng, config[0x1D], rng.getrandbits(8))),
        0x4AF894: struct.pack("<I", pick(rng, config[0x87], rng.getrandbits(8))),
        0x4AF898: struct.pack("<I", pick(rng, config[0x8B], rng.getrandbits(8))),
        0x4AF89C: struct.pack("<I", pick(rng, config[0x85], rng.getrandbits(8))),
        0x4C98F4: b"".join(struct.pack("<I", pick(rng, 0, 1, 2, 3, rng.getrandbits(32))) for _ in range(8)),
    })
    calls = rng.randrange(2, 80)
    mutations = {}
    for _ in range(pick(rng, 0, 0, 1, 2, 4)):
        a, kind = rng.choice(MUTABLE)
        value = (RACE if rng.randrange(2) else CONFIG) if kind is None else mutant(rng, kind)
        mutations[rng.randrange(calls)] = (a, value)
    for address, (a, kind) in TARGETED.items():
        if rng.randrange(4) == 0:
            value = (RACE if rng.randrange(2) else CONFIG) if kind is None else mutant(rng, kind)
            if address == 0x452940 and kind is None:
                value = NETREC + 0x100
            mutations[address] = (a, value)
    returns = [pick(rng, 0, 0, 1, 1, 2, 5, rng.randrange(-0x100, 0x100), rng.getrandbits(32)) for _ in range(200)]
    texts = [bytes(rng.randrange(0x20, 0x7F) for _ in range(rng.randrange(0, 20))) for _ in range(40)]
    colours = [bytes(rng.getrandbits(8) for _ in range(3)) for _ in range(8)]
    return dict(config=bytes(config), race=bytes(race), netrec=netrec, globals=g, mutations=mutations,
                returns=returns, texts=texts, colours=colours)


class Stubs:
    def __init__(self, m):
        self.m = m
        for address in CALLEES:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.active = set()

    def reset(self, st, active):
        self.log = []
        self.active = active
        self.returns = list(st["returns"])
        self.texts = list(st["texts"])
        self.colours = list(st["colours"])
        self.mutations = dict(st["mutations"])
        self.count = 0

    def _hook(self, uc, address, size, _):
        if address not in self.active:
            return
        n = NARGS.get(address, 0)
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (n + 1), bytes(uc.mem_read(esp, 4 * (n + 1))))
        args = list(words[1:])
        for i, a in enumerate(args):
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                if (address, i) in OUT_TEXT:
                    size_ = OUT_TEXT[(address, i)]
                    text = (self.texts.pop(0) if self.texts else b"x")[:size_ - 1] + b"\0"
                    if address == 0x4979F0:  # sprintf: the format text, then the numbers
                        fmt = bytes(uc.mem_read(args[1], 0x40)).split(b"\0")[0]
                        text = (fmt + b"%x %x" % (args[2], args[3]))[:size_ - 1] + b"\0"
                    uc.mem_write(a, text)
                    args[i] = ("out",)
                elif (address, i) in OUT_COLOUR:
                    uc.mem_write(a, self.colours.pop(0) if self.colours else b"\1\2\3")
                    args[i] = ("out",)
                elif (address, i) in COLOUR_BYTES:
                    args[i] = ("colour", bytes(uc.mem_read(a, COLOUR_BYTES[(address, i)])))
                else:
                    args[i] = ("text", bytes(uc.mem_read(a, 0x200)).split(b"\0")[0])
        watched = b"".join(bytes(uc.mem_read(a, n)) for a, n in WATCHED)
        self.log.append((hex(address), tuple(args), watched))
        mutation = self.mutations.pop(self.count, None) or self.mutations.pop(address, None)
        if mutation:
            uc.mem_write(mutation[0], struct.pack("<I", mutation[1] & 0xFFFFFFFF))
        self.count += 1
        eax = (self.returns.pop(0) if self.returns else 0) if address in RETURNS else 0
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def run_case(m, stubs, data0, name, address, returns, rng):
    st = make_state(rng, name)
    results = []
    active = set(CALLEES) - {address}
    for target in (address, m.symbols[name]):
        m.write(DATA[0], data0)
        m.write(SCRATCH, bytes(SCRATCH_SIZE))
        m.write(CONFIG, st["config"])
        m.write(RACE, st["race"])
        m.write(NETREC, st["netrec"])
        for a, v in st["globals"].items():
            m.write(a, v)
        m.write(0x4B2198, struct.pack("<I", CONFIG))
        m.write(0x4B1960, struct.pack("<I", RACE))
        m.write(0x4CB4B8, struct.pack("<I", NETREC))
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st, active)
        r = m.call(target, [])
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(*DATA) + m.read(SCRATCH, SCRATCH_SIZE)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        diff = [i for i in range(0, len(a), 4) if a[i:i + 4] != b[i:i + 4]]
        where = ""
        if diff:
            i = diff[0]
            addr = DATA[0] + i if i < DATA[1] else SCRATCH + i - DATA[1]
            where = f" (memory differs at {addr:#x}: {a[i:i + 4].hex()} ours {b[i:i + 4].hex()})"
        logs = ""
        la, lb = results[0][2], results[1][2]
        if la != lb:
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        screen = struct.unpack("<i", st["globals"][0x4AF64C])[0]
        return f"{name} (screen {screen:#x}): original {results[0][0]} ours {results[1][0]}{where}{logs}"
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 3000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    data0 = m.read(*DATA)
    stubs = Stubs(m)
    rng = random.Random(23)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        outcomes = {}
        for _ in range(iterations):
            msg = run_case(m, stubs, data0, name, address, returns, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
        del outcomes
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
