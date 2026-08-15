# tools/compare_gain.py - AmpNeve vs known-good gain ZDLs: artifact measurements
#
# Reproduces every number in docs/DESIGN.md "v18b gain naturalness" section.
# References: stock BASSDRV (real firmware, structural facts from its C674x
# disassembly - no long FIR, IIR + table soft-clip, no cab) and PurestDrive
# (community gain ZDL, exact algorithm port below).
#
# Uses the native core renderer (same DSP code as VST and ZDL builds):
#   cl /O2 /MD /I core tools\ampsim_render.c core\ampsim.c /Fe:build\ampsim_render_analyze.exe
#
# Outputs (tracked): out/gaincmp/  - test signals, renders, spectrogram PNG,
#                                     report.json, SUMMARY.txt
# Temp files:         out/gaincmp/tmp/  (gitignored, deterministic regen)
import numpy as np, wave, subprocess, os, json
from scipy.signal import resample_poly, spectrogram

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, 'out', 'gaincmp')
TMP = os.path.join(OUT, 'tmp')
os.makedirs(TMP, exist_ok=True)
EXE = os.path.join(ROOT, 'build', 'ampsim_render_analyze.exe')
DI = os.path.join(ROOT, 'out', '_ibanez_di.wav')
FS = 44100

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except Exception:
    HAVE_MPL = False

LOG = []
def log(s=''):
    print(s)
    LOG.append(s)

def write_wav16(path, x):
    x16 = np.clip(x, -1.0, 1.0) * 32767.0
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(FS)
        w.writeframes(x16.astype('<i2').tobytes())

def read_wav16(path):
    with wave.open(path, 'rb') as w:
        n = w.getnframes(); ch = w.getnchannels()
        raw = np.frombuffer(w.readframes(n), '<i2').astype(np.float64) / 32768.0
    return raw[::2] if ch == 2 else raw   # deinterleave: take L

DEFAULTS = dict(input=1.0, gain=0.35, bass=0.5, mid=0.5, treble=0.5,
                master=0.55, level=0.75, neve=1.0, presence=0.85, cabtype=0.0)
ORDER = ['input', 'gain', 'bass', 'mid', 'treble', 'master', 'level', 'neve', 'presence', 'cabtype']

def render_amp(inwav, outwav, **kw):
    p = dict(DEFAULTS); p.update(kw)
    args = [EXE, inwav, outwav] + [str(p[k]) for k in ORDER]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0: raise RuntimeError(r.stderr)

def purestdrive(x, intensity=1.0):
    """Exact port of src/airwindows/purestdrive/purestdrive.c (no dither)."""
    y = np.empty_like(x); s = np.sin(x); prev = 0.0
    for i in range(len(x)):
        dry = x[i]
        blend = abs(prev + s[i]) * 0.5 * intensity
        if blend > 1.0: blend = 1.0
        y[i] = dry * (1.0 - blend) + s[i] * blend
        prev = s[i]
    return y

def fold_of(f, fs=FS):
    return abs(f - round(f / fs) * fs)

def harmonic_report(x, f0, fs=FS):
    """(harmonics [k,db], thd_db, folded_db, dc, folded_bins)"""
    n = len(x); w = np.hanning(n)
    X = np.fft.rfft(x * w); fr = np.fft.rfftfreq(n, 1.0 / fs)
    mag = np.abs(X) / (w.sum() / 2.0)
    db = 20 * np.log10(mag + 1e-12)
    h = []
    for k in range(1, 41):
        f = k * f0
        i = int(round(f / (fs / n)))
        if i >= len(mag): break
        lo, hi = max(0, i - 3), min(len(mag) - 1, i + 3)
        h.append([k, round(float(db[lo:hi + 1].max()), 1)])
    fund = 10 ** (h[0][1] / 20.0)
    thd = np.sqrt(sum((10 ** (d / 20.0)) ** 2 for k, d in h[1:] if d > -200)) / max(fund, 1e-12)
    fold_db = [d for k, d in h if k * f0 > fs / 2 and d > -200]
    folded = (sum(10 ** (d / 20.0) for d in fold_db) / max(fund, 1e-12)) if fold_db else 0.0
    return h, 20 * np.log10(thd + 1e-12), 20 * np.log10(folded + 1e-12), float(np.mean(x)), fold_db

