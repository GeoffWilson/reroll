#!/usr/bin/env python3
"""Differential test of orig_pw.c (group PW: the movie player's decoder,
0x493a10-0x496250) against the original, on random state.

Callees outside the group are logging stubs in both runs (each logs the
arguments it reads; stack buffers by content): the Windows calls through
the import table (Sleep, InterlockedExchange, timeEndPeriod,
timeKillEvent), free 0x49827a / calloc 0x49946c (scripted results), the
fatal box 0x493340 (fastcall, ecx), and the COM methods of a fake
DirectDraw surface (GetPixelFormat, GetSurfaceDesc, Lock, Unlock) and
sound buffer (Release). The group's own functions run for real inside the
group (the original's tree against ours).

Decoder input is made to be well formed: frame streams of macroblocks
(motion copies, flat fills, skipped kinds and coded blocks whose bit
streams are encoded by `block_bits`), so the decoders end; frame sizes are
multiples of 16 (the copy loops count down by 16 to exactly 0).

Compared: the data section 0x4a7000-0x6d4000 (with the decoder state page
0x6d3000), the scratch memory, the call log, the return value where it is
one, esp after return (callee-popped arguments), the registers the
original keeps (ebx / esi / edi / ebp; all for the pushad functions; all
but eax for 0x495958), and the x87 control word, stack top and tag word.
The x87 control word is 0x27f, 0x37f or 0x07f.

    tests/emu_orig_pw.py EXE build/emu_pw.elf [iterations] [names...] [-v] [-c]

-c: branch coverage of the original (instructions reached / all).
"""
import random
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])  # run with python -I
import emu_fixed as E  # noqa: E402
from unicorn import UC_HOOK_CODE, UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,  # noqa: E402
                               UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP,
                               UC_X86_REG_FPCW, UC_X86_REG_FPSW, UC_X86_REG_FPTAG)

DATA, DATA_SIZE = 0x4A7000, 0x6D4000 - 0x4A7000
SCRATCH, SCRATCH_SIZE = 0x01000000, 0x80000
PLAYER = SCRATCH + 0x0000  # the player record (0x400)
SURF = SCRATCH + 0x0800  # fake surface: vtable pointer
SNDBUF = SCRATCH + 0x0880  # fake sound buffer
VTBL = SCRATCH + 0x0900  # their vtable (0x100)
QUAL = SCRATCH + 0x0C00  # quality bytes (0x100)
SRC = SCRATCH + 0x1000  # streams / inputs (0xF000)
FRAME_A = SCRATCH + 0x10000  # frame buffers (0xc000 each; the previous one with 0x4000 margins)
FRAME_B = SCRATCH + 0x24000
OUT = SCRATCH + 0x38000  # outputs / the screen surface (0x48000)
IATSTUB = 0x02200000
COMSTUB = IATSTUB + 0x800
STACK_FILL = b"\xcc" * E.STACK_SIZE
REGS = (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI,
        UC_X86_REG_EBP)
KEEP_C = (1, 4, 5, 6)  # ebx esi edi ebp
KEEP_ALL = (0, 1, 2, 3, 4, 5, 6)
KEEP_NOT_EAX = (1, 2, 3, 4, 5, 6)

# IAT slot: (name, stdcall argument kinds)
IAT = {
    0x4A3110: ("Sleep", "v"),
    0x4A314C: ("InterlockedExchange", "vv"),
    0x4A32B8: ("timeEndPeriod", "v"),
    0x4A32C4: ("timeKillEvent", "v"),
}
# vtable offset: (name, stdcall argument kinds after this)
COM = {
    0x08: ("Release", ""),
    0x54: ("GetPixelFormat", "P"),
    0x58: ("GetSurfaceDesc", "D"),
    0x64: ("Lock", "vDvv"),
    0x80: ("Unlock", "v"),
}
# address: (name, kinds, convention)
STUBS = {
    0x49827A: ("free", "v", "cdecl"),
    0x49946C: ("calloc", "vv", "cdecl"),
    0x493340: ("fatal", "", "fastcall"),
}


def pick(rng, *choices):
    return rng.choice(choices)


def on_stack(a):
    return E.STACK <= a < E.STACK + E.STACK_SIZE


