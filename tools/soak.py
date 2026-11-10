import sys, socket, time, os
sys.path.insert(0, os.path.dirname(__file__))
import vmon
lbl=sys.argv[1]; secs=int(sys.argv[2]); cond=sys.argv[3]
idle=[int(l.split()[1],16) for l in open(lbl) if l.split()[-1]=='._idle_t'][0]
s=socket.create_connection(('127.0.0.1',6510))
def rd(t=0.5):
    s.settimeout(t); b=b''
    while True:
        try:
            d=s.recv(65536)
            if not d: break
            b+=d
        except socket.timeout: break
    return b.decode('latin1')
rd()
for c in cond.split(';'):
    s.sendall((c+'\n').encode()); print(rd())
s.sendall(b'x\n'); rd()
t0=time.time(); n=0
while time.time()-t0<secs:
    out=rd(2.0)
    if 'Stop on' in out or 'JAM' in out.upper():
        print(out); s.sendall(b'r\n'); print(rd()); s.sendall(b'bt\n'); print(rd()); s.sendall(b'm 01c0 01ff\n'); print(rd()); break
    # keep the attract loop going: poke idle timer each ~10s
    n+=1
    if n%5==0:
        s.sendall(f'f {idle:x} {idle+1:x} e0 03\n'.encode()); rd(); s.sendall(b'x\n'); rd()
print('end', time.time()-t0)