def band_db(x, lo, hi, fs=FS):
    n = len(x); w = np.hanning(n)
    X = np.fft.rfft(x * w); fr = np.fft.rfftfreq(n, 1.0 / fs)
    mag = np.abs(X) / (w.sum() / 2.0)
    m = (fr >= lo) & (fr < hi)
    return 20 * np.log10(np.sqrt(np.sum(mag[m] ** 2)) + 1e-12)

def pk_db(x, f, fs=FS):
    n = len(x); w = np.hanning(n)
    X = np.fft.rfft(x * w); fr = np.fft.rfftfreq(n, 1.0 / fs)
    mag = np.abs(X) / (w.sum() / 2.0)
    db = 20 * np.log10(mag + 1e-12)
    i = int(round(f / (fs / n)))
    return db[max(0, i - 2):min(len(db) - 1, i + 2)].max()

# ---------------------------------------------------------------- signals
t = np.arange(int(4.0 * FS)) / FS
sine1k = 0.251 * np.sin(2 * np.pi * 1000 * t)
sine12k = 0.251 * np.sin(2 * np.pi * 12000 * t)
sine4k = 0.251 * np.sin(2 * np.pi * 4000 * t)
write_wav16(os.path.join(OUT, 's1k.wav'), sine1k)
write_wav16(os.path.join(OUT, 's12k.wav'), sine12k)
write_wav16(os.path.join(OUT, 's4k.wav'), sine4k)

freqs = np.geomspace(20, 20000, 40)
seg = np.zeros(0); marks = []
for f in freqs:
    tt = np.arange(int(0.35 * FS)) / FS
    seg = np.concatenate([seg, 0.126 * np.sin(2 * np.pi * f * tt), np.zeros(int(0.05 * FS))])
    marks.append((f, len(seg)))
write_wav16(os.path.join(OUT, 'sweep.wav'), seg)

report = {'our_chain': {}, 'purestdrive': {}}
GAINS = [0.20, 0.35, 0.60, 0.80, 1.00]

# ---------------------------------------------------------------- 1 kHz ladder
log('== 1 kHz harmonic ladder (rel fundamental, dB) ==')
for g in GAINS:
    render_amp(os.path.join(OUT, 's1k.wav'), os.path.join(OUT, f'amp_g{g:.2f}_1k.wav'), gain=g)
    x = read_wav16(os.path.join(OUT, f'amp_g{g:.2f}_1k.wav'))
    h, thd, folded, dc, _ = harmonic_report(x, 1000)
    report['our_chain'][f'g{g:.2f}'] = dict(thd_db=thd, folded_db=folded, dc=dc)
    hs = ' '.join(f'{k}:{d:+.0f}' for k, d in h if d > -80)
    log(f'amp g={g:.2f}: THD={thd:+.1f}dB folded={folded:+.0f}dB dc={dc:+.4f}\n    {hs}')
for intens in (0.5, 1.0):
    y = purestdrive(sine1k, intens)
    h, thd, folded, dc, _ = harmonic_report(y, 1000)
    report['purestdrive'][f'i{intens:.1f}'] = dict(thd_db=thd, folded_db=folded, dc=dc, peak=float(np.abs(y).max()))
    hs = ' '.join(f'{k}:{d:+.0f}' for k, d in h if d > -80)
    log(f'purest i={intens:.1f}: THD={thd:+.1f}dB folded={folded:+.0f}dB dc={dc:+.4f} peak={np.abs(y).max():.3f}\n    {hs}')

# ---------------------------------------------------------------- alias probes
log('== alias foldback probes ==')
for name, sig, f0 in (('12k', sine12k, 12000), ('4k', sine4k, 4000)):
    render_amp(os.path.join(OUT, f's{name}.wav'), os.path.join(OUT, f'amp_g1.0_{name}.wav'), gain=1.0)
    xa = read_wav16(os.path.join(OUT, f'amp_g1.0_{name}.wav'))
    ha, thda, foldeda, dca, foldbins = harmonic_report(xa, f0)
    report['our_chain'][f'{name}_g1.0'] = dict(thd_db=thda, folded_db=foldeda, dc=dca)
    fb = ' '.join(f'h{k}({fold_of(k * f0):.0f}Hz):{d:+.0f}' for k, d in foldbins if k >= 2 and d > -90)
    log(f'amp {name} g=1.0: folded={foldeda:+.0f}dB  [{fb}]')
    yp = purestdrive(sig * 4.0, 1.0)   # 4x gain so the reference saturates too
    _, _, foldedp, dcp, foldp = harmonic_report(yp, f0)
    fb = ' '.join(f'h{k}({fold_of(k * f0):.0f}Hz):{d:+.0f}' for k, d in foldp if k >= 2 and d > -90)
    log(f'purest {name} (4x in): folded={foldedp:+.0f}dB  [{fb}]')

