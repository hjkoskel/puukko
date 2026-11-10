#!/usr/bin/env python3
"""PUUKKO C64 – data generator.

Converts the browser game's audio into C64 data:
  * audio/*.wav curse voices  -> 4-bit packed digis for $D418 playback (NMI)
  * the JS chiptune note data -> SID event streams (2 voices)
  * the JS synth SFX          -> per-frame SID voice-3 tables

Usage: mkdata.py <audio_dir> <out.s> <out.h>
"""
import sys, wave, math
import numpy as np

PAL_CLOCK = 985248
DIGI_RATE = 6000                      # Hz, NMI timer rate
PAL_PERIOD = round(PAL_CLOCK / DIGI_RATE)
NTSC_PERIOD = round(1022727 / DIGI_RATE)
FPS = 50                              # PAL frame IRQ
HIDATA_SIZE = 0x09FA                  # $F600-$FFF9

# curse keys, in the order used by the C code (index = DIGI_xxx)
DIGIS = ['vittu', 'perkele', 'aargh', 'saatana', 'homo', 'eipas', 'perse',
         'paska', 'pillu', 'voi-paiva', 'kyrpa']


def sid_freq(hz):
    return max(0, min(0xFFFF, round(hz * 16777216 / PAL_CLOCK)))


def midi_hz(m, cents=0):
    return 440.0 * 2 ** ((m - 69 + cents / 100.0) / 12)


# ---------------------------------------------------------------- digis
def load_wav(path):
    w = wave.open(path)
    n = w.getnframes()
    sw = w.getsampwidth()
    raw = w.readframes(n)
    if sw == 1:
        d = np.frombuffer(raw, dtype=np.uint8).astype(np.float64) - 128.0
        d /= 128.0
    elif sw == 2:
        d = np.frombuffer(raw, dtype='<i2').astype(np.float64) / 32768.0
    else:
        raise SystemExit(f'{path}: unsupported sample width {sw}')
    ch = w.getnchannels()
    if ch > 1:
        d = d.reshape(-1, ch).mean(axis=1)
    return d, w.getframerate()


def resample(d, src, dst):
    if src == dst:
        return d
    # simple anti-alias: box filter when downsampling, then linear interp
    if dst < src:
        k = max(1, int(round(src / dst)))
        if k > 1:
            d = np.convolve(d, np.ones(k) / k, mode='same')
    n = int(len(d) * dst / src)
    x = np.arange(n) * (src / dst)
    return np.interp(x, np.arange(len(d)), d)


def digi(path):
    d, sr = load_wav(path)
    d = d - d.mean()
    d = resample(d, sr, DIGI_RATE)
    # trim leading/trailing silence
    a = np.abs(d)
    thr = a.max() * 0.06
    idx = np.nonzero(a > thr)[0]
    if len(idx):
        lo = max(0, idx[0] - int(0.01 * DIGI_RATE))
        hi = min(len(d), idx[-1] + int(0.02 * DIGI_RATE))
        d = d[lo:hi]
    # normalize + soft compression: 4-bit output wants it LOUD
    pk = np.abs(d).max() or 1.0
    d = d / pk
    d = np.tanh(d * 1.8) / np.tanh(1.8)
    q = np.clip(np.round(d * 7.5 + 7.5), 0, 15).astype(int)
    if len(q) & 1:
        q = np.append(q, 8)
    packed = (q[0::2] << 4) | q[1::2]
    return bytes(packed.tolist())


# ---------------------------------------------------------------- music
TRACKS = {
    'title': dict(bpm=76,
                  lead=[(64, 0, 8), (62, 8, 4), (59, 12, 4), (52, 16, 8), (55, 24, 4), (59, 28, 4),
                        (62, 32, 4), (64, 36, 8), (62, 44, 4), (59, 48, 8), (57, 56, 8), (52, 64, 8)],
                  bass=[(40, 0, 16), (40, 16, 16), (45, 32, 16), (47, 48, 16), (40, 64, 8)],
                  detune={}, loops=0),
    'fight': dict(bpm=148,
                  lead=[(64, 0, 4), (67, 4, 4), (69, 8, 4), (71, 12, 4), (69, 16, 4), (67, 20, 4), (64, 24, 4),
                        (62, 28, 4), (64, 32, 2), (64, 34, 2), (67, 36, 4), (69, 40, 4), (71, 44, 4), (74, 48, 8),
                        (71, 56, 4), (69, 60, 4), (67, 64, 4), (64, 68, 8), (62, 72, 4), (64, 76, 4), (67, 80, 4),
                        (64, 84, 4), (59, 88, 4), (62, 92, 4), (59, 96, 4), (55, 100, 4), (52, 104, 4),
                        (55, 108, 4), (59, 112, 8)],
                  bass=[(40, 0, 16), (40, 16, 16), (45, 32, 16), (47, 48, 16), (40, 64, 16), (40, 80, 8),
                        (38, 88, 16), (36, 104, 16)],
                  detune={67: 15, 62: -12, 55: 10}, loops=0),
    'victory': dict(bpm=132,
                    lead=[(76, 0, 3), (79, 3, 3), (83, 6, 6), (76, 12, 3), (79, 15, 3), (83, 18, 6),
                          (74, 24, 4), (76, 28, 12)],
                    bass=[(40, 0, 2), (40, 4, 2), (40, 8, 2), (40, 12, 2), (45, 16, 4), (47, 24, 4),
                          (40, 28, 12)],
                    detune={}, loops=2),
}


