# B1on / MS-series Firmware Research

Platform notes for the Zoom G1on / B1on / MS-70CDR family (C674x + SYS/BIOS).
Same-platform reverse engineering applies 1:1 across these devices; only the
effect library (FS.bin), factory patches (Preset.bin) and UI strings differ.

Materials on disk (`zoomreverse/ZoomMultistompZDL/firmware/`):

| File | Size | Contents |
|---|---|---|
| `boot.bin` | 262144 | SYS/BIOS bootloader + `Task_VersionUpdateMIDI` (USB-MIDI update task) |
| `Main.bin` | 720896 | Main OS image (effect engine, loader, tuner) |
| `FS.bin` | 2912256 | Effect library image: 138 ZDLs + FLST_SEQ.ZDT |
| `Preset.bin` | 12288 | Factory patches ("DualVerb", "Reverse", ...) |

Compiler: TI CGT 7.3.7 (FS.bin ZDLs), 7.3.1 (one leftover effect).

---

## FS.bin layout (fully mapped, 2026-08-15)

### Global header (0x0000)

```
55 AA 00 01 04 00 C8 00 C7 02 FF FF FF FF FF FF ... (rest FF)
```
Raw fields (u16 LE): `0x55AA` magic, `0x0100`, `0x0004`, `0x00C8` (200),
`0x02C7` (711). Interpretation TBD — candidates: table count (4), block
statistics (200 free / 711 something). Tables observed start at 0x3008.

### File tables (32-byte items)

Item layout (matches the Zoom-Firmware-Editor updater format):

```
+0  u16  block number (data region, see below)
+2  u8   0x01
+3  u8   0xFF
+4  u32  exact file size in bytes
+8  char[12] filename (e.g. "SLWATK.ZDL\0\0"), padded with 0
+20 12 bytes 0xFF
```

Tables found (contiguous item runs; category subsets + one master table):

| Table @ | Items | First item | Role |
|---|---:|---|---|
| 0x3008 | 12 | 160_COMP.ZDL | category subset |
| 0x31A8 | 15 | DETUNE.ZDL | category subset |
| 0x33A8 | 108 | DELAY.ZDL | category subset |
| 0x5008 | 28 | 160_COMP.ZDL | category subset |
| 0x53A8 | 108 | DELAY.ZDL | category subset (duplicate) |
| 0x7008 | 137 | 160_COMP.ZDL | category subset |
| 0x9008 | 138 | 160_COMP.ZDL | **master table** (137 ZDL + FLST_SEQ.ZDT) |

Category tables duplicate entries from the master table; the master table at
0x9008 is the authoritative one. FLST_SEQ.ZDT is the last item.

### Data region

```
offset(block) = 0xA006 + block * 4096
```
- Blocks are 4096 bytes, each with a 6-byte chain header:
  `[u16 prev_block][u16 next_block][u16 data_size]` — 0xFFFF terminates the
  chain; first block of a file: prev=0xFFFF; multi-block files chain through
  consecutive blocks (data_size <= 4090).
- First data block (0x0001, B_GEQ.ZDL) is at 0xB006, immediately after the
  header — ZDL bytes are stored contiguously across the chain, no padding.
- The item's block number counts from the data-region base; block 0x0001 = 0xB006.

Verified: all 137 table ZDLs have their `\0\0\0\0SIZE` signature exactly at
`0xA006 + block*4096`.

### FLST_SEQ.ZDT (menu order)

Last item (block 0x0255, 4108 bytes). Plain list of filenames in **menu
order**, `NUL`-separated, with `>>>` / `<<<` page markers (152 entries):
COMP.ZDL, RACKCOMP.ZDL, M_COMP.ZDL, OPTCOMP.ZDL, 160_COMP.ZDL, LIMITER.ZDL,
SLWATK.ZDL, ZNR.ZDL, NOISEGTE.ZDL, ...

### Orphan / hidden effect