# ---------------------------------------------------------------- IMD (two-tone)
log('== IMD g=1.0 (1.2k + 1.8k, -12 dBFS each) ==')
imd = 0.251 * np.sin(2 * np.pi * 1200 * t) + 0.251 * np.sin(2 * np.pi * 1800 * t)
write_wav16(os.path.join(OUT, 'imd.wav'), imd)
render_amp(os.path.join(OUT, 'imd.wav'), os.path.join(OUT, 'amp_g1.0_imd.wav'), gain=1.0)
x = read_wav16(os.path.join(OUT, 'amp_g1.0_imd.wav'))
fund = max(pk_db(x, 1200), pk_db(x, 1800))
imd_rows = []
for name, f in [('2f1-f2', 600), ('2f1', 2400), ('2f2', 3600), ('3f1', 3600), ('f1+f2', 3000),
                ('2f1+f2', 4200), ('f1+2f2', 4800), ('5f1', 6000), ('3f2', 5400)]:
    d = pk_db(x, f) - fund
    imd_rows.append((name, f, d))
    log(f'  {name:8s} {f:5d} Hz: {d:+.1f} dB rel')

# ---------------------------------------------------------------- impulse
log('== impulse (DC tail / thump) ==')
imp = np.zeros(FS); imp[100] = 1.0
write_wav16(os.path.join(TMP, 'imp.wav'), imp)
render_amp(os.path.join(TMP, 'imp.wav'), os.path.join(OUT, 'amp_g0.35_imp.wav'), gain=0.35)
y = read_wav16(os.path.join(OUT, 'amp_g0.35_imp.wav'))
env = np.abs(y); idx = np.where(env > env.max() * 0.01)[0]
imp_ms = (idx[-1] - idx[0]) * 1000 / FS
log(f'  peak {np.abs(y).max():.4f} mean {y.mean():+.6f} decay-to-40dB {imp_ms:.1f} ms')

# ---------------------------------------------------------------- compression curve
log('== compression curve (1k sine, g=1.0, rms out dBFS vs in) ==')
comp_rows = []
for dbFS in [-40, -30, -24, -18, -12, -9, -6, -3]:
    s = 10 ** (dbFS / 20) * np.sin(2 * np.pi * 1000 * t)
    write_wav16(os.path.join(TMP, 'c.wav'), s)
    render_amp(os.path.join(TMP, 'c.wav'), os.path.join(TMP, 'c_out.wav'), gain=1.0)
    y = read_wav16(os.path.join(TMP, 'c_out.wav'))
    rms = 10 * np.log10(np.mean(y ** 2) + 1e-12)
    comp_rows.append((dbFS, round(rms, 1)))
    log(f'  in {dbFS:+3d} dBFS -> out {rms:+6.1f} dBFS')

# ---------------------------------------------------------------- tone knob range
log('== tone knob range (response @freq, g=0.2) ==')
tone_rows = []
for knob, fr in [('bass', 140), ('mid', 850), ('treble', 5000)]:
    s = np.concatenate([0.126 * np.sin(2 * np.pi * fr * t[:int(0.5 * FS)]), np.zeros(int(0.05 * FS))])
    write_wav16(os.path.join(TMP, 't.wav'), s)
    out = []
    for v in [0.0, 0.5, 1.0]:
        render_amp(os.path.join(TMP, 't.wav'), os.path.join(TMP, 't_out.wav'), **{knob: v}, gain=0.2)
        y = read_wav16(os.path.join(TMP, 't_out.wav'))
        out.append(10 * np.log10(np.mean(y[int(0.3 * FS):int(0.5 * FS)] ** 2) + 1e-12))
    tone_rows.append((knob, fr, round(out[2] - out[0], 1)))
    log(f'  {knob:7s} @{fr:5d}Hz: span {out[2] - out[0]:+.1f} dB  (v0 {out[0]:+.1f} / v0.5 {out[1]:+.1f} / v1 {out[2]:+.1f})')

