#!/usr/bin/env python3
"""Print the first 8 bytes at each address of Rollcage.exe as a C initializer,
for the `expect` field of a hook.

    tools/expect.py EXE 0x461410 0x4291a0
"""
import struct
import sys


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    table = pe + 24 + opt_size
    for i in range(count):
        vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, table + i * 40 + 8)
        yield base + va, max(vsize, raw_size), raw_ptr


def read(data, address, n):
    for va, size, raw in sections(data):
        if va <= address < va + size:
            off = raw + address - va
            return data[off:off + n]
    raise SystemExit(f"{address:#x} is not in any section")


def main():
    data = open(sys.argv[1], "rb").read()
    for arg in sys.argv[2:]:
        b = read(data, int(arg, 0), 8)
        print(f"{arg}: {{" + ", ".join(f"0x{x:02x}" for x in b) + "}")


if __name__ == "__main__":
    main()
