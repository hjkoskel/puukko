#!/usr/bin/env python3
"""drive the debug build through the monitor: drive.py lbl outdir script
script: comma list of  j<bits>  (tap joystick-2 bits), k<code> (tap key K(r,b)+1), w<sec>, s<name> (screenshot)"""
import sys, os, time
sys.path.insert(0, os.path.dirname(__file__))
import vmon
lbl, outdir, script = sys.argv[1:4]
A = {l.split()[-1][2:]: int(l.split()[1], 16) for l in open(lbl) if l.startswith('al')}
for step in script.split(','):
    c, v = step[0], step[1:]
    if c == 'j':
        vmon.run([f'f {A["dbg_joy"]:x} {A["dbg_joy"]:x} {int(v):02x}', 'x']); time.sleep(0.15)
        vmon.run([f'f {A["dbg_joy"]:x} {A["dbg_joy"]:x} 00', 'x']); time.sleep(0.15)
    elif c == 'k':
        vmon.run([f'f {A["dbg_key"]:x} {A["dbg_key"]:x} {int(v):02x}', 'x']); time.sleep(0.15)
    elif c == 'h':                       # h<bits>:<seconds> hold
        b, t = v.split(':')
        vmon.run([f'f {A["dbg_joy"]:x} {A["dbg_joy"]:x} {int(b):02x}', 'x']); time.sleep(float(t))
        vmon.run([f'f {A["dbg_joy"]:x} {A["dbg_joy"]:x} 00', 'x']); time.sleep(0.1)
    elif c == 'w':
        time.sleep(float(v))
    elif c == 's':
        vmon.run([f'screenshot "{outdir}/{v}.png" 2', 'x'])