# ---------------------------------------------------------------- freq response
log('== response (sweep) ==')
resp_rows = {}
for g in (0.20, 1.00):
    render_amp(os.path.join(OUT, 'sweep.wav'), os.path.join(OUT, f'amp_g{g:.2f}_sweep.wav'), gain=g)
    xs = read_wav16(os.path.join(OUT, f'amp_g{g:.2f}_sweep.wav'))
    resp = []
    for j, (f, m) in enumerate(marks):
        lo = m - int(0.30 * FS)
        resp.append((f, np.sqrt(np.mean(xs[lo:m] ** 2))))
    r1k = [r for f, r in resp if 900 <= f <= 1100][0]
    db = [20 * np.log10(r / r1k + 1e-12) for f, r in resp]
    resp_rows[f'g{g:.2f}'] = dict(min_db=round(min(db), 1), max_db=round(max(db), 1))
    log(f'amp g={g:.2f}: response range {min(db):+.1f}..{max(db):+.1f} dB (ref 1k)')
    for f, d in zip(freqs, db):
        if d > 2.0 or d < -10.0:
            log(f'    {f:7.0f} Hz: {d:+.1f} dB')

# ---------------------------------------------------------------- DI guitar
di_metrics = {}
if os.path.exists(DI):
    with wave.open(DI, 'rb') as w:
        r48 = w.getframerate()
        di = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(np.float64) / 32768.0
    di = resample_poly(di, FS, r48)
    write_wav16(os.path.join(OUT, '_di44.wav'), di)
    render_amp(os.path.join(OUT, '_di44.wav'), os.path.join(OUT, 'amp_g0.80_di.wav'), gain=0.80)
    render_amp(os.path.join(OUT, '_di44.wav'), os.path.join(OUT, 'amp_g0.35_di.wav'), gain=0.35)
    ya = read_wav16(os.path.join(OUT, 'amp_g0.80_di.wav'))
    yb = read_wav16(os.path.join(OUT, 'amp_g0.35_di.wav'))
    yp = purestdrive(di * 1.6, 1.0)
    write_wav16(os.path.join(OUT, 'purest_di.wav'), yp)
    log('== DI guitar (18s) ==')
    for name, y in (('amp g0.80', ya), ('amp g0.35', yb), ('purest i1.0 x1.6', yp)):
        fz = band_db(y, 8000, 16000) - band_db(y, 500, 4000)
        top = band_db(y, 19000, 22000)
        di_metrics[name] = dict(peak=round(float(np.abs(y).max()), 3),
                                rms=round(float(np.sqrt(np.mean(y ** 2))), 4),
                                fizz_db=round(fz, 1), nyqfloor_db=round(top, 1))
        log(f'{name:16s} peak={np.abs(y).max():.3f} rms={np.sqrt(np.mean(y**2)):.4f} '
            f'fizz(8-16k rel 0.5-4k)={fz:+.1f}dB nyqfloor(19-22k)={top:.1f}dB')
    if HAVE_MPL:
        fig, ax = plt.subplots(3, 1, figsize=(11, 9))
        for a, y, lab in ((ax[0], ya, 'AmpNeve g=0.80'), (ax[1], yb, 'AmpNeve g=0.35'), (ax[2], yp, 'PurestDrive i=1.0')):
            f, tt, S = spectrogram(y, FS, nperseg=4096, noverlap=2048)
            a.pcolormesh(tt, f, 10 * np.log10(S + 1e-12), shading='auto', cmap='inferno', vmin=-110, vmax=-30)
            a.set_ylim(0, 22050); a.set_ylabel(lab)
        ax[2].set_xlabel('s'); ax[0].set_title('spectrograms')
        plt.tight_layout()
        plt.savefig(os.path.join(OUT, 'di_spectrograms.png'), dpi=110)
else:
    log('DI not found at ' + DI)

# ---------------------------------------------------------------- persist
report['imd_g1.0'] = dict(fund_db=round(fund, 1), products=imd_rows)
report['impulse_g0.35'] = dict(decay_ms=round(imp_ms, 1))
report['compression_g1.0'] = comp_rows
report['tone_knobs_g0.2'] = tone_rows
report['response'] = resp_rows
report['di_metrics'] = di_metrics
with open(os.path.join(OUT, 'report.json'), 'w') as f:
    json.dump(report, f, indent=1)
with open(os.path.join(OUT, 'SUMMARY.txt'), 'w', encoding='utf-8') as f:
    f.write('AmpNeve gain-naturalness measurements (tools/compare_gain.py)\n')
    f.write('References: stock BASSDRV (firmware disassembly) + PurestDrive (exact port)\n')
    f.write('=' * 70 + '\n')
    f.write('\n'.join(LOG) + '\n')
log('done -> ' + OUT)
