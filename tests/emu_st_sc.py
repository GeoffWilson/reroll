#!/usr/bin/env python3
"""Differential test of st_sc.c (group SC: the load step 0x435b64 and the
front-end load under it, the per-frame step 0x435552, controls gathering
0x4369b5, the keyboard state 0x43c420 and key tables, the binding readers)
against the original, in Unicorn.

Every callee outside the function under test is a logging stub in both runs
(network, sound, timers, DirectInput, archive loading, allocation, texture
upload, sprite building, other functions of this group): each records the
arguments it reads and returns a scripted value; game_time returns a rising
clock so the busy waits end. The whole data section 0x4a7000-0x6d3000 and a
scratch heap are compared, plus the return value and the call log.

    tests/emu_st_sc.py EXE build/emu_sc.elf [iterations] [names...] [-v]
"""
import hashlib
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

DATA = (0x4A7000, 0x6D3000 - 0x4A7000)
SCRATCH, SCRATCH_SIZE = 0x01000000, 0x40000
CONFIG, RACE, KEYBOARD = SCRATCH + 0x20000, SCRATCH + 0x21000, SCRATCH + 0x22000
BINDS = SCRATCH + 0x23000  # 4 players x 0x400
CARS = SCRATCH + 0x24000  # 6 x 0x100
MODELS, PALETTE = SCRATCH + 0x10000, SCRATCH + 0x18000
HEAP = SCRATCH + 0x28000  # what the allocator stubs return
HOOK_FN = SCRATCH + 0x3F000  # the tick hook (0x4be114) target

