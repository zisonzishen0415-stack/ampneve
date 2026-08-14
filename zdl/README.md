# AmpNeve ZDL (Zoom MultiStomp)

Custom AMP-category effect for the Zoom G1on / MS-series (ZDL). Original
boutique-amp DSP, strict real-amp structure (v15): input trim (fixed) ->
V1 (fixed ~4x gain, soft asymmetric clip, Miller LP @ 9 kHz) -> V2 cold
clipper (Gain knob + touch dynamics, asymmetric clip, Miller LP @ 5 kHz)
-> Klon-style clean/dirty mix (V1 clean tap + V2 driven path, both through
the tone stack and power amp) -> tone network -> phase inverter + push-pull
power amp with sag -> Neve transformer + 1073-style EQ -> speaker
resonance + cabinet voicing + 1024-tap cabinet IR convolution (miked-cab
kernel: mic + cone resonances + room) -> level.
Nine knobs across the LineSel pages (mirrors the VST). Voice is **fixed to
Nashville** on the pedal: the firmware's visible-knob ceiling is 9 (3 pages
x 3, reverson ABI.md 3.1), so the VST's Voice switch has no ZDL slot. An
Emo/Edge variant can be built by setting `AMP_PARAM_VOICE` to `1.0f` in
`amp_zdl_init()` (one-line change):

| Page | Knob | Param | Range | Maps to |
|---|---|---|---|---|
| P1 | 1 | Bass | 0..1 | tone low (0.5 flat) |
| P1 | 2 | Mid | 0..1 | tone mid (0.5 flat) |
| P1 | 3 | Treble | 0..1 | tone high (0.5 flat) |
| P2 | 1 | Gain | 0..1 | V2 cold-clipper drive (touch dynamics, ~17x max) |
| P2 | 2 | Master | 0..1 | PI + push-pull power drive + sag amount |
| P2 | 3 | Level | 0..1 | output (0.20..0.90 gain) |
| P3 | 1 | Neve | 0..1 | coloration wet/dry (0 = bypass) |
| P3 | 2 | Cab | 0..1 | cabinet dark(0)..bright(1) |
| P3 | 3 | Presence | 0..1 | speaker 3.5 kHz resonance |

Voice: fixed Nashville (0). See above - the 9-knob ceiling leaves no slot
for the VST's switch; bake an Emo/Edge variant instead.

Input trim is fixed at 1.0 (the calibrated reference) and takes no knob:
the pedal's hardware INPUT VOL does the level matching before the DSP,
exactly like plugging into a tube amp.

## State memory

The ZDL needs no preset system: the pedal stores every effect parameter
inside the saved patch, and the DSP reads `params[]` from the host every
block. Saving the patch on the pedal is exactly "remembering the last
state" - power off/on and reloading the patch restores the knobs. Factory
defaults (used only when a patch's param slot is invalid) match the VST:
gain 0.35, master 0.55, level 0.75, presence 0.85.

## Hardware / build status

- The core is ZDL-safe (no heap, no double, no math library, no division;
  all filter coefficients are precomputed constants). Verified statically by
  `tools/check_zdl_safe.py`.
- State lives in the host-managed `ctx[3]` arena (one Ampsim instance per
  channel, ~9.8 KB of DSP state total; 1248 floats reserved each - most of
  it the IR's rolling delay buffer, the kernel itself is static const).
- Every build runs a post-compile safety audit (`tools/check_zdl_obj.py`)
  that parses the `.obj` and fails on the frozen-pedal patterns from
  SAFE-DSP-RULES: writable `.fardata`, uninitialised statics, unexpected
  undefined symbols, SBR/B14-relative relocations, missing `.audio` section.
- NOT yet hardware-tested. The main open risk is CPU budget: ~1200
  operations per sample per channel (20 biquads + 1024-tap FIR + saturation)
  is plausible for the C674x-class DSP but the ZDL host's per-effect time
  slice is unmeasured. The 512-tap fallback is in `tools/gen_coeffs.py`
  (`AMP_CAB_IR_N`). Follow the hardware test sequence below.

## Build requirements

- TI C6000 Code Generation Tools (cl6x, e.g. ti-cgt-c6000 8.5.0 LTS) - free
  download from ti.com (requires a free TI account). Install anywhere and
  point `AMPNEVE_TI_ROOT` at it.
- Python 3 (the build and the .obj audit need no other Python packages).
- The ZoomMultistompZDL toolchain (linker.py). `build.py` looks for it at
  `../zoomreverse/ZoomMultistompZDL` or `AMPNEVE_ZDL_ROOT`.

## Build

```sh
export AMPNEVE_TI_ROOT=/path/to/ti-cgt-c6000_8.5.0.LTS

# 1. SMOKE build (audio NOPed - pass-through). Always flash this first.
python3 build.py --smoke
# -> ../dist_smoke/AmpNeve.ZDL

# 2. Real DSP build (post-compile .obj audit runs automatically).
python3 build.py
# -> ../dist/AmpNeve.ZDL
```

Load with Zoom Effect Manager (point it at the `dist/` folder), per
reverson/research_docs/docs_INSTALLING-ZDLS.md.

Compiler flags follow the reverson hardware-proven release pattern:
`-O2`, no `--opt_for_space` (high `--opt_for_space` emits
`__c6xabi_push_rts/pop_rts` helper calls whose stubs are not hardware-tested
here). Set `AMPNEVE_OPT_FOR_SPACE=n` only to experiment.

## Hardware test sequence (G1on)

1. **Smoke**: build with `--smoke`, flash `dist_smoke/AmpNeve.ZDL`, verify
   the effect appears in the AMP category, the three pages and the Voice
   switch render, and knob turns do not freeze the pedal. A freeze here is a
   loader/edit-handler problem, not DSP.
2. **Real DSP**: build normally, flash `dist/AmpNeve.ZDL`, play. Listen for
   dropouts, zipper noise, or freezes - these mean the per-effect CPU budget
   is exceeded (most likely suspect: the 1024-tap cabinet FIR).
3. **Stress**: put AmpNeve in a patch with delay + reverb and play at max
   Gain. If it survives, record the firmware version and fxid behaviour
   (whether 0x01A4 collides with anything in that firmware).
4. **Fallback**: if step 2/3 fails, rebuild with a shorter IR (halve
   `AMP_CAB_IR_N` in `tools/gen_coeffs.py`, regenerate `ampsim_coeffs.h`,
   rebuild). CPU drops ~50%.
5. Only after the pedal loads and plays cleanly should the ZDL be called
   hardware-verified.
