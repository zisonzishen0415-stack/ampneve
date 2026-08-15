# VST UI Redesign — Modern Hybrid (Layout B / Glacier Blue / Ticks Knob)

Date: 2026-08-15
Status: approved by user (browser + terminal rounds)
Scope: `vst/PluginEditor.h` + `vst/PluginEditor.cpp` only. **Zero DSP / parameter /
preset-data changes.**

## Motivation

The current editor is a faithful pedal sketch (flat dark knob + pointer, raw
combo box + two small buttons for presets, PAGE/BYPASS squeezed into one bottom
row). For DAW use it reads as unfinished: knob value is hard to see, the preset
row is cramped, hover/feedback states are missing. The LCD screen body stays —
the user explicitly likes it — but the rest moves to a modern plugin hybrid:
same pedal narrative, modern surfaces, one unified color identity.

## Design decisions (locked with the user)

| Topic | Decision |
|---|---|
| Layout | B: LCD top → 3 knobs center → preset strip below knobs → bottom row (BYPASS left, P1/P2/P3 tabs right) |
| Color scheme | S1 Glacier Blue: LCD bg `#0d2239`, lit pixels `#9fd4ff`, dim `#3d6a94`; accent (knob arc, BYPASS LED, page tab highlight) `#4db8ff`; panel `#17191e`, control chips `#23262e`, primary text `#eef2f8`, secondary `#8b93a3` |
| Knob style | K2 Ticks: dashed tick ring + glowing white pointer + translucent value arc; 270° travel |
| Preset UX | `◀ name ▶` cycling + click name → grouped popup (factory 5 / separator / user); SAVE + DEL icon chips |
| Page switching | Bottom P1/P2/P3 segmented tabs **and** clicking the `P1` text in the LCD header (pedal-style) |

## Component breakdown

### 1. Palette (single source of truth)
Replace the scattered `lcdBg/lcdDim/lcdLit/body/bezel/mGreen/...` statics with a
small `UiPalette` namespace in `PluginEditor.cpp`:

- `panel` `#17191e`, `chip` `#23262e`, `chipBorder` `#333944`
- `text` `#eef2f8`, `textDim` `#8b93a3`
- `lcdBg` `#0d2239`, `lcdLit` `#9fd4ff`, `lcdDim` `#3d6a94`, `lcdBorder` `#4a4e57`
- `accent` `#4db8ff` (knob arc / LED on / active tab), `accentSoft` = accent @ 22% alpha
- meter: keep green `#4ade80` / yellow `#e8c34a` / red `#e05252` (semantic, unchanged)

### 2. Knob — Ticks style (`PedalKnobLookAndFeel::drawRotarySlider` rewrite)
Draw order (all in `drawRotarySlider`):
1. Body: radial-gradient-like fill (two-tone ellipse: lighter top-left `#2c313a`,
   dark `#17191e`), outer ring `chipBorder`, inner shadow (subtle dark stroke at
   bottom) — approximate gradient with layered ellipses (JUCE has no gradient
   radial in all versions; layered alpha fills are fine).
2. Dashed tick ring: `drawArc` on a rect inset 3 px with a dash effect
   (`Path::createPathWithRoundedCorners` not needed — use
   `g.drawDashedLine` on a `Path` arc, or draw 24 short arc segments; simplest:
   `Path p; p.addArc(...); g.drawDashedLine(p, dashArray, 1.0f)`).
3. Value arc: `Path::addArc` from start angle to `start + pos * (end-start)`,
   stroke `accent` @ 2.5 px, plus a soft glow (second stroke, wider, `accentSoft`).
4. Pointer: line from center to 0.78 r at the value angle, colour `lcdLit`,
   2.5 px, with a subtle glow pass (wider, low alpha). Ticks start at the
   standard pedal gauge angles (start `pi*0.75`, end `pi*2.25` — unchanged).
5. Center hub: small filled circle `panel` + 1 px ring `chipBorder` (no text in
   the knob — value stays in the LCD slots).
6. Hover/focus: if `slider.isMouseOverOrDragging()` brighten pointer to white
   `#ffffff` and boost arc alpha.

Rotary parameters, range logic, double-click-to-default, wheel, vertical drag:
unchanged. **Cabtype stays a 2-way detent (range 0..1 step 1)** — remove the
stale constructor `setRange(0.0, 2.0, 1.0)`; `setPage()` already sets the right
range, so delete the constructor's special case (it is overwritten anyway).

### 3. Preset strip (new `PresetStrip` component, `PluginEditor.cpp`)
Replaces the `ComboBox` + `SAVE`/`DEL` buttons. One component, 6 child controls:

- `leftBtn` / `rightBtn`: flat icon buttons (`◀` / `▶`), 26×26, chip bg, `textDim`
  glyph, hover → `text`; click cycles preset list (factory+user, wraps around).
- `nameBtn`: rounded chip, centred name, `text`; **click → `PopupMenu`**:
  factory presets (item ids 1..5, check-marked when current), separator,
  user presets (ids 100..n), disabled `--- no custom presets ---` entry when
  the user list is empty. Menu highlight follows the standard dark theme
  (`PopupMenu::setLookAndFeel` not required; use the default LAF with our
  `LookAndFeel_V4` subclass to keep dark).
- `saveBtn` / `delBtn`: icon chips (💾 / ✕ drawn as paths or unicode, 26×26);
  SAVE: overwrite selected custom preset or create `Custom N.xml` (logic moved
  verbatim from `saveUserPreset`); DEL: enabled only when a custom preset is
  selected (greyed otherwise), deletes it.
- Component exposes `refresh()`, `selectByName(String)` helpers; the editor
  wires `onChange` callbacks exactly like today (applyFactory/loadUser/save/delete).

Keyboard: editor `keyPressed` — Left/Right arrows cycle presets when no knob
has focus (check `hasKeyboardFocus` of sliders), Space toggles bypass.

### 4. Bottom row — BYPASS + page tabs
- **BYPASS**: custom `ToggleButton` with a small LAF override
  (`drawButtonBackground` + `drawToggleState`): rounded chip, label "BYPASS",
  LED dot on the left. On state: LED `accent` with glow (`drawEllipse` + shadow
  pass), label `text`; off: LED `#3a3f4a`, label `textDim`. Same `ButtonAttachment`
  to `bypass` param.
- **Page tabs**: three `ToggleButton`s in an exclusive group (or a tiny custom
  segmented control), ids P1..P3. Active tab: `accentSoft` fill + `accent` text;
  inactive: transparent + `textDim`. Clicking sets the page (existing
  `setPage()`). Also: `mouseDown` on the LCD header's `P1` text region cycles
  the page (pedal-style, same handler as the PAGE button was).

### 5. LCD repaint (colors only + clickable P)
Same geometry (`lcdRect/slotRect/header/meter` code), new palette:
- header: `AMPNEVE` in `lcdLit`, `v18` in `lcdDim`, `P1` text in `lcdLit`
  (make its rect hit-testable: store `pageHitRect` in the editor, `mouseDown`
  checks it), cab label (`2X12/4X12`) `lcdDim`.
- meter: trough `#14304c` keep, bar green/yellow/red keep, peak marker `lcdLit`.
- slots: name `lcdDim`, value bar trough `#1e4268`, fill `lcdLit`, value text
  `lcdLit` when focused else `lcdDim`; focus frame `accent` instead of `lcdLit`
  (stronger affordance).

### 6. Layout / sizing (resized())
- Keep aspect ratio + resize limits (constrainer unchanged).
- LCD top (unchanged rect), knob square area (unchanged geometry), then preset
  strip (height 30, full LCD width), bottom row (height 34): BYPASS chip
  left (width ~110), tabs right (3 × ~34).
- Spacing/padding consistent: 16 px side margins (as today).

## Behavior invariants (must not regress)

- 9 params, 3 pages, ids/names tables unchanged.
- APVTS attachments, bypass attachment, preset XML format and paths unchanged.
- Double-click knob → default; wheel enabled; drag = vertical.
- Timer repaint at 30 Hz keeps LCD values + meter live.
- `setPage()` disabled-knob logic unchanged.
- Factory presets table unchanged (names/values).

## Non-goals

- No DSP/param/preset-data changes (specifically: preset XML schema stays).
- No new params, no undo history, no DAW-host automation UI.
- No changes to `PluginProcessor` beyond nothing (it stays untouched).
- No icon assets — all glyphs drawn (unicode or `Path`) so the repo stays
  asset-free.

## Verification

1. Build VST3 via `build/_relink.ps1` (or `_full_rebuild.ps1` if objects stale),
   install to the three VST3 locations, clear REAPER plugin cache if needed.
2. `tools/vst_load_test.exe` full lifecycle must pass.
3. Manual in REAPER (44.1 kHz): page switch (tabs + LCD P1 click), knob drag /
   double-click / wheel, bypass LED, preset cycle (arrows + popup + save + del),
   resize, meter.
4. `tools/compare_gain.py` output must be byte-identical in report.json for the
   same inputs (UI change must not touch audio; run once as regression proof).
