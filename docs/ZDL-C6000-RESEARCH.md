> **项目状态（2026-08-16 修订）：失败。**
> 经 G1on 实机多次验证，自制 ZDL **无法在 G1on 上稳定工作**（表现为：无 UI、bypass、或切换死机）。
> 此前文档中的“已验证 / 可用 / 可运行 / 已对齐 / 可收口”等表述，均不代表 G1on 实机可用。
> 本项目的 ZDL 目标当前**未达成**，现状为失败；任何后续结论不得以“G1on 可用”为前提。

# ZDL C6000 Platform Research & Budget Findings

Status: consolidated 2026-08-14 from the AmpNeve ZDL bring-up session.
All measurements were made on this machine against the REAL B1on firmware
(`zoomreverse/ZoomMultistompZDL/firmware/`) and real stock ZDLs
(`.../stock_zdls/`), compiled with the actual TI cl6x 8.5.0 LTS toolchain
and read back with dis6x. No numbers here are borrowed from third-party
claims.

## 1. Platform facts (B1on firmware, this machine)

- Firmware runs **TI SYS/BIOS** (Hwi / Swi / Task threads; the audio
  callback is a real-time thread context).
- Firmware built with **TI CGT v7.3.7** (older than our 8.5.0 LTS - fine,
  the ABI is compatible; `.c6xabi.attributes` in our objects declares the
  ISA for dis6x).