# address: (name, stack args, result: None / "i" int / "q" 64-bit / "p" pointer, stdcall)
STUBS = {
    0x43692A: ("game_time", 0, "t"),
    0x46A060: ("race_setup", 0, "i"),
    0x45EF80: ("net_is_game", 0, "i"),
    0x489890: ("fe_joy_axis", 2, "i"),
    0x428E10: ("controls_packet", 1, "q"),
    0x4365ED: ("tick_callback", 5, None, "std"),
    0x43C6A0: ("key_tables_init", 0, None),
    0x43C3F0: ("keyboard_init", 0, None),
    0x43D270: ("key_layout_set", 1, None),
    0x43775A: ("game_speed_set", 0, None),
    0x435DF6: ("net_session_start", 0, "i"),
    0x436565: ("tick_timer_start", 0, "i"),
    0x4365A4: ("tick_timer_stop", 0, None),
    0x436595: ("tick_count_reset", 0, None),
    0x43699C: ("quit_request", 0, None),
    0x4301F0: ("frontend_load", 0, "i"),
    0x430C10: ("frontend_data_load", 2, None),
    0x430D80: ("frontend_backdrop_load", 2, "i"),
    0x430E40: ("frontend_palette_set", 0, "i"),
    0x430190: ("fade_step", 0, None),
    0x429210: ("tick_hook_call", 0, None),
    0x463C70: ("frame_counter_step", 0, None),
    0x489770: ("joy_button_down", 2, "i"),
    0x4897C0: ("joy_pov_dir", 2, "i"),
    0x4898E0: ("joy_axis_x", 1, "i"),
    0x4898F0: ("joy_axis_y", 1, "i"),
    0x489900: ("joy_axis_z", 1, "i"),
    0x427D00: ("controls_mask_menu_keys", 2, "i"),
    0x453730: ("model_top_point", 1, "i"),
    0x48A0E0: ("pad_config_reset", 1, None),
    0x452D20: ("menu_dirty_set", 0, None),
    0x46A030: ("game_heap_setup", 0, None),
    0x4446F0: ("menu_system_init", 0, "i"),
    0x433CD0: ("race_option_clamp", 0, None),
    0x462A20: ("particles_init", 0, None),
    0x453780: ("frontend_models_setup", 0, None),
    0x4461D0: ("menu_sprites_setup", 0, None),
    0x48A140: ("pad_configs_reset", 0, None),
    # outside the group
    0x48A600: ("input_open", 0, "i"),
    0x45C340: ("net_45c340", 0, None),
    0x488D90: ("input_init", 0, None),
    0x489BC0: ("input_489bc0", 2, None),
    0x428B90: ("view_distance", 1, None),
    0x46FB80: ("sound_46fb80", 2, None),
    0x472F10: ("sound_472f10", 0, None),
    0x472F90: ("sound_472f90", 0, None),
    0x444080: ("viewports_setup", 0, None),
    0x43693F: ("fn_43693f", 0, None),
    0x45E4A0: ("net_45e4a0", 0, "i"),
    0x45EF90: ("net_45ef90", 0, "i"),
    0x45F040: ("net_45f040", 0, "i"),
    0x48D350: ("net_48d350", 0, None),
    0x476F90: ("timer_start", 3, "i"),
    0x4968B0: ("crt_4968b0", 0, None),
    0x46F650: ("sound_stop", 1, None),
    0x48D4A0: ("net_48d4a0", 0, None),
    0x48C630: ("net_48c630", 0, None),
    0x48C460: ("net_48c460", 0, None),
    0x45EBC0: ("net_45ebc0", 0, None),
    0x48C830: ("net_48c830", 0, "i"),
    0x48C900: ("net_48c900", 0, None),
    0x45DEB0: ("net_send_controls", 4, None),
    0x45EA60: ("net_local_controls", 2, None),
    0x45EF40: ("net_45ef40", 0, None),
    0x45E340: ("net_45e340", 0, None),
    0x45C7D0: ("net_45c7d0", 0, None),
    0x496590: ("archive_load_entry", 2, "load"),
    0x4445E0: ("heap_alloc", 1, "alloc"),
    0x49827A: ("crt_free", 1, None),
    0x4982C2: ("crt_malloc", 1, "alloc"),
    0x4987B5: ("crt_rand", 0, "i"),
    0x428C90: ("texture_upload", 6, "i"),
    0x428C80: ("texture_slot", 0, "i"),
    0x429340: ("high_colour", 0, "i"),
    0x428130: ("palette_set", 7, "i"),
    0x423F00: ("sprites_423f00", 1, None),
    0x42E2A0: ("text_42e2a0", 0, None),
    0x42E0C0: ("text_42e0c0", 1, None),
    0x453020: ("sprites_453020", 0, None),
    0x453130: ("sprites_453130", 0, None),
    0x453570: ("sprites_453570", 0, None),
    0x453710: ("sprites_453710", 0, None),
    0x4116F0: ("textures_4116f0", 0, None),
    0x457EA0: ("textures_457ea0", 0, None),
    0x48D4F0: ("net_48d4f0", 0, None),
    0x430E00: ("frontend_free", 0, None),
    0x460AA0: ("fn_460aa0", 1, None),
    0x47C2C0: ("fn_47c2c0", 0, None),
    0x4205E0: ("texture_end", 0, None),
    0x446250: ("sprites_446250", 0, None),
    0x445480: ("menu_445480", 0, None),
    0x4447D0: ("menu_4447d0", 0, None),
    0x4316D0: ("sprites_4316d0", 1, None),
    0x40F1E0: ("model_40f1e0", 1, None),
    0x460BA0: ("mesh_create", 4, "alloc"),
    0x4264F0: ("angles_4264f0", 1, None),
    0x48A020: ("pad_index", 1, "i"),
    0x48A010: ("pad_count", 0, "i"),
    0x42E6D0: ("text_entry", 4, "text"),
    0x444500: ("heap_create", 0, None),
    0x423880: ("fn_423880", 0, None),
    HOOK_FN: ("tick_hook", 0, None),
}


def pick(rng, *choices):
    return rng.choice(choices)


def r32(rng):
    return rng.getrandbits(32)


class Stubs:
    def __init__(self, m):
        self.m = m
        self.target = None
        self.unstub = set()
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)

    def reset(self, script, target, unstub):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}
        self.clock = script["clock"][0]
        self.steps = list(script["clock"][1:])
        self.heap = HEAP
        self.target = target
        self.unstub = unstub

    def _next(self, name, default):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        if address == self.target or address in self.unstub:
            return
        entry = STUBS[address]
        name, nargs, kind = entry[:3]
        std = len(entry) > 3
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<%dI" % (nargs + 1), bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        args = list(words[1:])
        for i, a in enumerate(args):  # stack buffers: compare contents, not where
            if E.STACK <= a < E.STACK + E.STACK_SIZE:
                args[i] = "stack"
        self.log.append((name, tuple(args)))
        eax, edx = 0, 0
        if kind == "t":
            self.clock = (self.clock + (self.steps.pop(0) if self.steps else 40)) & 0xFFFFFFFF
            eax = self.clock
        elif kind == "i":
            eax = self._next(name, 0)
        elif kind == "q":
            v = self._next(name, 0)
            eax, edx = v & 0xFFFFFFFF, v >> 32
        elif kind == "load":
            eax = self._next(name, 0)
        elif kind == "alloc":
            eax = self._next(name, None)
            if eax is None:
                eax = self.heap
                self.heap += 0x100
        elif kind == "text":
            text = self._next(name, b"")
            uc.mem_write(words[1], text + b"\0")
            eax = len(text)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * nargs if std else 0))
        uc.reg_write(UC_X86_REG_EIP, words[0])


