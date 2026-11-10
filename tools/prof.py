#!/usr/bin/env python3
"""read prof_acc over an interval: average cycles per loop for each section"""
import sys, os, re, time
sys.path.insert(0, os.path.dirname(__file__))
import vmon
lbl = sys.argv[1]
def addr(n): return [int(l.split()[1], 16) for l in open(lbl) if l.split()[-1] == '.' + n][0]
pa, la = addr('_prof_acc'), addr('_loop_cnt')
NAMES = {0: 'input', 1: 'spr_vsync', 2: 'pre-fight(digi q, keys)', 3: 'update first', 4: 'visuals first',
         5: 'update second', 6: 'hits+pickup', 7: 'ko/intro text', 8: 'blood', 9: 'timer+visuals second',
         14: 'irq/wake', 15: 'idle wait'}
def snap():
    out = vmon.run([f'm {pa:x} {pa+63:x}', f'm {la:x} {la+1:x}', 'x'])
    bs = []
    for line in out.splitlines():
        m = re.search(r'>C:[0-9a-f]{4}\s+((?:[0-9a-f]{2}\s+)+)', line)
        if m: bs += [int(x, 16) for x in m.group(1).split()]
    acc = [int.from_bytes(bytes(bs[4*i:4*i+4]), 'little') for i in range(16)]
    return acc, bs[64] | bs[65] << 8
a0, l0 = snap(); time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 3); a1, l1 = snap()
n = (l1 - l0) & 0xffff
print(f'loops: {n}')
tot = 0
for i in range(16):
    d = (a1[i] - a0[i]) & 0xffffffff
    if d:
        print(f'{i:2d} {NAMES.get(i, "?"):28s} {d / n:8.0f} cyc/loop')
        tot += d
print(f'   total {tot / n:8.0f} cyc/loop (PAL frame = 19656)')
