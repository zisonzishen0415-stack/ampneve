import numpy as np, os, sys
sys.stdout.reconfigure(encoding='utf-8')
FS = 44100.0
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, '..', 'core', 'ampsim_coeffs.h'))

# --- cabinet IR synthesis (static convolution kernel, replaces the old mic
#     biquads).  The IR is the miked-cab impulse response: the old mic
#     pickup's exact HP+LP response as the causal base, plus speaker-cone
#     modal resonances (damped ringing), a frequency-dependent room
#     scatter + diffuse early reflections, and all-pass phase dispersion
#     (the cone's time smear, zero magnitude change).
#     1024 taps @44.1k = 23.2 ms.  Tune the knobs below, then run
#     `python tools/gen_coeffs.py` to regenerate core/ampsim_coeffs.h. ---
CAB_IR_N = 1024            # taps (state: 1024 floats/channel of delay)
CAB_IR_INIT_DELAY = 29     # ~0.66 ms speaker-to-mic flight time
CAB_IR_ROOM_LEVEL = 0.028  # room scatter level (0 = off); sweep-verified:
                           # 0.018 too dry (decay40 12.5 ms), 0.040 too
                           # ripply (1.17 dB), 0.028 = decay40 15.9 ms /
                           # ripple 0.77 dB (old IR: 1.31 dB)
CAB_IR_ROOM_HP = 180.0     # scatter high-pass: keep the room off the IIR's
                           # 105/220 Hz bumps, but let the low tail ring
                           # (HP120 measured no decay gain, only +0.4 dB
                           # extra stacking at 220)