def voice_stream(notes, loop_len, f16, detune, transpose, loops):
    """events: (freq, frames); freq 0 = rest. Positions rounded cumulatively."""
    def fr(t16):
        return int(round(t16 * f16))
    notes = sorted(notes, key=lambda n: n[1])
    reps = loops if loops else 1
    ev = []
    for rep in range(reps):
        off = rep * loop_len
        t = off
        for i, (m, st, ln) in enumerate(notes):
            st += off
            if st > t:
                ev.append((0, fr(st) - fr(t)))
                t = st
            nxt = (notes[i + 1][1] + off) if i + 1 < len(notes) else off + loop_len
            hz = midi_hz(m + transpose, detune.get(m, 0) if transpose == 0 else 0)
            ev.append((sid_freq(hz), fr(nxt) - fr(st)))
            t = nxt
    out = []
    for f, d in ev:
        while d > 255:
            out.append((f, 255)); d -= 255; f = 0
        if d > 0:
            out.append((f, d))
    return out


# ---------------------------------------------------------------- sfx
TRI, SAW, PUL, NOI = 0x10, 0x20, 0x40, 0x80


def sweep(wave, f0, f1, n, expo=False):
    fr = []
    for i in range(n):
        t = i / max(1, n - 1)
        hz = f0 * (f1 / f0) ** t if expo else f0 + (f1 - f0) * t
        fr.append((wave | 1, sid_freq(hz)))
    return fr


def noise(reg_hi0, reg_hi1, n):
    fr = []
    for i in range(n):
        t = i / max(1, n - 1)
        fr.append((NOI | 1, int(reg_hi0 + (reg_hi1 - reg_hi0) * t) << 8))
    return fr


def rest(n):
    return [(0x00, 0)] * n


def build_sfx():
    S = {}
    S['stab'] = (0x00, 0xF3, noise(0x40, 0x20, 2) + sweep(PUL, 320, 70, 5, True))
    S['slash'] = (0x00, 0xF4, noise(0x70, 0x30, 7) + sweep(SAW, 500, 180, 3))
    import random
    rnd = random.Random(7)
    smash = [(NOI | 1, rnd.choice([0xF0, 0xC0, 0xE0, 0xA0, 0xD0]) << 8) for _ in range(6)]
    S['smash'] = (0x00, 0xF6, smash + noise(0x50, 0x30, 4))
    S['hit'] = (0x00, 0xF5, noise(0x12, 0x08, 2) + sweep(PUL, 140, 45, 5, True) + sweep(SAW, 130, 70, 8))
    S['block'] = (0x00, 0xF2, noise(0x20, 0x20, 1) + sweep(PUL, 160, 80, 4))
    S['drip'] = (0x00, 0xA1, sweep(TRI, 650, 220, 3, True))
    med = noise(0x60, 0x40, 5) + rest(2)
    for i in range(12):
        hz = 220 + 80 * i / 11 + 25 * math.sin(i * 2 * math.pi * 12 / 50)
        med.append((SAW | 1, sid_freq(hz)))
    S['medkit'] = (0x00, 0xE3, med)
    S['step'] = (0x00, 0x70, noise(0x0A, 0x06, 2))
    S['ko'] = (0x00, 0xF8, noise(0x10, 0x06, 6) + sweep(PUL, 160, 45, 40, True))
    S['blip'] = (0x00, 0xA0, sweep(PUL, 880, 880, 2))
    S['select'] = (0x00, 0xA0, sweep(PUL, 660, 990, 3))
    dr = []
    for _ in range(3):
        dr += sweep(TRI, 650, 300, 3) + rest(2)
    S['drink'] = (0x00, 0xC1, dr)
    tw = []
    for i in range(16):
        hz = (180 + 60 * i / 6) if i < 6 else (240 - 120 * (i - 6) / 9)
        hz += 30 * math.sin(i * 2 * math.pi * 20 / 50)
        tw.append((SAW | 1, sid_freq(hz)))
    S['taunt'] = (0x00, 0xE4, tw)
    return S


SFX_ORDER = ['stab', 'slash', 'smash', 'hit', 'block', 'drip', 'medkit', 'step', 'ko',
             'blip', 'select', 'drink', 'taunt']


def bytes_line(bs):
    return '\t.byte ' + ','.join(f'${b:02X}' for b in bs)


