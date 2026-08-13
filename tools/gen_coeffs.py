import numpy as np, os, sys
sys.stdout.reconfigure(encoding='utf-8')
FS = 44100.0
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', 'core', 'ampsim_coeffs.h'))

# --- cabinet IR synthesis (static convolution kernel, replaces the old mic
#     biquads).  The IR is the miked-cab impulse response: the old mic
#     pickup's exact HP+LP response as the causal base, plus speaker-cone
#     modal resonances (damped ringing) and a little room scatter.  Real
#     cones ring and real mics hear a room - that time structure is what
#     makes the amp read as a miked cab instead of an EQ'd DI.
#     1024 taps @44.1k = 23.2 ms.  Tune the knobs below, then run
#     `python tools/gen_coeffs.py` to regenerate core/ampsim_coeffs.h. ---
CAB_IR_N = 1024            # taps (state: 1024 floats/channel of delay)
CAB_IR_INIT_DELAY = 29     # ~0.66 ms speaker-to-mic flight time
CAB_IR_ROOM_LEVEL = 0.04   # room scatter level (0 = off)
CAB_IR_ROOM_TAU = 0.012    # room tail decay, seconds
CAB_IR_SEED_NASH = 12345   # deterministic scatter seeds (stable builds)
CAB_IR_SEED_EMO = 67890
# cone modal resonances: (f0 Hz, tau s, boost dB at f0)
CAB_IR_RESON_NASH = [
    (150.0, 0.006, 3.0),   # cab thump
    (350.0, 0.005, 2.0),   # low-mid body
    (900.0, 0.004, 2.5),   # cone bloom
    (1800.0, 0.0035, 1.5), # upper bloom
    (2800.0, 0.0028, 1.0), # edge ring
]
CAB_IR_RESON_EMO = [
    (160.0, 0.006, 3.0),
    (420.0, 0.005, 2.5),
    (900.0, 0.004, 3.0),
    (1700.0, 0.0032, 1.5),
    (2600.0, 0.0026, 1.0),
]

def _ba(c):
    return np.array([c[0], c[1], c[2]]), np.array([1.0, c[3], c[4]])

def _cascade(sections):
    b = np.array([1.0]); a = np.array([1.0])
    for c in sections:
        bk, ak = _ba(c)
        b = np.convolve(b, bk)
        a = np.convolve(a, ak)
    return b, a

def _chain_mag(sections, nfft):
    f = np.linspace(0.0, FS / 2.0, nfft)
    z = np.exp(-1j * 2.0 * np.pi * f / FS)
    H = np.ones(nfft, dtype=complex)
    for c in sections:
        b, a = _ba(c)
        H *= np.polyval(b, z) / np.polyval(a, z)
    return f, np.abs(H)

def _res_amp(f0, tau, db, m0):
    # spectral peak of A*exp(-t/tau)*sin(2*pi*f0*t) is ~ A*tau*fs/2,
    # so solve for the amplitude that adds a +db bump on top of m0.
    return m0 * (10.0 ** (db / 20.0) - 1.0) / (tau * FS / 2.0)

