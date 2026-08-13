"""tools/di_level_check.py - check a recorded guitar-DI WAV for amp-sim testing.

Reads a mono/stereo WAV (PCM16 or float32), reports levels and gives
amp-sim-friendly advice. Target for a recorded DI take:

  peak   : -12 .. -6 dBFS   (never above -3)
  rms    : -20 .. -16 dBFS  (normal playing)
  clipping samples : 0

Usage:
  python tools/di_level_check.py <in.wav>
"""
import sys, struct, math, wave

def load(path):
    with wave.open(path, 'rb') as w:
        ch = w.getnchannels(); sr = w.getframerate(); sw = w.getsampwidth()
        n = w.getnframes(); raw = w.readframes(n)
        tag = w.getcomptype()
    if sw == 2:
        x = struct.unpack(f'<{n*ch}h', raw)
        x = [v / 32768.0 for v in x]
    elif sw == 4:
        # assume float32
        x = struct.unpack(f'<{n*ch}f', raw)
    elif sw == 3:
        # 24-bit int
        x = []
        for i in range(0, len(raw), 3 * ch):
            for c in range(ch):
                b = raw[i+c*3:i+c*3+3]
                v = int.from_bytes(b, 'little', signed=True)
                x.append(v / 8388608.0)
    else:
        raise SystemExit(f'unsupported sample width {sw*8} bit')
    return sr, ch, x

def db(v):
    if v <= 0: return -120.0
    return 20.0 * math.log10(v)

def main():
    if len(sys.argv) < 2:
        print(__doc__); return 1
    sr, ch, x = load(sys.argv[1])
    n = len(x) // ch
    # mono mix
    if ch == 2:
        m = [ (x[i*2] + x[i*2+1]) * 0.5 for i in range(n) ]
    else:
        m = x[:n]
    peak = max(abs(v) for v in m)
    rms = math.sqrt(sum(v*v for v in m) / n)
    clip = sum(1 for v in m if abs(v) >= 0.999)
    # per-0.5s RMS envelope
    ws = int(sr * 0.5)
    env = []
    for i in range(0, n, ws):
        seg = m[i:i+ws]
        if len(seg) == 0: break
        env.append(math.sqrt(sum(v*v for v in seg) / len(seg)))
    rms_max = max(env) if env else 0.0
    rms_min = min(e for e in env if e > 0) if any(e > 0 for e in env) else 0.0
    peak_db = db(peak); rms_db = db(rms); rmsmax_db = db(rms_max); rmsmin_db = db(rms_min)
    print(f'file      : {sys.argv[1]}')
    print(f'format    : {sr} Hz, {ch} ch, {n/sr:.1f} s')
    print(f'peak      : {peak_db:7.1f} dBFS')
    print(f'rms (all) : {rms_db:7.1f} dBFS')
    print(f'rms max   : {rmsmax_db:7.1f} dBFS   (loudest 0.5s block)')
    print(f'rms min   : {rmsmin_db:7.1f} dBFS   (quietest non-silent 0.5s block)')
    print(f'clip samples: {clip}')
    ok = True
    if clip > 0:
        print('WARN: clipping detected - reduce input gain and re-record.')
        ok = False
    if peak_db > -3:
        print('WARN: peak above -3 dBFS - too hot, reduce input gain.')
        ok = False
    if peak_db < -18:
        print('WARN: peak below -18 dBFS - quite low; raise input gain (still avoid clipping).')
        ok = False
    elif -12 <= peak_db <= -6:
        print('OK  : peak in the -12..-6 dBFS sweet spot.')
    if -20 <= rmsmax_db <= -16:
        print('OK  : loudest block RMS in -20..-16 dBFS range.')
    elif rmsmax_db < -22:
        print('WARN: playing is quiet (RMS max %.1f dB) - raise input gain.' % rmsmax_db)
    if rmsmax_db - rmsmin_db > 40:
        print('NOTE: wide dynamic range (>40 dB). Amp-sim Gain may need + a few dB for quiet parts.')
    print('OK   : good for amp-sim testing.' if ok else 'NOTE : re-record or normalize before amp-sim testing.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
