#!/usr/bin/env python3
"""Reads profile.bin / profile.maps (native/profile.c, REROLL_PROFILE=1).

    tools/profile.py <game Direct3D folder> [--from FRAME] [--to FRAME] [--slow MS]

Prints where the main thread's time went: by function (original code named
from docs/functions.csv, ours from build/rollcage's symbols, libraries by
name), library time by the original function that called in, frame times,
and the slowest frames' own breakdown."""
import bisect, collections, csv, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RATE_MS = 0.25


def load_functions():
    starts, names = [], []
    with open(os.path.join(ROOT, 'docs', 'functions.csv')) as f:
        for row in csv.reader(f):
            try:
                a = int(row[0], 16)
            except (ValueError, IndexError):
                continue
            starts.append(a)
            names.append('exe:' + row[1])
    order = sorted(range(len(starts)), key=lambda i: starts[i])
    return [starts[i] for i in order], [names[i] for i in order]


def load_symbols():
    out = subprocess.run(['nm', '-n', '--defined-only', os.path.join(ROOT, 'build', 'rollcage')],
                         capture_output=True, text=True).stdout
    starts, names = [], []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'tTwW':
            starts.append(int(p[0], 16))
            names.append(p[2])
    return starts, names


def load_maps(path):
    maps = []
    for line in open(path):
        p = line.split()
        lo, hi = (int(x, 16) for x in p[0].split('-'))
        maps.append((lo, hi, os.path.basename(p[5]) if len(p) > 5 else '[anon]'))
    return maps


def main():
    args = sys.argv[1:]
    folder = args[0]
    lo_frame, hi_frame, slow_ms = 0, 1 << 32, 20.0
    if '--from' in args:
        lo_frame = int(args[args.index('--from') + 1])
    if '--to' in args:
        hi_frame = int(args[args.index('--to') + 1])
    if '--slow' in args:
        slow_ms = float(args[args.index('--slow') + 1])
    data = open(os.path.join(folder, 'profile.bin'), 'rb').read()
    samples = [s for s in struct.iter_unpack('<IIII', data) if lo_frame <= s[2] < hi_frame]
    fstarts, fnames = load_functions()
    sstarts, snames = load_symbols()
    maps = load_maps(os.path.join(folder, 'profile.maps'))
    cache = {}

    def name(a):
        if a in cache:
            return cache[a]
        if 0x400000 <= a < 0x4b0000:
            i = bisect.bisect_right(fstarts, a) - 1
            n = fnames[i] if i >= 0 else 'exe:?'
        elif sstarts and sstarts[0] <= a < 0x400000 or 0x8000000 <= a < 0x9000000:
            i = bisect.bisect_right(sstarts, a) - 1
            n = snames[i] if i >= 0 else '?'
        else:
            n = '?'
            for lo, hi, lib in maps:
                if lo <= a < hi:
                    n = lib
                    break
        cache[a] = n
        return n

    total = len(samples)
    print(f'{total} samples ({total * RATE_MS / 1000:.1f} s)')
    flat = collections.Counter(name(s[0]) for s in samples)
    print('\n-- by function (ms of the run, %)')
    for n, c in flat.most_common(45):
        print(f'{c * RATE_MS:9.1f} {100 * c / total:5.1f}%  {n}')
    lib = collections.Counter((name(s[0]), name(s[1])) for s in samples if s[1] and not (0x400000 <= s[0] < 0x4b0000))
    print('\n-- outside the exe, by the original function that called in')
    for (n, c_), c in lib.most_common(30):
        print(f'{c * RATE_MS:9.1f} {100 * c / total:5.1f}%  {n}  <-  {c_}')
    own = collections.Counter((name(s[0]), name(s[3])) for s in samples if s[3])
    print('\n-- libraries, by our function that called in')
    for (n, c_), c in own.most_common(30):
        print(f'{c * RATE_MS:9.1f} {100 * c / total:5.1f}%  {n}  <-  {c_}')
    frames = collections.Counter(s[2] for s in samples)
    if frames:
        times = sorted(c * RATE_MS for c in frames.values())
        print(f'\n-- {len(frames)} frames: median {times[len(times) // 2]:.2f} ms, 90% {times[len(times) * 9 // 10]:.2f}, '
              f'99% {times[len(times) * 99 // 100]:.2f}, max {times[-1]:.2f}')
        slow = [f for f, c in frames.items() if c * RATE_MS >= slow_ms]
        print(f'-- {len(slow)} frames of {slow_ms} ms or more; their time by function:')
        sl = set(slow)
        sc = collections.Counter(name(s[0]) for s in samples if s[2] in sl)
        st = sum(sc.values())
        for n, c in sc.most_common(25):
            print(f'{c * RATE_MS:9.1f} {100 * c / max(st, 1):5.1f}%  {n}')
        if slow:
            print('   slow frames:', ' '.join(f'{f}:{frames[f] * RATE_MS:.0f}' for f in sorted(slow)[:40]))


main()