def w32(st, address, value):
    st[address] = struct.pack("<I", value & 0xFFFFFFFF)


def w16(st, address, value):
    st[address] = struct.pack("<H", value & 0xFFFF)


def w8(st, address, value):
    st[address] = bytes([value & 0xFF])


def flag(rng):
    return pick(rng, 0, 1, 1, 0, 2, r32(rng))


def base_state(rng):
    """Pointers and blocks most tests share: config, race, keyboard."""
    st = {}
    config = bytearray(rng.getrandbits(8) for _ in range(0x1000))
    config[0x36] = pick(rng, 0, 2, 1, rng.getrandbits(8))
    config[0x97] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    config[0x410] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    config[0x9D0] = pick(rng, 8, 8, 0, rng.getrandbits(8))
    config[5] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    fv = pick(rng, 0.0, -0.0, 512.0, 1023.9, 1024.0, 4096.0, 65536.0, 65537.0, 1e9, -5.0, float("inf"),
              float("-inf"), float("nan"), rng.uniform(-1e5, 1e5))
    struct.pack_into("<f", config, 0x64, fv)
    if rng.randrange(8) == 0:
        struct.pack_into("<I", config, 0x64, r32(rng))
    st[CONFIG] = bytes(config)
    w32(st, 0x4B2198, CONFIG)
    race = bytearray(rng.getrandbits(8) for _ in range(0x800))
    race[0x585] = pick(rng, 0, 1, 2, 3, rng.getrandbits(8))
    st[RACE] = bytes(race)
    w32(st, 0x4B1960, RACE)
    keys = bytearray(pick(rng, 0, 0, 0, 0x80, rng.getrandbits(8)) if rng.randrange(3) else 0 for _ in range(256))
    st[KEYBOARD] = bytes(keys)
    w32(st, 0x4B2CA4, KEYBOARD)
    return st


def bindings_state(rng, st):
    for p in range(4):
        count = pick(rng, 1, rng.randrange(0, 40), rng.randrange(0, 40), rng.randrange(-3, 3))
        w32(st, BINDS + p * 0x400, count)
        w32(st, 0x4AC70C + p * 4, BINDS + p * 0x400)
        w32(st, 0x4AC6FC + p * 4, BINDS + p * 0x400 + 0x10)
        recs = bytearray()
        for _ in range(max(count, 0)):
            kind = pick(rng, 0, 1, 1, 1, 2, 3, rng.randrange(0, 6), r32(rng))
            code = pick(rng, rng.randrange(0, 256), rng.randrange(0, 0x80), 0xC8, 0xD0, 0xCB, 0xCD, 0x1C, 0x39, 1)
            if kind == 1:
                code = pick(rng, rng.randrange(0, 0x40), rng.randrange(0, 0x100))
            recs += struct.pack("<IIiI", pick(rng, 1 << rng.randrange(32), r32(rng), 0x40, 0x800), kind,
                                rng.randrange(-1, 4), code)
        st[BINDS + p * 0x400 + 0x10] = bytes(recs)


def pads_state(rng, st):
    w32(st, 0x4D9554, pick(rng, 0, 1, 2, 4, rng.randrange(-1, 5)))
    for p in range(4):
        pad = bytearray(rng.getrandbits(8) for _ in range(0x43C))
        struct.pack_into("<i", pad, 0, pick(rng, 0, 4, 8, 32, rng.randrange(-2, 40)))
        struct.pack_into("<i", pad, 0xC, pick(rng, 0, 1, 4, rng.randrange(-2, 8)))
        for h in range(16):
            struct.pack_into("<i", pad, 0xD8 + h * 4, pick(rng, -1, 0xFFFF, rng.randrange(0, 36000), 0x1194,
                                                            0x34BC, 0x57E4, 0x7B0C, r32(rng) - 0x80000000))
        for b in range(32):
            pad[0xE8 + b] = pick(rng, 0, 0x80, rng.getrandbits(8))
        st[0x4D75E8 + p * 0x43C] = bytes(pad)


