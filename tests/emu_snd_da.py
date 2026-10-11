#!/usr/bin/env python3
"""Differential test of snd_da.c (sound engine group DA, 0x46f000-0x470fff:
the QMixer session, wave table, the 64 channel records and their wrappers,
sound options, profile statistics) against the original, on random channel
tables, session flags and settings.

Everything the group calls outside itself is a logging stub in both runs:
the QMixer import slots (IAT 0x4a31d0 + 4 * n; each is given the address of a
stdcall stub that pops its own arguments), the voice code of group DB, the
x87 functions left original (0x470160, 0x470ef0), the file loader, free,
sprintf and the log writer. Stubs log every argument they read (memory
arguments by content, stack pointers by kind) and return scripted values, so
failure paths are exercised; the call logs and all memory the group can touch
are compared, with the return values. 0x427960 (square root table) is pure and
runs as is.

    tests/emu_snd_da.py EXE build/emu_da.elf [iterations] [names...] [-v]
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP  # noqa: E402

SCR, SCR_SIZE = 0x01000000, 0x10000
WAVEBLK = SCR  # 0x1000: the loaded wave file block
NEWBLK = SCR + 0x1000  # 0x1000: the block the file loader hands out
BOXES = SCR + 0x2000  # 4 x 0x100: box records
VECS = SCR + 0x2400  # 0x100: int16 vectors / wave formats / params
OUT = SCR + 0x3000  # 0x1000: output buffers
STUBPAGE = 0x02000000
IAT = 0x4A31D0
CH = 0x6CB500

REGIONS = [(CH, 0xF00), (0x4B1C60, 0x60), (0x4D44C0, 0xA0), (0x600160, 0x170)]
STACK_FILL = b"\xcc" * E.STACK_SIZE

# QMixer slot index: (name, argument count)
QM = {
    1: ("StartProfile", 1), 2: ("SetSpeakerPlacement", 2), 3: ("EnableChannel", 4), 4: ("SetOptions", 3),
    5: ("SetDistanceMapping", 4), 7: ("SetPolarPosition", 4), 8: ("SetVolume", 4), 9: ("SetFrequency", 4),
    10: ("IsChannelDone", 2), 11: ("PauseChannel", 3), 12: ("FlushChannel", 3), 13: ("PlayEx", 6),
    14: ("RestartChannel", 3), 15: ("InitEx", 1), 16: ("OpenWaveEx", 3), 17: ("OpenChannel", 3),
    18: ("GetDirectSoundBuffer", 3), 19: ("GetChannelParams", 3), 20: ("SetListenerRolloff", 3),
    21: ("GetDirectSound", 2), 22: ("Activate", 2), 23: ("CloseSession", 1), 24: ("FreeWave", 2),
}
# address: (name, argument count) of the cdecl functions outside the group
OTHER = {
    0x471370: ("voices_init", 0), 0x4713B0: ("voices_exit", 0), 0x471400: ("voice_data", 1),
    0x4714C0: ("channel_defaults", 2), 0x470160: ("channel_direction", 4), 0x470EF0: ("channels_refresh", 0),
    0x470E70: ("voices_crowded", 0), 0x496590: ("file_load", 2), 0x49827A: ("free", 1),
    0x4979F0: ("sprintf", 61), 0x4963C0: ("log_print", 2),
}
STUBS = {}  # stub address -> (name, argument count, stdcall)
for _i, (_n, _c) in QM.items():
    STUBS[STUBPAGE + _i * 0x10] = (_n, _c, True)
for _a, (_n, _c) in OTHER.items():
    STUBS[_a] = (_n, _c, False)

CHANNEL_TESTS = ["snd_chan_stop", "snd_chan_pause", "snd_chan_resume", "snd_chan_busy", "snd_channel_kind",
                 "snd_channel_buffer"]


def pick(rng, *choices):
    return rng.choice(choices)


def rint(rng):
    v = pick(rng, rng.randrange(-4, 0x40), rng.getrandbits(32), rng.getrandbits(16), 0, 1, 0xFFFFFFFF,
             0x80000000, 0x7FFFFFFF)
    return (v + 2**31) % 2**32 - 2**31


def rfloat(rng):
    return pick(rng, 0, 0x80000000, 0x3F800000, 0xBF800000, 0x7FC00000, 0x7F800001, 0xFFC00000, 0x7F800000,
                1, 0x80000001, 0x41200000, 0xC7F12000, rng.getrandbits(32), rng.getrandbits(32))


def channel_index(rng):
    return pick(rng, rng.randrange(0, 64), rng.randrange(0, 64), rng.randrange(0, 64), rng.randrange(-3, 68),
                0, 63, 64, -1)


def vec_from(rng, scale):
    return [rng.randrange(-scale, scale + 1) for _ in range(3)]


def make_box(rng, v=None):
    """A box record: min (x0, y0, z0) at +0, max at +0xc, diagonal v."""
    rec = bytearray(rng.getrandbits(8) for _ in range(0x100))
    lo = vec_from(rng, 0x100)
    v = v or vec_from(rng, 24)
    struct.pack_into("<3h", rec, 0, *lo)
    struct.pack_into("<3h", rec, 0xC, *[(lo[i] + v[i] + 0x8000) % 0x10000 - 0x8000 for i in range(3)])
    return rec


def make_wave_block(rng, count):
    blk = bytearray(rng.getrandbits(8) for _ in range(0x1000))
    struct.pack_into("<i", blk, 0x40, count)
    for i in range(max(min(count, 8), 0)):
        rec = 0x44 + i * 0x40
        struct.pack_into("<II", blk, rec, rng.randrange(0, 0x800), rng.randrange(0, 0x800))
        struct.pack_into("<I", blk, rec + 0xC, pick(rng, 11025, 22050, 44100, rng.getrandbits(32)))
        struct.pack_into("<I", blk, rec + 0x10, pick(rng, 0, rng.getrandbits(32), rng.getrandbits(32)))
        blk[rec + 0x14] = pick(rng, 8, 16, 16, rng.getrandbits(8))
        blk[rec + 0x15] = pick(rng, 1, 2, rng.getrandbits(8))
        blk[rec + 0x16] = pick(rng, 0, 0, 1, rng.getrandbits(8))
    return blk


def make_state(rng, name):
    ch = bytearray()
    for i in range(64):
        rec = bytearray(rng.getrandbits(8) for _ in range(0x3C))
        rec[0] = pick(rng, rng.getrandbits(8), rng.getrandbits(8) & 0xE3, 0, 1, 0x11, 0x10, 3)
        if name in ("snd_chan_play", "snd_chan_pause", "snd_chan_resume"):
            rec[0] = rng.getrandbits(8)
        if name in ("snd_chan_find", "snd_chan_claim"):  # few audible, free channels
            rec[0] = (rng.getrandbits(8) & 0xEF) | (0x10 if rng.randrange(12) == 0 else 0)
            rec[2] = pick(rng, 0, 0, 0, 3)
        rec[1] = pick(rng, 0, 0, 1, 2, 4, 8, 0xC, 0x15, rng.getrandbits(8))
        rec[2] = pick(rng, 0, 0, 1, 5, 10, rng.getrandbits(8))
        struct.pack_into("<i", rec, 8, pick(rng, 0, -10, rng.randrange(0, 0x10000), rint(rng)))
        struct.pack_into("<i", rec, 0xC, pick(rng, 0, -10, rng.randrange(0, 0x8000), rint(rng)))
        struct.pack_into("<3I", rec, 0x10, rfloat(rng), rfloat(rng), rfloat(rng))
        struct.pack_into("<I", rec, 0x34, pick(rng, 0, 0, rng.getrandbits(32)))
        struct.pack_into("<I", rec, 0x38, pick(rng, 0, rng.getrandbits(32)))
        ch += rec
    regs = {a: bytearray(rng.getrandbits(8) for _ in range(n)) for a, n in REGIONS}
    regs[CH] = ch

    def put(address, fmt, value):
        for a, n in REGIONS:
            if a <= address < a + n:
                struct.pack_into(fmt, regs[a], address - a, value)
                return
        raise KeyError(hex(address))

    working = pick(rng, 1, 1, 1, 1, 1, 1, 1, 0)
    put(0x4D4524, "<I", working)  # session open
    put(0x4D4528, "<I", pick(rng, 1, 1, 1, 1, 1, 0) if working else pick(rng, 0, 1))  # active
    put(0x4D4520, "<I", pick(rng, 0, 0, 1) if name == "snd_init" else pick(rng, 0, 1, 1))
    put(0x4D452C, "<I", pick(rng, 0, 1))
    put(0x4D453C, "<I", rng.getrandbits(32))
    put(0x4D4538, "<I", rng.getrandbits(32))
    put(0x4D4544, "<I", pick(rng, 0, 1))
    put(0x4D4548, "<I", pick(rng, 0, 1, 7))
    put(0x4D4550, "<I", pick(rng, 0, 1))
    put(0x4D4554, "<I", pick(rng, 0, 1))
    put(0x4D4558, "<I", pick(rng, 0, 1))
    put(0x4D454C, "<i", pick(rng, 0, 1, 2, 3, 4, -1, rng.randrange(-3, 8), rng.getrandbits(32) - (1 << 31)))
    put(0x4B1C6C, "<i", pick(rng, -1, rng.randrange(-3, 70), 62, 63, 63, 0))
    put(0x4B1C70, "<i", pick(rng, 22050, 11025, 44100, 0x2729, 0x272A, 0x2EF8, 0x2EF9, 0xA85C, 0xA85D, 0xB02B,
                              0xB02C, rint(rng)))
    put(0x4B1C74, "<i", pick(rng, rng.randrange(-4, 50), 8, 7, 32, 33, 16))
    for a in (0x4B1C78, 0x4B1C7C, 0x4B1C80, 0x4B1C84, 0x4B1C88):
        put(a, "<I", pick(rng, 0, 1, 1, rng.getrandbits(8)))
    put(0x4B1C90, "<I", rfloat(rng))
    put(0x4B1C94, "<I", rfloat(rng))
    put(0x4B1CAC, "<I", pick(rng, 0, 0xFFFFFFFF, rng.getrandbits(32), 0x10))
    put(0x4B1CB0, "<I", pick(rng, 0, 1))
    # the wave table: a block, with the globals pointing at it
    count = pick(rng, rng.randrange(1, 7), rng.randrange(1, 7), rng.randrange(1, 7), 0, 1, -1, 8)
    wave_block = make_wave_block(rng, count)
    have = pick(rng, 1, 1, 1, 1, 1, 0)
    put(0x4D4530, "<I", WAVEBLK if have else pick(rng, 0, 0, 0x1234))
    put(0x4D4534, "<i", count if have else pick(rng, 0, 0, 3))
    put(0x6002CC, "<I", WAVEBLK + 0x44)
    put(0x6002C8, "<I", WAVEBLK + 0x44 + (count & 0xFFFFFF) * 0x40)
    scr = bytearray(rng.getrandbits(8) for _ in range(SCR_SIZE))
    scr[0:0x1000] = wave_block
    new_count = pick(rng, rng.randrange(0, 7), 0, 1, -2, 3)
    scr[0x1000:0x2000] = make_wave_block(rng, new_count)
    script = {}
    fail = pick(rng, 0, 0, 0.1, 0.5)
    for n, _ in QM.values():
        script[n] = [pick(rng, -1, 1, 5) if rng.random() < fail else 0 for _ in range(300)]
    script["InitEx"] = [pick(rng, 0, rng.getrandbits(32) | 1, rng.getrandbits(32) | 1) for _ in range(2)]
    script["OpenWaveEx"] = [pick(rng, 0, 0, rng.getrandbits(32)) for _ in range(300)]
    script["PlayEx"] = [pick(rng, 0, 0, 0, 0, 1) for _ in range(300)]
    script["IsChannelDone"] = [pick(rng, 0, 1, 1, 1) for _ in range(300)]
    script["CloseSession"] = [pick(rng, 0, 1, rng.getrandbits(32))]
    script["out"] = [pick(rng, 0, rng.getrandbits(32)) for _ in range(300)]  # GetDirectSound(Buffer) results
    script["params"] = [(
        pick(rng, 0x01000000, 0x02000000, 0x01000000, 0, 0x03000000, 0x7F000000, rng.getrandbits(8) << 24 & 0x7F000000)
        | pick(rng, 0x10000, 0x20000, 0x40000, 0x80000, 0x30000, 0x100000, 0xFF0000, 0, rng.getrandbits(8) << 16)
        | pick(rng, 0, rng.getrandbits(16)), rng.getrandbits(32)) for _ in range(300)]
    script["voice_data"] = [pick(rng, 0, 0, rng.getrandbits(32)) for _ in range(300)]
    script["voices_crowded"] = [pick(rng, 0, 1)] * 4
    script["channels_refresh"] = [pick(rng, 0, -1)] * 8
    script["file_load"] = [pick(rng, NEWBLK, NEWBLK, WAVEBLK, 0)] * 2
    script["log_print"] = [pick(rng, 0, 5)]
    script["sprintf"] = [pick(rng, 0, 40, 7)]
    script["voices_init"] = []
    return dict(regs=regs, scr=scr, script=script)


def c_string(uc, address, limit=0x400):
    data = b""
    while len(data) < limit:
        chunk = bytes(uc.mem_read(address + len(data), 0x10 - (address + len(data)) % 0x10))
        if b"\0" in chunk:
            return data + chunk.split(b"\0", 1)[0]
        data += chunk
    return data[:limit]


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


class Stubs:
    def __init__(self, m):
        self.m = m
        m.uc.mem_map(STUBPAGE, 0x1000)
        m.uc.mem_write(STUBPAGE, b"\xc3" * 0x1000)
        for address in STUBS:
            m.uc.hook_add(UC_HOOK_CODE, self._hook, begin=address, end=address)
        self.reset({})

    def reset(self, script):
        self.log = []
        self.script = {k: list(v) for k, v in script.items()}

    def install_iat(self):
        for i in QM:
            self.m.uc.mem_write(IAT + 4 * i, struct.pack("<I", STUBPAGE + i * 0x10))

    def _next(self, name, default=0):
        values = self.script.get(name)
        return values.pop(0) if values else default

    def _hook(self, uc, address, size, _):
        try:
            self._stub(uc, address)
        except UcError:
            self.log.append(("fault", address))
            uc.emu_stop()

    def _stub(self, uc, address):
        name, nargs, stdcall = STUBS[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack(f"<{nargs + 1}I", bytes(uc.mem_read(esp, 4 * (nargs + 1))))
        ret, args = words[0], list(words[1:])
        logged = [("stack" if on_stack(a) else a) for a in args]
        eax = 0
        if name == "InitEx":
            logged = [bytes(uc.mem_read(args[0], 0x40))]
            eax = self._next(name)
        elif name == "SetDistanceMapping":
            logged = args[:3] + [bytes(uc.mem_read(args[3], 0x10))]
            eax = self._next(name)
        elif name == "SetPolarPosition":
            logged = args[:3] + [bytes(uc.mem_read(args[3], 12))]
            eax = self._next(name)
        elif name == "OpenWaveEx":
            desc = bytes(uc.mem_read(args[1], 20))
            fmt = bytes(uc.mem_read(struct.unpack_from("<I", desc, 0)[0], 0x12))
            logged = [args[0], desc[4:], fmt, args[2]]
            eax = self._next(name)
        elif name == "GetChannelParams":
            buf = args[2]
            logged = args[:2] + [bytes(uc.mem_read(buf, 0xDC))]
            eax = self._next(name)
            flags, v = self._next("params", (0, 0))
            uc.mem_write(buf + 4, struct.pack("<I", flags))
            uc.mem_write(buf + 0x5C, struct.pack("<I", v))
        elif name in ("GetDirectSound", "GetDirectSoundBuffer"):
            out_at = args[-1]
            logged = args[:-1]
            eax = self._next(name)
            value = self._next("out")
            uc.mem_write(out_at, struct.pack("<I", value))  # also on failure, as a leftover
        elif name == "CloseSession":
            eax = self._next(name)
        elif name == "file_load":
            eax = self._next(name)
        elif name in ("voice_data", "channels_refresh", "voices_crowded"):
            eax = self._next(name)
        elif name == "sprintf":
            words = struct.unpack("<62I", bytes(uc.mem_read(esp, 62 * 4)))
            logged = [("stack" if on_stack(args[0]) else args[0]), c_string(uc, args[1])] + list(words[3:])
            text = b"S%x" % (sum(words[3:]) & 0xFFFFFFFF)
            uc.mem_write(args[0], text + b"\0")
            eax = len(text)
        elif name == "log_print":
            logged = [c_string(uc, args[0]), c_string(uc, args[1])]
            eax = self._next(name)
        elif name in ("PlayEx", "IsChannelDone"):
            eax = self._next(name)
        elif name in ("voices_init", "voices_exit", "free", "channel_defaults", "channel_direction"):
            pass
        else:
            eax = self._next(name)
        self.log.append((name, tuple(logged)))
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * nargs if stdcall else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


def vec16(rng, n=3, scale=0x60):
    return [pick(rng, rng.randrange(-scale, scale), rng.randrange(-0x31, 0x32), rng.randrange(-0x8000, 0x8000),
                 rng.randrange(-8, 9)) for _ in range(n)]


def box_args(rng):
    """holder (+0xb8 -> other box), box (or -1)."""
    v1 = vec_from(rng, 24)
    parallel = rng.randrange(3) != 0
    k = pick(rng, 1, 2, 0.5, -1)
    v2 = [int(c * k) + pick(rng, 0, 0, 1, -1) for c in v1] if parallel else vec_from(rng, 24)
    return v1, v2


def args_for(rng, name, st):
    scr = st["scr"]
    if name == "snd_vec_length":
        struct.pack_into("<3h", scr, 0x2400, *vec16(rng))
        return [VECS]
    if name == "snd_box_merge":
        v1, v2 = box_args(rng)
        box = make_box(rng, v1)
        other = make_box(rng, v2)
        holder = bytearray(rng.getrandbits(8) for _ in range(0x100))
        mode = rng.randrange(8)
        scr[0x2000:0x2100] = holder
        scr[0x2100:0x2200] = box
        scr[0x2200:0x2300] = other
        struct.pack_into("<I", scr, 0x2000 + 0xB8, BOXES + 0x200 if mode != 0 else BOXES + 0x100)
        return [BOXES, pick(rng, BOXES + 0x100, BOXES + 0x100, BOXES + 0x100, 0xFFFFFFFF)]
    if name == "snd_rate_for":
        return [pick(rng, rint(rng), rng.randrange(0x2700, 0x2F20), rng.randrange(0xA800, 0xB060))]
    if name == "snd_activate":
        return [pick(rng, 0, 1, 1, 2, -1, rint(rng))]
    if name == "snd_set_rolloff":
        return [rfloat(rng)]
    if name == "snd_channel_params":
        return [OUT + 0x100, channel_index(rng)]
    if name in CHANNEL_TESTS:
        return [channel_index(rng)]
    if name in ("snd_chan_volume", "snd_chan_frequency"):
        ch = channel_index(rng)
        if rng.randrange(3) == 0 and 0 <= ch < 64:  # the cached value (or its negative / above the limit)
            off = 0xC if name == "snd_chan_volume" else 8
            cur = struct.unpack_from("<i", st["regs"][CH], ch * 0x3C + off)[0]
            return [ch, pick(rng, cur, -cur, 0x7FFF, 0xFFFF, 0x10000, -0x80000000)]
        return [ch, pick(rng, rng.randrange(-0x20000, 0x20000), rint(rng), 0, -0x7FFF, 0x7FFF, -0xFFFF, 0xFFFF)]
    if name == "snd_chan_position":
        ch = channel_index(rng)
        vals = [rfloat(rng) for _ in range(3)]
        if 0 <= ch < 64:
            cur = struct.unpack_from("<3I", st["regs"][CH], ch * 0x3C + 0x10)
            vals = [pick(rng, cur[i], cur[i], vals[i], cur[i] ^ 0x80000000) for i in range(3)]
        return [ch] + vals
    if name == "snd_wave_format":
        return [OUT, pick(rng, 1, 0, rng.getrandbits(32)), pick(rng, 1, 2, rng.getrandbits(32)),
                pick(rng, 22050, 44100, rng.getrandbits(32)), pick(rng, 8, 16, rng.getrandbits(32))]
    if name == "snd_wave_open":
        return [rng.getrandbits(32), rng.getrandbits(32), pick(rng, OUT, OUT), rng.getrandbits(32)]
    if name == "snd_wave_free":
        return [pick(rng, 0, rng.getrandbits(32))]
    if name == "snd_waves_load":
        return [pick(rng, rng.getrandbits(32), 0x4B0000)]
    if name == "snd_chan_play":
        count = struct.unpack_from("<i", st["regs"][0x4D44C0], 0x4D4534 - 0x4D44C0)[0]
        return [pick(rng, rng.randrange(-2, 9), rng.randrange(0, max(count, 1))), channel_index(rng),
                pick(rng, 0, 1, rng.getrandbits(32), 2)]
    if name == "snd_chan_claim":
        return [rint(rng)]
    if name == "snd_speaker_cycle":
        return [pick(rng, 0, 1, rint(rng)), pick(rng, 0, 1, rint(rng))]
    if name == "snd_channel_count_add":
        return [pick(rng, rng.randrange(-30, 30), rint(rng), 0)]
    if name in ("snd_zero16", "snd_zero6"):
        return [OUT + 0x200]
    if name == "snd_stats_format":
        return [OUT + 0x400]
    return []


# name: (address, returns a value?)
TESTS = {
    "snd_box_merge": (0x46F080, True),
    "snd_vec_length": (0x46F1B0, True),
    "snd_init": (0x46F4F0, True),
    "snd_shutdown": (0x46F610, True),
    "snd_activate": (0x46F650, True),
    "snd_rate_for": (0x46F6E0, True),
    "snd_dsound": (0x46F720, True),
    "snd_set_rolloff": (0x46F760, True),
    "snd_channel_params": (0x46F7A0, True),
    "snd_channel_kind": (0x46F7F0, True),
    "snd_channel_buffer": (0x46F8F0, True),
    "snd_open_channels": (0x46F940, True),
    "snd_channels_reset": (0x46F970, True),
    "snd_wave_format": (0x46FA00, True),
    "snd_wave_open": (0x46FA60, True),
    "snd_wave_free": (0x46FAD0, True),
    "snd_waves_free": (0x46FB00, False),
    "snd_waves_load": (0x46FB80, True),
    "snd_chan_play": (0x46FC40, True),
    "snd_chan_stop": (0x46FD40, True),
    "snd_chan_pause": (0x46FDA0, True),
    "snd_chan_resume": (0x46FE00, True),
    "snd_pause_all": (0x46FE60, False),
    "snd_resume_all": (0x46FE90, False),
    "snd_chan_busy": (0x46FEC0, True),
    "snd_chan_find": (0x46FF30, True),
    "snd_chan_claim": (0x46FFA0, True),
    "snd_chan_frequency": (0x46FFD0, True),
    "snd_chan_volume": (0x470040, True),
    "snd_chan_position": (0x4700B0, True),
    "snd_distance_map": (0x4702A0, True),
    "snd_options_apply": (0x470300, True),
    "snd_channels_enable": (0x470350, True),
    "snd_channel_limit": (0x470470, True),
    "snd_voice_flags": (0x4704B0, True),
    "snd_speakers": (0x470530, True),
    "snd_settings_apply": (0x4705D0, True),
    "snd_speaker_cycle": (0x4705F0, True),
    "snd_channel_count_add": (0x470640, True),
    "snd_stats_clear": (0x470680, False),
    "snd_zero16": (0x4706E0, True),
    "snd_zero6": (0x470720, True),
    "snd_profile_start": (0x470740, True),
    "snd_stats_format": (0x470CA0, True),
    "snd_stats_log": (0x470E40, True),
}
ARG_BYTES = {"snd_chan_play": 12}


def offset_of(address):
    if SCR <= address < SCR + SCR_SIZE:
        return address - SCR
    off = SCR_SIZE
    for a, n in REGIONS:
        if a <= address < a + n:
            return off + address - a
        off += n
    raise KeyError(hex(address))


def run_case(m, stubs, name, address, returns, rng):
    st = make_state(rng, name)
    args = args_for(rng, name, st)
    results = []
    for target in (address, m.symbols[name]):
        m.write(SCR, bytes(st["scr"]))
        for a, v in st["regs"].items():
            m.write(a, bytes(v))
        stubs.install_iat()
        m.write(E.STACK, STACK_FILL)
        stubs.reset(st["script"])
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        mem = m.read(SCR, SCR_SIZE) + b"".join(m.read(a, n) for a, n in REGIONS)
        results.append((r, mem, stubs.log))
    if results[0] != results[1]:
        a, b = results[0][1], results[1][1]
        first = next((i for i in range(len(a)) if a[i] != b[i]), None)
        if "-v" in sys.argv and first is not None:
            diff = sorted({i & ~3 for i in range(len(a)) if a[i] != b[i]})
            where = []
            for i in diff[:8]:
                if i < SCR_SIZE:
                    addr = SCR + i
                else:
                    off = i - SCR_SIZE
                    for ra, rn in REGIONS:
                        if off < rn:
                            addr = ra + off
                            break
                        off -= rn
                where.append((hex(addr), hex(struct.unpack_from("<I", a, i)[0]), hex(struct.unpack_from("<I", b, i)[0])))
            print("  args", [hex(x & 0xFFFFFFFF) for x in args], "differ:", where)
        logs = ""
        if results[0][2] != results[1][2]:
            la, lb = results[0][2], results[1][2]
            k = next((i for i in range(min(len(la), len(lb))) if la[i] != lb[i]), min(len(la), len(lb)))
            logs = f"\n  calls from {k}: {la[k:k + 3]}\n  ours:          {lb[k:k + 3]}"
        return f"{name}{tuple(hex(x & 0xFFFFFFFF) for x in args)}: original {results[0][0]} ours {results[1][0]}" + (
            "" if first is None else " (memory differs)") + logs, results[0][0]
    return None, results[0][0], len(results[0][2])


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 1000
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCR, SCR_SIZE)
    stubs = Stubs(m)
    rng = random.Random(46)
    failed = 0
    only = [x for x in sys.argv[4:] if x not in ("-v", "-s")]
    for name, (address, returns) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = calls = 0
        seen = {}
        for _ in range(iterations):
            msg, r, *rest = run_case(m, stubs, name, address, returns, rng)
            errors += r[0] != "ok"
            key = r[1] if len(r) > 1 else None
            key = key if key in (0, 1, 0xFFFFFFFF) else ("other" if key is not None else None)
            seen[key] = seen.get(key, 0) + 1
            calls += rest[0] if rest else 0
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs failed the same way in both)" if errors else ""
        spread = " results " + " ".join(f"{k}:{v}" for k, v in sorted(seen.items(), key=str)) if "-s" in sys.argv else ""
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note} [{calls} stub calls]{spread}")
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
