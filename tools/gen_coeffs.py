import numpy as np, os, sys, wave, struct
sys.stdout.reconfigure(encoding='utf-8')
FS = 44100.0
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', 'core', 'ampsim_coeffs.h'))
IRDIR = os.path.normpath(os.path.join(HERE, '..', 'ir'))

# --- cabinet IR kernels (v18): REAL sampled IRs, two cabs.
#     Tubes&Tone pack (redistribution allowed by the user):
#       CAB_SPECS[0] = 2x12 open-back (G12H30 + Blue)  -> '5T G12H30+BLU.44.1.wav'
#       CAB_SPECS[1] = 4x12 (Greenback family + 1x8)   -> '5T 412M25+108F59.44.1.wav'
#     The kernels carry the full miked-cab character (mic response, cone
#     resonances, room), so the IIR front chain degenerates to SAFETY
#     filters (HP 40 Hz rumble, LP 16 kHz hiss) and the speaker-resonance
#     biquads are identity (the Presence blend then passes unchanged).
#     2048 taps @44.1k = 46.4 ms: near-field + a good part of the room
#     decay. Tune below, then run
#     `python tools/gen_coeffs.py` to regenerate core/ampsim_coeffs.h. ---
CAB_IR_N = 2048            # taps @44.1k = 46.4 ms
CAB_SPECS = [
    ("2X12", "5T G12H30+BLU.44.1.wav"),
    ("4X12", "5T 412M25+108F59.44.1.wav"),
]
CAB_TYPES = len(CAB_SPECS)
AMP_CAB_VOICE_N = 6

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
    """Butterworth biquad sections without scipy (RBJ coefficients).
    2nd order: Q = 0.7071; 4th order: cascade Q = 0.5412 / 1.3066. """
    w0 = 2.0 * np.pi * fc / fs
    c = np.cos(w0)
    s = np.sin(w0)
    if kind == 'lowpass':
        def section(q):
            alpha = s / (2.0 * q)
            return np.array([(1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0,
                             -2.0 * c, 1.0 - alpha]) / (1.0 + alpha)
    else:
        def section(q):
            alpha = s / (2.0 * q)
            return np.array([(1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0,
                             -2.0 * c, 1.0 - alpha]) / (1.0 + alpha)
    if order == 2:
        return [section(0.70710678)]
    if order == 4:
        return [section(0.54119610), section(1.30656296)]
    raise ValueError('only order 2/4 supported')

def load_real_ir(wav_name):
    """Load a 44.1 kHz mono 16-bit IR, truncate to CAB_IR_N, end-window,
    and loudness-match the mid band (300..3000 Hz) to unity gain so the
    amp's level staging is unchanged from the synthesized kernels."""
    p = os.path.join(IRDIR, wav_name)
    if not os.path.exists(p):
        sys.exit(f'[gen_coeffs] missing IR file: {p}')
    w = wave.open(p, 'rb')
    if w.getframerate() != int(FS):
        sys.exit(f'[gen_coeffs] {wav_name}: expected 44100 Hz, got {w.getframerate()}')
    if w.getnchannels() != 1:
        sys.exit(f'[gen_coeffs] {wav_name}: expected mono')
    d = w.readframes(w.getnframes())
    w.close()
    vals = struct.unpack('<%dh' % (len(d) // 2), d[:len(d) // 2 * 2])
    x = np.array(vals, dtype=np.float64) / 32768.0
    if len(x) < CAB_IR_N:
        sys.exit(f'[gen_coeffs] {wav_name}: too short ({len(x)} < {CAB_IR_N})')
    x = x[:CAB_IR_N].copy()
    # end window: no truncation click (8 ms squared fade)
    nw = min(int(0.008 * FS), CAB_IR_N // 4)
    wnd = np.ones(CAB_IR_N)
    wnd[-nw:] = np.linspace(1.0, 0.0, nw) ** 2
    x *= wnd
    # loudness-match: mid-band (300..3000 Hz) gain -> 1.0
    X = np.fft.rfft(x, n=8192)
    fr2 = np.fft.rfftfreq(8192, 1.0 / FS)
    sel = (fr2 >= 300.0) & (fr2 <= 3000.0)
    ir_mid = np.mean(np.abs(X[sel]))
    x *= 1.0 / ir_mid
    return x

def fmt_ir(name, arr):
    L = [f'static const float {name}[AMP_CAB_IR_N] = {{']
    for i in range(0, len(arr), 8):
        row = ', '.join(f'{v:.9f}f' for v in arr[i:i+8])
        L.append('    ' + row + (',' if i + 8 < len(arr) else ''))
    L.append('};')
    return '\n'.join(L)

def fmt(name, arr, indent='    '):
    s = ', '.join(f'{v:.9f}f' for v in arr)
    return f'{indent}/* {name} */ {{ {s} }},\n'

# safety front chain per cab: HP40 + LP16k + 4 identity sections
# (the real IR carries the cab's own EQ; this chain only strips rumble/hiss)
IDENTITY = np.array([1.0, 0.0, 0.0, 0.0, 0.0])
VOICE_SECTIONS = [
    [butter(FS, 40.0, 2, 'highpass')[0], butter(FS, 16000.0, 2, 'lowpass')[0],
     IDENTITY, IDENTITY, IDENTITY, IDENTITY]
    for _ in CAB_SPECS
]
RESO_LOW = [IDENTITY.copy() for _ in CAB_SPECS]
RESO_HIGH = [IDENTITY.copy() for _ in CAB_SPECS]

C = []
C.append('/* generated by tools/gen_coeffs.py - do not edit by hand. */\n')
C.append('#ifndef AMPNEVE_COEFFS_H\n#define AMPNEVE_COEFFS_H\n\n')
C.append('#define AMPNEVE_COEFFS_FS 44100.0f\n')
C.append('typedef struct { float b0, b1, b2, a1, a2; } AmpBiquad;\n\n')
C.append(f'#define AMP_CAB_TYPES {CAB_TYPES}\n')
C.append(f'#define AMP_CAB_IR_N {CAB_IR_N}\n')
C.append(f'#define AMP_CAB_VOICE_N {AMP_CAB_VOICE_N}\n\n')

C.append('/* speaker resonance lows, per cab type (identity: the real IR owns it) */\n')
C.append('static const AmpBiquad AMP_CAB_RESO_LOW[] = {\n')
for i, lo in enumerate(RESO_LOW):
    C.append(fmt(f'{CAB_SPECS[i][0]} reso low', lo))
C.append('};\n\n')
C.append('/* speaker resonance highs, per cab type (identity) */\n')
C.append('static const AmpBiquad AMP_CAB_RESO_HIGH[] = {\n')
for i, hi in enumerate(RESO_HIGH):
    C.append(fmt(f'{CAB_SPECS[i][0]} reso high', hi))
C.append('};\n\n')

C.append('/* cab voicing chains: safety only (HP40 rumble + LP16k hiss +\n'
         '   identity) - the real IR carries the cab character */\n')
C.append('static const AmpBiquad AMP_CAB_VOICE[AMP_CAB_TYPES][AMP_CAB_VOICE_N] = {\n')
for i, secs in enumerate(VOICE_SECTIONS):
    C.append(f'    {{ /* {CAB_SPECS[i][0]} */\n')
    for j, sec in enumerate(secs):
        C.append(fmt(f'section {j}', sec, indent='        '))
    C.append('    },\n')
C.append('};\n\n')

# --- real cab IR kernels ---
C.append('/* cabinet IRs: real Tubes&Tone captures, truncated to 46.4 ms */\n')
for i, (name, wav) in enumerate(CAB_SPECS):
    ir = load_real_ir(wav)
    C.append(f'/* cabinet IR - {name}: {wav} (Tubes&Tone, redistributable) */\n')
    C.append(fmt_ir(f'AMP_CAB_IR_{name}', ir) + '\n')

# --- preamp interstage Miller-cap lowpasses (v15) ---
pre_lp1 = butter(FS, 9000.0, 2, 'lowpass')[0]
pre_lp2 = butter(FS, 5000.0, 2, 'lowpass')[0]
C.append('/* preamp interstage Miller-cap LPs (v15): shape stage harmonics */\n')
C.append('static const AmpBiquad AMP_PRE_LP1 = ')
C.append('{ ' + ', '.join(f'{v:.9f}f' for v in pre_lp1) + ' };\n')
C.append('static const AmpBiquad AMP_PRE_LP2 = ')
C.append('{ ' + ', '.join(f'{v:.9f}f' for v in pre_lp2) + ' };\n\n')

# --- neve 1073-style: LF shelf 110 +1.5dB, mid 700 +1dB Q0.7, HF shelf 12k +1.5dB
neve_lf = shelf(FS, 110.0, 1.5, False)
neve_mid = peaking(FS, 700.0, 0.7, 1.0)
neve_hf = shelf(FS, 12000.0, 1.5, True)
C.append('/* neve 1073-style tone (fixed brand color) */\n')
C.append('static const AmpBiquad AMP_NEVE[] = {\n')
C.append(fmt('lf shelf 110 +1.5dB', neve_lf)); C.append(fmt('mid 700 +1dB', neve_mid)); C.append(fmt('hf shelf 12k +1.5dB', neve_hf))
C.append('};\n#define AMP_NEVE_N 3\n\n')

# --- tone network runtime constants (Bass/Mid/Treble). ---
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