def axis_values(rng, n=12):
    return [pick(rng, 0, -0x41, 0x41, -0x40, 0x40, rng.randrange(-0x80, 0x80), 0x1F, -0x20, -0x1F, 0x20,
                 r32(rng) - 0x80000000) for _ in range(n)]


def common_script(rng):
    return {
        "clock": [r32(rng)] + ([rng.randrange(1, 4) for _ in range(300)] if rng.randrange(3) == 0 else
                               [pick(rng, 1, 16, 40, 99, 100, 300, 1001, rng.randrange(0, 2000)) for _ in range(40)]),
        "net_is_game": [pick(rng, 0, 1, 2)],
    }


# ---- per-function cases: (args, state, script, unstub set) ----

def case_game_load_step(rng):
    st = base_state(rng)
    w32(st, 0x4AC984, pick(rng, 0, 1, 1, 2))
    w32(st, 0x4BE47C, flag(rng))
    w32(st, 0x4BE3E8, pick(rng, 0, 0, 1, r32(rng)))
    sc = common_script(rng)
    for name in ("input_open", "tick_timer_start", "frontend_load", "race_setup", "net_session_start"):
        sc[name] = [pick(rng, 0, 1, 1, 1, 2) for _ in range(3)]
    return [], st, sc, set()


def case_net_session_start(rng):
    sc = common_script(rng)
    for name in ("net_45e4a0", "net_45ef90", "net_45f040"):
        sc[name] = [pick(rng, 0, 1, 1, 1, r32(rng))]
    return [], {}, sc, set()


def case_tick_timer_start(rng):
    st = {}
    w32(st, 0x4BE3E8, r32(rng))
    sc = common_script(rng)
    sc["timer_start"] = [pick(rng, 0, 1, r32(rng))]
    return [], st, sc, set()


def case_tick_count_reset(rng):
    st = {}
    w32(st, 0x4BE3E8, r32(rng))
    return [], st, common_script(rng), set()


def case_tick_callback(rng):
    st = base_state(rng)
    for a in (0x4BE468, 0x4BE3E8, 0x6C7FF8):
        w32(st, a, r32(rng))
    sc = common_script(rng)
    w32(st, 0x4BE4AC, (sc["clock"][0] + rng.randrange(-3000, 1000)) & 0xFFFFFFFF)
    w32(st, 0x4BE4B0, pick(rng, 0, 1, 2, 3, 4, r32(rng)))
    return [r32(rng) for _ in range(5)], st, sc, set()


def case_quit_request(rng):
    st = {}
    w32(st, 0x4BE48C, r32(rng))
    return [], st, common_script(rng), set()


def case_game_speed_set(rng):
    st = base_state(rng)
    w32(st, 0x4BE47C, flag(rng))
    w32(st, 0x4ACC50, r32(rng))
    return [], st, common_script(rng), set()


def case_fade_step(rng):
    st = {}
    w32(st, 0x4BE320, r32(rng))
    w32(st, 0x4BE330, pick(rng, 0, 1, 2, 3, r32(rng)))
    w32(st, 0x4AC990, pick(rng, 0, 0x14, 0x15, 0x16, 0xEB, 0xEC, 0xED, 0x100, rng.randrange(-0x200, 0x200),
                            0x7FFFFFF0, -0x7FFFFFF0, r32(rng)))
    return [], st, common_script(rng), set()


def case_tick_hook_call(rng):
    st = {}
    w32(st, 0x4BE114, pick(rng, 0, HOOK_FN))
    return [], st, common_script(rng), set()


def case_frame_counter_step(rng):
    st = {}
    w32(st, 0x4D1198, flag(rng))
    w32(st, 0x6B8D68, pick(rng, 0, 0x1FE, 0x1FF, 0x200, rng.randrange(-5, 0x210), r32(rng)))
    return [], st, common_script(rng), set()


def case_frame_net_step(rng):
    st = {}
    sc = common_script(rng)
    w32(st, 0x4BE46C, pick(rng, 0, 1, 1, 2))
    w32(st, 0x4BE470, (sc["clock"][0] - pick(rng, 0, 16, 17, 100, 200, rng.randrange(0, 400))) & 0xFFFFFFFF)
    w32(st, 0x4BE474, pick(rng, 0, 1, 2, 5, 10, r32(rng)))
    w32(st, 0x4BE47C, flag(rng))
    w32(st, 0x5D622C, r32(rng))
    sc["net_48c830"] = [pick(rng, 0, 1, 1)]
    return [], st, sc, set()


