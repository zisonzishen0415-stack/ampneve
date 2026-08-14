import numpy as np, os, sys
sys.stdout.reconfigure(encoding='utf-8')
FS = 44100.0
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', 'core', 'ampsim_coeffs.h'))

# --- cabinet IR synthesis (static convolution kernels, v17).
#     Cab types: 1x12 / 2x12 / 4x12. Each cab = N speakers summed (each with
#     its own cone-ring phase and mic-distance delay), then ONE room stage
#     on the sum (freq-dependent scatter + specular early reflections +
#     all-pass dispersion), exactly how one mic hears a multi-speaker cab
#     through one room. Single voice (Nashville character) - the Emo voice
#     was merged away in v17.
#     1024 taps @44.1k = 23.2 ms.  Tune below, then run
#     `python tools/gen_coeffs.py` to regenerate core/ampsim_coeffs.h. ---
CAB_IR_N = 1024            # taps (state: 1024 floats/channel of delay)
CAB_IR_INIT_DELAY = 29     # ~0.66 ms speaker-to-mic flight time
CAB_IR_ROOM_LEVEL = 0.028  # room scatter level (sweep-verified, v16)
CAB_IR_ROOM_HP = 180.0     # scatter high-pass: 80-300 Hz belongs to the cab chain
CAB_IR_ROOM_XOVER = 1000.0 # decay split: lows ring longer than highs
CAB_IR_ROOM_TAU_LOW = 0.016    # low-band room tail decay, seconds
CAB_IR_ROOM_TAU_HIGH = 0.005   # high-band room tail decay, seconds
CAB_IR_ROOM_ENERGY_LOW = 0.6   # low/high energy split (sums to 1)
# specular early reflections: (delay s, level dB rel IR peak)
CAB_IR_EARLY = [
    (0.0012, -22.0),
    (0.0023, -26.0),
    (0.0038, -30.0),
]
# cone phase dispersion: (f0 Hz, Q) 2nd-order all-passes - pure time smear.
CAB_IR_ALLPASS = [
    (1500.0, 0.7),
    (3200.0, 1.2),
]
CAB_IR_SEED = 12345        # deterministic room scatter seed

# --- cab type definitions: (name, speakers, speaker mic-delay offsets s,
#     cone resonances (f0 Hz, tau s, boost dB), speaker-resonance biquads
#     (f0 Hz, Q, gain dB), voicing chain (sections)). ---
CAB_SPECS = [
    # 1x12 - the v16 Nashville 1x12: tight, dry, glassy (unchanged).
    ("1X12", 1, [0.0], [
        (350.0, 0.005, 2.0),
        (1150.0, 0.004, 2.0),
        (1800.0, 0.0035, 1.0),
        (2800.0, 0.0028, 0.6),
        (5600.0, 0.0012, 1.5),
    ], [(105.0, 1.2, 2.5), (3500.0, 1.6, 2.0)], None),
    # 2x12 - two cones: lower modes (bigger cone), second speaker +0.4 ms
    # mic distance (subtle 1-2 kHz comb), more low-mid body.
    ("2X12", 2, [0.0, 0.0004], [
        (320.0, 0.0055, 2.0),
        (1050.0, 0.0045, 2.0),
        (1700.0, 0.0038, 1.0),
        (2600.0, 0.0030, 0.6),
        (5400.0, 0.0013, 1.3),
    ], [(95.0, 1.1, 3.0), (3400.0, 1.5, 2.0)], None),
    # 4x12 - four cones: denser mids, smoother top (averaging), bigger low end.
    ("4X12", 4, [0.0, 0.00035, 0.0007, 0.0010], [
        (300.0, 0.006, 2.0),
        (950.0, 0.005, 2.0),
        (1600.0, 0.004, 1.0),
        (2400.0, 0.0032, 0.6),
        (5000.0, 0.0014, 1.1),
    ], [(90.0, 1.1, 3.5), (3300.0, 1.5, 2.0)], None),
]
CAB_TYPES = len(CAB_SPECS)

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

def _apf(x, f0, q):
    """2nd-order all-pass (RBJ): flat magnitude, frequency-dependent phase."""
    w0 = 2.0 * np.pi * f0 / FS
    c = np.cos(w0)
    alpha = np.sin(w0) / (2.0 * q)
    b0, b1, b2 = 1.0 - alpha, -2.0 * c, 1.0 + alpha
    a0, a1, a2 = 1.0 + alpha, -2.0 * c, 1.0 - alpha
    return _lfilter(np.array([b0, b1, b2]) / a0, np.array([1.0, a1 / a0, a2 / a0]), x)

