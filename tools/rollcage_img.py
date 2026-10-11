#!/usr/bin/env python3
"""Read Rollcage (1999, PC) IDXData archives: Rollcage.idx + ROLLCAGE.IMG.

Rollcage.idx is a table of 16-byte records:
    u32 offset, u32 stored_size, u32 unpacked_size, s16 margin, s16 link
Entries are stored back to back in ROLLCAGE.IMG. An entry whose stored size
differs from its unpacked size is GT20-compressed (see gt20_decompress).
The game decompresses in place (loader at 0x496590): it allocates
margin + unpacked + 7 bytes and reads the blob into the 4-byte aligned tail,
so margin is the compressor's safety gap between output and unread input.
The GT20 header repeats it at offset 8. link is used by the loader's
seek-order search and is 0 in the shipped index.

Usage:
    rollcage_img.py list    <IDXData dir>
    rollcage_img.py extract <IDXData dir> <out dir> [entry ...]
    rollcage_img.py bt6     <file.BT6> <out dir>     (texture pages to PNG)
    rollcage_img.py hashes  <IDXData dir>            (FNV-1a per entry, for tests)
"""
import os
import struct
import sys


def read_names(exe_path):
    """Entry names from the game's lookup table at 0x4ad000 (count at 0x4adaa0).

    Records are (char *name, s16 entry). The lookup at 0x43b5a0 treats "__" as
    "/" and "_" as ".", so LOW__LEV1_BLP is LOW/LEV1.BLP.
    """
    exe = open(exe_path, "rb").read()

    def fo(va):  # .rdata/.data are mapped at file offset VA - image base
        return va - 0x400000

    count = struct.unpack_from("<I", exe, fo(0x4ADAA0))[0]
    names = {}
    for i in range(count):
        ptr, entry = struct.unpack_from("<Ih", exe, fo(0x4AD000) + i * 8)
        raw = exe[fo(ptr):exe.index(b"\0", fo(ptr))].decode("latin-1")
        path, _, ext = raw.replace("__", "/").rpartition("_")
        names[entry] = f"{path}.{ext}" if path else raw
    return names


def read_index(idx_path):
    data = open(idx_path, "rb").read()
    return [struct.unpack_from("<4I", data, i) for i in range(0, len(data), 16)]


def gt20_decompress(blob, expected_size=None):
    """Decompress one GT20 blob (header included).

    Port of the game's decoder at 0x497860 in Direct3D/Rollcage.exe.
    LZ77 with 32-bit little-endian flag words consumed LSB first:
      0           literal byte
      1 0 d A B   short match: distance 256-d (1..256), length 2 + 2*A + B
      1 1 w ...   long match: distance 0x2000-(w>>3); length (w&7)+2, or if
                  w&7 == 0 an extra byte e: bit 7 adds 0x2000 to the distance,
                  e&0x7f == 1 ends the stream, == 0 reads a u16 length,
                  otherwise length is (e&0x7f)+2
    """
    if blob[:4] != b"GT20":
        raise ValueError("not a GT20 blob")
    usz, _kind, skip = struct.unpack_from("<III", blob, 4)
    src = 16 + skip
    out = bytearray()
    flags = struct.unpack_from("<I", blob, src)[0]
    src += 4
    left = 32

    def next_bit():
        # Consume the current flag bit; refill from the stream when exhausted.
        nonlocal flags, left, src
        flags >>= 1
        left -= 1
        if left == 0:
            flags = struct.unpack_from("<I", blob, src)[0]
            src += 4
            left = 32

    while True:
        if not flags & 1:
            out.append(blob[src])
            src += 1
            next_bit()
            continue
        next_bit()
        if flags & 1:
            w = struct.unpack_from("<H", blob, src)[0]
            src += 2
            dist = 0x2000 - (w >> 3)
            length = w & 7
            if length:
                length += 2
            else:
                e = blob[src]
                src += 1
                if e & 0x80:
                    dist += 0x2000
                e &= 0x7F
                if e == 1:
                    break
                if e == 0:
                    length = struct.unpack_from("<H", blob, src)[0]
                    src += 2
                else:
                    length = e + 2
            next_bit()
        else:
            dist = 256 - blob[src]
            src += 1
            next_bit()
            length = 2
            if flags & 1:
                length += 2
            next_bit()
            if flags & 1:
                length += 1
            next_bit()
        start = len(out) - dist
        if start < 0:
            raise ValueError(f"back-reference before start of output at {len(out)}")
        # Byte-wise copy: overlapping matches repeat recent output.
        for i in range(length):
            out.append(out[start + i])

    if expected_size is not None and len(out) != expected_size:
        raise ValueError(f"unpacked {len(out)} bytes, expected {expected_size}")
    if len(out) != usz:
        raise ValueError(f"unpacked {len(out)} bytes, header says {usz}")
    return bytes(out)