def case_controls_gather(rng):
    st = {}
    w32(st, 0x4BE47C, flag(rng))
    w32(st, 0x5D622C, r32(rng))
    sc = common_script(rng)
    sc["controls_packet"] = [rng.getrandbits(64) for _ in range(6)]
    return [], st, sc, set()


def keyboard_tables(rng, st):
    state = bytearray(pick(rng, 0, 0, 1, 2, 3, 8, 0xFF, rng.getrandbits(8)) for _ in range(256))
    st[0x6C1FC0] = bytes(state)
    st[0x6C24C0] = bytes(pick(rng, 0, rng.getrandbits(8)) for _ in range(256))
    st[0x6C1EC0] = bytes(rng.getrandbits(8) for _ in range(256))
    st[0x6C20C0] = bytes(rng.getrandbits(8) for _ in range(1024))
    for e in (0x4AF438, 0x4AF450, 0x4AF468):
        ev = bytearray(rng.getrandbits(8) for _ in range(0x18))
        struct.pack_into("<i", ev, 0, pick(rng, -1, rng.randrange(0, 256)))
        st[e] = bytes(ev)


def case_keyboard_update(rng):
    st = base_state(rng)
    keyboard_tables(rng, st)
    w32(st, 0x4BEB78, pick(rng, 1, 1, 1, 0))
    w32(st, 0x4BEB7C, flag(rng))
    for a in range(0x4BEB38, 0x4BEB78, 4):
        w32(st, a, r32(rng))
    return [], st, common_script(rng), {0x43C3F0, 0x43C6A0}


def case_keyboard_init(rng):
    st = {}
    keyboard_tables(rng, st)
    w32(st, 0x4BEB78, pick(rng, 0, 1, r32(rng)))
    return [], st, common_script(rng), set()


def case_key_tables_init(rng):
    st = {}
    keyboard_tables(rng, st)
    w32(st, 0x4BEB7C, flag(rng))
    return [], st, common_script(rng), set()


def case_key_layout_set(rng):
    st = {}
    w32(st, 0x4BEB7C, r32(rng))
    return [flag(rng)], st, common_script(rng), set()


def case_controls_read(rng):
    st = base_state(rng)
    bindings_state(rng, st)
    sc = common_script(rng)
    for name in ("joy_button_down", "joy_pov_dir"):
        sc[name] = [pick(rng, 0, 1, 0) for _ in range(40)]
    for name in ("joy_axis_x", "joy_axis_y", "joy_axis_z", "fe_joy_axis"):
        sc[name] = axis_values(rng, 40)
    return [pick(rng, 0, 1, 2, 3, 0, 1, 2, 3, -1, 4, r32(rng)), pick(rng, 0, 0, 0, 1, r32(rng))], st, sc, set()


def case_controls_mask_menu_keys(rng):
    st = base_state(rng)
    bindings_state(rng, st)
    return [pick(rng, 0, 1, 2, 3, -1, 4), r32(rng)], st, common_script(rng), set()


def case_keyboard_nav_bits(rng):
    return [], base_state(rng), common_script(rng), set()


def case_controls_nav_bits(rng):
    st = {}
    for p in range(4):
        w16(st, 0x6BFDEC + p * 0x274, rng.randrange(-2, 5))
    sc = common_script(rng)
    sc["controls_mask_menu_keys"] = [pick(rng, 0, 0x40, 0x800, 0x840, r32(rng))]
    for name in ("joy_axis_x", "joy_axis_y"):
        sc[name] = axis_values(rng, 2)
    return [r32(rng), rng.randrange(0, 4)], st, sc, set()


def case_joy_button_down(rng):
    st = {}
    pads_state(rng, st)
    return [pick(rng, 0, 1, 2, 3, -1), pick(rng, 0, 1, 7, 31, rng.randrange(-2, 40))], st, common_script(rng), set()


def case_joy_pov_dir(rng):
    st = {}
    pads_state(rng, st)
    code = pick(rng, rng.randrange(0, 0x40), rng.randrange(0, 0x100), r32(rng))
    return [pick(rng, 0, 1, 2, 3, -1), code], st, common_script(rng), set()


def case_joy_axis(rng):
    sc = common_script(rng)
    sc["fe_joy_axis"] = axis_values(rng, 1)
    return [pick(rng, 0, 1, -1, r32(rng))], {}, sc, set()


def case_car_position_sum(rng):
    st = {}
    for s in range(6):
        w32(st, 0x6BFB80 + s * 0x274, CARS + s * 0x100)
    st[CARS] = bytes(rng.getrandbits(8) for _ in range(0x600))
    return [rng.randrange(0, 6)], st, common_script(rng), set()


