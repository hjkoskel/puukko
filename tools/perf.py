#!/usr/bin/env python3
"""loops per second of the main loop (should be 50 on PAL)"""
import sys, os, re, time
sys.path.insert(0, os.path.dirname(__file__))
import vmon
lbl = sys.argv[1]
a = [int(l.split()[1], 16) for l in open(lbl) if l.split()[-1] == '._loop_cnt'][0]
def sample():
    out = vmon.run(['r', f'm {a:x} {a+1:x}', 'x'])
    cyc = int(re.search(r'\.;[0-9a-f]{4} .* (\d+)\s*$', out, re.M).group(1))
    m = re.search(r'>C:[0-9a-f]{4}\s+([0-9a-f]{2}) ([0-9a-f]{2})', out)
    return cyc, int(m.group(1), 16) | int(m.group(2), 16) << 8
c0, l0 = sample(); time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 4); c1, l1 = sample()
sec = (c1 - c0) / 985248
print(f'{(l1 - l0) & 0xffff} loops in {sec:.2f}s = {((l1 - l0) & 0xffff) / sec:.1f} fps')