class Stubs:
    def __init__(self, m):
        m.uc.mem_map(IATSTUB, 0x1000)
        m.uc.mem_write(IATSTUB, b"\xf4" * 0x1000)
        self.calls = {}
        for i, (slot, (name, kinds)) in enumerate(sorted(IAT.items())):
            address = IATSTUB + i * 0x10
            m.uc.mem_write(slot, struct.pack("<I", address))
            self.calls[address] = (name, kinds, "stdcall")
        self.vtable = bytearray(0x100)
        for off, (name, kinds) in COM.items():
            address = COMSTUB + off * 4
            struct.pack_into("<I", self.vtable, off, address)
            self.calls[address] = (name, "t" + kinds, "stdcall")
        for address, (name, kinds, conv) in STUBS.items():
            self.calls[address] = (name, kinds, conv)
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
        name, kinds, conv = self.calls[address]
        esp = uc.reg_read(UC_X86_REG_ESP)
        words = struct.unpack("<8I", bytes(uc.mem_read(esp, 32)))
        ret, args = words[0], list(words[1:])
        logged = []
        for i, k in enumerate(kinds):
            a = args[i]
            if k == "D":  # a DDSURFACEDESC: where and its content
                logged.append(("stack" if on_stack(a) else a, bytes(uc.mem_read(a, 0x6C))))
            elif k == "P":  # a DDPIXELFORMAT
                logged.append(("stack" if on_stack(a) else a, bytes(uc.mem_read(a, 0x20))))
            else:
                logged.append("stack" if on_stack(a) else a)
        if conv == "fastcall":
            logged.append(uc.reg_read(UC_X86_REG_ECX))
        self.log.append((name, tuple(logged)))
        if len(self.log) > 3000:
            self.log.append("too many calls")
            uc.emu_stop()
            return
        eax = self._next(name, 0)
        if name == "Lock":
            surface, pitch = self._next("surface", (OUT, 0x400))
            uc.mem_write(args[2] + 0x24, struct.pack("<I", surface))
            uc.mem_write(args[2] + 0x10, struct.pack("<I", pitch))
        elif name == "GetSurfaceDesc":
            w, h = self._next("screen", (640, 480))
            uc.mem_write(args[1] + 8, struct.pack("<II", h, w))
        elif name == "GetPixelFormat":
            uc.mem_write(args[1] + 0x10, struct.pack("<3I", *self._next("masks", (0xF800, 0x7E0, 0x1F))))
        elif name == "InterlockedExchange":
            eax = self._next(name, 1 if self._next("locked", 0) else 0)
        uc.reg_write(UC_X86_REG_EAX, eax & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + (4 * len(kinds) if conv == "stdcall" else 0))
        uc.reg_write(UC_X86_REG_EIP, ret)


# ---- state builders: st = {address: bytes}; each case returns
# (stack args, {register: value}, st, script, (keep registers, returns)) ----

def w32(st, a, v):
    st[a] = struct.pack("<I", v & 0xFFFFFFFF)


def w16(st, a, v):
    st[a] = struct.pack("<H", v & 0xFFFF)


def r32(rng):
    return rng.getrandbits(32)


def tables_state(rng, st):
    """Random colour tables and dequantisation (small-ish 16.16 steps)."""
    for t in (0x4DAAC8, 0x4DACC8, 0x4DAEC8):
        st[t] = rng.randbytes(0x200)
    dq = [pick(rng, rng.randrange(0x8000, 0x80000), rng.randrange(0, 0x400000), r32(rng) & 0xFFFFF)
          for _ in range(64)]
    st[0x4DB0C8] = struct.pack("<64I", *dq)


def com_state(st, stubs):
    st[VTBL] = bytes(stubs.vtable)
    w32(st, SURF, VTBL)
    w32(st, SNDBUF, VTBL)


def player_state(rng, st, w=None, h=None):
    p = bytearray(rng.randbytes(0x400))
    w = w if w is not None else 16 * rng.randrange(1, 5)
    h = h if h is not None else 16 * rng.randrange(1, 4)
    struct.pack_into("<hh", p, 0xC, w, h)
    struct.pack_into("<I", p, 0x4C, FRAME_A)
    struct.pack_into("<I", p, 0x50, FRAME_B)
    struct.pack_into("<I", p, 0x90, SURF)
    st[PLAYER] = bytes(p)
    return w, h