def synth_cab_ir(mic_sections, n, init_delay, resonances, room_level, room_tau, seed):
    from scipy.signal import lfilter
    taps = n - init_delay
    b, a = _cascade(mic_sections)
    imp = np.zeros(taps); imp[0] = 1.0
    base = lfilter(b, a, imp)               # exact old-mic response, causal phase
    t = np.arange(taps) / FS
    ir = base.copy()
    Xb = np.fft.rfft(base, n=4096)
    fr = np.fft.rfftfreq(4096, 1.0 / FS)
    for f0, tau, db in resonances:
        m0 = np.abs(Xb[np.argmin(np.abs(fr - f0))])
        ir += _res_amp(f0, tau, db, m0) * np.exp(-t / tau) * np.sin(2.0 * np.pi * f0 * t + 0.7)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(taps + 8192)
    shaped = lfilter(b, a, noise)           # room scatter shaped by the mic EQ
    onset = int(0.0008 * FS)
    tail = np.zeros(taps)
    tail[onset:] = np.exp(-t[onset:] / room_tau)
    ir += room_level * shaped[:taps] * tail
    w = np.ones(taps)                        # end window: no truncation click
    nw = min(int(0.004 * FS), taps // 4)
    if nw > 0:
        w[-nw:] = np.linspace(1.0, 0.0, nw) ** 2
    ir *= w
    # loudness match to the mic chain in the core guitar band (300..3000 Hz)
    X = np.fft.rfft(ir, n=8192)
    fr2 = np.fft.rfftfreq(8192, 1.0 / FS)
    sel = (fr2 >= 300.0) & (fr2 <= 3000.0)
    ir_mid = np.mean(np.abs(X[sel]))
    f2, mag = _chain_mag(mic_sections, 2048)
    mic_mid = np.mean(mag[(f2 >= 300.0) & (f2 <= 3000.0)])
    ir *= mic_mid / ir_mid
    out = np.zeros(n)
    out[init_delay:] = ir
    return out

def fmt_ir(name, arr):
    L = [f'static const float {name}[AMP_CAB_IR_N] = {{']
    for i in range(0, len(arr), 8):
        row = ', '.join(f'{v:.9f}f' for v in arr[i:i+8])
        L.append('    ' + row + (',' if i + 8 < len(arr) else ''))
    L.append('};')
    return '\n'.join(L)

def peaking(fs, f0, q, gain_db):
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / fs
    alpha = np.sin(w0) / (2.0 * q)
    b0 = 1.0 + alpha * A; b1 = -2.0 * np.cos(w0); b2 = 1.0 - alpha * A
    a0 = 1.0 + alpha / A; a1 = -2.0 * np.cos(w0); a2 = 1.0 - alpha / A
    return np.array([b0/a0, b1/a0, b2/a0, a1/a0, a2/a0])

def shelf(fs, f0, gain_db, high):
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / fs
    alpha = np.sin(w0) * np.sqrt(2.0) / 2.0
    c = np.cos(w0); sA = 2.0 * np.sqrt(A) * alpha
    if not high:
        b0 = A * ((A+1.0) - (A-1.0)*c + sA); b1 = 2.0*A*((A-1.0) - (A+1.0)*c); b2 = A*((A+1.0) - (A-1.0)*c - sA)
        a0 = (A+1.0) + (A-1.0)*c + sA; a1 = -2.0*((A-1.0) + (A+1.0)*c); a2 = (A+1.0) + (A-1.0)*c - sA
    else:
        b0 = A * ((A+1.0) + (A-1.0)*c + sA); b1 = -2.0*A*((A-1.0) + (A+1.0)*c); b2 = A*((A+1.0) + (A-1.0)*c - sA)
        a0 = (A+1.0) - (A-1.0)*c + sA; a1 = 2.0*((A-1.0) - (A+1.0)*c); a2 = (A+1.0) - (A-1.0)*c - sA
    return np.array([b0/a0, b1/a0, b2/a0, a1/a0, a2/a0])

def butter(fs, fc, order, kind):
    from scipy import signal
    sos = signal.butter(order, fc, kind, fs=fs, output='sos')
    return [np.array([b0/a0, b1/a0, b2/a0, a1/a0, a2/a0]) for b0,b1,b2,a0,a1,a2 in sos]

def fmt(name, arr, indent='    '):
    s = ', '.join(f'{v:.9f}f' for v in arr)
    return f'{indent}/* {name} */ {{ {s} }},\n'

C = []
C.append('/* generated by tools/gen_coeffs.py - do not edit by hand. */\n')
C.append('#ifndef AMPNEVE_COEFFS_H\n#define AMPNEVE_COEFFS_H\n\n')
C.append('#define AMPNEVE_COEFFS_FS 44100.0f\n')
C.append('typedef struct { float b0, b1, b2, a1, a2; } AmpBiquad;\n\n')

# --- cab chains (speaker + enclosure, TONE knob dark..bright): HP95 + body220(+3) +
#     midcut550(-2) + presence + 4th-order LP. Every section in the chain is
#     processed (all 5 biquads) - a 4th-order LP is two biquads whose mid-band
#     gains cancel, so dropping one nulls the whole chain. ---
hp95 = butter(FS, 95.0, 2, 'highpass')[0]
body = peaking(FS, 220.0, 1.0, 3.0)
midcut = peaking(FS, 550.0, 1.2, -2.0)
pres_dark = peaking(FS, 4200.0, 1.1, 4.5)
lp_dark = butter(FS, 9500.0, 4, 'lowpass')
C.append('/* cab DARK chain (TONE=0): woody 1x12, tighter/papery */\n')
C.append('static const AmpBiquad AMP_CAB_DARK[] = {\n')
C.append(fmt('hp95', hp95)); C.append(fmt('body220 +3dB', body)); C.append(fmt('midcut550 -2dB', midcut))
C.append(fmt('pres4200 +4.5dB', pres_dark))
for i,b in enumerate(lp_dark): C.append(fmt(f'lp9500 4th #{i}', b))
C.append('};\n#define AMP_CAB_DARK_N 5\n\n')

pres_bright = peaking(FS, 5500.0, 1.1, 6.0)
lp_bright = butter(FS, 12000.0, 4, 'lowpass')
C.append('/* cab BRIGHT chain (TONE=1): glassier cone edge */\n')
C.append('static const AmpBiquad AMP_CAB_BRIGHT[] = {\n')
C.append(fmt('hp95', hp95)); C.append(fmt('body220 +3dB', body)); C.append(fmt('midcut550 -2dB', midcut))
C.append(fmt('pres5500 +6dB', pres_bright))
for i,b in enumerate(lp_bright): C.append(fmt(f'lp12000 4th #{i}', b))
C.append('};\n#define AMP_CAB_BRIGHT_N 5\n\n')

# --- speaker resonance: 105Hz +2.5dB Q1.2, 3.5kHz +2dB Q1.6 (fixed)
#     Nashville: tighter lows, less cone ring. ---
reso_low = peaking(FS, 105.0, 1.2, 2.5)
reso_hi = peaking(FS, 3500.0, 1.6, 2.0)


# --- EMO / EDGE voice (VOICE=1): midwest-emo / math-rock / post-punk platform.
#     Same head, but: earlier breakup (handled in core via gain base), more
#     400-800 Hz body (no de-honk), warmer SM57 top, less Neve sheen. ---
neve_emo_lf = shelf(FS, 110.0, 1.5, False)
neve_emo_mid = peaking(FS, 700.0, 0.7, 1.0)
neve_emo_hf = shelf(FS, 12000.0, 0.5, True)
C.append('/* neve EMO (VOICE=1): less HF sheen */\n')
C.append('static const AmpBiquad AMP_NEVE_EMO[] = {\n')
C.append(fmt('lf shelf 110 +1.5dB', neve_emo_lf)); C.append(fmt('mid 700 +1dB', neve_emo_mid)); C.append(fmt('hf shelf 12k +0.5dB', neve_emo_hf))
C.append('};\n#define AMP_NEVE_EMO_N 3\n\n')

hp_emo = butter(FS, 95.0, 2, 'highpass')[0]
body_emo = peaking(FS, 220.0, 1.0, 3.0)
body500 = peaking(FS, 500.0, 1.0, 1.5)   # 400-800 body instead of de-honk
pres_dark_emo = peaking(FS, 3800.0, 1.1, 4.0)
lp_dark_emo = butter(FS, 9000.0, 4, 'lowpass')
C.append('/* cab DARK EMO (VOICE=1): woody, mid-forward, warmer top */\n')
C.append('static const AmpBiquad AMP_CAB_DARK_EMO[] = {\n')
C.append(fmt('hp95', hp_emo)); C.append(fmt('body220 +3dB', body_emo)); C.append(fmt('body500 +1.5dB', body500))
C.append(fmt('pres3800 +4dB', pres_dark_emo))
for i,b in enumerate(lp_dark_emo): C.append(fmt(f'lp9000 4th #{i}', b))
C.append('};\n#define AMP_CAB_DARK_EMO_N 5\n\n')

pres_bright_emo = peaking(FS, 4800.0, 1.1, 5.0)
lp_bright_emo = butter(FS, 11000.0, 4, 'lowpass')
C.append('/* cab BRIGHT EMO (VOICE=1): glassier cone edge */\n')
C.append('static const AmpBiquad AMP_CAB_BRIGHT_EMO[] = {\n')
C.append(fmt('hp95', hp_emo)); C.append(fmt('body220 +3dB', body_emo)); C.append(fmt('body500 +1.5dB', body500))
C.append(fmt('pres4800 +5dB', pres_bright_emo))
for i,b in enumerate(lp_bright_emo): C.append(fmt(f'lp11000 4th #{i}', b))
C.append('};\n#define AMP_CAB_BRIGHT_EMO_N 5\n\n')

# --- cabinet IR (static convolution kernel, replaces the old mic biquads).
#     The cab voicing chains above still run in front, so the Cab knob
#     keeps working; the IR carries the miked-cab character after them. ---
mic_nash_ir = butter(FS, 70.0, 2, 'highpass') + [peaking(FS, 5800.0, 1.0, 3.0)] + butter(FS, 11000.0, 2, 'lowpass')
mic_emo_ir = butter(FS, 70.0, 2, 'highpass') + [peaking(FS, 5500.0, 1.0, 2.0)] + butter(FS, 10000.0, 2, 'lowpass')
ir_nash = synth_cab_ir(mic_nash_ir, CAB_IR_N, CAB_IR_INIT_DELAY, CAB_IR_RESON_NASH,
                       CAB_IR_ROOM_LEVEL, CAB_IR_ROOM_TAU, CAB_IR_SEED_NASH)
ir_emo = synth_cab_ir(mic_emo_ir, CAB_IR_N, CAB_IR_INIT_DELAY, CAB_IR_RESON_EMO,
                      CAB_IR_ROOM_LEVEL, CAB_IR_ROOM_TAU, CAB_IR_SEED_EMO)
C.append('#define AMP_CAB_IR_N 1024\n\n')
C.append('/* cabinet IR - Nashville (VOICE=0): tight lows, glassy top, drier room. */\n')
C.append(fmt_ir('AMP_CAB_IR_NASH', ir_nash) + '\n')
C.append('/* cabinet IR - Emo/Edge (VOICE=1): more mid body, warmer top. */\n')
C.append(fmt_ir('AMP_CAB_IR_EMO', ir_emo) + '\n')


C.append('/* speaker resonance (fixed) */\n')
C.append('static const AmpBiquad AMP_RESO_LOW = ')
C.append('{ ' + ', '.join(f'{v:.9f}f' for v in reso_low) + ' };\n')
C.append('static const AmpBiquad AMP_RESO_HIGH = ')
C.append('{ ' + ', '.join(f'{v:.9f}f' for v in reso_hi) + ' };\n\n')

# --- neve 1073-style: LF shelf 110 +1.5dB, mid 700 +1dB Q0.7, HF shelf 12k +1.5dB
#     Nashville: tight lows, subtle mid color, extra glass. ---
neve_lf = shelf(FS, 110.0, 1.5, False)
neve_mid = peaking(FS, 700.0, 0.7, 1.0)
neve_hf = shelf(FS, 12000.0, 1.5, True)
C.append('/* neve 1073-style tone (fixed brand color) */\n')
C.append('static const AmpBiquad AMP_NEVE[] = {\n')
C.append(fmt('lf shelf 110 +1.5dB', neve_lf)); C.append(fmt('mid 700 +1dB', neve_mid)); C.append(fmt('hf shelf 12k +1.5dB', neve_hf))
C.append('};\n#define AMP_NEVE_N 3\n\n')

# --- tone network runtime constants (Bass/Mid/Treble).
#     Fixed center frequencies; only the linear gain A changes at runtime,
#     so set_param needs just cos/sin/alpha constants + multiply-add. ---
def tone_consts(f0, q, high_shelf):
    w0 = 2.0 * np.pi * f0 / FS
    cos_w0 = np.cos(w0)
    sin_w0 = np.sin(w0)
    alpha = sin_w0 / (2.0 * q) if not high_shelf else sin_w0 * np.sqrt(2.0) / 2.0
    return cos_w0, sin_w0, alpha

cb, sb, ab = tone_consts(140.0, 0.9, False)    # bass: tight low control
cm, sm, am = tone_consts(850.0, 0.7, False)    # mid: forward presence (Nashville cut)
ct, st, at = tone_consts(5000.0, 0.8, False)   # treble: glass, no 3k honk
C.append('/* tone network runtime constants (linear gain A, no pow at runtime) */\n')
C.append(f'#define AMP_TONE_BASS_F0 140.0f\n')
C.append(f'#define AMP_TONE_BASS_COSW {cb:.9f}f\n')
C.append(f'#define AMP_TONE_BASS_SINW {sb:.9f}f\n')
C.append(f'#define AMP_TONE_BASS_ALPHA {ab:.9f}f\n')
C.append(f'#define AMP_TONE_MID_F0 850.0f\n')
C.append(f'#define AMP_TONE_MID_COSW {cm:.9f}f\n')
C.append(f'#define AMP_TONE_MID_SINW {sm:.9f}f\n')
C.append(f'#define AMP_TONE_MID_ALPHA {am:.9f}f\n')
C.append(f'#define AMP_TONE_TREB_F0 5000.0f\n')
C.append(f'#define AMP_TONE_TREB_COSW {ct:.9f}f\n')
C.append(f'#define AMP_TONE_TREB_SINW {st:.9f}f\n')
C.append(f'#define AMP_TONE_TREB_ALPHA {at:.9f}f\n')
C.append(f'#define AMP_TONE_TREB_HIGH 1\n\n')
C.append('#endif\n')

open(OUT, 'w', encoding='utf-8', newline='\n').write(''.join(C))
print('wrote', OUT)
