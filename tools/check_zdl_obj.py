#!/usr/bin/env python3
"""Post-compile safety audit for the AmpNeve ZDL .obj (pre-link gate).

Parses the TI C6000 relocatable ELF produced by `cl6x -c` with the reverson
ZoomMultistompZDL linker's own ObjFile parser, and fails the build on the
exact patterns that have frozen real MS-series / G1on pedals (see the
reverson repo: research_docs/docs_SAFE-DSP-RULES.md):

  * .fardata (far writable statics) must be 0 bytes - state belongs in the
    host-managed ctx[3] arena, not in static storage
  * no uninitialised ALLOC sections (.bss/.far/.common): no zero-init
    statics, no implicit C runtime startup assumptions
  * undefined external symbols limited to the set the reverson linker
    actually bundles (__c6xabi_divf / __c6xabi_push_rts / __c6xabi_pop_rts
    / __c6xabi_call_stub) - anything else (malloc, memset, sinf, ...) does
    not exist in the Zoom runtime
  * no SBR/B14-relative relocations (reloc types 11..23): the firmware
    never initialises B14, so SBR relocs dereference garbage - the cure is
    --mem_model:data=far on every compile
  * every allocated section must be one the reverson linker places
    (.text/.audio/.const/.fardata) - in particular .switch:* jump tables
    ($C$SW) are NOT placed: their relocations get skipped and knob turns
    jump into garbage on the pedal. switch statements must be if-else
    chains in the ZDL-compiled core.
  * a non-empty .audio section (the CODE_SECTION pragma must have landed)

Pure Python (no TI toolchain needed): it reuses the reverson linker's ELF
parser, so it runs on the .obj before linking.

Usage:
    python3 tools/check_zdl_obj.py zdl/ampsim_zdl.obj
Exit code: 0 = safe to link, 1 = unsafe (do not flash).
"""

from __future__ import annotations

import argparse
import os
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Same discovery as zdl/build.py: the reverson ZoomMultistompZDL checkout.
ZDL_ROOT = Path(os.environ.get(
    "AMPNEVE_ZDL_ROOT",
    r"C:\Users\34723\Documents\ChatGPT\zoomreverse\ZoomMultistompZDL"))
if not ZDL_ROOT.exists():
    ZDL_ROOT = ROOT.parent / "zoomreverse" / "ZoomMultistompZDL"
sys.path.insert(0, str(ZDL_ROOT / "build"))

# noqa: E402 - sys.path setup must precede these imports
from linker import (  # noqa: E402
    ObjFile, RT_ABS32, RT_PCR_S21, RT_ABS_L16, RT_ABS_H16,
    SHT_PROGBITS, SHT_NOBITS, SHF_ALLOC, _is_sbr_reloc,
)

# External symbols the reverson linker resolves for us (divf RTS blob +
# LineSel-spliced register helpers). Any other undefined symbol means the
# Zoom runtime would not be able to satisfy it.
ALLOWED_UNDEFINED = {
    "__c6xabi_divf",
    "__c6xabi_push_rts",
    "__c6xabi_pop_rts",
    "__c6xabi_call_stub",
}

KNOWN_RELOCS = {RT_ABS32, RT_PCR_S21, RT_ABS_L16, RT_ABS_H16}

# NOBITS/ALLOC sections that are legitimate even in a bare object.
UNINIT_OK = {".stack"}

# ALLOC PROGBITS sections the reverson linker places. Anything else (e.g.
# .switch:* jump tables) is dropped at link time with SKIPped relocations.
PLACED_PREFIXES = (".text", ".audio", ".const", ".fardata", ".far")


