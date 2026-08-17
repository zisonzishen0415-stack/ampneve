# AmpNeve Pedal — Circuit Design (schematic-level)

Effects pedal **platform**: mono guitar in → stereo out, native effect
library (AmpNeve #1, Reverson reverse reverb #2, stock effects), QSPI
effect/preset storage, USB-C loading. STM32H750 + CS4272 (stereo codec).
44.1kHz, 24-bit, latency ~2.5ms.

```
        ┌────────────── 9V DC (center-negative, 2.1mm jack) ──────────────┐
        │                                                                │
   [D1 reverse-prot]                                                [C_bulk 100µF]
        │                                                                │
        ├── 9V rail ──┬─────────────────────── op-amp rails (TL072x2)
        │             │
   [LDO1 3V3-dig]  [LDO2 3V3-aud]          Vref = 9V/2 (R divider + buffer)
        │             │
   H750 VDD/VDDA   CS4272 VA/VD            TL072 Vref pins
```

## 1. Power

| Net | Source | Loads | Notes |
|---|---|---|---|
| 9V | jack → D1 (SS34) reverse protection → fuse? (optional 500mA PTC) | op-amp rails, LDO ins | C1 100µF + 100nF |
| 3V3_D | LDO1 (AMS1117-3.3 or TPS7A20) | H750 VDD, SWD, OLED, encoders, logic | C2 10µF + 100nF |
| 3V3_A | LDO2 (LP5907-3.3, low noise) from 9V | CS4272 VA/VD/VL, input clamp ref | ferrite bead + 10µF + 100nF; star to AGND |
| Vref | R-divider 10k/10k from 9V → TL072 (unity buffer, 3rd op-amp channel or dedicated) | input stage + output stage bias | 4.5V, 100nF to GND, 10µF |

Grounding: AGND (analog) and DGND (digital) meet at ONE star point near the
codec; the codec's AGND/DGND are internally connected — keep the PCB split
under the CS4272 (see layout notes).

## 2. Input stage (hi-Z buffer + protection + calibration)

```
Guitar tip ──┬── R1 1MΩ ──┬── R2 1kΩ ──┬── TL072A (+) ──┬── C3 1µF ── CS4272 AINL
             │            │            │   (-)──┐       │
            GND          D2/D3        C4 100pF  │       │
             (1MΩ input    1N4148/   (RFI)     └───┬───┘
              impedance)   BAT54S    (to Vref)
                           clamp to
                           Vref±0.7
TL072A: unity follower biased at Vref (pin3 = Vref via 100k? use direct).
  (+) ← Vref  (bias)
  (–) ← output (unity)
C3 1µF AC-couples into the codec; CS4272 input HPF (register) or C3 size
  sets the low corner (1µF + ~50k codec input ≈ 3 Hz - use codec HPF on).
R2 + C4 form a simple RFI/EMI filter (fc ≈ 1.6MHz).

Calibration: replace R1 or add a series trimmer (10k log or 50k lin in
feedback) - target: DI recorded at -12..-6 dBFS peak lands at the codec
full-scale headroom, same reference as the VST.
```

## 3. Codec — CS4272 (I2S, codec = master)

```
            11.2896 MHz crystal (256 x 44.1k)
                   │ │ 18pF each
                XTI/XTO
CS4272:  MCLK   ← from XTI/XTO (internal osc enabled)
         SCLK   → STM32 SAI1_SCK_A     (I2S bit clock, 64 x fs)
         LRCK   → STM32 SAI1_FS_A      (word select)
         SDOUT  → STM32 SAI1_SD_A      (ADC -> MCU)
         SDIN   ← STM32 SAI1_SD_B      (MCU -> DAC; or tie SDIN low and
                                        route DSP output back into SDIN
                                        externally if using one SD line)
         (Simplify: MCU plays BOTH roles - SD_A receives, SD_B sends.)
         RST    ← GPIO (10k pull-up + 100nF)
         AD0    → 3V3  (I2C addr 0x4C)
         AD1    → GND
         MCLK sel → 256x (MCLK = 256 x fs via crystal; set mode bits)
         AINL   ← input stage (guitar)
         AINR   ← second input (optional) or AC-GND to Vref (stereo-ready)
         AOUTL  → output stage L
         AOUTR  → output stage R
         VA/VD/VL = 3V3_A; VA and VD filtered (100nF each + 10µF)
         I2C: SCL/SDA with 4.7k pull-ups to 3V3_A
```

Note: SD_A and SD_B are separate SAI1 lines; the block processes both the
codec ADC (SD_A) and DAC (SD_B) — standard H7 dual-SAI audio pattern.
Stereo: CS4272 is 2-in/2-out; the platform uses AINL (mono guitar) and
both AOUTL/AOUTR (reverb/delay/chorus effects go stereo).

## 4. MCU — STM32H750VBT6 (LQFP100)

| Function | Peripheral | Notes |
|---|---|---|
| HSE | 24 MHz crystal + 2x20pF | PLL to 480 MHz |
| Boot | BOOT0 → GND (boot from flash) | BOOT1 float |
| Debug | SWDIO/SWCLK header + NRST | 4-pin 1.27mm |
| I2S | SAI1 (A: SCK/FS/SD_A; B: SD_B) | pins per CubeMX AF table (e.g., SAI1_SCK_A=PB13, SAI1_FS_A=PB12, SAI1_SD_A=PB12 variants - confirm in datasheet); MCLK not needed (codec master) |
| Codec ctrl | I2C1 (PB6/PB7) or I2C2 | shared bus with OLED |
| OLED | I2C (addr 0x3C), + RST GPIO | SSD1306 128x64 |
| Encoders | 4x GPIO pairs + push (TIM enc or GPIO+EXTI) | EC11; 10k pull-ups, 100nF debounce |
| Footswitch | GPIO input (pull-up + RC debounce) | momentary |
| Bypass LED | GPIO + 1k | + optional PWM |
| UART | USART2 (PA2/PA3) | verification dump |
| USB | OTG_FS (PA11 DM / PA12 DP), USB-C connector + VBUS sense + 5V from 9V via buck/LDO | firmware + effect/preset loading |
| QSPI | W25Q64 (CLK PB2, NCS PB6, IO0-3 PD11/PD12/PE2/PD13 — per CubeMX AF) | effect library + presets (mandatory) |
| VDDA | ferrite + 1µF + 100nF | filter from 3V3_D |
| VBAT | → 3V3_D (or battery) | |
| NRST | 10k pull-up + 100nF | |
| FS1/FS2 | 2x GPIO inputs (pull-up + RC debounce) | bypass + effect/preset |

## 5. Output stage (stereo)

```
CS4272 AOUTL ──┬── TL072B (+) ──┬── C5 10µF ── R3 100Ω ── TRS tip (L)
               │   (-)──Rf/Rg──┘                (series out)
               │   bias: (+) via 100k to Vref (gain ~1.1x with Rf=10k/Rg=100k
               └── (codec outputs are differential-capable; use
                    single-ended AOUTL with internal VQ or external Vref)
CS4272 AOUTR ──┬── TL072C (+) ──┬── C6 10µF ── R4 100Ω ── TRS ring (R)
               └── same topology
C5/C6 block the Vref DC; R3/R4 set output impedance (~100Ω).
TRS jack: tip = L, ring = R, sleeve = GND (stereo out).
```

## 6. Bypass

Soft bypass (default): DSP passes input through when bypassed — the codec
ADC->DAC path stays live; MCU toggles a flag (no pop: crossfade 10ms).
Optional true bypass: relay (G5V-1 or small DPDT) driven by a GPIO+MOSFET
(2N7002) from the footswitch logic; relay coil flyback diode; make-before-
break contacts to avoid switch pop.

## 7. Full netlist (top level)

| Net | From | To |
|---|---|---|
| 9V | jack+ | D1, LDO1 in, LDO2 in, Vref divider, USB 5V regulator |
| 3V3_D | LDO1 | H750, OLED, encoders, LED |
| 3V3_A | LDO2 | CS4272 VA/VD/VL, clamp, codec I2C pull-ups |
| Vref | divider+TL072 | input stage bias, output stage bias |
| I2S_SCK | CS4272 SCLK | H750 SAI1_SCK_A |
| I2S_FS | CS4272 LRCK | H750 SAI1_FS_A |
| I2S_SDI | CS4272 SDOUT | H750 SAI1_SD_A |
| I2S_SDO | H750 SAI1_SD_B | CS4272 SDIN |
| I2C_SCL | H750 | CS4272 + OLED (4.7k pull-ups) |
| I2C_SDA | H750 | CS4272 + OLED |
| ENC1_A/B/SW | EC11 | H750 GPIO x3 (x4 encoders) |
| FS1/FS2 | footswitches | H750 GPIO |
| LED_B | H750 GPIO | bypass LED + 1k |
| QSPI | H750 | W25Q64 (CLK/NCS/IO0-3) |
| USB | H750 OTG_FS | USB-C (DM/DP/VBUS) |
| UART_TX/RX | H750 | debug header |
| SWDIO/SWCLK | H750 | debug header |

## 8. BOM (approx, CNY)

| Part | Value/Type | Qty | ~CNY |
|---|---|---|---|
| MCU | STM32H750VBT6 (on core board) | 1 | 30-60 |
| Codec | CS4272 (SOIC-28) | 1 | 25-40 |
| QSPI flash | W25Q64 | 1 | 6 |
| USB-C | connector + ESD (USBLC6-2) | 1 | 5 |
| Crystal | 11.2896 MHz HC-49S | 1 | 2 |
| Crystal | 24 MHz | 1 | 2 |
| Op-amps | TL072 x2 (or NE5532 x2) | 2 | 4 |
| LDOs | AMS1117-3.3 + LP5907-3.3 | 2 | 5 |
| OLED | SSD1306 128x64 I2C | 1 | 15-25 |
| Encoders | EC11 w/ push | 4 | 12 |
| Footswitch | momentary stomp | 1 | 8 |
| Jacks | 2x mono TRS/TS + 9V DC | 3 | 10 |
| Passives/caps/diodes | 0603/0805 kit | - | 20 |
| Enclosure | 1590B / 125B + knobs | 1 | 30 |
| Optional relay | G5V-1 + 2N7002 | 1 | 8 |
| **Total** | | | **~170-230** |

## 9. Layout & grounding notes

- Star ground near the CS4272; AGND/DGND meet there.
- MCLK (11.2896MHz) trace short, away from I2S lines.
- Codec analog pins: keep AIN/AOUT away from digital I2C/I2S.
- 100nF decoupling on every power pin; 10µF bulk per rail.
- Encoder/switch debounce in software (10ms) + RC on hardware (100nF).
- The core-board route (H750 board + separate codec/analog board) is the
  fast prototype path; a 2-layer PCB is feasible, 4-layer (dedicated
  ground plane) recommended for the final.

## 10. Bring-up order

1. Power rails (9V, 3V3_D, 3V3_A, Vref) - no codec/MCU yet.
2. SWD + blink.
3. I2C scan: CS4272 (0x4C) + OLED (0x3C) present.
4. I2S loopback: CS4272 in master mode, read/write silence - verify
   clocks with a scope on SCLK (2.8224 MHz) / LRCK (44.1 kHz).
5. DSP port + compare_gain verification (see AMPNEVE-PEDAL-DESIGN.md).
6. Encoders/OLED/footswitches.
7. Analog path calibration (input trim to -12..-6 dBFS reference).
8. QSPI: effect library + presets; USB: loader + transfer.
9. Stereo verify: reverb-type effect L/R output on TRS.