# -- the bit stream encoder for coded blocks (LSB first) --

class Bits:
    def __init__(self):
        self.bits = []

    def put(self, value, n):
        self.bits += [(value >> i) & 1 for i in range(n)]

    def data(self):
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            chunk = self.bits[i:i + 8]
            out.append(sum(b << j for j, b in enumerate(chunk)))
        return bytes(out)


def block_bits(rng, bits, overshoot=False):
    """One block: the DC byte and codes covering coefficients 63..1."""
    bits.put(rng.getrandbits(8), 8)
    k = 63
    while k > 0:
        choices = ["0", "2", "6", "lvl", "esc"]
        if k >= 2 or overshoot:
            choices.append("4")
        if k >= 2 or overshoot:
            choices.append("run")
        if k >= 3 or overshoot:
            choices.append("run5")
        c = rng.choice(choices)
        if c in "026":
            bits.put(int(c), 3)
            k -= 1
        elif c == "4":
            bits.put(4, 3)
            k -= 2
        elif c in ("run", "run5"):
            first = 1 if c == "run5" else 0
            room = (k - first) // 2
            n = rng.randrange(1, max(1, min(room, 31)) + 1) if not overshoot else rng.randrange(1, 32)
            bits.put((n << 3) | (5 if first else 1), 8)
            k -= first + 2 * n
        elif c == "lvl":
            while True:
                b = (rng.getrandbits(6) << 2) | 3
                if b & 0xFC != 0xFC:
                    break
            bits.put(b, 8)
            k -= 1
        else:
            bits.put(0xFF, 8)
            bits.put(rng.getrandbits(8), 8)
            k -= 1


def macroblock_bytes(rng):
    bits = Bits()
    for _ in range(6):
        block_bits(rng, bits, overshoot=rng.randrange(40) == 0)
    return bits.data()