CAB_IR_ROOM_XOVER = 1000.0 # decay split: lows ring longer than highs
CAB_IR_ROOM_TAU_LOW = 0.016    # low-band room tail decay, seconds
CAB_IR_ROOM_TAU_HIGH = 0.005   # high-band room tail decay, seconds
CAB_IR_ROOM_ENERGY_LOW = 0.6   # low/high energy split (sums to 1)
# specular early reflections: (delay s, level dB rel IR peak) as mic-colored
# copies of the dry impulse - real early reflections ARE specular. Levels are
# capped so the comb ripple stays <= ~+-1.5 dB worst case (noise bursts were
# tried and measured WORSE: their flat spectra beat against the base per bin).
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
CAB_IR_SEED_NASH = 12345   # deterministic scatter seeds (stable builds)
CAB_IR_SEED_EMO = 67890
# cone modal resonances: (f0 Hz, tau s, boost dB at f0).
# Low end is DELIBERATELY thin: the IIR chain (105 Hz reso + 220 Hz body)
# owns 80-300 Hz and the 1024-tap IR only resolves ~43 Hz/bin there.
# 900 Hz moved to 1150 Hz to clear the 850 Hz tone-stack Mid center.
CAB_IR_RESON_NASH = [
    (350.0, 0.005, 2.0),   # low-mid body
    (1150.0, 0.004, 2.0),  # cone bloom (clear of the 850 Hz Mid)
    (1800.0, 0.0035, 1.0), # upper bloom
    (2800.0, 0.0028, 0.6), # edge ring
    (5600.0, 0.0012, 1.5), # SM57 paper on the 4k glass
]
CAB_IR_RESON_EMO = [
    (160.0, 0.006, 1.5),   # a hint of thump (low end mostly IIR's job)
    (420.0, 0.005, 1.5),   # midwest-emo body (reduced, see body500 chain)
    (1150.0, 0.004, 2.0),  # cone bloom
    (1700.0, 0.0032, 1.0), # upper bloom
    (2600.0, 0.0026, 0.6), # edge ring
    (5200.0, 0.0012, 1.0), # warmer paper than Nashville
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

def _apf(x, f0, q):
    """2nd-order all-pass (RBJ): flat magnitude, frequency-dependent phase."""
    w0 = 2.0 * np.pi * f0 / FS
    c = np.cos(w0)
    alpha = np.sin(w0) / (2.0 * q)
    b0, b1, b2 = 1.0 - alpha, -2.0 * c, 1.0 + alpha
    a0, a1, a2 = 1.0 + alpha, -2.0 * c, 1.0 - alpha
    return _lfilter(np.array([b0, b1, b2]) / a0, np.array([1.0, a1 / a0, a2 / a0]), x)


def synth_cab_ir(mic_sections, n, init_delay, resonances, room_level, seed,
                 room_hp, room_xover, room_tau_low, room_tau_high, room_energy_low,
                 early, allpass):
    taps = n - init_delay
    b, a = _cascade(mic_sections)
    imp = np.zeros(taps); imp[0] = 1.0
    base = _lfilter(b, a, imp)              # exact old-mic response, causal phase
    t = np.arange(taps) / FS
    ir = base.copy()
    Xb = np.fft.rfft(base, n=4096)
    fr = np.fft.rfftfreq(4096, 1.0 / FS)
    for f0, tau, db in resonances:
        m0 = np.abs(Xb[np.argmin(np.abs(fr - f0))])
        ir += _res_amp(f0, tau, db, m0) * np.exp(-t / tau) * np.sin(2.0 * np.pi * f0 * t + 0.7)
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(taps + 8192)
    shaped = _lfilter(b, a, noise)          # room scatter shaped by the mic EQ
    hpb, hpa = _cascade(butter(FS, room_hp, 2, 'highpass'))
    shaped = _lfilter(hpb, hpa, shaped)     # 80-300 Hz stays with the IIR chain
    # frequency-dependent decay: lows ring longer than highs (real rooms).
    lb, la = _cascade(butter(FS, room_xover, 2, 'lowpass'))
    noise_low = _lfilter(lb, la, shaped)
    noise_high = shaped - noise_low
    onset = int(0.0008 * FS)
    tail_low = np.zeros(taps); tail_low[onset:] = np.exp(-t[onset:] / room_tau_low)
    tail_high = np.zeros(taps); tail_high[onset:] = np.exp(-t[onset:] / room_tau_high)
    ir += room_level * (room_energy_low * noise_low[:taps] * tail_low +
                        (1.0 - room_energy_low) * noise_high[:taps] * tail_high)
    # specular early reflections: mic-colored copies of the dry impulse at
    # capped levels (comb ripple <= ~+-1.5 dB worst case).
    pk = np.max(np.abs(ir))
    for delay, db in early:
        k = int(delay * FS)
        if 0 < k < taps:
            ir[k:] += (10.0 ** (db / 20.0)) * pk * base[:taps - k]
    # cone phase dispersion: zero magnitude change, pure time smear.
    for f0, q in allpass:
        ir = _apf(ir, f0, q)
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
pres_dark = peaking(FS, 4200.0, 1.1, 3.5)
lp_dark = butter(FS, 9500.0, 4, 'lowpass')
C.append('/* cab DARK chain (TONE=0): woody 1x12, tighter/papery */\n')
C.append('static const AmpBiquad AMP_CAB_DARK[] = {\n')
C.append(fmt('hp95', hp95)); C.append(fmt('body220 +3dB', body)); C.append(fmt('midcut550 -2dB', midcut))
C.append(fmt('pres4200 +4.5dB', pres_dark))
for i,b in enumerate(lp_dark): C.append(fmt(f'lp9500 4th #{i}', b))
C.append('};\n#define AMP_CAB_DARK_N 5\n\n')

pres_bright = peaking(FS, 5500.0, 1.1, 5.0)
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
pres_dark_emo = peaking(FS, 3800.0, 1.1, 3.0)
lp_dark_emo = butter(FS, 9000.0, 4, 'lowpass')
C.append('/* cab DARK EMO (VOICE=1): woody, mid-forward, warmer top */\n')
C.append('static const AmpBiquad AMP_CAB_DARK_EMO[] = {\n')
C.append(fmt('hp95', hp_emo)); C.append(fmt('body220 +3dB', body_emo)); C.append(fmt('body500 +1.5dB', body500))
C.append(fmt('pres3800 +4dB', pres_dark_emo))
for i,b in enumerate(lp_dark_emo): C.append(fmt(f'lp9000 4th #{i}', b))
C.append('};\n#define AMP_CAB_DARK_EMO_N 5\n\n')

pres_bright_emo = peaking(FS, 4800.0, 1.1, 4.0)
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
mic_nash_ir = butter(FS, 70.0, 2, 'highpass') + [peaking(FS, 5800.0, 1.0, 1.0)] + butter(FS, 11000.0, 2, 'lowpass')
mic_emo_ir = butter(FS, 70.0, 2, 'highpass') + [peaking(FS, 5500.0, 1.0, 0.5)] + butter(FS, 10000.0, 2, 'lowpass')
ir_nash = synth_cab_ir(mic_nash_ir, CAB_IR_N, CAB_IR_INIT_DELAY, CAB_IR_RESON_NASH,
                       CAB_IR_ROOM_LEVEL, CAB_IR_SEED_NASH, CAB_IR_ROOM_HP,
                       CAB_IR_ROOM_XOVER, CAB_IR_ROOM_TAU_LOW, CAB_IR_ROOM_TAU_HIGH,
                       CAB_IR_ROOM_ENERGY_LOW, CAB_IR_EARLY, CAB_IR_ALLPASS)
ir_emo = synth_cab_ir(mic_emo_ir, CAB_IR_N, CAB_IR_INIT_DELAY, CAB_IR_RESON_EMO,
                      CAB_IR_ROOM_LEVEL, CAB_IR_SEED_EMO, CAB_IR_ROOM_HP,
                      CAB_IR_ROOM_XOVER, CAB_IR_ROOM_TAU_LOW, CAB_IR_ROOM_TAU_HIGH,
                      CAB_IR_ROOM_ENERGY_LOW, CAB_IR_EARLY, CAB_IR_ALLPASS)
# equalize voice loudness over the guitar band (80..5000 Hz) so the Emo/Edge
# voice doesn't sit ~4-8 dB quieter than Nashville (its room-scatter comb
# nulled the lows). Keeps each voice's spectral character, fixes the level.
def _band_rms(ir, lo, hi):
    X = np.fft.rfft(ir, n=8192)
    fr2 = np.fft.rfftfreq(8192, 1.0 / FS)
    sel = (fr2 >= lo) & (fr2 <= hi)
    return np.sqrt(np.mean(np.abs(X[sel]) ** 2))
rms_nash = _band_rms(ir_nash, 80.0, 5000.0)
rms_emo = _band_rms(ir_emo, 80.0, 5000.0)
ir_emo *= rms_nash / rms_emo
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

# --- preamp interstage Miller-cap lowpasses (v15) ---------------------
# Real tube gain stages roll off in the treble via Miller capacitance
# (a typical 12AX7 stage with a 68k source clips ~15.5 kHz, AikenAmps),
# and high-gain preamps (Soldano SLO, Mesa) add interstage RC networks
# that "short the highs and lows to ground" so each stage's harmonics are
# shaped before the next stage multiplies them - that is why cranked tube
# amps stay warm instead of piercing. Two poles:
#   V1 stage -> LP1 @ 9 kHz   (shapes the first stage's harmonics)
#   V2 cold clipper -> LP2 @ 5.0 kHz (shapes the 3-6 kHz harmonic cluster
#     before the tone stack boosts it - that cluster is what makes cranked
#     drive read "modern/Friedman" instead of warm)
# The tone stack / power amp / cab sit after LP2, so treble control,
# presence and the speaker resonance still add top end on purpose.
pre_lp1 = butter(FS, 9000.0, 2, 'lowpass')[0]
pre_lp2 = butter(FS, 5000.0, 2, 'lowpass')[0]
C.append('/* preamp interstage Miller-cap LPs (v15): shape stage harmonics */\n')
C.append('static const AmpBiquad AMP_PRE_LP1 = ')
C.append('{ ' + ', '.join(f'{v:.9f}f' for v in pre_lp1) + ' };\n')
C.append('static const AmpBiquad AMP_PRE_LP2 = ')
C.append('{ ' + ', '.join(f'{v:.9f}f' for v in pre_lp2) + ' };\n\n')

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