- Target ISA: **C674x** (`-mv6740`, TI manual SPRAB89B).
- Sample rate: **44.1 kHz** (core hardcodes it; consistent with the
  firmware's cycleEnd assumptions).
- **Stock ZDL binary layout** (B1Xon__BASSDRV.ZDL, 18 286 bytes):
  - ELF starts at file offset **76** (76-byte Zoom header), ET_DYN,
    EM_TI_C6000.
  - The audio function lives in **`.text`** (PROGBITS, ALLOC|EXEC, 6368 B),
    NOT in a `.audio` section (stock effects do not use CODE_SECTION; the
    loader finds the entry via the descriptor).
  - `.symtab`/`.strtab` are present but **function names are stripped**
    (only section symbols, data objects like `BassDrive` in `.const`, and
    `$C$L` labels remain). `.debug_info` is present (stock builds ship
    with debug sections).
  - `.fardata` = 24 bytes (tiny; stock effects keep writable statics
    minimal, like our rule).
- **Stock effects never use long FIRs**: `.const` is ~2.0-2.6 KB across
  BASSDRV / BAS_PRE / AC_BPRE / BASS_BB / BASS_TS / B_OD - enough for
  knob bitmaps + small tables, not for an 8 KB 2048-tap kernel. The bass
  preamp/amp (BAS_PRE) is waveshaper + EQ class.

## 2. What cl6x does with our core (measured, not assumed)

Compile: `cl6x --c99 --opt_level=2 -mv6740 --abi=eabi
--mem_model:data=far` (the ZDL build flags).

- **Ring buffer auto-restructured to the double-write trick**: the
  2048-tap FIR loop has ZERO modulo branches. cl6x saw the circular
  access and emitted writes of each input sample at two positions
  2048 apart, so the convolution reads linearly. This is exactly the
  manual optimization we would have written - the compiler did it.
- **FIR loop is software-pipelined** (SPLOOPD): 17 instructions per
  2 taps (LDNDW loads 2 floats, 3x MPYSP + 3x ADDSP in the pipeline).
  Realistic floor ~1.5 cycles/tap (VLIW packet estimate, not silicon).
- `#pragma MUST_ITERATE(AMP_CAB_IR_N, AMP_CAB_IR_N, 2)` before both FIR
  loops locks the pipelined form (guarded by `__TI_COMPILER_VERSION__`,
  ignored by MSVC/gcc).
- `bq_run` was NOT inlined by default: ~8 CALLP per sample in
  `Ampsim_process`. `#pragma FUNC_ALWAYS_INLINE(bq_run)` halved the call
  count (8 -> 4). Implementation-only; renders verified bit-identical.
- **No math-library helpers anywhere**: `recip_approx` is the
  bit-trick + Newton; the audio path is multiply-add only.
- `__cregister volatile unsigned int TSCL/TSCH` (c6x.h): the compiler
  emits pure `MVC.S2 TSCL,B4` - a cycle counter with zero helper cost.
  This is the basis of the `--meter` probe.

## 3. Budget estimate (stereo, per sample)

| Part | Cycles (estimate) |
|---|---|
| FIR 2 x 2048 taps @ ~1.5 cyc/tap | ~6144 |
| 20 biquads + saturators + envelopes (inlined) | ~800-1200 |
| **Total stereo steady state** | **~7000-7500** |
| Cab crossfade (both FIRs run, ~12 ms) | ~2x transiently |

At 44.1 kHz that is ~310-330 M cycles/s: >100% of a 300 MHz-class C674x
core, ~70% at 456 MHz. These are estimates from kernel instruction
counts - the ONLY ground truth is the `--meter` probe on the pedal.

## 4. Comparison with every known-working effect (same compiler, measured)

| Effect | .text instr (code size) | SPLOOP loops | MPYSP | DSP class | est. cyc/sample |
|---|---|---|---|---|---|
| Stock BASSDRV (B1on firmware) | 1136 | 6 | 105 | waveshaper + EQ | ~200-500 |
| ToTape9 (hardware-confirmed) | 1367 | 0 | 249 | tape echo, IIR | ~300-600 |
| VerbTiny | 1530 | 3 | 100 | reverb (delay lines) | ~200-400 |
| Galactic | 584 | 1 | 43 | reverb | ~100-200 |
| **AmpNeve** | 444 code + 2 x 2048-tap FIR | 2 | - | **long FIR convolution** | **~7000-7500** |

Structural conclusion: every known-working effect (stock firmware,
community Airwindows ports, and the Reverson FDN reverb) is
per-sample IIR / delay-line DSP - delay lines cost ~1 op/sample, a whole
reverb ~30 ops/sample. AmpNeve is the only effect built on a long FIR
convolution and executes ~4096 kernel iterations per sample - roughly
**10-40x the executed cost of the heaviest known-working effects**.

The G1on/B1on can run 4-5 stock effects per patch, but each is tiny; the
whole stock chain is ~1000-3000 cycles/sample class. Our single effect
is in that ballpark alone.

## 5. The plan that follows (no algorithm compromise)

- Requirement: **AmpNeve or Reverson alone in a patch** - no chain budget.
  The full-algorithm direct-form build stays as the candidate.
- **`--meter` probe build** (zdl/build.py --meter): TSCL reads around the
  8-sample block, smoothed every 64 blocks, exposed on the Input knob
  display as cycles/8samples / 60000 (0.9-1.0 = full-algorithm load).
  Caveats to verify on hardware: whether the host clobbers params[]
  writes per block (display mechanism), and the DSP clock.
- Hardware sequence: `--smoke` (load/UI) -> `--meter` (budget) ->
  normal (listen).
- If the meter says over budget: **hybrid partitioned convolution**
  (direct 64-tap head + hand-written radix-2 FFT tail, overlap-add,
  math-identical output, ZDL-safe: multiply-add + const twiddle table
  generated by gen_coeffs.py). Expected ~500-800 cycles/sample, i.e.
  community-class. Not built yet - only if the meter demands it.

## 6. Open questions (hardware-only)

- DSP clock (200 / 300 / 456 MHz class) - PLL config is in the boot
  code; no documentation found. The meter display makes this moot: the
  value is cycles, not time.
- Per-effect time slice of the ZDL host runtime - the meter answers it.
- Does the host overwrite params[] every block (meter display
  mechanism) - probe behavior to observe on first flash.

## 7. Reproducible measurement method

1. `cl6x ... -c core/ampsim.c --output_file=x.obj` (same flags as zdl/build.py)
2. `dis6x x.obj > x.dis` (UTF-16 output; parse address+mnemonic lines)
3. Count instructions; find `SPLOOPD..SPKERNEL` kernels; count CALLP/MPYSP.
4. For raw stock .text chunks: wrap in a minimal ELF with
   `.c6xabi.attributes` + `.TI.section.flags` first (dis6x needs the
   compact-instruction hint; see firmware/wrap_for_dis6x.py pattern).
5. Stock ZDL .text extraction: ELF at file offset 76, section offsets
   relative to the ELF start.