def frame_stream(rng, w, h):
    out = bytearray()
    for _ in range((h // 16) * (w // 16)):
        kind = rng.choice(["coded", "coded", "motion", "fill3", "fill6", "fill12", "skip"])
        if kind == "coded":
            body = macroblock_bytes(rng)
            while len(body) > 0xF0:
                body = macroblock_bytes(rng)
            n = rng.randrange(max(13, len(body)), 0x100)
            body = body + rng.randbytes(n - len(body))
        elif kind == "motion":
            n, body = 1, rng.randbytes(1)
        elif kind == "skip":
            n = pick(rng, 0, 2, 4, 5, 7, 8, 9, 10, 11)
            body = rng.randbytes(n)
        else:
            n = {"fill3": 3, "fill6": 6, "fill12": 12}[kind]
            body = rng.randbytes(n)
        out.append(n ^ 0xB6)
        out += body
    return bytes(out) + rng.randbytes(16)


# -- cases --

def case_audio_close(rng, stubs):
    st = {}
    player_state(rng, st)
    com_state(st, stubs)
    w32(st, PLAYER + 0x330, pick(rng, 0, r32(rng), 7))
    w32(st, PLAYER + 0x94, pick(rng, 0, SNDBUF))
    tries = pick(rng, 0, 0, 1, 5, 1100)
    script = {"InterlockedExchange": [1] * tries + [pick(rng, 0, 2)], "timeKillEvent": [pick(rng, 0, 0, 1)],
              "Release": [r32(rng)]}
    return [], {UC_X86_REG_ECX: PLAYER}, st, script, (KEEP_C, True)


def rand_bpp(rng):
    return pick(rng, 15, 16, 16, 24, 32, rng.randrange(0, 40))


def case_tables_build(rng, stubs):
    st = {}
    tables_state(rng, st)
    return [pick(rng, 0, 1, r32(rng))], {UC_X86_REG_ECX: rand_bpp(rng), UC_X86_REG_EDX: pick(rng, 0xFF, 0, r32(rng))}, \
        st, {}, (KEEP_C, True)


def channel(rng):
    return pick(rng, rng.randrange(-300, 300), rng.randrange(-20, 20), r32(rng), 0x7FFFFFFC, -0x7FFFFFFE, 0)


def case_colour_pack(rng, stubs):
    args = [channel(rng), channel(rng), pick(rng, 0, 0xFF, r32(rng)), pick(rng, 0, 1, r32(rng))]
    return args, {UC_X86_REG_ECX: rand_bpp(rng), UC_X86_REG_EDX: channel(rng)}, {}, {}, (KEEP_C, True)


def case_block_fill(rng, stubs):
    st = {}
    tables_state(rng, st)
    pitch = pick(rng, 0x40, 0x80, 0x100, 0x400, rng.randrange(0x40, 0x800))
    dst = OUT + rng.randrange(0, 0x100)
    args = [r32(rng), r32(rng), r32(rng), r32(rng), dst, pitch]
    return args, {UC_X86_REG_ECX: r32(rng), UC_X86_REG_EDX: r32(rng)}, st, {}, (KEEP_C, False)


def blit_state(rng, st, stubs):
    w = 16 * rng.randrange(1, 5)
    h = pick(rng, 16, 32, rng.randrange(0, 40))
    player_state(rng, st, w, h)
    com_state(st, stubs)
    st[FRAME_A] = rng.randbytes(w * max(h, 1) * 4)
    flags = r32(rng) & pick(rng, 0xFF, 0x3F, 0x30, 0x10, 0xBF)
    w32(st, PLAYER + 0x1C, flags)
    w32(st, PLAYER + 0x20, rng.randrange(0, 0x40))
    w32(st, PLAYER + 0x24, rng.randrange(0, 0x20))
    w32(st, 0x4DA2A4, rng.randrange(w, 0x100))
    w32(st, 0x4DA2A0, rng.randrange(h, 0x80))
    pitch = pick(rng, 0x400, 0x200, rng.randrange(0x200, 0x400), 0x402)
    return pitch


def case_present(rng, stubs):
    st = {}
    pitch = blit_state(rng, st, stubs)
    fails = pick(rng, 0, 0, 1, 3)
    script = {"Lock": [0x8876021C] * fails + [0], "Unlock": [0x8876021C] * pick(rng, 0, 0, 2) + [0],
              "surface": [(OUT + rng.randrange(0, 0x40) * 4, pitch)] * (fails + 1)}
    return [], {UC_X86_REG_ECX: PLAYER}, st, script, (KEEP_C, False)


def case_blit(rng, stubs):
    st = {}
    pitch = blit_state(rng, st, stubs)
    return [pitch], {UC_X86_REG_ECX: PLAYER, UC_X86_REG_EDX: OUT + rng.randrange(0, 0x40) * 4}, st, {}, \
        (KEEP_C, False)


def case_blit_24(rng, stubs):
    args, regs, st, script, spec = case_blit(rng, stubs)
    w = struct.unpack("<h", st[PLAYER][0xC:0xE])[0]
    flags = struct.unpack("<I", st[PLAYER + 0x1C])[0]
    if flags & 0x30 == 0x10 and w & 7:  # the original never ends (both cut off)
        w32(st, PLAYER + 0x1C, flags & ~0x10)
    return args, regs, st, script, spec


def frame_state(rng, st, stubs):
    w, h = player_state(rng, st)
    com_state(st, stubs)
    tables_state(rng, st)
    st[FRAME_B - 0x4000] = rng.randbytes(0xC000)  # the previous frame with margins for motion
    st[FRAME_A] = rng.randbytes(w * h * 4)
    stream = frame_stream(rng, w, h)
    st[SRC] = stream
    return w, h


def case_frame_decode(rng, stubs):
    st = {}
    frame_state(rng, st, stubs)
    return [FRAME_A, FRAME_B], {UC_X86_REG_ECX: PLAYER, UC_X86_REG_EDX: SRC}, st, {}, (KEEP_C, False)


def case_frames_swap(rng, stubs):
    st = {}
    player_state(rng, st)
    w32(st, PLAYER + 0x4C, r32(rng))
    w32(st, PLAYER + 0x50, r32(rng))
    return [], {UC_X86_REG_ECX: PLAYER}, st, {}, (KEEP_C, True)


def mask_set(rng):
    return pick(rng, (0xF800, 0x7E0, 0x1F), (0x7C00, 0x3E0, 0x1F), (0xFF0000, 0xFF00, 0xFF), (0x1F, 0x7E0, 0xF800),
                tuple(r32(rng) | 1 << rng.randrange(32) for _ in range(3)))


def case_video_init(rng, stubs):
    st = {}
    player_state(rng, st, rng.randrange(-4, 64), rng.randrange(-4, 64))
    com_state(st, stubs)
    tables_state(rng, st)
    if rng.randrange(8) == 0:
        w32(st, PLAYER + 0x90, 0)  # no surface: the original calls through null's vtable (both fault)
    script = {"calloc": [FRAME_A, FRAME_B], "GetSurfaceDesc": [pick(rng, 0, 0, 0x887600FF)],
              "screen": [(rng.randrange(0, 2000), rng.randrange(0, 2000))], "masks": [mask_set(rng)]}
    return [], {UC_X86_REG_ECX: PLAYER}, st, script, (KEEP_C, False)


def case_video_free(rng, stubs):
    st = {}
    player_state(rng, st)
    w32(st, PLAYER + 0x58, pick(rng, 0, 1, r32(rng)))
    w32(st, PLAYER + 0x4C, pick(rng, 0, FRAME_A))
    w32(st, PLAYER + 0x50, pick(rng, 0, FRAME_B))
    return [], {UC_X86_REG_ECX: PLAYER}, st, {}, (KEEP_C, False)


def case_video_frame(rng, stubs):
    st = {}
    frame_state(rng, st, stubs)
    st[QUAL] = rng.randbytes(0x100)
    w32(st, PLAYER + 0x64, QUAL)
    w32(st, PLAYER + 0x344, rng.randrange(0, 0x100))
    rec = SRC + 0xE000
    st[rec] = struct.pack("<III", SRC, r32(rng), pick(rng, 3, 3, 3, 2, r32(rng)))
    return [], {UC_X86_REG_ECX: PLAYER, UC_X86_REG_EDX: rec}, st, {}, (KEEP_C, False)


def adpcm_state(rng, st):
    st[SRC] = rng.randbytes(0x100)
    pred, index = SRC + 0x200, SRC + 0x204
    return pred, index


def case_adpcm_mono(rng, stubs):
    st = {}
    pred, index = adpcm_state(rng, st)
    w32(st, pred, pick(rng, r32(rng), rng.randrange(-0x8000, 0x8000)))
    w32(st, index, pick(rng, rng.randrange(0, 0x1601), rng.randrange(0, 89) * 64, rng.randrange(0, 0x40)))
    args = [pick(rng, 0, 1, rng.randrange(0, 8)), SRC, OUT, pick(rng, rng.randrange(-2, 200), 0x80), pred, index]
    regs = {r: r32(rng) for r in REGS}
    return args, regs, st, {}, (KEEP_ALL, False)


def case_adpcm_stereo(rng, stubs):
    st = {}
    pred, index = adpcm_state(rng, st)
    w32(st, pred, r32(rng))
    w32(st, index, pick(rng, rng.randrange(0, 89) * 64 | rng.randrange(0, 89) * 64 << 16,
                        rng.randrange(0, 0x1601) | rng.randrange(0, 0x1601) << 16, r32(rng) & 0x0FFF0FFF,
                        rng.randrange(0, 0x40) | rng.randrange(0, 0x40) << 16))
    args = [r32(rng), SRC, OUT, pick(rng, rng.randrange(-2, 100), 0x40), pred, index]
    regs = {r: r32(rng) for r in REGS}
    return args, regs, st, {}, (KEEP_ALL, False)


def case_rle(rng, stubs):
    st = {}
    data = bytearray()
    while len(data) < 0x200:
        b = rng.getrandbits(8) if rng.randrange(3) else pick(rng, 0, 0xFF, 7)
        data.append(b)
        if rng.randrange(3) == 0:
            data.append(b)
            data.append(pick(rng, 0, 1, rng.getrandbits(8)))
    st[SRC] = bytes(data)
    return [pick(rng, -1, 0, 1, rng.randrange(1, 0x180))], {UC_X86_REG_ECX: SRC, UC_X86_REG_EDX: OUT}, st, {}, \
        (KEEP_C, True)


def case_block_coeffs(rng, stubs):
    st = {}
    tables_state(rng, st)
    bits = Bits()
    block_bits(rng, bits, overshoot=rng.randrange(10) == 0)
    data = bits.data() + rng.randbytes(8)
    st[SRC] = data[1:]
    st[0x4DA2A8] = rng.randbytes(0x100)
    w32(st, 0x6D3000, SRC)
    w32(st, 0x6D3004, data[0] | (rng.getrandbits(24) << 8 if rng.randrange(4) == 0 else 0))
    w32(st, 0x6D3008, 8)
    return [], {}, st, {}, ((), False)


def idct_input(rng):
    if rng.randrange(4) == 0:
        return [rng.randrange(-0x200000, 0x200000)] + [0] * 7
    scale = pick(rng, 0x100, 0x10000, 0x1000000, 0x40000000)
    return [rng.randrange(-scale, scale) if rng.randrange(3) else 0 for _ in range(8)]


def case_idct(rng, stubs):
    st = {}
    st[SRC] = struct.pack("<8i", *idct_input(rng))
    st[0x6D3010] = rng.randbytes(0x10)
    w32(st, 0x6D300C, OUT + rng.randrange(0, 0x40) * 4)
    return [], {UC_X86_REG_ESI: SRC}, st, {}, ((), False)


def case_row_to_pixels(rng, stubs):
    st = {}
    tables_state(rng, st)
    st[SRC] = rng.randbytes(0x40)
    st[SRC + 0x100] = rng.randbytes(0x120)
    return [], {UC_X86_REG_ESI: SRC, UC_X86_REG_EBX: SRC + 0x100, UC_X86_REG_EDI: OUT}, st, {}, ((), False)


def case_macroblock(rng, stubs):
    st = {}
    tables_state(rng, st)
    st[SRC] = macroblock_bytes(rng) + rng.randbytes(16)
    pitch = pick(rng, 0x40, 0x100, 0x400, rng.randrange(0x40, 0x800))
    regs = {r: r32(rng) for r in REGS}
    return [SRC, OUT + rng.randrange(0, 0x100), pitch], regs, st, {}, (KEEP_NOT_EAX, False)


TESTS = {
    "fmv_audio_close": (0x493A10, case_audio_close),
    "fmv_colour_tables_build": (0x493AA0, case_tables_build),
    "fmv_colour_pack": (0x493BD0, case_colour_pack),
    "fmv_block_fill": (0x493E80, case_block_fill),
    "fmv_present_16": (0x494040, case_present),
    "fmv_blit_16": (0x4940E0, case_blit),
    "fmv_present_24": (0x4943B0, case_present),
    "fmv_blit_24": (0x494450, case_blit_24),
    "fmv_present_32": (0x494740, case_present),
    "fmv_blit_32": (0x4947E0, case_blit),
    "fmv_frame_decode": (0x494B20, case_frame_decode),
    "fmv_frames_swap": (0x494D30, case_frames_swap),
    "fmv_video_init": (0x494D40, case_video_init),
    "fmv_video_free": (0x494ED0, case_video_free),
    "fmv_video_frame": (0x494F10, case_video_frame),
    "fmv_adpcm_mono": (0x495000, case_adpcm_mono),
    "fmv_adpcm_stereo": (0x4950A3, case_adpcm_stereo),
    "fmv_rle_unpack": (0x495220, case_rle),
    "fmv_block_coeffs": (0x4952C0, case_block_coeffs),
    "fmv_idct_rows": (0x49545B, case_idct),
    "fmv_idct_cols": (0x4955BE, case_idct),
    "fmv_row_to_pixels": (0x4956D2, case_row_to_pixels),
    "fmv_macroblock_decode": (0x495958, case_macroblock),
}


def run_case(m, stubs, base, name, address, builder, rng):
    args, regs_in, st, script, (keep, returns) = builder(rng, stubs)
    if name == "fmv_present_24":
        w = struct.unpack("<h", st[PLAYER][0xC:0xE])[0]
        flags = struct.unpack("<I", st[PLAYER + 0x1C])[0]
        if flags & 0x30 == 0x10 and w & 7:
            w32(st, PLAYER + 0x1C, flags & ~0x10)
    cw = pick(rng, 0x27F, 0x37F, 0x07F)
    data = bytearray(base)
    scratch = bytearray(SCRATCH_SIZE)
    for a, v in st.items():
        if DATA <= a < DATA + DATA_SIZE:
            data[a - DATA:a - DATA + len(v)] = v
        elif SCRATCH <= a < SCRATCH + SCRATCH_SIZE:
            scratch[a - SCRATCH:a - SCRATCH + len(v)] = v
    results = []
    for target in (address, m.symbols[name]):
        m.write(DATA, bytes(data))
        m.write(SCRATCH, bytes(scratch))
        m.write(E.STACK, STACK_FILL)
        for i, reg in enumerate(REGS):
            m.uc.reg_write(reg, regs_in.get(reg, 0x11111111 * (i + 1)))
        m.uc.reg_write(UC_X86_REG_FPCW, cw)
        m.uc.reg_write(UC_X86_REG_FPSW, 0)
        m.uc.reg_write(UC_X86_REG_FPTAG, 0xFFFF)
        stubs.reset(script)
        r = m.call(target, [a & 0xFFFFFFFF for a in args])
        if not returns and r[0] == "ok":
            r = ("ok",)
        regs = tuple(m.uc.reg_read(REGS[i]) for i in keep) + (m.uc.reg_read(UC_X86_REG_ESP),
                                                               m.uc.reg_read(UC_X86_REG_FPCW),
                                                               (m.uc.reg_read(UC_X86_REG_FPSW) >> 11) & 7,
                                                               m.uc.reg_read(UC_X86_REG_FPTAG))
        if r[0] != "ok":
            regs = ()  # stopped or faulted: the registers there differ
        mem = m.read(DATA, DATA_SIZE) + m.read(SCRATCH, SCRATCH_SIZE)
        results.append((r, regs, stubs.log, mem))
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


def func_sizes():
    sizes = {}
    for line in open("build/groups/pw_owned.txt"):
        if line.startswith("0x"):
            parts = line.split()
            sizes[int(parts[0], 16)] = int(parts[2])
    return sizes


def instruction_addresses(address, size):
    out = set()
    for line in open("build/rollcage.asm"):
        head = line.split(":", 1)[0].strip()
        try:
            a = int(head, 16)
        except ValueError:
            continue
        if address <= a < address + size and "\tnop" not in line and "int3" not in line:
            out.add(a)
    return out


def main():
    exe, elf = sys.argv[1], sys.argv[2]
    iterations = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 500
    only = [x for x in sys.argv[4:] if not x.startswith("-")]
    m = E.Machine(exe, elf)
    m.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    stubs = Stubs(m)
    base = m.read(DATA, DATA_SIZE)
    rng = random.Random(0x5057)
    failed = 0
    for name, (address, builder) in TESTS.items():
        if only and name not in only:
            continue
        if name not in m.symbols:
            print(f"{name}: not in {elf}")
            failed += 1
            continue
        bad = errors = 0
        seen = set()
        size = func_sizes().get(address, 0) if "-c" in sys.argv else 0
        if size:
            hook = m.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: seen.add(a), begin=address, end=address + size - 1)
        for _ in range(iterations):
            msg, r = run_case(m, stubs, base, name, address, builder, rng)
            errors += r[0] != "ok"
            if msg:
                if bad < 3:
                    print(msg)
                bad += 1
        note = f" ({errors} runs stopped or faulted the same way in both)" if errors else ""
        if size:
            m.uc.hook_del(hook)
            insns = instruction_addresses(address, size)
            missed = sorted(insns - seen)
            note += f" [coverage {len(insns) - len(missed)}/{len(insns)}" + (
                f", missed {[hex(a) for a in missed][:16]}" if missed else "") + "]"
        print(f"{name}: {'ok' if not bad else f'{bad} of {iterations} differ'}{note}", flush=True)
        failed += bad != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