Block 0x0032 (0x3C006) holds a ZDL **not present in any file table**:
**SilkyCho** (silky chorus) — 11 params (OnOff/LoMix/HiMix/ChMix/LoPit/HiPit/
PreD/Rate/Depth/Tone), compiled with CGT 7.3.1 (older than the 7.3.7 of the
shipped library). The shipped SILKYCHO.ZDL is a different, larger build
(24579 B vs 20914 B).

**Conclusion:** firmware updates append new effect versions and only edit the
file table + FLST_SEQ; superseded data blocks stay behind as orphans. This is
exactly the mechanism firmware-level effect injection must replicate: patch
the master table (add/replace item), patch FLST_SEQ (menu order), append or
reuse data blocks.

---

## boot.bin (update bootloader) — container cracked, 2026-08-15

`boot.bin` is a **"TIPAcYSX" container**: a header + a stream of tagged
chunks. Load segments use the framing

```
[4B tag] [u32 addr BE] [u32 size BE] [size bytes data]
tag = 01 59 53 58   ("YSX" type 0x01; addresses/sizes big-endian!)
```

Each segment is loaded at its own runtime address (DDR 0xC03A0000-0xC03E8000),
so the file is NOT a linear image. Earlier header records (types 0x0D / 0x07 /
0x04 / 0x02, short payloads: version numbers, 441 = sample-rate /100, sizes)
precede the segments; semantics TBD.

Segments (from `tools/bootbin_parse.py`):

| Runtime addr | Size | Content |
|---|---|---|
| 0xC03AC0F0 | 16 | "System Error" string |
| 0xC03AC100 | 127264 | **main boot code** (SYS/BIOS kernel + tasks) |
| 0xC03CB220 | 96 | config table |
| 0xC03E00F8 | 8 | "(null)" |
| 0xC03E13C8 | 10628 | string table + SYS/BIOS module fxn tables |
| 0xC03E3D60 | 7968 | code (DMA/SPI-flash flavoured: 0x280 consts, B14 paging) |
| 0xC03E72EC | 204 | dense function-pointer table (32 entries, HWI dispatch?) |
| 0xC03E7400 | 512 | BSS (zeros) |
| 0xC03E7FF4 | 1384 | config table (HWI numbers/priorities) |

Task/event names (normal ASCII, inside the 0xC03E13C8 segment):

- `Event_MIDIDataReady` @0xC03E259E — event signalling incoming MIDI
- `Task_VersionUpdateMIDI` @0xC03E25B2 — **the firmware-update task**
- `Task_BootMain` — main boot task
- `ti.sysbios.knl.Task.IdleTask`, `GateMutex_SPI` (SPI flash mutex),
  plus `prev = %` style log format strings

Not yet resolved: the TSK entry addresses (name pointers are not absolute;
SYS/BIOS objects reference them by offset), the exact MIDI SysEx protocol
and flash-write commands inside `Task_VersionUpdateMIDI`, and the header
records' semantics. The 0xC03E3D60 segment's DMA/SPI-flavoured code is the
prime candidate for the flash-write path.

---

## Open items (next steps)

- [ ] Global header field meanings (0x0004 / 0x00C8 / 0x02C7) — correlate with
      block allocation.
- [ ] Which table(s) the firmware actually reads (probably 0x9008 master via
      the header), and whether category tables are derived or independent.
- [ ] `boot.bin`: `Task_VersionUpdateMIDI` — MIDI SysEx update protocol,
      checksum/validation, block transfer commands (device ID, opcodes).
- [ ] `Preset.bin` format — factory patch structure (chains, effect slots,
      parameter values).
- [ ] `Main.bin` — ZDL loader call chain, SonicStomp struct, init/edit
      handler context, stereo declaration (reverson open questions).
- [ ] Obtain the real B1on firmware updater (official zoom.co.jp / Softpedia
      "Zoom B1on Guitar Pedal Firmware 1.21") — network blocked from this
      machine; user-side download needed.
