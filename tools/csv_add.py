#!/usr/bin/env python3
"""Add integrated groups to docs/functions.csv.

    tools/csv_add.py PREFIX GROUP...

For each hook in build/groups/<g>_hooks.txt, the row is
address,name,verified,"src/game/<prefix>_<g>.c. <the function's comment>.
Emulator fuzz" (the comment is the one starting /* 0xADDR in that file).
Rows already present for an address are replaced."""
import csv, io, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    prefix, groups = sys.argv[1], sys.argv[2:]
    path = os.path.join(ROOT, 'docs', 'functions.csv')
    rows = list(csv.reader(open(path)))
    index = {}
    for i, r in enumerate(rows):
        if r and r[0].startswith('0x'):
            index[int(r[0], 16)] = i
    added = 0
    for g in groups:
        src_rel = f'src/game/{prefix}_{g}.c'
        src = open(os.path.join(ROOT, src_rel)).read()
        for line in open(os.path.join(ROOT, 'build', 'groups', f'{g}_hooks.txt')):
            if line.startswith('---'):
                break
            m = re.search(r'\{"(\w+)",\s*(0x[0-9a-f]+)', line)
            if not m:
                continue
            name, addr = m.group(1), int(m.group(2), 16)
            c = re.search(r'/\*\s*0x0*%x\b[^:]*:?\s*(.*?)\*/' % addr, src, re.S)
            desc = re.sub(r'\s+', ' ', re.sub(r'\n\s*\*\s?', ' ', c.group(1))).strip() if c else ''
            row = [f'0x{addr:06x}', name, 'verified', f'{src_rel}. {desc} Emulator fuzz'.replace('  ', ' ')]
            if addr in index:
                rows[index[addr]] = row
            else:
                rows.append(row)
                index[addr] = len(rows) - 1
            added += 1
    out = io.StringIO()
    csv.writer(out, lineterminator='\n').writerows(rows)
    open(path, 'w').write(out.getvalue())
    print(f'{added} rows')


main()
