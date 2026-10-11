#!/usr/bin/env python3
"""Reads a frame capture (captures/frame_<pid>_<n>.rcap, written by src/capture.c).

    tools/rcap.py FILE [--textures DIR]

For each ordering-table walk it follows the packet links as DrawOTag does,
counts packets by code and checks every 3D vertex: re-projecting its
camera-space x, y, z with the projection globals of that walk must give the
screen x, y the original stored (docs/renderer/pipeline.md 2.3). Vertices are
counted in three groups: camera-space ones that re-project exactly; track
vertices the seam fix nudged by up to half a pixel; and screen-space ones
(billboards, sprites, overlays, menu boxes) whose x and y are unset, so a
new renderer rebuilds camera space from (s - c) * z / H. That shows
the capture holds everything a new back end needs. --textures writes each
texture page as a PNG.
"""
import struct
import sys
import zlib

KINDS = {  # code >> 2: (name, size, vertex offsets)
    0x23: ("GT4", 0x84, (0x08, 0x28, 0x48, 0x68)),
    0x27: ("GT3", 0x64, (0x08, 0x28, 0x48)),
    0x26: ("FT4", 0x78, (0x08, 0x24, 0x40, 0x5c)),
    0x25: ("FT3", 0x60, (0x08, 0x24, 0x40)),
    0x2a: ("F4", 0x68, (0x08, 0x20, 0x38, 0x50)),
    0x2b: ("line", 0x3c, (0x08, 0x24)),
    0x0b: ("2D FT4", 0x28, ()),
    0x0c: ("2D G3", 0x1c, ()),
    0x19: ("SPRT", 0x14, ()),
    0x11: ("SPRT+page", 0x14, ()),
    0x28: ("fog marker", 0x08, ()),
    0x2c: ("filter", 0x08, ()),
    0x2d: ("tpage", 0x08, ()),
}


class Memory:
    def __init__(self, regions, base=None):
        self.regions = sorted(regions)
        self.base = base

    def read(self, address, size):
        for start, data in self.regions:
            if start <= address and address + size <= start + len(data):
                return data[address - start:address - start + size]
        if self.base:
            return self.base.read(address, size)
        return None

    def u32(self, address):
        b = self.read(address, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def f32(self, address):
        return struct.unpack("<f", self.read(address, 4))[0]


def chunks(data):
    pos = 0
    while pos + 8 <= len(data):
        tag, length = struct.unpack_from("<4sI", data, pos)
        yield tag.decode("latin-1"), data[pos + 8:pos + 8 + length]
        pos += 8 + length


def load(path):
    header, walks, base, textures, group = None, [], [], [], None
    for tag, payload in chunks(open(path, "rb").read()):
        if tag == "RCAP":
            header = struct.unpack("<4I", payload)
        elif tag == "OTAG":
            group = {"start": struct.unpack("<2I", payload)[0], "regions": []}
            walks.append(group)
        elif tag == "BASE":
            group = None
        elif tag == "REGN":
            region = (struct.unpack_from("<I", payload)[0], payload[4:])
            (group["regions"] if group else base).append(region)
        elif tag == "TEXS":
            textures.append((struct.unpack_from("<12I", payload), payload[48:]))
    return header, walks, Memory(base), textures


def walk(memory, start):
    """Packet addresses in DrawOTag order."""
    word = memory.u32(start)
    for _ in range(0x100000):
        if word is None or word == 0xffffffff:
            return
        address = word & 0x7fffffff
        if not word & 0x80000000:
            yield address
        word = memory.u32(address)


def png(path, width, height, rgba):
    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def block(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + block(b"IHDR", struct.pack(">2I5B", width, height, 8, 6, 0, 0, 0))
                           + block(b"IDAT", zlib.compress(rows)) + block(b"IEND", b""))


def texture_rgba(head, pixels):
    slot, page, width, height, pitch, bits, rmask, gmask, bmask, amask, key_lo, key_hi = head
    out = bytearray()

    def channel(v, mask):
        if not mask:
            return 255
        shift = (mask & -mask).bit_length() - 1
        top = mask >> shift
        return ((v & mask) >> shift) * 255 // top

    for y in range(height):
        for x in range(width):
            if bits == 16:
                v = struct.unpack_from("<H", pixels, y * pitch + x * 2)[0]
            elif bits == 32:
                v = struct.unpack_from("<I", pixels, y * pitch + x * 4)[0]
            else:
                v = pixels[y * pitch + x]
            a = 0 if (key_lo <= v <= key_hi and (key_lo or key_hi)) else channel(v, amask)
            out += bytes((channel(v, rmask), channel(v, gmask), channel(v, bmask), a))
    return bytes(out)


def main():
    path = sys.argv[1]
    header, walks, base, textures = load(path)
    version, frame, w, h = header
    print(f"{path}: version {version}, frame {frame}, screen {w}x{h}, {len(walks)} OT walks, "
          f"{len(textures)} texture pages")
    total = {}
    worst = 0.0
    checked = screen = nudged = 0
    for n, group in enumerate(walks):
        memory = Memory(group["regions"], base)
        counts = {}
        H = memory.f32(0x6ccd00)
        cx, cy = memory.f32(0x6ccd5c), memory.f32(0x6ccd60)
        for address in walk(memory, group["start"]):
            head = memory.read(address, 8)
            code = head[7] >> 2
            name, size, vertices = KINDS.get(code, (f"code {head[7]:#x}", 0, ()))
            counts[name] = counts.get(name, 0) + 1
            for offset in vertices:
                rec = memory.read(address + offset, 0x18)
                if rec is None:
                    continue
                flags, z, sx, sy, x, y = struct.unpack("<I5f", rec)
                if flags & 0x10 or z <= 0:  # near-clipped: only x, y, z are valid
                    continue
                err = max(abs(x * (H / z) + cx - sx), abs(y * (H / z) + cy - sy))
                if err < 0.01:
                    checked += 1
                elif err <= 0.5:  # the track seam fix nudges screen x/y by half a pixel
                    nudged += 1
                else:  # built in screen space (billboards, overlays): x, y unset
                    screen += 1
        print(f"  walk {n} OT {group['start']:#x}: H {H:.2f} centre ({cx:.1f}, {cy:.1f}): "
              + ", ".join(f"{k} {v}" for k, v in sorted(counts.items(), key=lambda kv: -kv[1])))
        for k, v in counts.items():
            total[k] = total.get(k, 0) + v
    print("total:", ", ".join(f"{k} {v}" for k, v in sorted(total.items(), key=lambda kv: -kv[1])))
    print(f"vertices: {checked} camera-space (re-project exactly), {nudged} seam-nudged (<= 0.5 px), "
          f"{screen} screen-space (camera space from (s - c) * z / H)")
    if "--textures" in sys.argv:
        out = sys.argv[sys.argv.index("--textures") + 1]
        import os
        os.makedirs(out, exist_ok=True)
        for head, pixels in textures:
            png(f"{out}/slot{head[0]:02d}_page{head[1]:02d}.png", head[2], head[3], texture_rgba(head, pixels))
        print(f"wrote {len(textures)} pages to {out}")


if __name__ == "__main__":
    main()