def read_entry(img, rec):
    off, stored, unpacked, _type = rec
    img.seek(off)
    blob = img.read(stored)
    if stored == unpacked:
        return blob
    return gt20_decompress(blob, unpacked)


def bt6_pages(data):
    """Texture pages from a .BT6 file as a list of 256x256 RGBA numpy arrays.

    BT6 header (little endian): "BTP " magic, u32 at 0x24 = pixel data offset,
    u32 at 0x34 = page count. Pixels are 16-bit ARGB1555, 256x256 per page.
    """
    import numpy as np

    start = struct.unpack_from("<I", data, 0x24)[0]
    count = struct.unpack_from("<I", data, 0x34)[0]
    px = np.frombuffer(data, dtype="<u2", count=count * 256 * 256, offset=start)
    px = px.reshape(count, 256, 256)
    channels = [((px >> shift) & 31) * 255 // 31 for shift in (10, 5, 0)]
    alpha = np.where(px & 0x8000, 255, 0)
    rgba = np.stack(channels + [alpha], axis=-1).astype(np.uint8)
    return list(rgba)


def guess_ext(data):
    magic = data[:4]
    if magic[:2] == b"BM":
        return ".bmp"
    if magic.isalnum() or magic == b"BTP ":
        return "." + magic.decode("ascii").strip().lower()
    if b"Sound Effect File" in data[:64]:
        return ".sfx"
    return ".bin"


def export_bt6(bt6_path, out_dir):
    from PIL import Image

    os.makedirs(out_dir, exist_ok=True)
    pages = bt6_pages(open(bt6_path, "rb").read())
    for i, page in enumerate(pages):
        Image.fromarray(page, "RGBA").save(os.path.join(out_dir, f"page{i:02d}.png"))
    print(f"{bt6_path}: {len(pages)} pages -> {out_dir}")


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def main(argv):
    if len(argv) == 4 and argv[1] == "bt6":
        export_bt6(argv[2], argv[3])
        return
    if len(argv) == 3 and argv[1] == "hashes":
        # "<entry> <fnv1a32>" per entry, matching tests/test_gt20.c
        recs = read_index(os.path.join(argv[2], "Rollcage.idx"))
        with open(os.path.join(argv[2], "ROLLCAGE.IMG"), "rb") as img:
            for i, rec in enumerate(recs):
                print(f"{i} {fnv1a(read_entry(img, rec)):08x}")
        return
    if len(argv) < 3 or argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    idx_dir = argv[2]
    recs = read_index(os.path.join(idx_dir, "Rollcage.idx"))
    # The installed game keeps the executable next to IDXData
    exe_path = os.path.join(idx_dir, os.pardir, "Direct3D", "Rollcage.exe")
    names = read_names(exe_path) if os.path.exists(exe_path) else {}
    if argv[1] == "list":
        for i, (off, stored, unpacked, margin) in enumerate(recs):
            print(f"{i:4d} margin={margin & 0xFFFF} offset={off:9d} stored={stored:9d} "
                  f"unpacked={unpacked:9d} {names.get(i, '')}")
        return
    out_dir = argv[3]
    wanted = [int(a) for a in argv[4:]] or range(len(recs))
    with open(os.path.join(idx_dir, "ROLLCAGE.IMG"), "rb") as img:
        for i in wanted:
            data = read_entry(img, recs[i])
            name = names.get(i) or f"UNNAMED/{i:03d}{guess_ext(data)}"
            path = os.path.join(out_dir, *name.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            print(f"{i:4d} {name} {len(data)}")


if __name__ == "__main__":
    main(sys.argv)
