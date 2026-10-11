#!/usr/bin/env python3
"""Publishes a saved capture to /dev/shm/reroll_live in the live layout
(src/capture.c), so the viewer tools/rview.c can be tried without the game.

    tools/rlive_feed.py FILE.rcap [SECONDS]
"""
import mmap
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import rcap  # noqa: E402

TEX_STRIDE = 0x40 + 256 * 256 * 2
FRAME_SIZE = 0x1000000
TEX_OFFSET = 0x1000
FRAME_OFFSET = TEX_OFFSET + 64 * TEX_STRIDE
SIZE = FRAME_OFFSET + 3 * FRAME_SIZE


def main():
    data = open(sys.argv[1], "rb").read()
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
    _, _, _, textures = rcap.load(sys.argv[1])
    # the frame without its texture chunks
    out, pos = bytearray(), 0
    while pos + 8 <= len(data):
        tag, length = struct.unpack_from("<4sI", data, pos)
        if tag != b"TEXS":
            out += data[pos:pos + 8 + length]
        pos += 8 + length
    fd = os.open("/dev/shm/reroll_live", os.O_RDWR | os.O_CREAT, 0o644)
    os.ftruncate(fd, SIZE)
    m = mmap.mmap(fd, SIZE)
    for head, pixels in textures:
        slot, w, h, pitch = head[0], head[2], head[3], head[4]
        base = TEX_OFFSET + slot * TEX_STRIDE
        compact = b"".join(pixels[y * pitch:y * pitch + w * 2] for y in range(h))
        m[base + 4:base + 4 + 48] = struct.pack("<12I", *(head[:4] + (w * 2,) + head[5:]))
        m[base + 0x40:base + 0x40 + len(compact)] = compact
        m[base:base + 4] = struct.pack("<I", 1)
    m[FRAME_OFFSET:FRAME_OFFSET + len(out)] = out
    header = struct.pack("<5I3I3I3I", 0x56494C52, 1, 0, 3, 3, len(out), 0, 0,
                         FRAME_OFFSET, FRAME_OFFSET + FRAME_SIZE, FRAME_OFFSET + 2 * FRAME_SIZE,
                         TEX_OFFSET, TEX_STRIDE, FRAME_SIZE)
    m[0:len(header)] = header
    m[0x38:0x3c] = struct.pack("<I", os.getpid())
    end, seq = time.time() + seconds, 0
    while time.time() < end:  # republish the same frame 30 times a second
        seq += 1
        m[12:16] = struct.pack("<I", 0)  # latest slot
        m[8:12] = struct.pack("<I", seq)
        time.sleep(1 / 30)


if __name__ == "__main__":
    main()