def audit(obj: ObjFile) -> tuple[list[str], list[str]]:
    """Return (problems, warnings). Problems fail the build."""
    problems: list[str] = []
    warnings: list[str] = []

    sec_by_idx = {s["idx"]: s for s in obj.sections}

    # ---- 1. section inventory -------------------------------------------
    for sec in obj.sections:
        name, size = sec["name"], sec["size"]
        if not (sec["flags"] & SHF_ALLOC):
            continue
        if name in (".fardata", ".far") and size > 0:
            problems.append(
                f"{name}: {size} bytes of writable far statics. DSP state must "
                "live in the host ctx[3] arena; writable statics have frozen "
                "pedals (SAFE-DSP-RULES).")
        if sec["type"] == SHT_NOBITS and size > 0 and name not in UNINIT_OK:
            problems.append(
                f"{name}: {size} bytes of uninitialised static storage. "
                "The custom ZDL path has no C runtime startup to zero it.")
        if sec["type"] == SHT_PROGBITS:
            if name.startswith(".switch"):
                problems.append(
                    f"{name}: switch jump table - the reverson linker does "
                    "NOT place .switch sections; $C$SW relocations are skipped "
                    "and knob turns would jump into garbage on the pedal. "
                    "Rewrite the switch statement as an if-else chain.")
            elif not name.startswith(PLACED_PREFIXES):
                problems.append(
                    f"{name}: allocated section the reverson linker does not "
                    "place - its relocations would be skipped at link time.")

    # ---- 2. .audio section ----------------------------------------------
    audio = obj.get_section(".audio")
    if audio is None or audio["size"] == 0:
        problems.append(
            "no .audio section - the CODE_SECTION(Fx_AMP_AmpNeve, \".audio\") "
            "pragma did not land; the linker cannot place the audio function.")

    # ---- 3. undefined symbols -------------------------------------------
    for sym in obj.symbols:
        if sym["shndx"] == 0 and sym["name"]:
            if sym["name"] not in ALLOWED_UNDEFINED:
                problems.append(
                    f"undefined external '{sym['name']}' - not bundled by the "
                    "reverson linker and not present in the Zoom runtime "
                    "(no libc, no math lib, no heap).")

    # ---- 4. relocations --------------------------------------------------
    reloc_types: Counter[int] = Counter()
    for sec_idx, rels in obj.relocs.items():
        sec = sec_by_idx.get(sec_idx, {})
        for rel in rels:
            rtype = rel["type"]
            reloc_types[rtype] += 1
            if _is_sbr_reloc(rtype):
                problems.append(
                    f"{sec.get('name', '?')}+0x{rel['offset']:X}: SBR/B14-relative "
                    f"reloc type {rtype} - the firmware never initialises B14. "
                    "Compile with --mem_model:data=far.")
            elif rtype not in KNOWN_RELOCS:
                warnings.append(
                    f"{sec.get('name', '?')}+0x{rel['offset']:X}: unknown reloc "
                    f"type {rtype} - the linker will SKIP it; verify manually.")

    return problems, warnings


def report(obj: ObjFile, problems: list[str], warnings: list[str]) -> int:
    print("=== AmpNeve .obj safety audit ===")
    print("\nsections (allocated):")
    total_const = 0
    for sec in obj.sections:
        if not (sec["flags"] & SHF_ALLOC):
            continue
        print(f"  {sec['name']:<18} type={sec['type']:<2} flags=0x{sec['flags']:X} "
              f"size={sec['size']}")
        if sec["name"] == ".const" or sec["name"].startswith(".const:"):
            total_const += sec["size"]
    print(f"\n.const total (IR kernels + coefficient tables): {total_const} bytes")

    undefined = sorted(sym["name"] for sym in obj.symbols
                       if sym["shndx"] == 0 and sym["name"])
    if undefined:
        print(f"undefined externals: {', '.join(undefined)}")
    else:
        print("undefined externals: none")

    reloc_total = sum(len(r) for r in obj.relocs.values())
    print(f"relocations: {reloc_total}")

    for msg in warnings:
        print(f"  WARN: {msg}")
    if problems:
        print("\nUNSAFE - do not link or flash:")
        for msg in problems:
            print(f"  FAIL: {msg}")
        return 1
    print("\nSAFE: no frozen-pedal patterns found; ok to link.")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("obj", help="path to the cl6x -c output (.obj)")
    args = parser.parse_args(argv[1:])

    obj_path = Path(args.obj)
    if not obj_path.exists():
        print(f"[check_zdl_obj] {obj_path} not found", file=sys.stderr)
        return 1

    try:
        obj = ObjFile(obj_path)
    except AssertionError as e:
        print(f"[check_zdl_obj] {obj_path.name}: {e} (expected a TI C6000 ELF "
              ".obj from cl6x --abi=eabi)", file=sys.stderr)
        return 1

    problems, warnings = audit(obj)
    return report(obj, problems, warnings)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