def case_frontend_load(rng):
    st = {}
    w32(st, 0x4D4650, flag(rng))
    w32(st, 0x6CCE7C, flag(rng))
    for a in (0x6C8160, 0x4BE320, 0x4BE328, 0x4AC990):
        w32(st, a, r32(rng))
    sc = common_script(rng)
    sc["high_colour"] = [pick(rng, 0, 1, 2) for _ in range(2)]
    sc["frontend_backdrop_load"] = [pick(rng, 0, 1, r32(rng))]
    return [], st, sc, set()


def model_block(rng):
    block = bytearray(rng.getrandbits(8) for _ in range(0x2000))
    for off in (0x110, 0xD8, 0xCC, 0x84):
        struct.pack_into("<I", block, off, rng.randrange(0, 0x2000))
    count = pick(rng, 0, 1, 3, 8)
    struct.pack_into("<H", block, 0x2C, count)
    table = 0x400
    struct.pack_into("<I", block, 0x18, table)
    for i in range(count):
        entry = 0x800 + i * 0x80
        struct.pack_into("<I", block, table + i * 4, entry)
        for off in (0xC, 0x40, 0x18):
            struct.pack_into("<I", block, entry + off, rng.randrange(0, 0x1000))
        struct.pack_into("<I", block, entry + 0x44, pick(rng, 0, rng.randrange(0, 0x1000)))
    return bytes(block)


def case_frontend_data_load(rng):
    st = {MODELS: model_block(rng), PALETTE: bytes(rng.getrandbits(8) for _ in range(0x100))}
    sc = common_script(rng)
    sc["archive_load_entry"] = [PALETTE, MODELS]
    return [rng.randrange(0, 8), rng.randrange(0, 8)], st, sc, set()


def case_frontend_backdrop_load(rng):
    st = {}
    w32(st, 0x4BE348, pick(rng, 0, PALETTE))
    sc = common_script(rng)
    sc["archive_load_entry"] = [pick(rng, 0, PALETTE)]
    sc["frontend_palette_set"] = [pick(rng, 0, 1, r32(rng))]
    sc["texture_upload"] = [pick(rng, 0, 1, r32(rng))]
    sc["texture_slot"] = [r32(rng)]
    return [r32(rng), rng.randrange(0, 0x80)], st, sc, set()


def case_frontend_palette_set(rng):
    st = {PALETTE: bytes(rng.getrandbits(8) for _ in range(0x100))}
    w32(st, 0x4BE348, pick(rng, 0, PALETTE, PALETTE))
    sc = common_script(rng)
    sc["high_colour"] = [pick(rng, 0, 1, 7)]
    sc["palette_set"] = [r32(rng)]
    return [], st, sc, set()


def case_race_option_clamp(rng):
    return [], base_state(rng), common_script(rng), set()


def case_key_slots_reset(rng):
    st = {0x4BEAF0: bytes(rng.getrandbits(8) for _ in range(0x30))}
    return [], st, common_script(rng), set()


def case_menu_system_init(rng):
    st = base_state(rng)
    for a in (0x4CB494, 0x4CB498, 0x4D4650):
        w32(st, a, flag(rng))
    for a in (0x4CB4B8, 0x4AF650, 0x4AF64C, 0x4BE32C):
        w32(st, a, r32(rng))
    sc = common_script(rng)
    sc["crt_malloc"] = [pick(rng, 0, HEAP, HEAP)]
    return [], st, sc, set()


def case_menu_sprites_setup(rng):
    return [], {}, common_script(rng), set()


