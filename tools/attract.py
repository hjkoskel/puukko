#!/usr/bin/env python3
"""poke the title idle counter so the attract-mode fight starts now"""
import sys, subprocess, os
lbl = sys.argv[1]
a = None
for l in open(lbl):
    p = l.split()
    if p[-1] == '._idle_t':
        a = int(p[1], 16)
sys.path.insert(0, os.path.dirname(__file__))
import vmon
print(vmon.run([f'f {a:x} {a+1:x} e0 03', 'x']))
