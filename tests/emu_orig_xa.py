#!/usr/bin/env python3
"""Differential test of src/game/orig_xa.c (group XA: code Ghidra never made
into functions: the track objects' and driller's hit callbacks, the movie
player's frame callback, the race clock tick and its two screen OT steps,
the movie menu's table entries and the front end menu handlers / item
callbacks) against the original, on random state.

Every callee outside the group is a logging stub in both runs (stack
buffers compared by content, never by address), including the movie table's
functions and the PeekMessageA / GetMessageA import slots, which point at
stubs. The function under test is never stubbed (0x423af0 / 0x423ba0 are
stubs for clock_tick and the targets of their own tests). Return value, the
scratch page and the touched globals are compared; the stack is filled with
a pattern first.

    tests/emu_orig_xa.py EXE build/emu_xa.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

S, SIZE = 0x01000000, 0x10000
STUB_PAGE = 0x00340000
TRAMP = 0x00350000
VERBOSE = "-v" in sys.argv

OBJ = S + 0x0000      # 0x200
OTHER = S + 0x0200    # 0x800
OWNER = S + 0x0A00    # 0x700
HIT = S + 0x1100      # 0x100
CFG = S + 0x2000      # 0x1000
RACE = S + 0x3000     # 0x800
ITEMS = S + 0x4000    # 4 items of 0x730
STRS = S + 0x6000     # strings, 0x80 apart
DST = S + 0x8000      # 0x200

GLOBALS = [(0x4B11DC, 4), (0x4B1960, 4), (0x4B2198, 4), (0x4BE2B4, 12), (0x4AC738, 4), (0x6CC400, 0x80),
           (0x4BDE74, 4), (0x6CE5F0, 0x20), (0x4D1198, 4), (0x4BE388, 4), (0x6C0A38, 4), (0x4CB588, 4),
           (0x4CB49C, 4), (0x4CB4B8, 4), (0x4CB5E8, 0x100), (0x4AF70C, 8), (0x4C98E0, 4), (0x4C2914, 8),
           (0x4CB468, 4), (0x4C2480, 4), (0x6BF160, 8), (0x4CB574, 4), (0x4CB16C, 0x40), (0x4AF680, 0x70),
           (0x4AF704, 4), (0x4BEB38, 4)]

# address: (name, argument count, stdcall, per-argument log kinds)
#  v value, b low byte, mN N bytes at the pointer, s value or "stack"
STUBS = {
    0x419300: ("body_retire", 1, False, "v"),
    0x4611D0: ("grid_unlink", 1, False, "v"),
    0x4427A0: ("crumble_start", 1, False, "v"),
    0x478430: ("pair_list_remove", 1, False, "v"),
    0x403950: ("object_debris", 1, False, "v"),
    0x42AA30: ("explosion_effects", 5, False, ["m12", "m12", "v", "v", "v"]),
    0x4599C0: ("car_blast_hit", 2, False, "vv"),
    0x45EF80: ("check_a", 0, False, ""),
    0x45EED0: ("check_b", 0, False, ""),
    0x45EF30: ("check_c", 0, False, ""),
    0x45FBE0: ("score_show", 1, False, "v"),
    0x4600A0: ("net_kill_message", 3, False, "vvv"),
    0x471F40: ("sound_voice_stop", 1, False, "b"),
    0x458A20: ("trail_update", 2, False, "vv"),
    0x488CC0: ("movie_text_clear", 1, False, "v"),
    0x488AD0: ("movie_text_draw", 3, False, "vvv"),
    STUB_PAGE + 0x00: ("PeekMessageA", 5, True, "svvvv"),
    STUB_PAGE + 0x10: ("GetMessageA", 4, True, "svvv"),
    0x4265D0: ("viewport_apply", 1, False, "v"),
    0x428480: ("draw_otag_rect", 2, False, "vv"),
    0x428470: ("draw_otag", 1, False, "v"),
    0x427A70: ("ot_clear_reverse", 2, False, "vv"),
    0x423BA0: ("screen_ot_swap", 0, False, ""),
    0x423AF0: ("screen_ot_first", 0, False, ""),
    0x42DBF0: ("movie_win", 1, False, "v"),
    0x448880: ("menu_back_run", 0, False, ""),
    0x449160: ("menu_net_input", 8, False, "vvvvvvvv"),
    0x447F20: ("toggle_step", 3, False, "vvv"),
    0x44D000: ("menu_param_callback", 4, False, "vvvv"),
    0x4475F0: ("movie_allowed", 1, False, "v"),
    0x443A10: ("movie_begin", 0, False, ""),
    0x443B00: ("movie_end", 0, False, ""),
    0x4979F0: ("sprintf", 4, False, "vvvv"),
    0x448000: ("attract_restart", 0, False, ""),
}
for _i in range(14):
    STUBS[STUB_PAGE + 0x100 + 0x10 * _i] = ("movie_entry_%d" % _i, 0, False, "")

TESTS = {}
OUTCOMES = {}


def p32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def p16(v):
    return struct.pack("<H", v & 0xFFFF)


def pick(rng, *c):
    return rng.choice(c)


def rbytes(rng, n):
    return rng.randbytes(n)


def rstr(rng, n=0x60):
    k = rng.randrange(0, n)
    return bytes(rng.randrange(1, 256) for _ in range(k)) + b"\0"


class Stubs:
    def __init__(self, m):
        self.m = m
        self.log = []
        self.script = {}
        self.target = None
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, target):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.target = target

    def _next(self, name, default=0):
        v = self.script.get(name)
        return v.pop(0) if v else default

    def _hook(self, uc, address, size, _):
        if address == self.target:
            return
        name, nargs, stdcall, kinds = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        raw = list(words[1:])
        args = []
        for i, a in enumerate(raw):
            k = kinds[i] if i < len(kinds) else "s"
            if k == "v":
                args.append(a)
            elif k == "b":
                args.append(a & 0xFF)
            elif k.startswith("m"):
                args.append(bytes(uc.mem_read(a, int(k[1:]))))
            else:
                args.append(("stack",) if E.STACK <= a < E.STACK + E.STACK_SIZE else a)
        self.log.append((name, tuple(args)))
        eax = self._act(uc, name, raw)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * nargs if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, words[0])

    def _act(self, uc, name, a):
        if name in ("PeekMessageA", "GetMessageA"):
            ok = self._next(name)
            msg = self._next("msg", None)
            if msg is not None:
                uc.mem_write(a[0], msg)
            return ok
        return self._next(name, 0)


def call_fpu(m, target, args, cw):
    """fninit; fldcw [cw]; jmp target (one trampoline per target)."""
    slots = call_fpu.slots
    if target not in slots:
        at = TRAMP + 0x10 * len(slots)
        code = b"\xdb\xe3" + b"\xd9\x2d" + p32(TRAMP + 0xF00)
        code += b"\xe9" + p32(target - (at + len(code) + 5))
        m.write(at, code)
        slots[target] = at
    m.write(TRAMP + 0xF00, p16(cw))
    return m.call(slots[target], args)


call_fpu.slots = {}


def base_world(rng, w):
    for a, n in GLOBALS:
        w.append((a, rbytes(rng, n)))
    w.append((0x4B1960, p32(RACE)))
    w.append((0x4B2198, p32(CFG)))
    w.append((CFG, rbytes(rng, 0x1000)))
    w.append((RACE, rbytes(rng, 0x800)))
    w.append((0x4A325C, p32(STUB_PAGE + 0x00)))
    w.append((0x4A3258, p32(STUB_PAGE + 0x10)))
    w.append((DST, rbytes(rng, 0x200)))


def test(address, returns=True):
    def wrap(f):
        TESTS[f.__name__[3:]] = (address, returns, f)
        return f
    return wrap


# ---------------------------------------------------------------- hit callbacks

def object_state(rng, w):
    obj = bytearray(rbytes(rng, 0x200))
    struct.pack_into("<I", obj, 0, rng.getrandbits(32) | pick(rng, 0, 0x2000, 0x2200, 0x200))
    if rng.randrange(2):
        struct.pack_into("<I", obj, 0, struct.unpack_from("<I", obj, 0)[0] & ~0x2000)
    w.append((OBJ, bytes(obj)))
    w.append((0x4B11DC, p32(pick(rng, 0, 1, 0x7FFFFFFF, rng.getrandbits(32)))))


@test(0x401120)
def st_track_object_hit_blast(rng, w):
    object_state(rng, w)
    return [OBJ], {}


@test(0x401280)
def st_track_object_hit_k23(rng, w):
    object_state(rng, w)
    return [OBJ], {}


@test(0x424990)
def st_driller_hit(rng, w):
    self_ = bytearray(rbytes(rng, 0x200))
    other = bytearray(rbytes(rng, 0x800))
    owner = bytearray(rbytes(rng, 0x700))
    hit = bytearray(rbytes(rng, 0x100))
    struct.pack_into("<I", self_, 0x180, pick(rng, OWNER, OWNER, OWNER, OWNER, 0))
    struct.pack_into("<I", self_, 0x1B0, pick(rng, OTHER, 0, rng.getrandbits(32), OBJ))
    struct.pack_into("<I", self_, 0x1B4, pick(rng, OTHER, 0, rng.getrandbits(32), OBJ))
    struct.pack_into("<I", self_, 0x1AC, pick(rng, 0x7F, 0x7F, rng.randrange(32), rng.getrandbits(32)))
    struct.pack_into("<I", other, 0, (rng.getrandbits(32) & ~0x1FF) | pick(rng, 3, 3, 3, 0x103, rng.getrandbits(9)))
    other[0x70C] = pick(rng, 8, 0, 1, rng.getrandbits(8))
    struct.pack_into("<H", other, 0x64E, rng.getrandbits(16))
    struct.pack_into("<H", owner, 0x64E, rng.randrange(0, 0x7F0))
    struct.pack_into("<i", hit, 0x44, pick(rng, -4, -4, 0, rng.getrandbits(32) - 2 ** 31))
    w.append((OBJ, bytes(self_)))
    w.append((OTHER, bytes(other)))
    w.append((OWNER, bytes(owner)))
    w.append((HIT, bytes(hit)))
    race = bytearray(rbytes(rng, 0x800))
    race[0x551] = pick(rng, 3, 8, 3, 8, 0, rng.getrandbits(8))
    w.append((RACE, bytes(race)))
    oth = pick(rng, OTHER, OTHER, 0, 0)
    h = pick(rng, HIT, HIT, 0)
    chk = lambda: [pick(rng, 0, 1, 1, 1, 2, rng.getrandbits(32)) for _ in range(2)]  # noqa: E731
    return [OBJ, oth, h], {"check_a": chk(), "check_b": chk(), "check_c": chk()}


# ---------------------------------------------------------------- movie player

@test(0x42D8E0)
def st_movie_frame_check(rng, w):
    frame = rng.randrange(-3, 40)
    count = pick(rng, 0, 1, 2, 5, 8, -1)
    w.append((0x4BE2B4, p32(frame) + p32(count) + p32(pick(rng, 0, 1))))
    w.append((0x4AC738, p32(pick(rng, -1, -1, frame - 5, frame, rng.randrange(-3, 40), rng.getrandbits(32)))))
    recs = b""
    for _ in range(8):
        recs += p32(rng.getrandbits(32)) + p32(rng.getrandbits(32))
        recs += p32(pick(rng, frame, frame + 1, frame - 1, rng.randrange(-3, 40), rng.getrandbits(32)))
        recs += p32(rng.getrandbits(32))
    w.append((0x6CC400, recs))
    msg = p32(rng.getrandbits(32)) + p32(pick(rng, 0x100, 0x100, 0x101, rng.getrandbits(32)))
    msg += p32(pick(rng, 8, 9, 13, 27, 32, rng.randrange(0, 40), rng.getrandbits(32))) + rbytes(rng, 16)
    return [], {"PeekMessageA": [pick(rng, 0, 1, 1, rng.getrandbits(32))],
                "GetMessageA": [pick(rng, 0, 1, 1, -1)], "msg": [msg, msg]}


# ---------------------------------------------------------------- race clock

@test(0x435140, returns=False)
def st_clock_tick(rng, w):
    tick = rng.getrandbits(32)
    w.append((0x4D1198, p32(pick(rng, 0, 0, 1))))
    w.append((0x4BE388, p32(tick)))
    w.append((0x6C0A38, p32(pick(rng, tick, tick + 1, tick + 2, tick - 1, rng.getrandbits(32)))))
    w.append((0x6CE600, p32(pick(rng, 0, 1, 1, rng.getrandbits(32)))))
    race = bytearray(rbytes(rng, 0x800))
    race[0x54C] = pick(rng, 0, 1, 1, rng.getrandbits(8))
    race[0x552] = pick(rng, 0, 1, rng.getrandbits(8))
    w.append((RACE, bytes(race)))
    return [], {}


def ot_state(rng, w):
    w.append((0x4BDE74, p32(rng.getrandbits(32))))
    w.append((0x6CE60C, p32(pick(rng, 0x6CD4A0, 0x6CDD10, rng.getrandbits(32)))))
    w.append((0x6CE608, p32(pick(rng, 0x6CE5F0, 0x6CE5F8, rng.getrandbits(32)))))
    race = bytearray(rbytes(rng, 0x800))
    race[0x54C] = pick(rng, 0, 1, rng.getrandbits(8))
    w.append((RACE, bytes(race)))


@test(0x423AF0, returns=False)
def st_screen_ot_first(rng, w):
    ot_state(rng, w)
    return [], {}


@test(0x423BA0, returns=False)
def st_screen_ot_swap(rng, w):
    ot_state(rng, w)
    return [], {}


# ---------------------------------------------------------------- movie table

def st_lang(rng, w):
    return [], {"movie_win": [rng.getrandbits(32)]}


for _i, _a in enumerate((0x447F90, 0x447FA0, 0x447FB0, 0x447FC0, 0x447FD0, 0x447FE0)):
    TESTS["movie_win_lang%d" % _i] = (_a, False, st_lang)


# ---------------------------------------------------------------- menus

@test(0x448CF0, returns=False)
def st_menu_item_text_keep(rng, w):
    n = pick(rng, 0, 1, 2, 4)
    items = bytearray(rbytes(rng, 4 * 0x730))
    for i in range(4):
        s = rstr(rng, 0xF0)
        items[i * 0x730 + 0x1FC:i * 0x730 + 0x1FC + len(s)] = s
    w.append((ITEMS, bytes(items)))
    w.append((0x4CB4B8, p32(ITEMS)))
    w.append((0x4CB49C, p32(n)))
    w.append((0x4CB588, bytes([pick(rng, 0, 1, 2, 3, 0xFF, 0x80)])))
    return [rng.getrandbits(32)], {}


@test(0x448D60)
def st_menu_input_back10(rng, w):
    return [pick(rng, 10, 10, 9, 11, 0, rng.getrandbits(32))], {}


@test(0x449120)
def st_menu_input_net(rng, w):
    return [rng.getrandbits(32)], {"menu_net_input": [rng.getrandbits(32)]}


for _o, _a in ((0x64, 0x44CE70), (0x68, 0x44CE80), (0x6C, 0x44CE90), (0x70, 0x44CEA0)):
    TESTS["menu_slider_set_%02x" % _o] = (_a, True, lambda rng, w: ([rng.getrandbits(32)], {}))


def cmd(rng):
    return pick(rng, 0x12, 0x13, 0x14, 0x15, 0x16, 0x11, 0x17, rng.getrandbits(32))


@test(0x44CEB0)
def st_menu_names_cb(rng, w):
    count = pick(rng, 0, 1, 3, 16, -1)
    names = b""
    for i in range(16):
        s = rstr(rng)
        w.append((STRS + 0x80 * i, s))
        names += p32(STRS + 0x80 * i)
    w.append((0x4CB16C, names))
    w.append((0x4CB574, p32(count)))
    w.append((CFG + 0x86, bytes([pick(rng, 0, 1, 2, 15, 16, rng.getrandbits(8))])))
    return [cmd(rng), DST], {}


def param_cb(rng, w):
    return [cmd(rng), DST], {"menu_param_callback": [pick(rng, -1, 0, 1, 0x14, 100, rng.getrandbits(32))]}


TESTS["menu_param_cb_0b"] = (0x44CFC0, True, param_cb)
TESTS["menu_param_cb_1f"] = (0x44D130, True, param_cb)


@test(0x44D170)
def st_menu_split_mode_cb(rng, w):
    w.append((CFG + 0xF, bytes([pick(rng, 0, 1, 2, 3, 4, 5, 0xFF, rng.getrandbits(8))])))
    return [cmd(rng), DST], {}


def movie_menu(rng, w):
    table = b""
    for i in range(14):
        s = rstr(rng)
        w.append((STRS + 0x80 * i, s))
        table += p32(STUB_PAGE + 0x100 + 0x10 * i) + p32(STRS + 0x80 * i)
    w.append((0x4AF680, table))
    count = pick(rng, 14, 14, 1, 5, 0x100, 0x101)
    w.append((0x4AF704, p32(count)))
    w.append((CFG + 0xD, bytes([pick(rng, 0, 1, 12, 13, 14, 0xFF if count > 0xFF else 13)])))


@test(0x44D290)
def st_menu_movie_play(rng, w):
    movie_menu(rng, w)
    return [pick(rng, 6, 7, 6, 7, 5, 8, rng.getrandbits(32))], {"movie_allowed": [pick(rng, 0, 1, 2)]}


@test(0x44D2F0)
def st_menu_movie_cb(rng, w):
    movie_menu(rng, w)
    return [cmd(rng), DST], {}


def toggle(rng, w):
    return [pick(rng, 0, 6, 0xE, 0xF, 0x10, rng.getrandbits(32))], {
        "toggle_step": [pick(rng, 0, 1, 2, 0x100, 0xFFFFFFFE, rng.getrandbits(32))]}


for _n, _a in (("menu_toggle_0c", 0x44D3E0), ("menu_toggle_0e", 0x44D420), ("menu_toggle_39", 0x44D460),
               ("menu_toggle_0a", 0x44D4A0), ("menu_race_bit_720_5", 0x44D4E0), ("menu_race_bit_721_0", 0x44D540),
               ("menu_race_bit_721_1", 0x44D590), ("menu_race_bit_721_2", 0x44D5E0),
               ("menu_race_bit_721_3", 0x44D640), ("menu_race_bit_721_5", 0x44D6A0),
               ("menu_race_bit_721_4", 0x44D700), ("menu_race_bit_720_1", 0x44D760),
               ("menu_race_bit_720_2", 0x44D7B0), ("menu_race_bit_720_3", 0x44D810),
               ("menu_race_bit_720_4", 0x44D870), ("menu_toggle_10", 0x44D8D0), ("menu_toggle_11", 0x44D910)):
    TESTS[_n] = (_a, True, toggle)


@test(0x44D950)
def st_menu_time_cb(rng, w):
    w.append((0x4BEB38, p32(pick(rng, 0, 1, rng.getrandbits(32)))))
    v = pick(rng, 0, 1, 99, 100, 999, 1000, 4999, 5000, 30000, 59000, 59900, 60000, 60001, 0xFFFF,
             rng.getrandbits(16), rng.randrange(5000, 60001))
    w.append((CFG + 0x12, p16(v)))
    return [cmd(rng), DST], {"sprintf": [rng.getrandbits(32)]}


@test(0x44DA90)
def st_menu_moved_idle(rng, w):
    return [rng.getrandbits(32), rng.getrandbits(32)], {"attract_restart": [rng.getrandbits(32)]}


# ---------------------------------------------------------------- driver

CONTROL_WORDS = (0x007F, 0x027F, 0x037F)
STACK_FILL = bytes((i * 0x9D + 0x37) & 0xFF for i in range(E.STACK_SIZE))


def run_case(m, stubs, name, rng):
    address, returns, builder = TESTS[name]
    w = []
    base_world(rng, w)
    args, script = builder(rng, w)
    cw = pick(rng, *CONTROL_WORDS)
    results = []
    for target in (address, m.symbols[name]):
        m.write(S, bytes(SIZE))
        for a, data in w:
            m.write(a, data)
        m.write(E.STACK, STACK_FILL)
        stubs.reset(script, address)
        r = call_fpu(m, target, args, cw)
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(S, SIZE) + b"".join(m.read(a, n) for a, n in GLOBALS)
        results.append((r, mem, stubs.log))
    OUTCOMES[results[0][0][0]] = OUTCOMES.get(results[0][0][0], 0) + 1
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        where = ""
        if first is not None:
            if first < SIZE:
                where = f" (memory differs at scratch +{first:#x})"
            else:
                off = first - SIZE
                for ga, gn in GLOBALS:
                    if off < gn:
                        where = f" (memory differs at {ga + off:#x})"
                        break
                    off -= gn
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {str(la[k:k + 2])[:400]}\n  ours:          {str(lb[k:k + 2])[:400]}"
        return f"{name}: args {[hex(x) for x in args]} original {results[0][0]} ours {results[1][0]}" + where + logs
    return None


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 500
    m = E.Machine(exe, elf)
    m.uc.mem_map(S, SIZE)
    m.uc.mem_map(STUB_PAGE, 0x1000)
    m.write(STUB_PAGE, b"\xc3" * 0x1000)
    m.uc.mem_map(TRAMP, 0x1000)
    stubs = Stubs(m)
    rng = random.Random(0x5A)
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    failed = 0
    for name in TESTS:
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        OUTCOMES.clear()
        for _ in range(iterations):
            msg = run_case(m, stubs, name, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'} {OUTCOMES}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