def case_model_top_point(rng):
    model = bytearray(rng.getrandbits(8) for _ in range(0x48))
    struct.pack_into("<H", model, 4, pick(rng, 0, 1, 5, 40, 0x3000 if rng.randrange(20) == 0 else 3))
    struct.pack_into("<I", model, 0x40, MODELS)
    verts = bytes(rng.getrandbits(8) for _ in range(0x80000 // 0x10))
    if rng.randrange(3) == 0:
        verts = b"".join(struct.pack("<hhhh", 0, pick(rng, -0x8000, -0x7FFF, -0x7FFE, 0), 0, 0) for _ in range(0x1000))
    st = {PALETTE: bytes(model), MODELS: verts[:0x8000]}
    return [PALETTE], st, common_script(rng), set()


def case_frontend_models_setup(rng):
    st = {}
    st[0x6C1E30 - 0] = struct.pack("<I", SCRATCH + 0x30000)
    setp = bytearray(0x60)
    struct.pack_into("<I", setp, 0x18, SCRATCH + 0x30100)
    struct.pack_into("<I", setp, 0x58, SCRATCH + 0x30400)
    st[SCRATCH + 0x30000] = bytes(setp)
    st[SCRATCH + 0x30100] = b"".join(struct.pack("<I", SCRATCH + 0x31000 + i * 0x40) for i in range(64))
    st[SCRATCH + 0x30400] = b"".join(struct.pack("<h", rng.randrange(0, 64)) for _ in range(256))
    ids = []
    for _ in range(53):
        ident = pick(rng, rng.randrange(0, 0x30), 5, 6, 0x21, 0x22,
                     struct.unpack_from("<i", BASE_DATA, 0x4B0980 - DATA[0] + rng.randrange(0, 28) * 4)[0])
        ids.append(struct.pack("<iiII", ident, rng.randrange(0, 256), pick(rng, 0, 1, r32(rng)), r32(rng)))
    st[0x4B0630] = b"".join(ids)
    sc = common_script(rng)
    sc["model_top_point"] = [r32(rng) for _ in range(60)]
    return [], st, sc, set()


def case_particles_init(rng):
    st = {HEAP: bytes(rng.getrandbits(8) for _ in range(0x4C00))}
    sc = common_script(rng)
    sc["crt_rand"] = [rng.getrandbits(15) for _ in range(600)]
    sc["heap_alloc"] = [HEAP]
    return [], st, sc, set()


def case_game_heap_setup(rng):
    return [], {}, common_script(rng), set()


def case_pad_config_reset(rng):
    st = {0x4D9658: bytes(rng.getrandbits(8) for _ in range(0x90 * 4))}
    sc = common_script(rng)
    sc["pad_index"] = [pick(rng, -1, 0, 1, r32(rng))]
    return [rng.randrange(0, 4)], st, sc, set()


def case_pad_configs_reset(rng):
    sc = common_script(rng)
    sc["pad_count"] = [pick(rng, 0, 1, 2, 4, -1) for _ in range(8)]
    return [], {}, sc, set()


def case_menu_dirty_set(rng):
    st = {}
    w32(st, 0x4CB58C, r32(rng))
    return [], st, common_script(rng), set()


def case_race_options_set(rng):
    return [r32(rng) for _ in range(5)], base_state(rng), common_script(rng), set()


def case_driver_name(rng):
    st = base_state(rng)
    st[SCRATCH + 0x38000] = bytes(rng.getrandbits(8) for _ in range(0x210))
    sc = common_script(rng)
    sc["text_entry"] = [bytes(rng.randrange(1, 256) for _ in range(pick(rng, 0, 1, 5, 30, 0x1FF)))]
    return [SCRATCH + 0x38000, pick(rng, -1, 0, 3, 6, 7, r32(rng))], st, sc, set()


# name: (original address, case builder, returns a value)
TESTS = {
    "game_load_step": (0x435B64, case_game_load_step, True),
    "net_session_start": (0x435DF6, case_net_session_start, True),
    "tick_timer_start": (0x436565, case_tick_timer_start, True),
    "tick_count_reset": (0x436595, case_tick_count_reset, False),
    "tick_callback": (0x4365ED, case_tick_callback, False),
    "quit_request": (0x43699C, case_quit_request, False),
    "game_speed_set": (0x43775A, case_game_speed_set, False),
    "fade_step": (0x430190, case_fade_step, False),
    "tick_hook_call": (0x429210, case_tick_hook_call, False),
    "frame_counter_step": (0x463C70, case_frame_counter_step, False),
    "frame_net_step": (0x435552, case_frame_net_step, False),
    "controls_gather": (0x4369B5, case_controls_gather, False),
    "keyboard_update": (0x43C420, case_keyboard_update, False),
    "keyboard_init": (0x43C3F0, case_keyboard_init, False),
    "key_tables_init": (0x43C6A0, case_key_tables_init, False),
    "key_layout_set": (0x43D270, case_key_layout_set, False),
    "controls_read": (0x427B10, case_controls_read, True),
    "controls_mask_menu_keys": (0x427D00, case_controls_mask_menu_keys, True),
    "keyboard_nav_bits": (0x428D20, case_keyboard_nav_bits, True),
    "controls_nav_bits": (0x428D70, case_controls_nav_bits, True),
    "joy_button_down": (0x489770, case_joy_button_down, True),
    "joy_pov_dir": (0x4897C0, case_joy_pov_dir, True),
    "joy_axis_x": (0x4898E0, case_joy_axis, True),
    "joy_axis_y": (0x4898F0, case_joy_axis, True),
    "joy_axis_z": (0x489900, case_joy_axis, True),
    "car_position_sum": (0x444050, case_car_position_sum, True),
    "frontend_load": (0x4301F0, case_frontend_load, True),
    "frontend_data_load": (0x430C10, case_frontend_data_load, False),
    "frontend_backdrop_load": (0x430D80, case_frontend_backdrop_load, True),
    "frontend_palette_set": (0x430E40, case_frontend_palette_set, True),
    "race_option_clamp": (0x433CD0, case_race_option_clamp, False),
    "key_slots_reset": (0x439FD0, case_key_slots_reset, False),
    "menu_system_init": (0x4446F0, case_menu_system_init, True),
    "menu_sprites_setup": (0x4461D0, case_menu_sprites_setup, False),
    "model_top_point": (0x453730, case_model_top_point, True),
    "frontend_models_setup": (0x453780, case_frontend_models_setup, False),
    "particles_init": (0x462A20, case_particles_init, False),
    "game_heap_setup": (0x46A030, case_game_heap_setup, False),
    "pad_config_reset": (0x48A0E0, case_pad_config_reset, False),
    "pad_configs_reset": (0x48A140, case_pad_configs_reset, False),
    "menu_dirty_set": (0x452D20, case_menu_dirty_set, False),
    "race_options_set": (0x452FA0, case_race_options_set, False),
    "driver_name": (0x4484A0, case_driver_name, False),
}
BASE_DATA = b""
OUTCOMES, CALLS = {}, [0]


def run_case(m, stubs, name, address, builder, returns, rng):
    args, st, script, unstub = builder(rng)
    data = bytearray(BASE_DATA)
    scratch = bytearray(SCRATCH_SIZE)
    for a, v in st.items():
        if DATA[0] <= a < DATA[0] + DATA[1]:
            data[a - DATA[0]:a - DATA[0] + len(v)] = v
        else:
            scratch[a - SCRATCH:a - SCRATCH + len(v)] = v
    results = []
    for target in (address, m.symbols[name]):
        m.write(DATA[0], bytes(data))
        m.write(SCRATCH, bytes(scratch))
        m.write(E.STACK, bytes([0xCC]) * E.STACK_SIZE)
        stubs.reset(script, address, unstub)
        r = m.call(target, args)
        if not returns and r[0] == "ok":
            r = ("ok",)
        d = m.read(*DATA)
        s = m.read(SCRATCH, SCRATCH_SIZE)
        results.append((r, hashlib.sha1(d + s).digest(), stubs.log, d, s))
    a, b = results
    OUTCOMES[a[0][0]] = OUTCOMES.get(a[0][0], 0) + 1
    CALLS[0] += len(a[2])
    if a[:3] != b[:3]:
        msg = f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {a[0]} ours {b[0]}"
        if a[1] != b[1]:
            for base, x, y in ((DATA[0], a[3], b[3]), (SCRATCH, a[4], b[4])):
                diff = [i for i in range(0, len(x), 4) if x[i:i + 4] != y[i:i + 4]]
                if diff:
                    msg += f"\n  memory differs at {[hex(base + i) for i in diff[:8]]}"
                    if "-v" in sys.argv:
                        msg += f"\n  orig {[x[i:i + 4].hex() for i in diff[:8]]}\n  ours {[y[i:i + 4].hex() for i in diff[:8]]}"
        if a[2] != b[2]:
            k = next((i for i in range(min(len(a[2]), len(b[2]))) if a[2][i] != b[2][i]), min(len(a[2]), len(b[2])))
            msg += f"\n  calls differ at #{k}: orig {a[2][k:k + 3]}\n                 ours {b[2][k:k + 3]}"
        return msg
    return None


def main():
    global BASE_DATA
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    only = [x for x in sys.argv[4:] if x not in ("-v", "-s")]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    BASE_DATA = m.read(*DATA)
    stubs = Stubs(m)
    rng = random.Random(0x5C)
    failed = 0
    for name, (address, builder, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = 0
        OUTCOMES.clear()
        CALLS[0] = 0
        for _ in range(iterations):
            msg = run_case(m, stubs, name, address, builder, returns, rng)
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}" +
              (f"  {OUTCOMES} calls {CALLS[0]}" if "-s" in sys.argv else ""))
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