def synth_speaker(mic_sections, resonances, phase0, delay_s, n):
    """One speaker's dry IR: mic response + cone modal ringing, at a given
    mic-distance delay. cone-ring phase varies per speaker (physical units)."""
    taps = n - CAB_IR_INIT_DELAY
    b, a = _cascade(mic_sections)
    imp = np.zeros(taps); imp[0] = 1.0
    ir = _lfilter(b, a, imp)
    t = np.arange(taps) / FS
    Xb = np.fft.rfft(ir, n=4096)
    fr = np.fft.rfftfreq(4096, 1.0 / FS)
    for f0, tau, db in resonances:
        m0 = np.abs(Xb[np.argmin(np.abs(fr - f0))])
        ir += _res_amp(f0, tau, db, m0) * np.exp(-t / tau) * np.sin(2.0 * np.pi * f0 * t + phase0)
    d = int(delay_s * FS)
    out = np.zeros(n)
    out[CAB_IR_INIT_DELAY + d:] = ir[:max(0, n - CAB_IR_INIT_DELAY - d)]
    return out

def synth_cab(spec, seed):
    """Sum the cab's speakers, then add the shared room stage."""
    name, nspk, delays, resonances, reso, voice = spec
    ir = np.zeros(CAB_IR_N)
    for i in range(nspk):
        ir += synth_speaker(MIC_SECTIONS, resonances, 0.7 + 1.9 * i, delays[i], CAB_IR_N)
    taps = CAB_IR_N - CAB_IR_INIT_DELAY
    b, a = _cascade(MIC_SECTIONS)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(taps + 8192)
    shaped = _lfilter(b, a, noise)
    hpb, hpa = _cascade(butter(FS, CAB_IR_ROOM_HP, 2, 'highpass'))
    shaped = _lfilter(hpb, hpa, shaped)
    lb, la = _cascade(butter(FS, CAB_IR_ROOM_XOVER, 2, 'lowpass'))
    noise_low = _lfilter(lb, la, shaped)
    noise_high = shaped - noise_low
    t = np.arange(taps) / FS
    onset = int(0.0008 * FS)
    tail_low = np.zeros(taps); tail_low[onset:] = np.exp(-t[onset:] / CAB_IR_ROOM_TAU_LOW)
    tail_high = np.zeros(taps); tail_high[onset:] = np.exp(-t[onset:] / CAB_IR_ROOM_TAU_HIGH)
    ir[CAB_IR_INIT_DELAY:] += CAB_IR_ROOM_LEVEL * (
        CAB_IR_ROOM_ENERGY_LOW * noise_low[:taps] * tail_low +
        (1.0 - CAB_IR_ROOM_ENERGY_LOW) * noise_high[:taps] * tail_high)
    # specular early reflections: mic-colored copies of the summed dry cab
    pk = np.max(np.abs(ir))
    dry = ir[CAB_IR_INIT_DELAY:]
    for delay, db in CAB_IR_EARLY:
        k = int(delay * FS)
        if 0 < k < taps:
            dry[k:] += (10.0 ** (db / 20.0)) * pk * dry[:taps - k]
    ir[CAB_IR_INIT_DELAY:] = dry
    # cone phase dispersion: zero magnitude change, pure time smear.
    ir = _apf(ir, *CAB_IR_ALLPASS[0])
    ir = _apf(ir, *CAB_IR_ALLPASS[1])
    # end window: no truncation click
    w = np.ones(CAB_IR_N)
    nw = min(int(0.004 * FS), CAB_IR_N // 4)
    if nw > 0:
        w[-nw:] = np.linspace(1.0, 0.0, nw) ** 2
    ir *= w
    # loudness match to the mic chain in the core guitar band (300..3000 Hz)
    X = np.fft.rfft(ir, n=8192)
    fr2 = np.fft.rfftfreq(8192, 1.0 / FS)
    sel = (fr2 >= 300.0) & (fr2 <= 3000.0)
    ir_mid = np.mean(np.abs(X[sel]))
    f2, mag = _chain_mag(MIC_SECTIONS, 2048)
    mic_mid = np.mean(mag[(f2 >= 300.0) & (f2 <= 3000.0)])
    ir *= mic_mid / ir_mid
    return ir

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

def _lfilter(b, a, x):
    """Arbitrary-order direct-form I filter (a0 normalized to 1)."""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    y = np.zeros(len(x))
    for i in range(len(x)):
        acc = b[0] * x[i]
        for k in range(1, len(b)):
            if i >= k:
                acc += b[k] * x[i - k]
        for k in range(1, len(a)):
            if i >= k:
                acc -= a[k] * y[i - k]
        y[i] = acc
    return y

# old-mic pickup (single voice): HP70 + 5.8k peaking + LP11k
MIC_SECTIONS = butter(FS, 70.0, 2, 'highpass') + [peaking(FS, 5800.0, 1.0, 1.0)] + butter(FS, 11000.0, 2, 'lowpass')

def fmt(name, arr, indent='    '):
    s = ', '.join(f'{v:.9f}f' for v in arr)
    return f'{indent}/* {name} */ {{ {s} }},\n'

C = []
C.append('/* generated by tools/gen_coeffs.py - do not edit by hand. */\n')
C.append('#ifndef AMPNEVE_COEFFS_H\n#define AMPNEVE_COEFFS_H\n\n')
C.append('#define AMPNEVE_COEFFS_FS 44100.0f\n')
C.append('typedef struct { float b0, b1, b2, a1, a2; } AmpBiquad;\n\n')
C.append(f'#define AMP_CAB_TYPES {CAB_TYPES}\n')
C.append(f'#define AMP_CAB_IR_N {CAB_IR_N}\n')
C.append(f'#define AMP_CAB_VOICE_N 6\n\n')

# --- per-cab voicing chains (HP + body + de-honk + presence + 4th-order LP) ---
VOICE_SECTIONS = []
for name, nspk, delays, resonances, reso, voice in CAB_SPECS:
    hp_f0 = 95.0 if name == '1X12' else (90.0 if name == '2X12' else 85.0)
    body = 3.0 if name == '1X12' else (3.5 if name == '2X12' else 4.0)
    cut = -2.0 if name == '1X12' else (-1.5 if name == '2X12' else -1.0)
    pres_f0 = 4200.0 if name == '1X12' else (4300.0 if name == '2X12' else 4400.0)
    pres_db = 4.5 if name == '1X12' else (4.0 if name == '2X12' else 3.5)
    lp_f0 = 9500.0 if name == '1X12' else (10000.0 if name == '2X12' else 10500.0)
    body_f0 = 220.0 if name != '4X12' else 230.0
    VOICE_SECTIONS.append([
        butter(FS, hp_f0, 2, 'highpass')[0],
        peaking(FS, body_f0, 1.0, body),
        peaking(FS, 550.0, 1.2, cut),
        peaking(FS, pres_f0, 1.1, pres_db),
        butter(FS, lp_f0, 4, 'lowpass')[0],
        butter(FS, lp_f0, 4, 'lowpass')[1],
    ])

# --- speaker resonance tables per cab ---
RESO_LOW = [peaking(FS, s[4][0][0], s[4][0][1], s[4][0][2]) for s in CAB_SPECS]
RESO_HIGH = [peaking(FS, s[4][1][0], s[4][1][1], s[4][1][2]) for s in CAB_SPECS]

C = []
C.append('/* generated by tools/gen_coeffs.py - do not edit by hand. */\n')
C.append('#ifndef AMPNEVE_COEFFS_H\n#define AMPNEVE_COEFFS_H\n\n')
C.append('#define AMPNEVE_COEFFS_FS 44100.0f\n')
C.append('typedef struct { float b0, b1, b2, a1, a2; } AmpBiquad;\n\n')
C.append(f'#define AMP_CAB_TYPES {CAB_TYPES}\n')
C.append(f'#define AMP_CAB_IR_N {CAB_IR_N}\n')
C.append(f'#define AMP_CAB_VOICE_N 6\n\n')

C.append('/* speaker resonance lows, per cab type (index = CABTYPE) */\n')
C.append('static const AmpBiquad AMP_CAB_RESO_LOW[] = {\n')
for i, lo in enumerate(RESO_LOW):
    C.append(fmt(f'{CAB_SPECS[i][0]} reso low', lo))
C.append('};\n\n')
C.append('/* speaker resonance highs, per cab type */\n')
C.append('static const AmpBiquad AMP_CAB_RESO_HIGH[] = {\n')
for i, hi in enumerate(RESO_HIGH):
    C.append(fmt(f'{CAB_SPECS[i][0]} reso high', hi))
C.append('};\n\n')

C.append('/* cab voicing chains: HP + body + de-honk + presence + 4th-order LP,\n'
         '   one 5-section chain per cab type (index = CABTYPE) */\n')
C.append('static const AmpBiquad AMP_CAB_VOICE[AMP_CAB_TYPES][AMP_CAB_VOICE_N] = {\n')
for i, secs in enumerate(VOICE_SECTIONS):
    C.append(f'    {{ /* {CAB_SPECS[i][0]} */\n')
    for j, sec in enumerate(secs):
        C.append(fmt(f'section {j}', sec, indent='        '))
    C.append('    },\n')
C.append('};\n\n')

# --- cab IR kernels ---
C.append('/* cabinet IRs, one per cab type (index = CABTYPE) */\n')
for i, spec in enumerate(CAB_SPECS):
    ir = synth_cab(spec, CAB_IR_SEED + i)
    C.append(f'/* cabinet IR - {spec[0]}: {spec[1]} speaker(s). */\n')
    C.append(fmt_ir(f'AMP_CAB_IR_{spec[0]}', ir) + '\n')

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
