#!/usr/bin/env python3
"""Differential test of orig_pf.c (group PF: the trails and beams effects,
the window and dialog procedures, the clock, the loading picture, the HUD
update and sprite quads, 0x433d90-0x4387d0) against the original, on
random state.

Callees outside the group are logging stubs in both runs (each logs the
arguments it reads; strings, rectangles and vectors by content; stack
buffers as "stack"): Windows calls through the import table (each slot used
gets a stub of its own that pops its stdcall arguments), the particle
spawner, the HUD pieces, the text table, sprintf / strcat (done in Python),
the image loader and texture pages, the dialog helpers, shutdown. Pure game
code runs for real in both: the GTE transform / projection behind the
pointers 0x6ccd78 / 0x6ccd80 (0x426a20 / 0x426b00, x87), _ftol, the sine
table, vec_length / fix_div16 / vec_cross, and the group's own functions
called inside the group. The x87 control word is 0x27f, 0x37f or 0x07f.
Compared: the whole data section 0x4a7000-0x6d3000, the scratch memory, the
return value, esp after return (stdcall), ebx / esi / edi / ebp, the x87
status top and tag word, and the call log.

    tests/emu_orig_pf.py EXE build/emu_pf.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,  # noqa: E402
                               UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                               UC_X86_REG_FPTAG)

DATA, DATA_SIZE = 0x4A7000, 0x6D3000 - 0x4A7000
SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
CONFIG = SCRATCH + 0x0000  # 0x1000
RACE = SCRATCH + 0x1000  # 0x1000
ARGS = SCRATCH + 0x2000  # argument blocks (0x1000)
SLOT = SCRATCH + 0x3000  # HUD slot + car (0x1000)
TEX = SCRATCH + 0x4000  # texture records (0x2000)
TEXBASE = SCRATCH + 0x6000  # *0x6c1eb0 block
POINTS = SCRATCH + 0x7000  # beams' shape points (4 x 0x110)
OTS = SCRATCH + 0x8000  # OT heads (0x1000)
TRAILS = SCRATCH + 0x10000  # 0x6800: the trails' quads
BEAMS = SCRATCH + 0x18000  # 0x3c78: the beams' quads
IATSTUB = 0x02200000
STACK_FILL = b"\xcc" * E.STACK_SIZE

# IAT slot: (name, stdcall argument kinds)
IAT = {
    0x4A3238: ("PostQuitMessage", "v"),
    0x4A323C: ("ClientToScreen", "vv"),
    0x4A3244: ("GetDlgItem", "vv"),
    0x4A3254: ("SetRect", "ovvvv"),
    0x4A3260: ("DestroyWindow", "v"),
    0x4A327C: ("SetFocus", "v"),
    0x4A3284: ("EnableWindow", "vv"),
    0x4A3288: ("EndDialog", "vv"),
    0x4A3290: ("SendDlgItemMessageA", "vvvvm"),
    0x4A3294: ("GetActiveWindow", ""),
    0x4A3298: ("SetActiveWindow", "v"),
    0x4A32A0: ("DefWindowProcA", "vvvv"),
    0x4A32B4: ("timeGetTime", ""),
}

# address: (name, cdecl argument kinds: v value, s string, o out buffer, r rect (16 bytes), c 12-byte vector)
STUBS = {
    0x462690: ("particle_spawn", "ccv"),
    0x42E6D0: ("text_entry", "ovvv"),
    0x4979F0: ("sprintf", "osvv"),
    0x498460: ("strcat", "os"),
    0x477530: ("image_load", "svv"),
    0x4201F0: ("image_tiles_upload", "vvvvv"),
    0x4203A0: ("image_tiles_draw", "vvvrvv"),
    0x474260: ("boot_data_save", ""),
    0x435E2C: ("shutdown_all", "v"),
    0x4360E1: ("cheat_code_char", "vvvvv"),
    0x489600: ("input_poll", ""),
    0x42E3C0: ("language_set", "v"),
    0x4883C0: ("device_list_fill", "vv"),
    0x487020: ("device_select", "v"),
    0x488470: ("video_mode_list_fill", "vvvv"),
    0x474B70: ("pair_lookup_default", ""),
    0x473C50: ("config_default_texts", "v"),
    0x488D60: ("joy_device_value", "v"),
    0x42DF90: ("strings_load", "v"),
    0x438DF0: ("hud_438df0", "v"),
    0x438E60: ("hud_438e60", "v"),
    0x4390C0: ("hud_4390c0", "v"),
    0x439050: ("hud_439050", "v"),
    0x438FB0: ("hud_438fb0", "v"),
    0x439270: ("hud_439270", "vv"),
    0x439F20: ("hud_439f20", "vv"),
    0x439400: ("hud_439400", "v"),
    0x43A000: ("hud_43a000", "vv"),
    0x45CFE0: ("hud_45cfe0", "vv"),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


def s32(v):
    return struct.unpack("<i", struct.pack("<I", v & 0xFFFFFFFF))[0]


def fbits(v):
    return struct.unpack("<I", struct.pack("<f", v))[0]


def c_string(uc, a, limit=0x200):
    return bytes(uc.mem_read(a, limit)).split(b"\0")[0]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(IATSTUB, 0x1000)
        m.uc.mem_write(IATSTUB, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, kinds)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, kinds, True)
        for address, (name, kinds) in STUBS.items():
            self.calls[address] = (name, kinds, False)
        for address in self.calls:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _stub(self, uc, address):
        name, kinds, stdcall = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            if k == "o":
                logged.append("stack" if on_stack(a) else a)
            elif k == "s":
                logged.append(c_string(uc, a))
            elif k == "r":
                logged.append(bytes(uc.mem_read(a, 16)))
            elif k == "c":
                logged.append(bytes(uc.mem_read(a, 12)))
            elif k == "m":  # SendDlgItemMessageA's lParam: a string for WM_SETTEXT / CB_ADDSTRING
                logged.append(c_string(uc, a) if args[2] in (0xC, 0x143) else a)
            else:
                logged.append(a)
        eax = 0
        if name == "text_entry":
            dst, n, sec, idx = args[0], s32(args[1]), args[2], s32(args[3])
            text = b"T%x.%d%%d" % (sec, idx)
            if dst and n > 0:
                uc.mem_write(dst, text[:n - 1] + b"\0")
            eax = len(text)
        elif name == "sprintf":
            text = b"S" + c_string(uc, args[1])[:40] + b"%x/%x" % (args[2], args[3])
            uc.mem_write(args[0], text + b"\0")
            eax = len(text)
        elif name == "strcat":
            d = c_string(uc, args[0])
            uc.mem_write(args[0] + len(d), c_string(uc, args[1]) + b"\0")
            eax = args[0]
        elif name == "SetRect":
            uc.mem_write(args[0], struct.pack("<4I", *args[1:5]))
            eax = 1
        elif name == "GetDlgItem":
            eax = 0x5000 + args[1]
        else:
            eax = self._next(name, 0)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * len(kinds) if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---- state builders: st = {address: bytes}, script = {stub name: [results]} ----

def w32(st, a, v):
    st[a] = struct.pack("<I", v & 0xFFFFFFFF)


def w16(st, a, v):
    st[a] = struct.pack("<H", v & 0xFFFF)


def w8(st, a, v):
    st[a] = bytes([v & 0xFF])


def base_state(rng):
    st = {}
    config = bytearray(rng.randbytes(0x1000))
    config[0x36] = pick(rng, 0, 1, 2, 3, 4, 5, rng.getrandbits(8))
    config[0x96] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    struct.pack_into("<i", config, 0x48, pick(rng, -1, 0, 1, 4, 4, 0xB, 0x7F, 0x80, 0x81, rng.randrange(-5, 300), s32(r32(rng))))
    st[CONFIG] = bytes(config)
    w32(st, 0x4B2198, CONFIG)
    race = bytearray(rng.randbytes(0x1000))
    race[0x551] = pick(rng, *range(10), rng.getrandbits(8))
    race[0x552] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    st[RACE] = bytes(race)
    w32(st, 0x4B1960, RACE)
    return st


def gte_state(rng, st):
    """The x87 transform / projection (0x426a20 / 0x426b00) and its state."""
    w32(st, 0x6CCD78, 0x426A20)
    w32(st, 0x6CCD80, 0x426B00)
    for a in (0x6CCD1C, 0x6CCD20, 0x6CCD24, 0x6CCD2C, 0x6CCD30, 0x6CCD34, 0x6CCD3C, 0x6CCD40, 0x6CCD44):
        w32(st, a, fbits(pick(rng, rng.uniform(-1, 1), rng.uniform(-1, 1), 0.0, 1.0, rng.uniform(-20, 20))))
    for a in (0x6CCD0C, 0x6CCD10, 0x6CCD14):
        w32(st, a, fbits(pick(rng, rng.uniform(-3000, 3000), 0.0, rng.uniform(-1e5, 1e5))))
    w32(st, 0x6CCD00, fbits(pick(rng, 256.0, 320.0, rng.uniform(1, 1000))))
    w32(st, 0x6CCD04, fbits(pick(rng, 10.0, 1.0, 50.0, rng.uniform(0, 100))))
    w32(st, 0x6CCD08, fbits(pick(rng, 20000.0, 65536.0, 1e9, rng.uniform(0, 30000))))
    w32(st, 0x6CCD5C, fbits(pick(rng, 320.0, 0.0, rng.uniform(0, 2000))))
    w32(st, 0x6CCD60, fbits(pick(rng, 240.0, 0.0, rng.uniform(0, 2000))))
    for a, v in ((0x6CCD64, 0.0), (0x6CCD68, 640.0), (0x6CCD6C, 0.0), (0x6CCD70, 480.0)):
        w32(st, a, fbits(pick(rng, v, v, rng.uniform(-100, 1000))))
    w32(st, 0x6CE60C, pick(rng, 0x6CDD10, 0x6CDD10, 0x6CD508, r32(rng)))
    w32(st, 0x6CCE84, pick(rng, 0, 1, r32(rng)))
    for table in (0x6CD510, 0x6CDD80):  # OT heads: pointers into the scratch with the flag bit sometimes set
        st[table] = b"".join(struct.pack("<I", OTS + rng.randrange(0, 0x400) * 4 | pick(rng, 0, 0, 0x80000000))
                             for _ in range(0x200))
    st[OTS] = bytes(rng.randbytes(0x1000))


def case_trail_point_add(rng):
    st = base_state(rng)
    w32(st, 0x6C80FC, pick(rng, 0, 1, 7, 8, rng.randrange(0, 8)))
    now = r32(rng)
    w32(st, 0x6C0A50, now)
    w32(st, 0x4BE380, pick(rng, now - 5, now + 5, now - 4, now + 4, now, now - 100, r32(rng), now ^ 0x80000000))
    scale = pick(rng, 1 << 20, 1 << 16, 1 << 24, 1 << 30)
    pos = [rng.randrange(-scale, scale) for _ in range(3)]
    d = pick(rng, 0x20, 0x100, 0x1000, 0x10000, 0x40000, rng.randrange(1, 1 << 20))
    ahead = [max(-0x8000, min(0x7FFF, (p >> 8) + rng.randrange(-d, d + 1) // 256)) for p in pos]
    if rng.randrange(4) == 0:
        ahead = [rng.randrange(-0x8000, 0x8000) for _ in range(3)]
    up = [pick(rng, 0, 0x10000, -0x10000, rng.randrange(-0x10000, 0x10001)) for _ in range(3)]
    st[ARGS] = struct.pack("<3i", *pos)
    st[ARGS + 0x10] = struct.pack("<3h", *ahead)
    st[ARGS + 0x20] = struct.pack("<3i", *up)
    return [ARGS, ARGS + 0x10, ARGS + 0x20], st, {}


def trail_records(rng):
    recs = b""
    for _ in range(8):
        pos = [rng.randrange(-0x8000, 0x8000) for _ in range(3)]
        a = [pick(rng, rng.randrange(-0x4000, 0x4000), rng.randrange(-0x8000, 0x8000), 0) for _ in range(3)]
        b = [pick(rng, rng.randrange(-0x4000, 0x4000), rng.randrange(-0x8000, 0x8000), 0) for _ in range(3)]
        age = pick(rng, 0, 1, 5, 15, 16, 17, 0x1E, 0x1F, 0x20, 0xFFFF, rng.randrange(0, 0x20))
        live = pick(rng, 0, 1, 1, 1, rng.getrandbits(16))
        recs += struct.pack("<3hH3hH3hH", *pos, age, *a, live, *b, rng.getrandbits(16))
    return recs


def case_trails_draw(rng):
    st = base_state(rng)
    gte_state(rng, st)
    st[0x6C8020] = trail_records(rng)
    tick = r32(rng)
    w32(st, 0x6C0A50, tick)
    w32(st, 0x6C8004, pick(rng, tick, tick + 1))
    w32(st, 0x4BE380, pick(rng, tick - 10, tick - 2, r32(rng)))
    w32(st, 0x6C80FC, rng.randrange(0, 8))
    st[0x6C80F0] = struct.pack("<3i", *[rng.randrange(-1 << 22, 1 << 22) for _ in range(3)])
    st[0x6C80E0] = struct.pack("<3h", *[rng.randrange(-0x8000, 0x8000) for _ in range(3)])
    st[0x6C8010] = struct.pack("<3i", *[rng.randrange(-0x10000, 0x10001) for _ in range(3)])
    w32(st, 0x4BE378, TRAILS)
    st[TRAILS] = bytes(rng.randbytes(0x6800))
    return [], st, {}


def case_beams_draw(rng):
    st = base_state(rng)
    gte_state(rng, st)
    beams = b""
    for _ in range(4):
        start = [rng.randrange(-0x8000, 0x8000) for _ in range(3)]
        end = [max(-0x8000, min(0x7FFF, s + rng.randrange(-0x2000, 0x2000))) for s in start]
        if rng.randrange(4) == 0:
            end = [rng.randrange(-0x8000, 0x8000) for _ in range(3)]
        beams += struct.pack("<4H3hH3hH", pick(rng, 0, 0, 1, 2, 30, rng.getrandbits(16)), rng.getrandbits(16),
                             pick(rng, 0, rng.getrandbits(6), rng.getrandbits(16)), rng.getrandbits(16), *start,
                             rng.getrandbits(16), *end, rng.getrandbits(16))
    st[0x6C8100] = beams
    w32(st, 0x4B11D8, r32(rng) & pick(rng, 0x7FFFFFFF, 0xFFFFFFFF))
    w32(st, 0x4BE37C, POINTS)
    st[POINTS] = bytes(rng.randbytes(0x440))
    w32(st, 0x4BE374, BEAMS)
    st[BEAMS] = bytes(rng.randbytes(0x3C78))
    w8(st, 0x6CCD84, rng.getrandbits(8))
    return [], st, {}


WM = [2, 3, 0x100, 0x104, 0x105, 0x111, 0x112, 0x112, 0x112, 0x110, 0x200, 1, 0x101, 0x106]
SYS = [0xF170, 0xF140, 0xF130, 0xF040, 0xF050, 0xF100, 0xF090, 0xF060, 0xF020, 0xF000, 0]


def case_window_proc(rng):
    st = base_state(rng)
    msg = pick(rng, *WM, r32(rng))
    wp = pick(rng, *SYS, 0x9C41, 0x19C41, 0x9C40, r32(rng))
    w32(st, 0x4D5F90, r32(rng))
    w32(st, 0x4D5F94, r32(rng))
    return [r32(rng), msg, wp, r32(rng)], st, {"DefWindowProcA": [r32(rng)]}


def case_window_destroy_quit(rng):
    st = base_state(rng)
    w32(st, 0x4BE460, r32(rng))
    return [], st, {}


def case_window_command(rng):
    return [r32(rng), pick(rng, 0x9C41, 0x9C40, 0x19C41, rng.getrandbits(16))], base_state(rng), {}


def case_input_focus_poll(rng):
    st = base_state(rng)
    w = pick(rng, 0, r32(rng))
    w32(st, 0x6C8000, w)
    return [], st, {"GetActiveWindow": [pick(rng, w, w + 1, 0)]}


def case_game_time_get(rng):
    return [], base_state(rng), {"timeGetTime": [r32(rng)]}


def case_loading_picture_draw(rng):
    st = base_state(rng)
    w32(st, 0x4BE490, pick(rng, 0, 0, 1, r32(rng)))
    w32(st, 0x4D4650, pick(rng, 0, 1))
    w32(st, 0x4ACC64, pick(rng, -1, -1, 0, 3, r32(rng)))
    for a in (0x4BE398, 0x4BE39C, 0x4BE3A0, 0x4BE3A4, 0x4BE3A8, 0x4BE3AC, 0x4ACC38, 0x4ACC3C):
        w32(st, a, r32(rng))
    sc = {"image_load": [pick(rng, 0, 1, r32(rng))], "image_tiles_upload": [pick(rng, -1, 0, 5, r32(rng))]}
    return [pick(rng, 0, 1, 1, 2, r32(rng))], st, sc


def case_mode_dialog_languages_fill(rng):
    return [r32(rng)], base_state(rng), {}


def case_mode_dialog_texts_set(rng):
    return [r32(rng)], base_state(rng), {}


def dialog_state(rng):
    st = base_state(rng)
    for a in (0x4BE4B8, 0x4BE448):
        w32(st, a, pick(rng, 0, 1, 2, 5, r32(rng)))
    for a in (0x4ACC58, 0x4BE44C):
        w32(st, a, pick(rng, 0, 1, 1, 2, 3))
    sc = {"SendDlgItemMessageA": [pick(rng, 0, 1, 1, 2, -1, r32(rng)) for _ in range(12)],
          "pair_lookup_default": [pick(rng, 0, 1, r32(rng))], "joy_device_value": [pick(rng, 0, 1, r32(rng))]}
    return st, sc


def case_mode_dialog_proc(rng):
    st, sc = dialog_state(rng)
    msg = pick(rng, 0x110, 0x111, 0x111, 0x111, 0x200, *WM, r32(rng))
    wp = pick(rng, 0x3EA, 0x3E8, 1, 2, 3, 0x3E9, 0, r32(rng)) | pick(rng, 0, 1, 1, 2, rng.getrandbits(16)) << 16
    return [r32(rng), msg, wp, r32(rng)], st, sc


def case_error_dialog_proc(rng):
    st, sc = dialog_state(rng)
    msg = pick(rng, 0x110, 0x110, 0x111, 0x111, *WM, r32(rng))
    wp = pick(rng, 1, 2, 3, 0, r32(rng)) | rng.getrandbits(16) << 16
    return [r32(rng), msg, wp, r32(rng)], st, sc


def slot_state(rng, st):
    st[SLOT] = struct.pack("<I", SLOT + 0x100) + bytes(rng.randbytes(0xFC))
    st[SLOT + 0x100] = bytes(rng.randbytes(0xF00))
    w32(st, 0x6C7FDC, r32(rng))


def case_hud_update(rng):
    st = base_state(rng)
    slot_state(rng, st)
    return [SLOT], st, {"hud_439050": [pick(rng, 0, 1, r32(rng))]}


def case_hud_update_extra(rng):
    st = base_state(rng)
    slot_state(rng, st)
    return [SLOT], st, {}


SPRITES = [2, 3, 4, 5, 6, 7, 8, 45, 46, 47, 52, 65, 66, 0, 1, 9, 0x10, 0x2C, 0x30, 0x33, 0x35, 0x40, 0x43, 0x45]


def case_hud_sprite_set(rng):
    st = base_state(rng)
    n = pick(rng, *SPRITES, rng.randrange(0, 0x46))
    packet = bytearray(rng.randbytes(0xF0))
    for off in (0x10, 0x14, 0x2C, 0x30, 0x48, 0x4C, 0x64, 0x68):
        struct.pack_into("<I", packet, off, pick(rng, fbits(rng.uniform(0, 640)), fbits(rng.uniform(-1e4, 1e4)),
                                                 fbits(0.0), r32(rng)))
    for off in (0x20, 0x21, 0x3C, 0x59):
        packet[off] = pick(rng, 0, 0xFF, 0xFE, 1, rng.getrandbits(8))
    st[0x6C3EC0 + n * 0xF0] = bytes(packet)
    w16(st, 0x6C7ED4 + n * 2, rng.randrange(-4, 64))
    w32(st, 0x6C1EB0, TEXBASE)
    st[TEXBASE] = bytes(rng.randbytes(0x100))
    w16(st, TEXBASE + 0x2E, rng.randrange(0, 64))
    w32(st, 0x6C1E14, TEX + 0x100)
    st[TEX] = bytes(rng.randbytes(0x2000))
    w32(st, 0x4ACC38, pick(rng, 640, 800, 1024, 3440, rng.randrange(0, 5000), r32(rng)))
    w32(st, 0x4ACFF0, pick(rng, 640, 640, 512, 1, -1, 0, rng.randrange(1, 5000), r32(rng)))
    frame = pick(rng, -1, 0, 1, 2, 3, rng.randrange(0, 16))
    a = pick(rng, 0, 1, 4, 5, 6, 9, rng.randrange(-3, 12), r32(rng))
    return [n, a, frame], st, {}


# name: (original address, case builder, returns a value)
TESTS = {
    "trail_point_add": (0x433D90, case_trail_point_add, False),
    "trails_draw": (0x433F00, case_trails_draw, False),
    "beams_draw": (0x4345C0, case_beams_draw, False),
    "window_proc": (0x435ED1, case_window_proc, True),
    "window_destroy_quit": (0x436034, case_window_destroy_quit, False),
    "window_command": (0x436542, case_window_command, False),
    "input_focus_poll": (0x4368F0, case_input_focus_poll, False),
    "game_time_get": (0x43692A, case_game_time_get, True),
    "loading_picture_draw": (0x436C2C, case_loading_picture_draw, False),
    "mode_dialog_languages_fill": (0x436ED6, case_mode_dialog_languages_fill, False),
    "mode_dialog_texts_set": (0x436F3B, case_mode_dialog_texts_set, False),
    "mode_dialog_proc": (0x437171, case_mode_dialog_proc, True),
    "error_dialog_proc": (0x4375C7, case_error_dialog_proc, True),
    "hud_update": (0x4386F0, case_hud_update, False),
    "hud_update_extra": (0x438760, case_hud_update_extra, False),
    "hud_sprite_set": (0x4387D0, case_hud_sprite_set, True),
}
REGS = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run_case(m, stubs, base, name, address, builder, returns, rng):
    args, st, script = builder(rng)
    cw = pick(rng, 0x27F, 0x37F, 0x07F)
    data = bytearray(base)
    scratch = bytearray(SCRATCH_SIZE)
    for a, v in st.items():
        if DATA <= a < DATA + DATA_SIZE:
            data[a - DATA:a - DATA + len(v)] = v
        else:
            scratch[a - SCRATCH:a - SCRATCH + len(v)] = v
    results = []
    for target in (address, m.symbols[name]):
        m.write(DATA, bytes(data))
        m.write(SCRATCH, bytes(scratch))
        m.write(E.STACK, STACK_FILL)
        for i, reg in enumerate(REGS):
            m.uc.reg_write(reg, 0x11111111 * (i + 1))
        m.uc.reg_write(UC_X86_REG_FPCW, cw)
        m.uc.reg_write(UC_X86_REG_FPSW, 0)
        m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        stubs.reset(script)
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        regs = tuple(m.uc.reg_read(reg) for reg in REGS) + (m.uc.reg_read(UC_X86_REG_ESP),
                                                              (m.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7,
                                                              m.uc.reg_read(UC_X86_REG_FPTAG))
        if r[0] != "ok":
            regs = ()  # faulted: the registers at the fault differ
        mem = bytearray(m.read(DATA, DATA_SIZE) + m.read(SCRATCH, SCRATCH_SIZE))
        mem[0x6CCC6A - DATA:0x6CCC6C - DATA] = b"\0\0"  # V0 z high half: an unwritten stack word in the original
        if on_stack(struct.unpack_from("<I", mem, 0x6CCD74 - DATA)[0]):
            mem[0x6CCD74 - DATA:0x6CCD78 - DATA] = b"STAK"  # the transform's record pointer: a stack address
        results.append((r, regs, stubs.log, bytes(mem)))
    a, b = results
    if a[:3] == b[:3] and a[3] == b[3]:
        return None, a[0]
    msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)} cw {cw:#x}: original {a[0]} ours {b[0]}"
    if a[1] != b[1]:
        msg += f"\n  registers / esp / x87 differ: {[hex(x) for x in a[1]]} vs {[hex(x) for x in b[1]]}"
    if a[3] != b[3]:
        x, y = a[3], b[3]
        diff = [i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]]
        where = [hex(DATA + i if i < DATA_SIZE else SCRATCH + i - DATA_SIZE) for i in diff[:8]]
        msg += f"\n  memory differs at {where} ({len(diff)} words)"
        if "-v" in sys.argv:
            msg += f"\n  orig {[x[i:i + 4].hex() for i in diff[:8]]}\n  ours {[y[i:i + 4].hex() for i in diff[:8]]}"
    if a[2] != b[2]:
        la, lb = a[2], b[2]
        k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
        msg += f"\n  calls from #{k}: orig {la[k:k + 3]}\n                ours {lb[k:k + 3]}"
    return msg, a[0]


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    base = m.read(DATA, DATA_SIZE)
    rng = random.Random(0x5F)
    failed = 0
    for name, (address, builder, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        for _ in range(iterations):
            msg, r = run_case(m, stubs, base, name, address, builder, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs faulted the same way in both)" if errors else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
