#!/usr/bin/env python3
"""Differential test of st_sb.c (startup group SB: the startup movies, the font
map, back to the front end and the race set-up under it, the archive file
layer, the replay file loader and the boot data) against the original, on
random settings, race, archive and image state.

Everything outside the group (DirectDraw and the movie player, the sound
engine, the registry helpers, the allocator, the file reading, the loading
picture, the world / track tables of other groups) is stubbed in both runs:
each stub logs its arguments (strings by content, stack buffers by content)
and returns a scripted value, and the call logs are compared as well as the
memory the group can touch.

    tests/emu_st_sb.py EXE build/emu_sb.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCR, SCR_SIZE = 0x01000000, 0x140000
RACE, RACE_BIG = SCR, 0xC0000  # the race block (replay files go to +0x12a0)
SET = SCR + 0xC0000  # the settings block (0x1900)
HEAP = SCR + 0xC2000  # 8 blocks of 0x1000
STR = SCR + 0xCA000  # random C strings, 0x80 each
TAB = SCR + 0xD6000  # the settings table and what it points at
IMG = SCR + 0xD8000  # font image: palette 0x400, pixels 0x4000
OTHER = SCR + 0xE0000  # output buffers
FILES = SCR + 0x100000  # memory file contents
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))
OUTCOMES = {}
SIGNED_MIN = 0x80000000

# address: (name, arg kinds, returns)
# kinds: i raw, S c-string at the pointer, B 0x30 bytes at the pointer, o pointer to an output (only stack-ness)
STUBS = {
    0x4982C2: ("mem_alloc", "i", True),
    0x49827A: ("mem_free", "i", False),
    0x42E6D0: ("string_load", "iiSi", True),
    0x475070: ("file_exists", "S", True),
    0x46B650: ("registry_read", "SSoi", True),
    0x474ED0: ("path_fix", "ii", False),
    0x4979F0: ("sprintf", "iSi", True),
    0x487830: ("movie_prepare", "", False),
    0x4291F0: ("debug_nop", "", False),
    0x493210: ("movie_run", "SiC", False),
    0x46F720: ("sound_direct", "", True),
    0x4885D0: ("display_start", "iiiiiii", True),
    0x487210: ("display_stop", "", False),
    0x4876F0: ("screen_flip", "", False),
    0x41E040: ("screen_fill", "i", False),
    0x41E170: ("screen_ready", "", True),
    0x41E190: ("screen_done", "", False),
    0x436C2C: ("loading_picture", "i", False),
    0x4205E0: ("screen_end", "", False),
    0x477530: ("image_load", "Sii", True),
    0x477810: ("image_free", "i", False),
    0x477460: ("palette_convert", "i", True),
    0x496510: ("arena_state", "i", False),
    0x496680: ("arena_get", "", True),
    0x496590: ("archive_load_entry", "ii", True),
    0x496660: ("arena_release", "i", False),
    0x43B5A0: ("archive_name_equal", "SS", True),
    0x43B730: ("file_cache_init", "", False),
    0x476E10: ("os_file_close", "i", False),
    0x476E20: ("os_file_read", "iii", True),
    0x476C30: ("os_file_open", "S", True),
    0x476E80: ("os_file_size", "i", True),
    0x436935: ("fe_time", "", True),
    0x433570: ("world_number", "i", True),
    0x4335C0: ("world_place", "i", True),
    0x4335E0: ("world_track", "ii", True),
    0x448670: ("world_track_count", "i", True),
    0x4486B0: ("world_locked_tracks", "i", True),
    0x4485B0: ("track_title_text", "oii", True),
    0x472F70: ("sound_reset", "", False),
    0x426200: ("view_scale", "ii", False),
    0x417ED0: ("view_project", "iiiii", False),
    0x41F2D0: ("view_clip", "iiii", False),
    0x46B6D0: ("registry_get", "iiBi", True),
    0x46B730: ("registry_put", "iiBi", True),
}


def r32(rng):
    return rng.choice([0, 0, 1, 1, 2, 3, 5, 0xFFFFFFFF, 0x20, 0x40, rng.randrange(0, 0x60), rng.getrandbits(32)])


def small(rng):
    return rng.choice([0, 0, 1, 1, 2, 3, 0xFF, rng.randrange(0, 8), rng.getrandbits(8)])


_POP = [0] * 10 + [1] * 5 + [2, 3, 0xFF, 4, 5, 6, 7, 0x10, 0x20, 0x80] + list(random.Random(3).randbytes(24))


def sbytes(rng, n):
    """Mostly small bytes (flags), some random."""
    return bytes(rng.choices(_POP, k=n))


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def ret_gen(rng, name):
    if name == "mem_alloc":
        return rng.choice([0] + [HEAP + 0x1000 * k for k in range(8)] * 3)
    if name == "string_load":
        return rng.choice([-1, -1, 0, 5, 0x7F])
    if name in ("file_exists", "screen_ready", "os_file_open"):
        return rng.choice([0, 1, 1]) if name != "os_file_open" else rng.choice([0, 0x77, 0x1234])
    if name == "registry_read":
        return rng.choice([-1, 0, 0, 5])
    if name == "sprintf":
        return rng.randrange(0, 20)
    if name == "sound_direct":
        return rng.choice([0, 0x4D5000])
    if name == "display_start":
        return rng.choice([0, 1, 1, 1])
    if name == "image_load":
        return rng.choice([0, 1, 1, 1])
    if name == "palette_convert":
        return 1
    if name == "arena_get":
        return rng.getrandbits(32)
    if name == "archive_load_entry":
        return rng.choice([FILES, FILES + 0x2000, FILES + 0x4000, 0])
    if name == "archive_name_equal":
        return rng.choice([0, 0, 0, 1, 1, 0x10001, 0x7FFF0001])
    if name == "os_file_read":
        return rng.choice([0, 1])
    if name == "os_file_size":
        return rng.choice([0x1E884, 0x1E884, 0x1E884, 0, 0x1E883, 0xFFFFFFFF, 0x100])
    if name == "fe_time":
        return rng.getrandbits(32)
    if name == "world_number":
        return rng.choice([-1, 1, 2, 3, 4])
    if name == "world_place":
        return rng.choice([-1, 0, 1, 2, 3, 4, 5])
    if name == "world_track":
        return rng.choice([-1, 0, 5, 12, 19, 20, 30, 0x7FFFFFFF])
    if name == "world_track_count":
        return rng.choice([3, 4, 5, 3, 0, 1])
    if name == "world_locked_tracks":
        return rng.choice([0, 0, 1, 3, 4, 7, 0x1F, 0xFF, rng.getrandbits(16)])
    if name == "track_title_text":
        return rng.randrange(0, 30)
    if name in ("registry_get",):
        return rng.choice([-1, 0, 0xAA0, 0x1854, 0x10, 0xAA1, 0x100])
    if name in ("registry_put",):
        return rng.choice([-1, 0, 0, 1])
    return rng.getrandbits(32)


class Region:
    """A piece of state: written before each run, compared after."""
    __slots__ = ("addr", "data")

    def __init__(self, addr, data):
        self.addr, self.data = addr, bytearray(data)


class State:
    def __init__(self):
        self.regions = []
        self.compare_only = []  # (addr, size) compared but not written

    def add(self, addr, data):
        r = Region(addr, data)
        self.regions.append(r)
        return r

    def find(self, addr, size=1):
        for r in self.regions:
            if r.addr <= addr and addr + size <= r.addr + len(r.data):
                return r
        raise KeyError(hex(addr))

    def put(self, addr, value, size=4):
        r = self.find(addr, size)
        o = addr - r.addr
        r.data[o:o + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")

    def get(self, addr, size=4):
        r = self.find(addr, size)
        o = addr - r.addr
        return int.from_bytes(r.data[o:o + size], "little")


# data globals the group touches: (address, size)
GLOBALS32 = [0x4AC738, 0x4AC984, 0x4AC998, 0x4ACAB0, 0x4ACC38, 0x4ACC3C, 0x4ACC40, 0x4ACC5C, 0x4ACC60,
             0x4ADAA0, 0x4ADAA4, 0x4BE2B4, 0x4BE2B8, 0x4BE2BC, 0x4BE31C, 0x4BE34C, 0x4BE370, 0x4BE448,
             0x4BE450, 0x4BE454, 0x4BE47C, 0x4BE490, 0x4BE49C, 0x4BEB3C,
             0x4CB48C, 0x4CB560, 0x4CB564, 0x4CB568, 0x4CB56C, 0x4CB570, 0x4CB58C, 0x4D4650, 0x4D4654, 0x4D4658,
             0x4D4660, 0x4D466C, 0x4D4670, 0x4D46F4, 0x4D46F8, 0x4D5F40, 0x4D5F48, 0x4DB260, 0x5D622C, 0x6C8000, 0x6CCD00, 0x4ACC44, 0x4ACC48, 0x4ACC4C, 0x4ACC58,
             0x4BE444, 0x4BE44C, 0x4AC978, 0x4AF5A0, 0x4AF5A4, 0x4AF5A8]
BYTES = [(0x4BE1B0, 0x100), (0x5FF0C0, 0x108), (0x6CC400, 0x410), (0x6C0A60, 0x520), (0x6C1A7C, 0x100),
         (0x4C2004, 0x400), (0x4C2408, 0x40), (0x4AD000, 0x40), (0x6C25C0, 0x40), (0x4CA52C, 0x2B0),
         (0x4BE3F4, 0x20), (0x4BE398, 0x20), (0x4AF658, 8), (0x5FDB20, 0x30), (0x4B1C70, 0x20), (0x4D4540, 0x20)]


def make_state(rng, big=False):
    st = State()
    for a in GLOBALS32:
        st.add(a, p32(r32(rng)))
    for a, n in BYTES:
        st.add(a, sbytes(rng, n))
    # the screen sizes and a few flags that want sane values
    st.put(0x4ACC38, rng.choice([320, 640, 800, 1024, rng.randrange(0, 2000)]))
    st.put(0x4ACC3C, rng.choice([240, 480, 600, 768, rng.randrange(0, 2000)]))
    st.put(0x4BE2B8, rng.choice([0, 0, 1, 5, 0x3E, 0x3F, 0x40, 0x41, rng.randrange(-2, 0x44) & 0xFFFFFFFF]))
    st.put(0x4AC998, rng.choice([0, 0, 1]))
    st.put(0x4ADAA0, rng.randrange(0, 6))
    st.put(0x4ADAA4, rng.randrange(0, 6))
    # the race and the settings
    race = bytearray(sbytes(rng, 0x2000))
    st.add(RACE, race)
    st.add(SET, sbytes(rng, 0x1900))
    st.add(0x4B1960, p32(RACE))
    st.add(0x4B2198, p32(SET))
    if big:
        st.add(RACE + 0x2000, rng.randbytes(RACE_BIG - 0x2000))
    # heap blocks, strings, files, table targets, image, output
    st.add(HEAP, rng.randbytes(0x8000))
    st.add(TAB, rng.randbytes(0x1000))
    st.add(OTHER, rng.randbytes(0x4000))
    # strings: printable with a terminator within 0x40 bytes
    strs = bytearray()
    for _ in range(0x4000 // 0x80):
        n = rng.randrange(0, 0x40)
        s = bytes(rng.choice(b"abcxyz_AZ09.\\ /") for _ in range(n)) + b"\0"
        strs += s.ljust(0x80, b"\0")
    st.add(STR, strs)
    # the text buffers at 0x4ca52c.. get real-looking text
    for off in range(0, 0x2B0, 0x80):
        n = rng.randrange(0, 0x30)
        s = bytes(rng.choice(b"abcxyz ABZ09") for _ in range(n)) + b"\0"
        r = st.find(0x4CA52C)
        r.data[off:off + len(s)] = s[:min(len(s), 0x2B0 - off)]
    # font image: palette (small values make blank cells), pixels mostly zero
    pal = bytearray(sbytes(rng, 0x400))
    pix = bytes(rng.choices([0, 0, 0, 0, 0, 1, 2, 3, 9, 77, 200, 255, 130, 17], k=0x4000))
    st.add(IMG, bytes(pal) + bytes(pix))
    st.put(0x4C2418, IMG)
    st.put(0x4C241C, IMG + 0x400)
    for off, vals in ((0x4C240A, [16, 32, 64, 128]), (0x4C240C, [16, 32, 64, 128, 200]),
                      (0x4C240E, [8, 16, 32, 64, 100, 128])):
        st.put(off, rng.choice(vals), 2)
    # the character map / cell table near the font: sane cell widths
    # the archive tables: names in STR, entries in FILES
    for i in range(6):
        st.put(0x4AD000 + i * 8, STR + 0x80 * rng.randrange(0, 0x80))
        st.put(0x4AD004 + i * 8, rng.choice([0, 1, 2, 3, 4, 5, 6, 0x7FFF, 0xFFFF, rng.getrandbits(16)]), 2)
    for i in range(6):
        st.put(0x6C25C0 + i * 8, rng.choice([0, FILES, FILES + 0x2000, FILES + 0x4000, HEAP]))
        st.put(0x6C25C4 + i * 8, rng.choice([0, 0, 4, 0x100, rng.randrange(0, 0x1000)]))
    return st


class Stubs:
    def __init__(self, m):
        self.log = []
        self.script = {}
        self.effects = {}
        self.m = m
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, effects=None):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.effects = effects or {}

    def cstring(self, uc, a):
        try:
            out = bytearray()
            for i in range(0x100):
                c = uc.mem_read(a + i, 1)[0]
                if c == 0:
                    break
                out.append(c)
            return bytes(out)
        except Exception:
            return b"<bad>"

    def _hook(self, uc, address, size, _):
        name, kinds, returns = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (len(kinds) + 1), bytes(uc.mem_read(esp, 4 * (len(kinds) + 1))))
        args = []
        for k, a in zip(kinds, words[1:]):
            on_stack = E.STACK <= a < E.STACK + E.STACK_SIZE
            if k == "S":
                args.append(self.cstring(uc, a))
            elif k == "C":
                args.append(bytes(uc.mem_read(a, 0x28)))
            elif k == "B":
                args.append(bytes(uc.mem_read(a, 0x30)) if on_stack or SCR <= a < SCR + SCR_SIZE else a)
            elif on_stack:
                args.append("stack")
            else:
                args.append(a)
        self.log.append((name, tuple(args)))
        values = self.script.get(name)
        eax = values.pop(0) if values else 0
        if eax & 0xFFFFFFFF:
            for a, data in self.effects.get(name, ()):
                uc.mem_write(a, data)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF if returns else 0)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, words[0])


def sp(rng):
    return STR + 0x80 * rng.randrange(0, 0x80)


# name: (args(rng, st) -> list, returns: None | 'u32' | 'u16', big race?)
# The race and the settings are random bytes (mostly small); a test may shape them.
def shape_settings(rng, st):
    for off, vals in {0xC: [0, 0, 1], 0xE: [0, 0, 1], 0x39: [0, 1], 0x3B: [0, 4, 3, 0x10], 0x9C: [0, 1, 1],
                      0x17: [0, 0, 0, 0, 0, 0, 1], 0x21: [0, 1, 1], 0x14: list(range(0, 11)) + [11, 0xFF], 0x76: [0, 1, 1],
                      0x74: [0, 1, 2, 3, 4, 5], 0x88: [0, 1, 2, 3, 4], 0x82: [0, 1], 0x31: [0, 1, 2], 0x15: list(range(0, 14)),
                      0x16: [0, 1, 100, 101, 5], 0x1F: [0, 1, 100, 100, 7, 20], 0x27: [0, 1, 2, 3, 4],
                      0x2D: [0, 1, 2, 3], 0x38: list(range(0, 22)), 0xF: [0, 1, 2, 3, 4, 5],
                      0x39: [0, 1], 0x5: [0, 1, 2, 3], 0x8: [0, 1], 0x2F: [0, 1, 5, 40, 60], 0x35: [0, 1, 4, 60],
                      0x77: [0, 1, 4, 60], 0x85: [0, 2, 6, 70], 0x75: [0, 1], 0x8A: [0, 1, 2, 3, 4, 5],
                      0x18: list(range(0, 8)), 0x19: list(range(0, 8)), 0x1A: list(range(0, 8)), 0x1B: list(range(0, 8))}.items():
        st.put(SET + off, rng.choice(vals), 1)
    # the speed class float
    st.put(SET + 0x6C, struct.unpack("<I", struct.pack("<f", rng.choice([0.0, 10.0, 24.0, 60.0, 112.0, 113.0, 500.0,
                                                                          -5.0, float("nan"), 23.9, 75.5])))[0])


def shape_race(rng, st):
    for off, vals in {0xE: [0, 1, 2, 3, 4], 0x58C: [0, 1, 2], 0x54D: [0, 1, 2, 3, 4, 5, 6], 0x585: [0, 1, 2, 3],
                      0x551: list(range(0, 11)), 0x720: [0, 0x10, 0x30], 0xA89: [0, 1, 2, 3],
                      0x1C: [0, 1, 2, 3, 4, 5, 6, 7, 0xFF], 0x579: list(range(0, 12))}.items():
        st.put(RACE + off, rng.choice(vals), 1)
    for i in range(6):
        st.put(RACE + 0x1C + i, rng.choice([0, 1, 2, 3, 4, 5, 6, 0xFF, 0xFF, 7]), 1)


def noargs(rng, st):
    return []


def a_str(rng, st):
    return [rng.choice([sp(rng), 0x4AC78C, 0x4AC794])]


def a_movie_path(rng, st):
    return [rng.choice([0, 0x4AC78C, sp(rng)]), rng.choice([sp(rng), 0x4AC794, 0x4AC7A8, 0x4AC838])]


def a_lang(rng, st):
    return [rng.randrange(-2, 10)]


def a_add(rng, st):
    text = rng.choice([0, sp(rng), 0x4CA52C, 0x4CA5AC, 0x4CA7AC, OTHER + 0x100])
    return [rng.choice([0, 1, 2]), rng.choice([10, 0x190, rng.getrandbits(32)]), text]


def a_text(rng, st):
    return [rng.choice([sp(rng), 0x4CA52C, 0x4CA62C, 0x4CA7AC, STR + 0x80 * rng.randrange(0, 8)])]


def a_scan(rng, st):
    step = rng.choice([1, -1, 2, 16, -16, 32, 0])
    across = rng.choice([1, 16, 32, 64, 0, 3])
    count = rng.choice([0, 1, 4, 8, -1])
    width = rng.choice([0, 1, 5, 8, -2])
    return [IMG + 0x400 + 0x1000 + rng.randrange(0, 0x400), step, count, across, width]


def a_cells(rng, st):
    cell = rng.choice([16, 16, 8, 4, 2, 3, 1, 32, 0 if rng.randrange(20) == 0 else 16])
    return [sp(rng), cell, OTHER + 0x200, rng.choice([0, 2, 0x36, 8, 20, 200, rng.randrange(0, 130)]),
            rng.choice([1, 2, 3, 8, 30, 64, 100, 300, rng.randrange(1, 130)]), rng.choice([0, 0, 1, 3, 6, 0x10])]


def a_info(rng, st):
    return [OTHER, OTHER + 8, OTHER + 16]


def a_i1(rng, st):
    return [rng.getrandbits(32)]


def a_view(rng, st):
    return [rng.choice([0, -5, 10, 100, 640, rng.randrange(-300, 1000)]), rng.choice([0, -5, 10, 100, 480, rng.randrange(-300, 1000)]),
            rng.choice([0, 3, 100, 640, 1024, rng.randrange(-300, 2000)]), rng.choice([0, 3, 100, 480, 768, rng.randrange(-300, 2000)])]


def a_name(rng, st):
    return [sp(rng)]


def a_key(rng, st):
    return [rng.choice([0x7A, 0x7B, 0x42, 0x4A, 0x41, rng.getrandbits(8), 0])]


def a_handle(rng, st):
    tab = [st.get(0x6C25C0 + i * 8) for i in range(6)]
    return [rng.choice(tab + [0, 0x77, HEAP])]


def a_read(rng, st):
    tab = [st.get(0x6C25C0 + i * 8) for i in range(6)]
    return [rng.choice(tab + [0, 0x77]), rng.choice([0, OTHER + 0x400, OTHER + 0x800]),
            rng.choice([0, 1, 5, 0x40, 0x101, 0xFFFFFFFF, rng.randrange(0, 0x200)])]


def a_idx(rng, st):
    return [rng.choice([0, 1, 5, 0x7FFF, 0xFFFF, rng.getrandbits(32)])]


def a_rand(rng, st):
    return [rng.choice([0, 1, -1, 2, 4, 7, 0x20, 0x40, 0x14, rng.getrandbits(32), rng.randrange(-5, 100)])]


def a_mask(rng, st):
    return [rng.choice([0, 0xFFFF, 0xFFFFFFFF, 0x1F, 0x7FFF, rng.getrandbits(16), rng.getrandbits(32)]),
            rng.choice([0, 1, 5, 6, 15, 16, 32, 33, rng.randrange(-2, 40)])]


def a_pick(rng, st):
    return a_mask(rng, st) + [rng.choice([-1, 0, 7])]


def a_flags(rng, st):
    return [rng.choice([0, 1, 8, 10, 16, 5, rng.getrandbits(32)]), rng.choice([0, 0, 1])]


def a_slot_value(rng, st):
    return [rng.choice([0, 1, 5, 6, 7, 0xFF, 0x100, 0xFFFFFFFF])]


def a_char(rng, st):
    return [rng.choice([0, 1, 5]), rng.choice([0, 1])]


def a_rec(rng, st):
    rec = bytearray(rng.randbytes(4))
    rec[0] = rng.randrange(0, 20)
    rec[1] = rng.randrange(0, 20)
    st.put(OTHER + 0x1000, int.from_bytes(rec, "little"))
    return [OTHER + 0x1000, rng.choice([0, 3, 4, 5, 15, 16, 32, 33, -1]),
            rng.choice([0, 0xFFFF, 0xFFFFFFFF, 0x1F, rng.getrandbits(16), rng.getrandbits(32)])]


def a_choice(rng, st):
    rec = rng.randbytes(4)
    st.put(OTHER + 0x1000, int.from_bytes(rec, "little"))
    return [OTHER + 0x1000, rng.choice([1, 2, 3, 4, 0, 6]), rng.choice([100, 0x65, 3, 0, 0x64, 7, rng.getrandbits(32)])]


def a_slots(rng, st):
    return [rng.choice([0, 1, 2, 3, 4, 5, -1]), rng.choice([0, 1]), rng.choice([0, 1]), rng.choice([0, 1]),
            rng.choice([0, 1])]


def a_title(rng, st):
    return [OTHER + 0x1000, rng.choice([-1, 0, 5, 19, 20])]


def a_choose(rng, st):
    st.put(0x4ACAB0, rng.choice([0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 3, 0]))
    kind = rng.choice(list(range(0, 15)) + [10, 10, 10, 5, 6, 1, 2, 3, 4])
    if kind == 10:
        st.put(SET + 0x1F, rng.choice([0, 0, 100, 100, 7, 1]), 1)
        st.put(SET + 0x38, rng.choice([0, 1, 5, 0x12, 0x13, 0x13, 0x14, 0xFF]), 1)
    return [kind, rng.choice([0, 1, 5, 100, 0x65, 20, rng.getrandbits(32)])]


def a_op(rng, st):
    """The settings table: 3-word records {kind, field, other}."""
    n = rng.randrange(0, 8)
    words = []
    for i in range(n):
        kind = rng.choice([0, 1, 2, 3, 4, 5, 9, 0xFFFFFFFE])
        words += [kind, TAB + 0x400 + i * 0x40 + (0 if rng.randrange(5) else 0x3), TAB + 0x600 + i * 0x40]
    if rng.randrange(6) == 0 and n:
        words[1 + 3 * rng.randrange(n)] = 0
    if rng.randrange(6) == 0 and n:
        words[2 + 3 * rng.randrange(n)] = 0
    words += [0xFFFFFFFF, 0, 0]
    r = st.find(TAB, 4)
    data = b"".join(p32(w) for w in words)
    r.data[0:len(data)] = data
    return [rng.choice([100, 0x65, 100, 0x65, 0, 7]), TAB]


def a_table_default(rng, st):
    """0x4b21a0 itself: a table in the exe (read-only data); the call only
    works when its pointers are valid, so test with the original table."""
    return []


def a_bd(rng, st):
    return [rng.choice([OTHER + 0x1000, OTHER + 0x2000]), rng.choice([0, 1, 0x10, 0xAA0, 0xAA1]), sp(rng)]


def a_save(rng, st):
    return [rng.choice([OTHER + 0x1000, SET, RACE]), rng.choice([0, 0x10, 0xAA0, 0x1854]), sp(rng)]


def shaped(rng, st, fn):
    shape_settings(rng, st)
    shape_race(rng, st)
    return fn(rng, st)


TESTS = {
    "movie_path": (0x42D5B0, a_movie_path, "u32", False),
    "movie_play": (0x42D7E0, a_str, None, False),
    "movie_psygnosis": (0x42DA20, noargs, None, False),
    "movie_atd": (0x42DA40, noargs, None, False),
    "movie_intro": (0x42DA60, noargs, None, False),
    "credits_play": (0x42DA80, noargs, None, False),
    "movie_cop_loses": (0x42DB90, noargs, None, False),
    "movie_cop_challenge": (0x42DBB0, noargs, None, False),
    "movie_expert": (0x42DBD0, noargs, None, False),
    "movie_win": (0x42DBF0, a_lang, None, False),
    "movie_texts_free": (0x42DC60, noargs, None, False),
    "movie_text_add": (0x42DCB0, a_add, None, False),
    "text_width": (0x488C30, a_text, "u32", False),
    "movie_mode": (0x444C50, noargs, "u32", False),
    "credits_wanted": (0x444CA0, noargs, "u32", False),
    "movies_wanted": (0x444CE0, noargs, "u32", False),
    "movie_texts_build": (0x444D00, noargs, None, False),
    "bitmap_scan": (0x443260, a_scan, "u32", False),
    "font_cells_build": (0x443780, a_cells, "u32", False),
    "font_map_init": (0x443A10, noargs, None, False),
    "font_image_release": (0x443B00, noargs, None, False),
    "font_image_info": (0x443B20, a_info, None, False),
    "startup_movies": (0x4359F1, noargs, "u32", False),
    "loading_screen_show": (0x436B9D, a_i1, None, False),
    "front_end_return": (0x435607, noargs, "u32", False),
    "flags_5d622c_clear": (0x48D370, noargs, None, False),
    "setting5_next": (0x4483C0, noargs, None, False),
    "debug_key_event": (0x43605A, a_key, None, False),
    "view_rect_set": (0x428850, a_view, None, False),
    "archive_find_by_name": (0x43B550, a_name, "u32", False),
    "archive_load_by_index": (0x43B630, a_idx, "u32", False),
    "file_open_any": (0x43B6C0, a_name, "u32", False),
    "file_slot_find": (0x43B7B0, a_handle, "u16", False),
    "file_close_any": (0x43B760, a_handle, None, False),
    "file_read_any": (0x43B7F0, a_read, "u32", False),
    "rand_next": (0x430AE0, a_rand, "u32", False),
    "rand_coin": (0x430B50, noargs, "u32", False),
    "mask_full": (0x430B70, a_mask, "u32", False),
    "mask_pick": (0x430BA0, a_pick, "u32", False),
    "level_name_set": (0x430130, noargs, None, False),
    "start_kind_set": (0x432B70, noargs, None, False),
    "weapons_flags_set": (0x432BD0, a_flags, None, False),
    "race_option554_set": (0x432C80, noargs, None, False),
    "slot_value_free": (0x433CA0, a_slot_value, "u32", False),
    "character_pick": (0x433BF0, a_char, "u32", False),
    "pick_next_random": (0x433980, a_rec, "u32", False),
    "pick_next_step": (0x433A00, a_rec, "u32", False),
    "track_choice": (0x433A50, a_choice, "u32", False),
    "race_slots_init": (0x433AB0, a_slots, None, False),
    "track_title": (0x448580, a_title, None, False),
    "race_track_choose": (0x433670, a_choose, "u32", False),
    "replay_files_load": (0x41C400, noargs, "u32", True),
    "race_init": (0x432EC0, noargs, "u32", True),
    "table_copy": (0x474500, a_op, None, False),
    "settings_table_load": (0x4745D0, a_table_default, None, False),
    "settings_screen_store": (0x4746B0, noargs, None, False),
    "registry_load": (0x474030, a_bd, "u32", False),
    "registry_save": (0x474230, a_save, "u32", False),
    "replay_load": (0x474200, noargs, "u32", False),
    "replay_save": (0x474330, noargs, "u32", False),
    "boot_data_save": (0x474260, noargs, None, False),
}


def compare_regions(st, big):
    out = [(r.addr, len(r.data)) for r in st.regions]
    if big:
        out.append((RACE + 0x2000, RACE_BIG - 0x2000))
    return out


def run_case(m, stubs, name, address, argfn, returns, big, rng):
    st = make_state(rng, big)
    shape_settings(rng, st)
    shape_race(rng, st)
    if name in ("race_init", "front_end_return", "replay_files_load"):
        st.put(SET + 0x15, rng.choice([0, 1, 5, 7, 0xB, 0xD, 0xE]), 1)
    if name == "replay_files_load":
        st.put(RACE + 0x54D, rng.randrange(0, 7), 1)  # more would read past the six paths on the stack
        # the paths: short strings
        for i in range(6):
            s = bytes(rng.choice(b"abc.\\_") for _ in range(rng.randrange(0, 12))) + b"\0"
            r = st.find(SET + 0xCCC)
            o = SET + 0xCCC + 0x20 * i - r.addr
            r.data[o:o + len(s)] = s
    if name == "front_end_return":
        st.put(RACE + 0x54D, rng.randrange(0, 7), 1)
    args = argfn(rng, st)
    # a table for table_copy / settings_table_load: the exe's own at 0x4b21a0 needs real pointers
    script = {}
    for addr, (stub, kinds, rets) in STUBS.items():
        script[stub] = [ret_gen(rng, stub) for _ in range(40)]
    if name in ("race_init", "front_end_return", "replay_files_load") and rng.randrange(2):
        # a replay that loads: the registry gives all the bytes, the files exist and have the right size
        script["registry_get"] = [0xAA0] * 40
        script["os_file_open"] = [0x77] * 40
        script["os_file_size"] = [0x1E884] * 40
        if rng.randrange(2):
            st.put(0x4ADAA0, 0)
    if name in ("race_init", "front_end_return"):
        st.put(SET + 0x17, 1 if rng.randrange(3) == 0 else 0, 1)
        st.put(SET + 0x21, 1, 1)
        st.put(SET + 0x14, rng.randrange(0, 11), 1)
        for off in (0x2F, 0x35, 0x77, 0x85):
            if rng.randrange(2):
                st.put(SET + off, 0, 1)
    regions = compare_regions(st, big)
    # the image loader (0x477530) fills the image record: its pixel and palette pointers and sizes
    effects = {"image_load": [(a, bytes(st.find(a, n).data[a - st.find(a, n).addr:][:n]))
                              for a, n in ((0x4C240A, 6), (0x4C2418, 12))]}
    results = []
    for target in (address, m.symbols[name]):
        for r in st.regions:
            m.write(r.addr, bytes(r.data))
        m.write(E.STACK, STACK_FILL)
        stubs.reset(script, effects)
        r = m.call(target, args)
        if returns is None and r[0] == "ok":
            r = ("ok",)
        elif returns == "u16" and r[0] == "ok":
            r = ("ok", r[1] & 0xFFFF)
        mem = b"".join(m.read(a, n) for a, n in regions)
        results.append((r, mem, stubs.log))
    key = results[0][0][0] if results[0][0][0] != "error" else "error:" + results[0][0][1][:34]
    OUTCOMES[key] = OUTCOMES.get(key, 0) + 1
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            off = first
            for addr, n in regions:
                if off < n:
                    where = f" (memory differs at {addr + off:#x})"
                    break
                off -= n
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {results[0][0]} ours {results[1][0]}{where}{logs}"
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(0x5B)
    m.write(FILES, rng.randbytes(0x30000))  # the memory files' contents: read only
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, argfn, returns, big) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        OUTCOMES.clear()
        n = iterations if not big else max(20, iterations // 2)
        for _ in range(n):
            msg = run_case(m, stubs, name, address, argfn, returns, big, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {n} differ'} {OUTCOMES}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