def main():
    audio_dir, out_s, out_h = sys.argv[1:4]
    S = []
    H = ['/* generated by tools/mkdata.py – do not edit */', '#ifndef GEN_DATA_H', '#define GEN_DATA_H', '']
    S.append('; generated by tools/mkdata.py – do not edit')
    S.append('.export _digi_lo, _digi_hi, _digi_elo, _digi_ehi, _digi_period_pal, _digi_period_ntsc')
    S.append('.export _mus_tab_lo, _mus_tab_hi, _sfx_tab_lo, _sfx_tab_hi')
    S.append('.segment "RODATA"')
    S.append(f'_digi_period_pal:  .word {PAL_PERIOD}')
    S.append(f'_digi_period_ntsc: .word {NTSC_PERIOD}')

    # digis: the largest set that fits goes to HIDATA (relocated under the
    # KERNAL at startup), the rest stays in RODATA
    total = 0
    datas = [digi(f'{audio_dir}/{k}.wav') for k in DIGIS]
    best, hi = 0, set()
    for mask in range(1 << len(DIGIS)):
        tot = sum(len(datas[i]) for i in range(len(DIGIS)) if mask >> i & 1)
        if best < tot <= HIDATA_SIZE:
            best, hi = tot, {i for i in range(len(DIGIS)) if mask >> i & 1}
    room = HIDATA_SIZE - best
    for i, k in enumerate(DIGIS):
        data = datas[i]
        total += len(data)
        S.append('.segment "HIDATA"' if i in hi else '.segment "RODATA"')
        S.append(f'digi_{i}:')
        for j in range(0, len(data), 16):
            S.append(bytes_line(data[j:j + 16]))
        S.append(f'digi_{i}_end:')
        H.append(f'#define DIGI_{k.upper().replace("-", "_")} {i}')
    S.append('.segment "RODATA"')
    print(f'digis in HIDATA: {HIDATA_SIZE - room} bytes', file=sys.stderr)
    S.append('_digi_lo:  .lobytes ' + ','.join(f'digi_{i}' for i in range(len(DIGIS))))
    S.append('_digi_hi:  .hibytes ' + ','.join(f'digi_{i}' for i in range(len(DIGIS))))
    S.append('_digi_elo: .lobytes ' + ','.join(f'digi_{i}_end' for i in range(len(DIGIS))))
    S.append('_digi_ehi: .hibytes ' + ','.join(f'digi_{i}_end' for i in range(len(DIGIS))))
    H.append(f'#define DIGI_COUNT {len(DIGIS)}')
    H.append('')
    print(f'digis: {total} bytes @ {DIGI_RATE} Hz (4-bit packed)', file=sys.stderr)

    # music: per track 2 streams (lead, bass): flo, fhi, dur ; dur 0 => end (flo 0 loop, 1 stop)
    mus_labels = []
    for ti, (name, tr) in enumerate(TRACKS.items()):
        f16 = FPS * 60 / (tr['bpm'] * 4)
        loop_len = max(n[1] + n[2] for n in tr['lead'] + tr['bass'])
        for vi, (key, tp) in enumerate((('lead', 0), ('bass', -12))):
            ev = voice_stream(tr[key], loop_len, f16, tr['detune'], tp, tr['loops'])
            lab = f'mus_{name}_{key}'
            mus_labels.append(lab)
            S.append(f'{lab}:')
            for f, d in ev:
                S.append(bytes_line([f & 0xFF, f >> 8, d]))
            S.append(bytes_line([1 if tr['loops'] else 0, 0, 0]))
        H.append(f'#define MUS_{name.upper()} {ti}')
    S.append('_mus_tab_lo: .lobytes ' + ','.join(mus_labels))
    S.append('_mus_tab_hi: .hibytes ' + ','.join(mus_labels))
    H.append('')

    # sfx: AD, SR, then frames (ctrl, flo, fhi), $FF terminator
    sfx = build_sfx()
    labs = []
    for i, k in enumerate(SFX_ORDER):
        ad, sr, frames = sfx[k]
        lab = f'sfx_{k}'
        labs.append(lab)
        S.append(f'{lab}:')
        S.append(bytes_line([ad, sr]))
        last_wave = TRI
        for c, f in frames:
            if c:
                last_wave = c & 0xF0
            S.append(bytes_line([c if c else last_wave, f & 0xFF, f >> 8]))
        S.append(bytes_line([last_wave, 0, 0]))          # gate off -> release
        S.append(bytes_line([0xFF]))
        H.append(f'#define SFX_{k.upper()} {i}')
    S.append('_sfx_tab_lo: .lobytes ' + ','.join(labs))
    S.append('_sfx_tab_hi: .hibytes ' + ','.join(labs))
    H += ['', '#endif', '']

    open(out_s, 'w').write('\n'.join(S) + '\n')
    open(out_h, 'w').write('\n'.join(H))


if __name__ == '__main__':
    main()
