#!/usr/bin/env python3
"""Compares renders of captures with the game's own screenshots.

    tools/rcapcompare.py ORIGINAL.png RENDER.png [SIDE_BY_SIDE.png]

Prints the mean absolute difference per channel (0..255) and the share of
pixels differing by more than 24 in any channel, and optionally writes the
original, the render and a difference map (x4) side by side.
"""
import struct
import sys
import zlib

import numpy as np


def read_png(path):
    data = open(path, "rb").read()
    pos, chunks = 8, []
    width = height = 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour == 6, "RGBA8 PNGs only"
        elif kind == b"IDAT":
            chunks.append(body)
        pos += 12 + length
    raw = zlib.decompress(b"".join(chunks))
    stride = width * 4
    out = np.zeros((height, stride), np.uint8)
    prev = np.zeros(stride, np.int32)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = np.frombuffer(raw, np.uint8, stride, y * (stride + 1) + 1).astype(np.int32)
        if f == 0:
            cur = line
        elif f == 2:
            cur = (line + prev) & 255
        else:
            cur = np.zeros(stride, np.int32)
            for x in range(stride):
                a = cur[x - 4] if x >= 4 else 0
                b = prev[x]
                c = prev[x - 4] if x >= 4 else 0
                if f == 1:
                    p = a
                elif f == 3:
                    p = (a + b) // 2
                else:
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    p = a if pa <= pb and pa <= pc else b if pb <= pc else c
                cur[x] = (line[x] + p) & 255
        out[y] = cur
        prev = cur
    return out.reshape(height, width, 4)


def write_png(path, image):
    h, w, _ = image.shape
    raw = b"".join(b"\0" + image[y].tobytes() for y in range(h))

    def block(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + block(b"IHDR", struct.pack(">2I5B", w, h, 8, 6, 0, 0, 0))
                           + block(b"IDAT", zlib.compress(raw, 6)) + block(b"IEND", b""))


def main():
    a = read_png(sys.argv[1])[:, :, :3].astype(np.int32)
    b = read_png(sys.argv[2])[:, :, :3].astype(np.int32)
    diff = np.abs(a - b)
    mean = diff.reshape(-1, 3).mean(axis=0)
    share = (diff.max(axis=2) > 24).mean() * 100
    print(f"{sys.argv[2]}: mean difference r {mean[0]:.1f} g {mean[1]:.1f} b {mean[2]:.1f}; "
          f"{share:.1f}% of pixels differ by more than 24")
    if len(sys.argv) > 3:
        d = np.clip(diff * 4, 0, 255)
        row = np.concatenate([a, b, d], axis=1).astype(np.uint8)
        alpha = np.full(row.shape[:2] + (1,), 255, np.uint8)
        write_png(sys.argv[3], np.concatenate([row, alpha], axis=2))


if __name__ == "__main__":
    main()
