#!/usr/bin/env python3
"""Tiny helper to drive VICE's remote monitor (text protocol, port 6510).
usage: vmon.py 'cmd1' 'cmd2' ...   (each command's output is printed)"""
import socket, sys, time

def run(cmds, port=int(__import__('os').environ.get('VPORT', 6510)), wait=0.4):
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    s.settimeout(wait)
    out = []
    def drain():
        buf = b''
        while True:
            try:
                d = s.recv(65536)
                if not d: break
                buf += d
            except socket.timeout:
                break
        return buf.decode('latin1')
    drain()
    for c in cmds:
        s.sendall((c + '\n').encode())
        time.sleep(0.05)
        out.append(f'>> {c}\n' + drain())
    s.close()
    return '\n'.join(out)

if __name__ == '__main__':
    print(run(sys.argv[1:]))
