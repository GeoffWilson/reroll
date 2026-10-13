#!/usr/bin/env python3
"""Differential test of orig_xd.c (group XD: the last reachable originals,
0x4219d0-0x496c60: name entry input and box, network menu boxes and player
slot quads, session search, network helpers, movie subtitles, CD replay)
against the original, on random state.

Every callee outside the group is a logging stub in both runs (as many
arguments as params.txt lists; strings and pointed-to buffers by content),
including GetKeyState through the import slot 0x4a3240 and IDirectPlay's
EnumSessions through a fake vtable (both stdcall), and the handler's
callbacks. mem_fill 0x476f50 and the group's own functions run for real.
Return value (when not void), the global regions below and the scratch
pages are compared bit for bit; the stack is zeroed before each run and esi
is random.

    tests/emu_orig_xd.py EXE build/emu_xd.elf [iterations] [names...]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,  # noqa: E402
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

SCR = 0x01000000
SCR_SIZE = 0x40000
CONFIG = SCR + 0x0000
TEXT = SCR + 0x1000  # name entry buffer
RECTS = SCR + 0x2000  # 0x4be340 + 0x380 / + 0x390
LISTS = SCR + 0x3000  # ordering list heads
DATA = SCR + 0x3800  # net_send_type7 input
OBJ = SCR + 0x3900  # IDirectPlay object, vtable at +0x10
STUBS_AT = SCR + 0x3C00  # code addresses for the import / COM / callback stubs
PIXELS = SCR + 0x4000  # font image (0x2000)
PALETTE = SCR + 0x6000  # 256 words
SURF = SCR + 0x8000  # the locked surface (0x10000)

GETKEYSTATE = STUBS_AT + 0x00
ENUMSESSIONS = STUBS_AT + 0x10
MENU_HOOK = STUBS_AT + 0x20
ON_CHANGE = STUBS_AT + 0x30
ON_EVENT = STUBS_AT + 0x40

REGIONS = [
    (0x4A3240, 4),
    (0x4ACC30, 0x20),
    (0x4AF430, 0x20),
    (0x4B2190, 0x10),
    (0x4B4A90, 4),
    (0x4BE300, 0x80),
    (0x4BEB30, 0x20),
    (0x4C9800, 0x400),
    (0x4CB000, 0x800),
    (0x4D5F40, 0x40),
    (0x5D2000, 0x4300),
    (0x6C0A70, 0x1110),
    (0x6C8000, 4),
    (0x6CCE80, 0x8A0),
]


def S(name, kinds, pop=0):
    return (name, kinds, pop)


# argument kinds: v dword, s string, p dword pointed at, o out pointer (written by the stub),
# d the lock's description (written), m the type 7 message, e the session description
STUBS = {
    0x41DB00: S("d3d_render_states", ""),
    0x431780: S("fe_box_frame_place", "vvpvvvv"),
    0x443B20: S("font_image_info", "ooo"),
    0x4875A0: S("dd_surface_lock", "vvd"),
    0x487610: S("dd_surface_unlock", "v"),
    0x4475F0: S("fe_repeat_select", "v"),
    0x447AB0: S("fe_menu_back", ""),
    0x4479D0: S("fe_menu_goto", "vv"),
    0x472A90: S("voice_play_plain", "vvv"),
    0x48CDD0: S("net_message_send_to", "vvmvv"),
    0x496770: S("cdaudio_play", "v"),
    0x4980D0: S("strstr", "vs"),
    0x498540: S("toupper", "v"),
    0x49867B: S("tolower", "v"),
    GETKEYSTATE: S("GetKeyState", "v", 4),
    ENUMSESSIONS: S("EnumSessions", "vevvvv", 24),
    MENU_HOOK: S("menu_hook", "v"),
    ON_CHANGE: S("on_change", "s"),
    ON_EVENT: S("on_event", "v"),
}


def rd(m, address, n):
    return bytes(m.uc.mem_read(address, n))


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


def mapped(a):
    return a and (SCR <= a < SCR + SCR_SIZE or 0x400000 <= a < 0x700000 or on_stack(a))


def cstr(m, address, limit=0x100):
    if not mapped(address):
        return ("ptr?", address)
    return rd(m, address, limit).split(b"\0")[0]


class State:
    def __init__(self, rng):
        self.rng = rng
        self.mem = {a: bytearray(rng.randbytes(n)) for a, n in REGIONS}
        self.scr = bytearray(rng.randbytes(SCR_SIZE))
        self.script = {}
        self.put(0x4B2198, "<I", CONFIG)
        self.put(0x4A3240, "<I", GETKEYSTATE)
        self.put(0x4BE340, "<I", RECTS - 0x380)
        self.put(0x5D2060, "<I", OBJ)
        self.sput(OBJ, "<I", OBJ + 0x10)
        self.sput(OBJ + 0x10 + 0x34, "<I", ENUMSESSIONS)
        for k in range(0x41):
            self.put(0x6CD518 + 8 * k, "<I", (LISTS + 4 * rng.randrange(0x100)) | rng.choice([0, 0x80000000]))
        self.esi = rng.getrandbits(32)

    def find(self, address):
        for a, n in REGIONS:
            if a <= address < a + n:
                return a
        raise KeyError(hex(address))

    def put(self, address, fmt, value):
        a = self.find(address)
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.mem[a], address - a, value)

    def get(self, address, fmt="<I"):
        a = self.find(address)
        return struct.unpack_from(fmt, self.mem[a], address - a)[0]

    def sput(self, address, fmt, value):
        if isinstance(value, int) and fmt[-1] in "iIhHbB":
            value &= (1 << (8 * struct.calcsize(fmt))) - 1
            fmt = fmt[:-1] + fmt[-1].upper()
        struct.pack_into(fmt, self.scr, address - SCR, value)

    def sputb(self, address, data):
        self.scr[address - SCR:address - SCR + len(data)] = data


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
        name, kinds, pop = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        raw = list(words[1:1 + len(kinds)])
        logged = []
        for kind, a in zip(kinds, raw):
            if kind == "v":
                logged.append(None if on_stack(a) else a)
            elif kind == "s":
                logged.append(cstr(self.m, a))
            elif kind == "p":
                logged.append(struct.unpack("<I", rd(self.m, a, 4))[0] if mapped(a) else ("ptr?", a))
            elif kind == "m":
                msg = rd(self.m, a, 16)
                logged.append((msg[0], msg[8:14]))
            elif kind == "e":
                logged.append(rd(self.m, a, 0x50))
        self.log.append((name, tuple(logged)))
        eax = self._next(name, 0)
        if name == "font_image_info":
            for slot, v in zip(raw, self._next("font", (PIXELS, PALETTE, 64))):
                uc.mem_write(slot, struct.pack("<I", v & 0xFFFFFFFF))
        elif name == "dd_surface_lock" and eax:
            pitch, surface = self._next("desc", (0x100, SURF))
            uc.mem_write(raw[2] + 0x10, struct.pack("<I", pitch & 0xFFFFFFFF))
            uc.mem_write(raw[2] + 0x24, struct.pack("<I", surface & 0xFFFFFFFF))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def pick(rng, *choices):
    return rng.choice(choices)


# ---------------------------------------------------------------- generators

def g_none(st, rng):
    st.script["d3d_render_states"] = [rng.getrandbits(32)]
    return [], True


def g_capslock(st, rng):
    st.script["GetKeyState"] = [pick(rng, 0, 1, 0xFFFF8001, 0x12340001, 0xFFFF, 0x8000, rng.getrandbits(32))]
    return [], True


def g_count(st, rng):
    st.put(0x5D5CC8, "<B", rng.getrandbits(8))
    return [], True


def g_slot(st, rng):
    for k in range(6):
        st.put(0x5D2CF0 + k * 0x66C, "<i", pick(rng, 5, 5, 4, 0, rng.getrandbits(32)))
    return [pick(rng, 0, 1, 2, 3, 4, 5, 6, 7, -1, rng.getrandbits(32))], True


def g_flags(st, rng):
    return [], True


def g_replay(st, rng):
    st.put(0x4B4A90, "<i", pick(rng, -1, -1, 0, 1, 5, rng.getrandbits(32)))
    return [], False


def g_send7(st, rng):
    st.put(0x5D622C, "<I", (rng.getrandbits(32) & ~3) | rng.randrange(4))
    st.script["net_message_send_to"] = [rng.getrandbits(32)]
    return [DATA], True


def g_search(st, rng):
    st.script["EnumSessions"] = [pick(rng, 0, 0, 0x8877015E, 0x80004005, 1, rng.getrandbits(32))]
    return [], True


def g_session_cb(st, rng):
    # 0x48bb30 (lead, after xd): the EnumSessions callback
    st.put(0x5D2CC4, "<I", rng.randrange(9))
    desc = bytearray(rng.getrandbits(8) for _ in range(0x50))
    name = bytes(rng.randrange(32, 127) for _ in range(rng.randrange(0, 40))) + b"\0"
    desc[0x30:0x34] = (DATA + 0x100).to_bytes(4, "little")
    desc[0x28:0x2C] = pick(rng, 0, 1, 6, 8, rng.getrandbits(32)).to_bytes(4, "little")
    desc[0x2C:0x30] = pick(rng, 0, 1, 6, 8, rng.getrandbits(32)).to_bytes(4, "little")
    desc[4] = pick(rng, 0, 1, 0x20, 0x21, 0x40, rng.getrandbits(8))
    st.sputb(DATA, bytes(desc))
    st.sputb(DATA + 0x100, name)
    return [DATA, DATA + 0x200, pick(rng, 0, 1, 2, 3, rng.getrandbits(32)), rng.getrandbits(32)], True


def g_box(st, rng):
    st.put(0x4CB540, "<I", pick(rng, 0, 0x20, 0x100, rng.getrandbits(32)))
    return [], False


def set_slots(st, rng):
    for k in range(6):
        st.put(0x4CB110 + 4 * k, "<i", pick(rng, -1, 0, 1, 2, 3, 4, 5, 6, 6, 3))
        st.put(0x5D2CF0 + k * 0x66C, "<i", pick(rng, 5, 0, 1))
    st.put(0x6CCE84, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    st.put(0x6CD520, "<I", (LISTS + 4 * rng.randrange(0x100)) | rng.choice([0, 0x80000000]))
    st.put(0x4ACC38, "<i", pick(rng, 640, 800, 1024, 3440, 320, rng.getrandbits(32), rng.randrange(-5000, 5000)))
    st.put(0x4ACC3C, "<i", pick(rng, 480, 600, 768, 1440, 240, rng.getrandbits(32), rng.randrange(-5000, 5000)))
    st.put(0x4CB540, "<i", pick(rng, 0, 0x20, 0x100, -0x100, rng.getrandbits(32), rng.randrange(-1000, 1000)))
    # a packet's tag may point at its own list (no aliasing with the heads)


def g_slots(st, rng):
    set_slots(st, rng)
    return [], False


def g_host(st, rng):
    set_slots(st, rng)
    for a in (0x4CB474, 0x4CB478, 0x4CB47C, 0x4CB480, 0x4CB7F0, 0x4CB7F4):
        st.put(a, "<i", pick(rng, 0, 10, 100, 300, -5, rng.randrange(-1000, 1000), rng.getrandbits(32)))
    return [], False


def g_input(st, rng):
    size = pick(rng, 0, 1, 2, 3, 5, 9, 16, 16, 20, 32, -1)
    text = bytearray(rng.randbytes(40))
    n = rng.randrange(0, 40)
    for i in range(n):
        text[i] = rng.choice(b"abcXYZ 019.-")
    text[n] = 0
    st.sputb(TEXT, bytes(text))
    st.put(0x4C99BC, "<i", pick(rng, 0, 1, 2, 3, 5, 8, 15, 16, 19, 20, -1, rng.randrange(-3, 40)))
    st.put(0x4CB528, "<I", pick(rng, 0, MENU_HOOK))
    st.put(0x4BE33C, "<I", pick(rng, 0, 1, 1))
    st.put(0x4AF448, "<I", pick(rng, 0, 1, 0))
    key = pick(rng, 0xD, 0x1B, 0xF5, 0xF6, 0x41, 0x31, 0x08, 0xFB, 0x60, 0xBA, rng.getrandbits(8), rng.getrandbits(32))
    st.put(0x4AF43C, "<I", key)
    st.put(0x4AF444, "<I", pick(rng, 0, 1, 1, 1))
    for a in (0x4BEB38, 0x4BEB3C, 0x4BEB40):
        st.put(a, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    st.put(0x4BEB40, "<I", pick(rng, 0, 0, 0, 1))
    st.sput(CONFIG + 0x36, "<B", pick(rng, 2, 2, 0, 1))
    interesting = [0, 8, 8, 0xFB, 0xFB, 0x60, 0x2B, 0x51, 0x23, 0x27, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x33, 0x36,
                   0x39, 0x3B, 0x3C, 0x3D, 0x5B, 0x5C, 0x5D, 0x41, 0x5A, 0x61, 0x7A, 0x40, 0x3A, 0x7B, 0xFFFFFFDF,
                   0xFFFFFFA3] + list(range(0xE2, 0xF0))
    every = list(range(0x20, 0x7F)) + [0xFFFFFFDF, 0xFFFFFFA3]
    st.script["tolower"] = [pick(rng, *interesting, rng.choice(every), rng.choice(every), rng.getrandbits(32))]
    st.script["toupper"] = [pick(rng, *interesting, rng.choice(every), rng.getrandbits(8)) for _ in range(2)]
    st.script["GetKeyState"] = [pick(rng, 0, 1, 0xFFFF0000, 0x10000, rng.getrandbits(32))]
    st.script["strstr"] = [pick(rng, 0, 0, 1, 0x4AFB80)]
    st.script["fe_repeat_select"] = [pick(rng, 0, 1, 1)]
    if rng.randrange(3):
        st.put(0x4AF448, "<I", 0)
        st.put(0x4AF444, "<I", 1)
        st.put(0x4BEB40, "<I", 0)
        size = pick(rng, 2, 3, 5, 9, 16, 16, 20, 32)
    args = [pick(rng, 0, 1, 4, 5, 7, 7, 10, 10, 10, 10, 2, 3, 6, 8, 9, 11, rng.getrandbits(32))
            if rng.randrange(2) else 10,
            pick(rng, TEXT, TEXT, TEXT, TEXT, 0),
            pick(rng, 0, 1, 2, 3, 0, 0),
            size, rng.getrandbits(32), pick(rng, 2, 1, 0, rng.getrandbits(32)),
            ON_CHANGE, pick(rng, 0, ON_EVENT, ON_EVENT)]
    return args, True


def set_font(st, rng, w_screen=None):
    for c in range(256):
        st.put(0x6C1A7C + c, "<B", pick(rng, 0xFF, rng.randrange(0x100)))
    for g in range(0x100):
        a = 0x6C0A7C + 16 * g
        st.put(a, "<H", rng.randrange(0, 40))
        st.put(a + 2, "<H", rng.randrange(0, 40))
        st.put(a + 4, "<H", rng.randrange(1, 9))
        st.put(a + 6, "<H", rng.randrange(1, 9))
    st.put(0x6C0A70, "<i", pick(rng, 4, 8, -3))
    st.put(0x6C0A74, "<i", pick(rng, 0, 1, 2, -1))
    st.put(0x4D5F70, "<I", pick(rng, 0x10, 0x10, 0x10, 8, 0x20))
    st.put(0x4D5F48, "<I", rng.getrandbits(32))


def g_draw(st, rng):
    set_font(st, rng)
    n = rng.randrange(0, 8)
    text = bytes(pick(rng, 0x20, 0x41, 0x61, 0x30, rng.randrange(1, 256)) for _ in range(n)) + b"\0"
    st.sputb(TEXT, text)
    st.script["dd_surface_lock"] = [pick(rng, 0, 1, 1, 1, 5)]
    st.script["desc"] = [(pick(rng, 0x100, 0x120, 0x200), SURF + rng.randrange(0, 0x100) * 2)]
    st.script["font"] = [(PIXELS + rng.randrange(0, 0x100), PALETTE, pick(rng, 48, 64, 100))]
    return [TEXT, rng.randrange(0, 0x60), rng.randrange(0x28, 0x50)], False


def g_clear(st, rng):
    st.put(0x4D5F70, "<I", pick(rng, 0x10, 0x10, 0x10, 8, 0))
    st.put(0x4D5F48, "<I", rng.getrandbits(32))
    w = pick(rng, 0, 1, 7, 40, 100)
    h = rng.randrange(1, 60)
    st.put(0x4ACC38, "<I", w)
    st.put(0x4ACC3C, "<I", h)
    st.script["dd_surface_lock"] = [pick(rng, 0, 1, 1, 1)]
    st.script["desc"] = [(2 * w + pick(rng, 0, 4, 0x20), SURF + rng.randrange(0, 0x40) * 2)]
    return [rng.randrange(0, h)], True


# name: (address, generator)
TESTS = {
    "d3d_render_states_thunk": (0x4219D0, g_none),
    "capslock_state": (0x43D260, g_capslock),
    "net_player_count": (0x45F4F0, g_count),
    "net_slot_ready": (0x460980, g_slot),
    "net_joined": (0x48D490, g_flags),
    "cdaudio_replay_request": (0x496C60, g_replay),
    "net_send_type7": (0x45FBE0, g_send7),
    "net_sessions_search_start": (0x48B610, g_search),
    "net_sessions_search_poll": (0x48B6D0, g_search),
    "net_session_enum_cb": (0x48BB30, g_session_cb),
    "name_entry_box_place": (0x448880, g_box),
    "net_sessions_box_place": (0x452CE0, g_box),
    "net_slots_place": (0x452980, g_slots),
    "net_host_boxes_place": (0x452C20, g_host),
    "name_entry_input": (0x449160, g_input),
    "movie_text_draw": (0x488AD0, g_draw),
    "movie_text_clear": (0x488CC0, g_clear),
}


def apply(m, st, cw):
    m.uc.reg_write(UC_X86_REG_FPCW, cw)
    m.uc.reg_write(UC_X86_REG_FPSW, 0)
    m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
    m.uc.reg_write(UC_X86_REG_ESI, st.esi)
    m.write(E.STACK, bytes(E.STACK_SIZE))
    for a, data in st.mem.items():
        m.write(a, bytes(data))
    m.write(SCR, bytes(st.scr))


def run_case(m, stubs, name, address, gen, rng):
    st = State(rng)
    args, returns = gen(st, rng)
    cw = pick(rng, 0x27F, 0x27F, 0x37F, 0xA7F, 0x67F)
    results = []
    for target in (address, m.symbols[name]):
        apply(m, st, cw)
        stubs.reset(st.script)
        r = m.call(target, args)
        if r[0] == "ok":
            r = ("ok", r[1]) if returns else ("ok",)
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
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from #{k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {results[0][0]} ours " \
               f"{results[1][0]}" + ("" if where is None else f" (memory differs at {where:#x})") + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(54)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v",)]
    for name, (address, gen) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, gen, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
