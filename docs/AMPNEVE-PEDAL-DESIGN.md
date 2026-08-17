# AmpNeve Pedal — Full-Algorithm Hardware Design

Status: design (2026-08-15). Goal: a real hardware pedal running the **full
AmpNeve algorithm** (2048-tap real-IR convolution, no compromise), verified
byte-for-byte against the x86 reference via `tools/compare_gain.py`.

## 1. Platform decision

| | STM32H750 (chosen) | ESP32-S3 (rejected for full algo) |
|---|---|---|
| CPU | Cortex-M7 @480MHz, single-precision FPU, 128KB DTCM | LX7 @240MHz, no FPU |
| FIR 2048-tap mono 44.1k | CMSIS-DSP `arm_fir_f32` ≈ 1.5-2.5 cyc/tap → **38-54% of core** | 3 cyc/tap (esp-dsp) → 113% — over budget |
| Rest of chain (~15 biquads + clips + env) | < 5% (FPU) | soft-float → ~50%+ |
| Availability (CN) | core boards ¥30-50, everywhere | cheap, but needs SIMD/定点 port |
| Verdict | **full algorithm fits with headroom** | needs 1024-tap or fixed-point |

The core's own ring-buffer FIR (branch-based wrap) would cost ~4-6 cyc/tap on
M7 → ~94% core: **too tight**. The convolution MUST be `arm_fir_f32`
(block-based, unrolled) or an equivalently optimized loop. Everything else
(ampsim chain) is sample-based plain C and ports as-is.

## 2. Performance budget (mono, 44.1kHz)

| Block | Cost | Core % |
|---|---:|---:|
| 2048-tap FIR (arm_fir_f32, ~2 cyc/tap) | 181M cyc/s | 38% |
| Amp chain (biquads, clips, env, tone) | ~100-200 cyc/sample | ~5% |
| DC blocks, blends, crossfade | ~30 cyc/sample | ~1% |
| **Total** | | **~45% of one 480MHz core** |

Latency: block 64 samples = 1.45 ms + codec ~1 ms ≈ **2.5 ms** (inaudible).
Memory: IR 8KB + dual delay 16KB + state ~17KB → all in DTCM (128KB) ✓.

## 3. Hardware

```
Guitar → [TL072 hi-Z buffer ~1MΩ + clamp] → CS4272 ADC (I2S, 24-bit)
                                                  ↑
9V DC → 5V/3.3V LDO(s) ← 24V/12V-OK for pedals — CS4272 MCLK 11.2896MHz (own crystal)
                                                  ↓
      STM32H750 (SAI1 slave, I2C, ADC pots, GPIO footswitch/UART)
                                                  ↓
CS4272 DAC → [op-amp line driver] → mono TRS out / headphones amp
```

Key decisions:
- **Codec = master**: CS4272 runs from its own 11.2896 MHz audio crystal
  (256×44.1k), generates BCLK/LRCK; STM32 SAI1 in slave mode. No MCU clock
  jitter on the audio path, no audio-PLL configuration rabbit hole.
- **CS4272** (24-bit, 114dB, I2S, cheap, common) — or ES8388 as fallback.
- **Hi-Z input**: TL072/NE5532 unity follower, ~1MΩ, input clamp diodes,
  trim for -12..-6 dBFS DI reference (same calibration as the VST).
- **Power**: 9V DC jack → buck/linear to 3.3V analog + 3.3V digital,
  star grounding, audio-grade decoupling on CS4272 rails.
- **Controls** (4 push-encoders + OLED; mirrors the VST module concept):

  | Knob | Rotate | Short press | Long press |
  |---|---|---|---|
  | K1-K3 | current module's params (TONE: Bass/Mid/Treble, AMP: Gain/Master/Level, CAB: Neve/Cabtype/Presence) | reset that param to default (VST double-click equivalent) | — |
  | K4 | **Input trim (constant**, like the VST's 4th knob) | reset Input to 1.0 | — |
  | K1 long | — | — | cycle module TONE->AMP->CAB |
  | K2 long | — | — | bypass toggle (synced with footswitch) |
  | K3 long | — | — | save current as preset (10 flash slots) |
  | K4 long | — | — | load next preset |
  | Footswitch | — | bypass (LED) | (reserved: tuner) |

  - Encoders: EC11-style detented, endless; rotation = value step (fine
    under slow turns); values shown on the OLED (no pointer on the knob).
  - OLED 128x64 (SSD1306, I2C): main screen = module name + 4 param rows
    (name, value bar, value) + input meter + bypass state; feedback
    screens for reset/preset operations.
- **Debug/verification**: UART or USB-CDC dump of rendered test WAVs.

## 4. Software architecture

```
main loop:   ADC pots (10-bit) → debounce → param smoothing (~15ms one-pole)
             encoder/footswitch state machine → bypass, module select
             OLED render (30 Hz)
I2S ISR (64-sample blocks):
             read CS4272 block → Ampsim_process per sample
             (core/ampsim.c verbatim; FIR swapped to arm_fir_f32)
             → write block
verification: test-signal WAVs compiled in → run → dump output over UART
             → compare with x86 render (compare_gain protocol)
```

Porting notes:
- `core/ampsim.c` + `core/ampsim_coeffs.h`: plain C, no OS, no heap — port
  verbatim (one `Ampsim` instance, mono).
- FIR: replace the inner loop with `arm_fir_f32` using the same kernel
  (`AMP_CAB_IR_*`) and a 2048+63-sample state buffer; keep the dual-buffer
  crossfade by running two instances of the FIR (or the core loop during
  the ~12 ms fade — fade is rare, correctness first).
- Float32 everywhere (M7 FPU); `-O2 -ffast-math` acceptable (verified
  against the reference).
- Sample rate fixed 44100; biquad/IR constants already 44.1k.

## 5. Verification protocol (the important part)

1. Compile the same test set as `tools/compare_gain.py` into the firmware
   (1k/12k sines, sweep, DI clip at 44.1k).
2. Dump the pedal's output (UART, e.g. 16-bit PCM hex) and diff against the
   x86 `ampsim_render_analyze` render of the same input.
3. Acceptance: max sample diff < 2 LSB (float32 vs x86 rounding) and all
   compare_gain metrics (THD/folded/DC/response) match within 0.1 dB.
4. Only then: knob/OLED/switch integration and sound checks.

## 6. Roadmap

| Phase | Deliverable | Est. |
|---|---|---|
| 0. Board bring-up | H750 core board + CS4272 board, I2S loopback, UART printf | 1-2 weeks |
| 1. DSP port + verify | core ports, FIR optimized, compare_gain protocol passes on target | days |
| 2. Control surface | pots/encoder/OLED/footswitch + smoothing + bypass | 1 week |
| 3. Hardware finalize | schematic → PCB (input buffer, power, codec, MCU) → prototype → enclosure | 2-4 weeks |
| 4. Polish | true-bypass relay, noise floor (<-100dB), battery option, preset storage (flash) | 1 week |

## 7. Risks & fallbacks

- 2048-tap FIR on arm_fir_f32 in DTCM is the whole design; if it measures
  >60% core, fall back to 1024-tap (23 ms - still full "cab sound") or
  split the IR (direct 128 taps + remainder via a second FIR) — algorithm
  stays unmodified otherwise.
- CS4272 availability → ES8388 (same I2S, slightly lower SNR).
- Codec-master requires the MCU SAI to lock to external clocks: use
  `HAL_SAI_Init` with the FSD/SCK polarity of the codec and verify with a
  loopback before DSP work.
