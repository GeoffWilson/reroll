#!/usr/bin/env python3
"""Differential test of st_sa.c (startup and shutdown, group SA: the WinMain
body 0x4351e0 and its tree) against the original, on random option, game
and global state.

Every callee outside the group is a logging stub in both runs: WinAPI calls
through the import address table (each IAT slot used gets a stub address of
its own; the stubs pop their stdcall arguments), window and DirectX setup,
the registry helpers, the sound engine, the text table reader (writes a
text made from its section and index), the C runtime string helpers (done in
Python), the time entry builder 0x405480, binding_get and so on. Each stub
logs its arguments (strings by content, stack buffers by kind) and the call
logs are compared, as well as the whole data section 0x4a7000-0x6d3000 and
the scratch memory (options, game state, buffers).

The main loop runs until a scripted GetMessageA returns 0 or a scripted
per-frame stub sets the quit flag; game_time advances by a random step so
the waits end.

    tests/emu_st_sa.py EXE build/emu_sa.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

DATA, DATA_SIZE = 0x4A7000, 0x6D3000 - 0x4A7000
SCRATCH = 0x01000000
CONFIG = SCRATCH  # 0x2000
STRINGS = SCRATCH + 0x2000  # 16 strings of 0x100
BINDINGS = SCRATCH + 0x3000  # 4 x 0x1b x 0x10
BUFFER = SCRATCH + 0x4000  # 0x1000: out buffers
TEXT = SCRATCH + 0x5000  # 0x1000: string file
ARCREC = SCRATCH + 0x6000  # 0x10: archive record
RACE = SCRATCH + 0x10000  # 0xb85b8 (cleared by config_defaults)
RACE_SIZE = 0xB85B8
IATSTUB = 0x02200000
STACK_FILL = b"\xcc" * E.STACK_SIZE

# IAT slot: (name, stdcall argument kinds, returns)
IAT = {
    0x4A3004: ("GetUserNameA", "od"),
    0x4A3054: ("GetStockObject", "v"),
    0x4A3118: ("GetUserDefaultLangID", ""),
    0x4A3248: ("DispatchMessageA", "p"),
    0x4A324C: ("TranslateMessage", "p"),
    0x4A3250: ("ShowCursor", "v"),
    0x4A3258: ("GetMessageA", "ovvv"),
    0x4A325C: ("PeekMessageA", "ovvvv"),
    0x4A3260: ("DestroyWindow", "v"),
    0x4A327C: ("SetFocus", "v"),
    0x4A3280: ("MessageBoxA", "vssv"),
    0x4A328C: ("DialogBoxParamA", "vvvvv"),
    0x4A329C: ("ShowWindow", "vv"),
    0x4A32A4: ("GetClientRect", "vo"),
    0x4A32A8: ("SetForegroundWindow", "v"),
    0x4A3300: ("CoUninitialize", ""),
    0x4A3308: ("CoInitialize", "v"),
}

# address: (name, cdecl argument kinds: v value, o output buffer, s string, p pointer)
STUBS = {
    0x43692A: ("game_time", ""),
    0x43699C: ("fatal_exit", ""),
    0x436A52: ("present_frame", ""),
    0x42E6D0: ("text_entry", "ovvv"),
    0x49827A: ("crt_free", "v"),
    0x405480: ("time_entry_fill", "ovvv"),
    0x405380: ("track_record_held_by", "vv"),
    0x449BF0: ("binding_get", "vv"),
    0x4291F0: ("nop", ""),
    0x496570: ("archive_get_record", "v"),
    0x496590: ("archive_load_entry", "vv"),
    0x49867B: ("toupper", "v"),
    0x498540: ("tolower", "v"),
    0x4A1D70: ("strupr", "o"),
    0x4980D0: ("strstr", "ss"),
    0x4962C0: ("chdir_4962c0", "s"),
    0x46B690: ("reg_read_dword", "sso"),
    0x474030: ("reg_load", "vvs"),
    0x4745D0: ("f_4745d0", ""),
    0x474500: ("f_474500", "vv"),
    0x474360: ("replay_delete", "s"),
    0x46B770: ("reg_delete", "ss"),
    0x46B900: ("event_open", "os"),
    0x46B940: ("event_set", "v"),
    0x46B890: ("handle_close", "v"),
    0x41C8C0: ("cpuid_ok", ""),
    0x401050: ("is_amd", ""),
    0x4010C0: ("has_3dnow", ""),
    0x4010F0: ("cpu_ext", ""),
    0x43C370: ("is_intel", ""),
    0x43C3E0: ("intel_no", ""),
    0x48A010: ("joystick_count", ""),
    0x489FA0: ("joystick_flag", "v"),
    0x48A040: ("joystick_buttons", "v"),
    0x46B9E0: ("env_exists", "s"),
    0x46B870: ("instance_mutex", "s"),
    0x474FA0: ("cwd_get", "v"),
    0x475030: ("cwd_set", "v"),
    0x498450: ("strcpy", "os"),
    0x498460: ("strcat", "os"),
    0x475070: ("file_exists", "s"),
    0x46B650: ("reg_read_string", "ssov"),
    0x474ED0: ("path_dir", "os"),
    0x496430: ("archive_open", "sv"),
    0x474840: ("cd_find_data_path", "v"),
    0x476A50: ("pool_init", ""),
    0x41C930: ("cpu_sysinfo", ""),
    0x426090: ("render_init", ""),
    0x477130: ("window_class", "vvv"),
    0x4771C0: ("window_create", "vvvvvv"),
    0x474470: ("replays_find_delete", ""),
    0x46F4F0: ("sound_init", ""),
    0x4877D0: ("ddraw_window", "vvv"),
    0x487E10: ("ddraw_init", "vvvvvvv"),
    0x487210: ("ddraw_release", ""),
    0x41CED0: ("d3d_init", "vv"),
    0x41D130: ("d3d_release", ""),
    0x41EEB0: ("d3d_clear", ""),
    0x41E040: ("d3d_viewport", "v"),
    0x4876F0: ("ddraw_flip", ""),
    0x428270: ("render_flag", ""),
    0x487D70: ("ddraw_modes", ""),
    0x474B90: ("uninstall", ""),
    0x477090: ("timer_kill", "v"),
    0x496700: ("crt_6700", ""),
    0x46FB00: ("sound_free", ""),
    0x46F610: ("sound_close", ""),
    0x48A8B0: ("net_close", ""),
    0x489560: ("input_close", ""),
    0x41E190: ("d3d_end", ""),
    0x476A90: ("pool_free", ""),
    0x496410: ("crt_exit", ""),
    0x474CF0: ("relaunch", ""),
    0x46B850: ("mutex_close", ""),
    0x436B9D: ("display_set", "v"),
    0x435B64: ("data_load", ""),
    0x41E170: ("d3d_begin", ""),
    0x43C420: ("input_frame", ""),
    0x43605A: ("f_43605a", "v"),
    0x4369B5: ("f_4369b5", ""),
    0x4302D0: ("frontend_frame", ""),
    0x4440B0: ("race_frame", ""),
    0x428390: ("clear_colour", "vvv"),
    0x428850: ("f_428850", "vvvv"),
    0x41EFE0: ("f_41efe0", "v"),
    0x435552: ("f_435552", ""),
    0x435607: ("display_restart", ""),
    0x4359F1: ("display_setup", ""),
}


def pick(rng, *choices):
    return rng.choice(choices)


def rint(rng):
    return pick(rng, rng.randrange(-4, 0x40), rng.getrandbits(32), rng.getrandbits(16), 0, 1, 0xFFFFFFFF)


def string_ptr(rng):
    return STRINGS + rng.randrange(0, 16) * 0x100


def out(rng):
    return BUFFER + rng.randrange(0, 8) * 0x200


def entry_ptr(rng):
    return BUFFER + 0xF00 + rng.randrange(0, 8) * 0x10


# name: (address, argument maker, returns)
TESTS = {
    "stristr": (0x436688, lambda r: [pick(r, string_ptr(r), string_ptr(r), 0), pick(r, string_ptr(r), string_ptr(r), 0)], True),
    "unquote_copy": (0x474E80, lambda r: [pick(r, out(r), out(r), 0), pick(r, string_ptr(r), string_ptr(r), 0)], False),
    "command_token_copy": (0x474DC0, lambda r: [pick(r, out(r), out(r), 0), pick(r, string_ptr(r), string_ptr(r), 0)], False),
    "records_defaults": (0x405320, lambda r: [], False),
    "records_mark_old": (0x405340, lambda r: [], False),
    "time_entry_lap": (0x405570, lambda r: [entry_ptr(r), r.randrange(-2, 8)], False),
    "time_entry_race": (0x4055B0, lambda r: [entry_ptr(r), r.randrange(-2, 8)], False),
    "record_beats": (0x405680, lambda r: [entry_ptr(r), entry_ptr(r)], True),
    "record_find_rank": (0x405620, lambda r: [entry_ptr(r), r.randrange(-2, 18)], True),
    "record_insert_at": (0x405720, lambda r: [entry_ptr(r), r.randrange(-2, 18), r.randrange(-2, 8)], False),
    "record_insert_slot": (0x4055D0, lambda r: [r.randrange(-2, 18), r.randrange(-2, 8)], False),
    "records_insert_players": (0x4057C0, lambda r: [r.randrange(-2, 18)], False),
    "strings_free": (0x42DFB0, lambda r: [], False),
    "strings_unload": (0x42DFA0, lambda r: [], False),
    "strings_load_entry": (0x42DFE0, lambda r: [r.randrange(0, 20)], False),
    "strings_load": (0x42DF90, lambda r: [r.randrange(0, 20)], True),
    "strings_load_once": (0x43756D, lambda r: [], False),
    "language_set": (0x42E3C0, lambda r: [rint(r)], False),
    "frontend_buffers_free": (0x42E350, lambda r: [], False),
    "frontend_textures_free": (0x430E00, lambda r: [], False),
    "menu_table_free": (0x444790, lambda r: [], False),
    "frontend_free_all": (0x430AC0, lambda r: [], False),
    "activity_index": (0x449C70, lambda r: [pick(r, r.randrange(-2, 0x20), rint(r))], True),
    "binding_saved_get": (0x449C40, lambda r: [r.randrange(-2, 6), pick(r, r.randrange(-2, 0x22), rint(r))], True),
    "joystick_usable": (0x48A5C0, lambda r: [pick(r, r.randrange(-2, 10), 0xFF, rint(r))], True),
    "binding_from_table": (0x449AB0, lambda r: [pick(r, r.randrange(0, 4), r.randrange(0, 4), r.randrange(-2, 6), rint(r)),
                                                r.randrange(-1, 0x1c), pick(r, 0, 1, 0xFF, 0xFF, 2, -1, rint(r))], True),
    "binding_default": (0x449960, lambda r: [r.randrange(-1, 5), r.randrange(-1, 0x1c), pick(r, 0, 1, 1, 2)], False),
    "bindings_reset_player": (0x449910, lambda r: [r.randrange(-1, 5)], False),
    "bindings_reset_all": (0x4498F0, lambda r: [], False),
    "language_from_registry": (0x474770, lambda r: [], True),
    "config_default_texts": (0x473C50, lambda r: [pick(r, 0, 1, 1, 2)], False),
    "config_defaults": (0x473D50, lambda r: [], False),
    "config_display_apply": (0x4745F0, lambda r: [], False),
    "config_load": (0x4740C0, lambda r: [], True),
    "settings_load_65": (0x4745E0, lambda r: [], False),
    "replays_delete": (0x4743E0, lambda r: [pick(r, 0, 1, rint(r))], False),
    "chain_event_signal": (0x474CB0, lambda r: [], False),
    "network_average": (0x45E890, lambda r: [out(r)], False),
    "cpu_detect_amd": (0x401000, lambda r: [], True),
    "cpu_detect_intel": (0x43C320, lambda r: [], True),
    "config_series_set": (0x4498D0, lambda r: [r.randrange(-2, 5)], False),
    "config_flag_9d_set": (0x445160, lambda r: [], False),
    "result_menu_return": (0x4480B0, lambda r: [], False),
    "race_rankings": (0x445170, lambda r: [pick(r, 0, 1, 1), pick(r, r.randrange(-1, 7), rint(r))], False),
    "league_won_texts": (0x444DD0, lambda r: [], False),
    "race_result_flow": (0x444800, lambda r: [], False),
    "command_line_parse": (0x43677B, lambda r: [BUFFER + 0xC00], True),
    "startup_init": (0x4356AB, lambda r: [BUFFER + 0xC00], True),
    "startup_window": (0x4358B0, lambda r: [pick(r, 0, 0, 1)], True),
    "startup_options": (0x435932, lambda r: [], True),
    "startup_sound": (0x4359D3, lambda r: [], True),
    "startup_direct3d": (0x435AAE, lambda r: [], True),
    "startup_mode_dialog": (0x4370B2, lambda r: [], True),
    "error_dialog": (0x43758F, lambda r: [], False),
    "timer_stop": (0x4365A4, lambda r: [], True),
    "shutdown_all": (0x435E2C, lambda r: [pick(r, 0, 1)], False),
    "shutdown_restart": (0x435E99, lambda r: [], False),
    "shutdown_final": (0x435515, lambda r: [], False),
    "game_main": (0x4351E0, lambda r: [0x400000, pick(r, 0, 1), BUFFER + 0xC00, 1], True),
}

SWITCHES = [b"/PLAYBACK", b"/playback", b"/MONO=", b"/mono=\"c:\\x y\"", b"/MONO-", b"/Mono+", b"/FRONTEND",
            b"/frontend", b"NOCD", b"nocd", b"/W", b"/w", b"/E", b"/e", b"=", b"\"", b" ", b"abc", b"/MON"]
TEXT_ALPHABET = b"ab Cd_EF 01~\"=,/\t\x05"
FLAG_ADDRS = [0x4BE460, 0x4BE450, 0x4BE484, 0x4BE488, 0x4BE48C, 0x4AC984, 0x4BE464, 0x4AF444]


def make_text(rng, alphabet=TEXT_ALPHABET):
    n = pick(rng, 0, rng.randrange(1, 8), rng.randrange(1, 40), rng.randrange(1, 0xF0))
    return bytes(rng.choice(alphabet) for _ in range(n))


def bcd(rng):
    return pick(rng, bytes([rng.randrange(0, 6) << 4 | rng.randrange(10), rng.randrange(0, 6) << 4 | rng.randrange(10),
                            rng.randrange(10) << 4 | rng.randrange(10)]), b"\0\0\0", b"\x59\x59\x99",
                bytes(rng.getrandbits(8) for _ in range(3)))


def make_state(rng):
    config = bytearray(rng.getrandbits(8) for _ in range(0x2000))
    for t in range(16):  # record tables
        for j in range(6):
            off = (t + 0xD) * 0x54 + j * 0xE
            config[off:off + 3] = bcd(rng)
    for o, choices in ((0x17, (0, 1, 2)), (0x14, (0, 1, 1)), (0x43, (0, 1)), (0x1C, (8, 9, 7, 0)), (0x1D, (0, 1, 2, 3)),
                       (0x1E, (0, 1, 2, 3, 4)), (0x74, (0, 1, 2, 4, 6)), (0x84, (0, 1, 5, 7)), (0xA, (0, 1)),
                       (0x36, (0, 1, 4))):
        config[o] = pick(rng, *choices, rng.getrandbits(8))
    struct.pack_into("<i", config, 0x48, pick(rng, -1, -1, -1, 0, 0xB))
    for p in range(4):
        config[p * 0x90 + 0x1A1] = pick(rng, 0xFF, 0, 1, 2, 9, rng.getrandbits(8))
        for a in range(0x20):
            config[0x1B0 + p * 0x90 + a * 4] = pick(rng, 0, 1, 2, rng.getrandbits(8))
    for o in (0x120, 0x12B):
        if rng.random() < 0.5:
            name = make_text(rng, b"abcXYZ")[:11]
            config[o:o + len(name) + 1] = name + b"\0"
        if rng.random() < 0.3:
            config[o] = 0
    for o in (0x42C, 0x42E, 0x430):
        struct.pack_into("<H", config, o, pick(rng, 0, 640, rng.getrandbits(16)))
    race = bytearray(RACE_SIZE)
    race[0:0x1000] = bytes(rng.getrandbits(8) for _ in range(0x1000))
    for i in range(6):
        race[i] = pick(rng, 8, 0, rng.getrandbits(8))
        race[8 + i] = pick(rng, 0, rng.randrange(0, 40), 0x1E, 0x28, rng.getrandbits(8))
    race[8] = pick(rng, 0x1E, 0x28, race[8])
    race[0x54D] = pick(rng, 0, 1, 3, 6, 7, rng.getrandbits(8))
    race[0x551] = pick(rng, *range(10), rng.getrandbits(8))
    race[0x579] = pick(rng, rng.randrange(0, 16), rng.randrange(0, 20))
    race[0x585] = pick(rng, 0, 1, 2, 3)
    struct.pack_into("<i", race, 0x57C, pick(rng, 7, 8, 9, 0, rng.randrange(0, 12)))
    strings = bytearray()
    for _ in range(16):
        s = pick(rng, make_text(rng), b"".join(rng.choice(SWITCHES) for _ in range(rng.randrange(0, 6))))
        strings += (s[:0xFF] + b"\0").ljust(0x100, b"\0")
    bindings = bytes(rng.getrandbits(8) for _ in range(4 * 0x1B * 0x10))
    buffer = bytearray(rng.getrandbits(8) for _ in range(0x1000))
    line = b"".join(rng.choice(SWITCHES) for _ in range(rng.randrange(0, 6)))
    buffer[0xC00:0xC00 + len(line) + 1] = line + b"\0"
    for i in range(8):
        buffer[0xF00 + i * 0x10:0xF00 + i * 0x10 + 3] = bcd(rng)
        if i and rng.random() < 0.4:  # same minutes (and seconds): compare the later bytes
            j = 0xF00 + rng.randrange(0, i) * 0x10
            buffer[0xF00 + i * 0x10:0xF00 + i * 0x10 + rng.choice((1, 2))] = buffer[j:j + 2]
    text = bytes(rng.choice(b"ab~\x01\x09\x1f \n") for _ in range(0x1000))
    arcrec = struct.pack("<4I", 0, 0, pick(rng, 0, 1, 2, rng.randrange(0, 0xFFF)), 0)

    g = {}  # data section writes: address -> bytes

    def put(a, fmt, v):
        g[a] = struct.pack(fmt, v & 0xFFFFFFFF if fmt == "<I" else s32(v) if fmt == "<i" else v & 0xFF)

    for a in (0x4D4650, 0x4ACC5C, 0x4BE440, 0x4D4658, 0x4BE4A0, 0x4BE318, 0x4BE31C, 0x4CB554, 0x4CB558, 0x4CB570,
              0x4D4660, 0x4ACC34, 0x4BE4BC, 0x4BE468, 0x4BE4A8, 0x4D4664, 0x4D4668, 0x4BE45C, 0x6CCE7C, 0x4BE3F0):
        put(a, "<I", pick(rng, 0, 0, 1, rng.getrandbits(32)))
    for a in FLAG_ADDRS:
        put(a, "<I", pick(rng, 0, 0, 0, 1))
    for a in (0x4BE460, 0x4BE450, 0x4BE318):
        put(a, "<I", pick(rng, 0, 0, 0, 0, 0, 1))
    for a in (0x4BE2EC, 0x4BE2F0, 0x4BE304, 0x4BE308, 0x4BE30C, 0x4BE310, 0x4BE344, 0x4BE348, 0x4CB4B8):
        put(a, "<I", pick(rng, 0, rng.getrandbits(32)))
    put(0x4AF878, "<i", pick(rng, 0x1B, 0x1B, rng.randrange(0, 0x1C), 0, -1))
    for i in range(0x1B):  # the activity table: 0x1b entries, then its count
        put(0x4AF7A0 + i * 8, "<i", pick(rng, i, i, i, rng.randrange(-1, 0x1C)))
        put(0x4AF7A4 + i * 8, "<I", pick(rng, 0xFFFFFFFF, 1 << rng.randrange(32), 1 << rng.randrange(8),
                                         rng.getrandbits(8), 0))
    for a in (0x4AC6E8, 0x4AC6EC, 0x4AC6F0, 0x4AC6F4, 0x4AC6F8):
        put(a, "<i", pick(rng, rng.randrange(1, 8), rng.randrange(1, 8), 0, -1))
    for t in (0x4AC398, 0x4AC498, 0x4AC508, 0x4AC578, 0x4AC5E8):
        for i in range(7):
            put(t + i * 16, "<I", pick(rng, rng.getrandbits(32), rng.getrandbits(8), 0xFFFFFFFF, 0xFFFFFFFF))
    put(0x4AF650, "<i", rint(rng))
    for i in range(6):
        put(0x4AF668 + i, "<B", rng.getrandbits(8))
    put(0x4C24C8, "<B", pick(rng, 0, rng.getrandbits(8)))
    put(0x5D597C, "<i", pick(rng, 0, rng.randrange(-5, 50), -1))
    put(0x5D5978, "<i", pick(rng, rng.randrange(-1000, 1000), -0x80000000, rint(rng)))
    script = {
        "game_time_step": [rng.randrange(1, 4000)],
        "time_entry": {(s, k): bcd(rng) + bytes(rng.getrandbits(8) for _ in range(11))
                       for s in range(-2, 8) for k in (0, 1)},
        "binding_null": [pick(rng, 0, 0, 0, 1, 2)],
        "text_empty": [pick(rng, 0, 0, 1) for _ in range(64)],
    }
    for name in ("track_record_held_by", "cpuid_ok", "is_amd", "has_3dnow", "cpu_ext", "is_intel", "intel_no",
                 "joystick_count", "joystick_flag", "joystick_buttons", "reg_load", "env_exists", "instance_mutex",
                 "file_exists", "reg_read_string", "cd_find_data_path", "window_class", "window_create",
                 "ddraw_init", "d3d_init", "data_load", "d3d_begin", "frontend_frame", "race_frame",
                 "present_frame", "f_41efe0", "display_restart", "display_setup", "reg_read_dword", "event_open",
                 "GetUserDefaultLangID", "CoInitialize", "DialogBoxParamA", "PeekMessageA", "GetMessageA",
                 "GetStockObject"):
        script[name] = [pick(rng, 0, 1, 1, 1, 1, 1, 1, -1, 2, rng.getrandbits(32)) for _ in range(12)]
    script["CoInitialize"] = [pick(rng, 0, 0, 0, 1)]
    script["instance_mutex"] = [pick(rng, 0, 0, 0, 1)]
    script["GetUserDefaultLangID"] = [pick(rng, 0x409, 0x409, 0x409, 0x411, 0x10411, 0x809)]
    script["cd_find_data_path"] = [pick(rng, 0, 1, 1, 1, 2)]
    script["reg_read_dword_value"] = [pick(rng, 0, 1, 4, 5, rng.getrandbits(32))]
    script["DialogBoxParamA"] = [pick(rng, -1, -2, 0, rng.randrange(0, 0x40) << 16 | rng.getrandbits(16))]
    script["track_record_held_by"] = [pick(rng, 0, 0, 1) for _ in range(8)]
    script["f_41efe0"] = [pick(rng, 0, 1) for _ in range(40)]
    script["display_restart"] = [pick(rng, 0, 0, 1)] + [0]
    script["joystick_count"] = [pick(rng, 0, 1, 2, 8)] * 40
    script["joystick_flag"] = [pick(rng, 0, 0, 1)] * 40
    script["joystick_buttons"] = [pick(rng, 12, 10, 9, 0)] * 40
    script["frontend_frame"] = [pick(rng, 1, 1, 1, 0) for _ in range(40)]
    script["race_frame"] = [pick(rng, 1, 1, 1, 0) for _ in range(40)]
    script["present_frame"] = [pick(rng, 1, 1, 1, 0) for _ in range(80)]
    script["PeekMessageA"] = [pick(rng, 0, 1) for _ in range(rng.randrange(0, 20))]
    script["GetMessageA"] = [pick(rng, 1, 1, 1, 0) for _ in range(rng.randrange(0, 20))]
    # per-frame side effects of the input stub: flag writes
    script["fx_input"] = [{a: pick(rng, 0, 1) for a in rng.sample(FLAG_ADDRS, rng.randrange(0, 3))}
                          if rng.random() < 0.3 else {} for _ in range(30)]
    script["fx_modes"] = [{0x4BE440: pick(rng, 0, 1)}]
    script["msg"] = bytes(rng.getrandbits(8) for _ in range(28))
    script["rect"] = struct.pack("<4i", 0, 0, rng.randrange(0, 2000), rng.randrange(0, 2000))
    return dict(config=config, race=race, strings=bytes(strings), bindings=bindings, buffer=buffer, text=text,
                arcrec=arcrec, globals=g, script=script)


def c_string(uc, address, limit=0x400):
    data = b""
    while len(data) < limit:
        chunk = bytes(uc.mem_read(address + len(data), 0x10 - (address + len(data)) % 0x10))
        if b"\0" in chunk:
            return data + chunk.split(b"\0", 1)[0]
        data += chunk
    return data[:limit]


def s32(v):
    return struct.unpack("<i", struct.pack("<I", v & 0xFFFFFFFF))[0]


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
        self.script = {k: (list(v) if isinstance(v, list) else v) for k, v in script.items()}
        self.time = 1000

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
        words = struct.unpack("<12I", bytes(uc.mem_read(esp, 48)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            on_stack = E.STACK <= a < E.STACK + E.STACK_SIZE
            if k == "o":
                logged.append("stack" if on_stack else a)
            elif k == "s":
                logged.append(c_string(uc, a) if a else 0)
            elif k == "p":
                logged.append(("stack", bytes(uc.mem_read(a, 28))) if on_stack else a)
            elif k == "d":
                logged.append(("stack", bytes(uc.mem_read(a, 4))) if on_stack else a)
            else:
                logged.append(a)
        eax = 0
        if name == "game_time":
            eax = self.time
            self.time += self.script["game_time_step"][0]
        elif name == "text_entry":
            dst, size, sec, index = args[0], s32(args[1]), args[2], s32(args[3])
            text = b"" if self._next("text_empty", 0) else b"T%x.%d" % (sec, index)
            if dst and size > 0:
                uc.mem_write(dst, text[:size - 1] + b"\0")
            eax = len(text)
        elif name == "time_entry_fill":
            key = (max(-2, min(7, s32(args[1]))), args[3] & 1)
            if args[0]:
                uc.mem_write(args[0], self.script["time_entry"][key])
        elif name == "binding_get":
            s, n = s32(args[0]), s32(args[1])
            if self.script["binding_null"][0] == 2:  # any set: one of the four
                eax = BINDINGS + ((s & 3) * 0x1B + n % 0x1B) * 0x10
            elif 0 <= s < 4 and 0 <= n < 0x1B and not self.script["binding_null"][0]:
                eax = BINDINGS + (s * 0x1B + n) * 0x10
        elif name == "archive_get_record":
            eax = ARCREC
        elif name == "archive_load_entry":
            eax = TEXT
        elif name in ("toupper", "tolower"):
            c = args[0] & 0xFF
            eax = bytes([c]).upper()[0] if name == "toupper" else bytes([c]).lower()[0]
        elif name == "strupr":
            s = c_string(uc, args[0])
            uc.mem_write(args[0], s.upper())
            eax = args[0]
        elif name == "strstr":
            hay, needle = c_string(uc, args[0]), c_string(uc, args[1])
            i = hay.find(needle)
            eax = 0 if i < 0 else args[0] + i
        elif name == "strcpy":
            uc.mem_write(args[0], c_string(uc, args[1]) + b"\0")
            eax = args[0]
        elif name == "strcat":
            d = c_string(uc, args[0])
            uc.mem_write(args[0] + len(d), c_string(uc, args[1]) + b"\0")
            eax = args[0]
        elif name == "path_dir":
            s = c_string(uc, args[1])
            uc.mem_write(args[0], s[:s.rfind(b"\\") + 1] + b"\0")
            eax = args[0]
        elif name == "reg_read_string":
            eax = self._next(name, 0)
            if s32(eax) != -1:
                uc.mem_write(args[2], b"D:\\SRC\\x\0")
        elif name == "reg_read_dword":
            eax = self._next(name, 0)
            uc.mem_write(args[2], struct.pack("<I", self.script["reg_read_dword_value"][0] & 0xFFFFFFFF))
        elif name == "event_open":
            uc.mem_write(args[0], struct.pack("<I", 0x1234))
            eax = self._next(name, 0)
        elif name == "input_frame":
            for a, v in self._next("fx_input", {}).items():
                uc.mem_write(a, struct.pack("<I", v))
        elif name == "ddraw_modes":
            for a, v in self._next("fx_modes", {}).items():
                uc.mem_write(a, struct.pack("<I", v))
        elif name == "GetUserNameA":
            uc.mem_write(args[0], b"user\0")
            uc.mem_write(args[1], struct.pack("<I", 5))
            eax = 1
        elif name in ("PeekMessageA", "GetMessageA"):
            uc.mem_write(args[0], self.script["msg"])
            eax = self._next(name, 1 if name == "PeekMessageA" else 0)
        elif name == "GetClientRect":
            uc.mem_write(args[1], self.script["rect"])
            eax = 1
        else:
            eax = self._next(name, 0)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * len(kinds) if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


def run_case(m, stubs, base, name, address, maker, returns, rng):
    st = make_state(rng)
    args = maker(rng)
    if name in ("shutdown_final", "game_main") and st["globals"][0x5D597C] == struct.pack("<i", -1):
        st["globals"][0x5D597C] = struct.pack("<i", 1)  # 0x45e890's INT_MIN / -1: ours drops the unused division
    results = []
    for target in (address, m.symbols[name]):
        m.write(DATA, base)
        for a, v in st["globals"].items():
            m.write(a, v)
        m.write(0x4B2198, struct.pack("<I", CONFIG))
        m.write(0x4B1960, struct.pack("<I", RACE))
        m.write(SCRATCH, bytes(0x10000))
        m.write(CONFIG, bytes(st["config"]))
        m.write(STRINGS, st["strings"])
        m.write(BINDINGS, st["bindings"])
        m.write(BUFFER, bytes(st["buffer"]))
        m.write(TEXT, st["text"])
        m.write(ARCREC, st["arcrec"])
        m.write(RACE, bytes(st["race"]))
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"])
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        if name == "game_main" and r[0] == "ok" and not any(c[0] == "PeekMessageA" for c in stubs.log):
            r = ("ok", "msg unset")  # the original returns an uninitialised MSG's wParam
        mem = m.read(DATA, DATA_SIZE) + m.read(SCRATCH, 0x10000) + m.read(RACE, RACE_SIZE)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None) if a != b else None
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            where = []
            for i in diff[:8]:
                if i < DATA_SIZE:
                    addr = DATA + i
                elif i < DATA_SIZE + 0x10000:
                    addr = SCRATCH + i - DATA_SIZE
                else:
                    addr = RACE + i - DATA_SIZE - 0x10000
                where.append((hex(addr), hex(struct.unpack_from("<I", a, i)[0]), hex(struct.unpack_from("<I", b, i)[0])))
            print("  args", [hex(x & 0xFFFFFFFF) for x in args], "differ:", where)
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else " (memory differs)") + logs, results[0][0]
    return None, results[0][0]


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, 0x10000 + ((RACE_SIZE + 0xFFFF) & ~0xFFFF))
    stubs = Stubs(m)
    base = m.read(DATA, DATA_SIZE)
    rng = random.Random(0x5A)
    failed = 0
    only = [x for x in sys.argv[4:] if x != "-v"]
    for name, (address, maker, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        for _ in range(iterations):
            msg, r = run_case(m, stubs, base, name, address, maker, returns, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs failed the same way in both)" if errors else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
