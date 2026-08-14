#!/usr/bin/env python3
"""Build AmpNeve.ZDL from ampsim_zdl.c + manifest.json.

Requires TI C6000 Code Generation Tools (cl6x). Point AMPNEVE_TI_ROOT at the
compiler install, or set it in the file below. Example:
  AMPNEVE_TI_ROOT=/Applications/ti/ccs2050/ccs/tools/compiler/ti-cgt-c6000_8.5.0.LTS \\
    python3 build.py

Options:
  --smoke        link with audio_nop (pass-through DSP) - the FIRST thing a
                 new pedal should ever see. Output goes to dist_smoke/, not
                 dist/. Flash it, verify the effect appears and the pages/
                 knobs work, and only then build the real DSP.
  --no-inspect   skip the post-compile .obj safety audit (tools/check_zdl_obj.py).
                 The audit is pure Python (no TI tools) and fails the build on
                 the frozen-pedal patterns from SAFE-DSP-RULES; leave it on.

Compiler flags follow the reverson hardware-proven release pattern: -O2 and
NO --opt_for_space. High --opt_for_space emits __c6xabi_push_rts/pop_rts
helper calls whose bundled stubs are not hardware-tested here (see
SAFE-DSP-RULES). Set AMPNEVE_OPT_FOR_SPACE=n to experiment explicitly.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
# ZoomMultistompZDL linker (sibling checkout of the reverson repo)
ZDL_ROOT = Path(os.environ.get("AMPNEVE_ZDL_ROOT",
    r"C:\Users\34723\Documents\ChatGPT\zoomreverse\ZoomMultistompZDL"))
if not ZDL_ROOT.exists():
    ZDL_ROOT = HERE.parent.parent / "zoomreverse" / "ZoomMultistompZDL"
sys.path.insert(0, str(ZDL_ROOT / "build"))
sys.path.insert(0, str(ZDL_ROOT / "src" / "airwindows" / "common"))

from linker import LinkerConfig, link, params_from_manifest  # noqa: E402
from manifest_params import write_param_header  # noqa: E402

TI_ROOT = Path(os.environ.get("AMPNEVE_TI_ROOT",
    "/Applications/ti/ccs2050/ccs/tools/compiler/ti-cgt-c6000_8.5.0.LTS"))
CL6X = TI_ROOT / "bin" / "cl6x"
if not CL6X.exists() and (TI_ROOT / "bin" / "cl6x.exe").exists():
    CL6X = TI_ROOT / "bin" / "cl6x.exe"      # Windows install

CFLAGS = [
    "--c99",
    "--opt_level=2",
    "-mv6740",
    "--abi=eabi",
    "--mem_model:data=far",                 # critical, see ABI.md
    f"--include_path={ROOT / 'core'}",
    f"--include_path={TI_ROOT / 'include'}",
]
# Experiment-only escape hatch (see module docstring). Unset by default.
_opt_for_space = os.environ.get("AMPNEVE_OPT_FOR_SPACE")
if _opt_for_space:
    CFLAGS.insert(3, f"--opt_for_space={_opt_for_space}")


def check_effect_name(name: str) -> None:
    """Reject basenames Zoom may truncate or that collide with stock ZDLs
    (SAFE-DSP-RULES: both have frozen real pedals)."""
    if len(name) > 8:
        sys.exit(f"[ampneve] effect_name '{name}' is >8 chars; Zoom truncates "
                 "basenames and truncated duplicates have frozen pedals.")
    trimmed = name[:8].upper()
    stock_dir = ZDL_ROOT / "stock_zdls"
    if stock_dir.exists():
        for z in stock_dir.glob("*.ZDL"):
            if z.stem[:8].upper() == trimmed:
                sys.exit(f"[ampneve] basename collision with stock effect "
                         f"'{z.name}' - rename the effect.")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--smoke", action="store_true",
                        help="link with audio_nop (pass-through); output to dist_smoke/")
    parser.add_argument("--no-inspect", action="store_true",
                        help="skip the post-compile .obj safety audit")
    return parser.parse_args(argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    manifest = json.loads((HERE / "manifest.json").read_text(encoding="utf-8-sig"))
    name = manifest["effect_name"]
    check_effect_name(name)

    if not CL6X.exists():
        sys.exit(
            f"[ampneve] TI C6000 compiler not found at {CL6X}\n"
            "  Install TI CGT C6000 8.5.x LTS (free TI-account download) and set\n"
            "  AMPNEVE_TI_ROOT (see zdl/README.md).")

    smoke = args.smoke
    audio_nop = bool(manifest.get("audio_nop", False)) or smoke
    out_dir = ROOT / ("dist_smoke" if smoke else "dist")
    out_dir.mkdir(parents=True, exist_ok=True)

    if smoke:
        print("[ampneve] *** SMOKE BUILD: audio function is NOPed (pass-through). ***")
        print("[ampneve] *** Flash this FIRST on a new pedal. Do not publish. ***")

    write_param_header(manifest, HERE / "ampsim_zdl_params.h", "AMPNEVE")

    src_c = HERE / "ampsim_zdl.c"
    obj = HERE / "ampsim_zdl.obj"
    out_zdl = out_dir / f"{name}.ZDL"

    print(f"[ampneve] compiling {src_c.name} -> {obj.name}")
    subprocess.run([str(CL6X), *CFLAGS, "-c", str(src_c), f"--output_file={obj}"],
                   check=True, cwd=HERE)

    if not args.no_inspect:
        print("[ampneve] auditing", obj.name, "(tools/check_zdl_obj.py)")
        subprocess.run([sys.executable, "-B", str(ROOT / "tools" / "check_zdl_obj.py"),
                        str(obj)], check=True)

    for junk in ("compiler.opt", "linker.cmd"):
        p = HERE / junk
        if p.exists():
            p.unlink()

    cfg = LinkerConfig(
        effect_name=name,
        audio_func_name=manifest.get("audio_func_name"),
        gid=manifest["gid"],
        fxid=manifest["fxid"],
        params=params_from_manifest(manifest["params"]),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=manifest.get("fxid_version", "1.00").encode("ascii"),
        flags_byte=manifest.get("flags_byte", 0x01),
        audio_nop=audio_nop,
        use_object_edit_handlers=False,
        synthesize_linesel_edit_handlers=True,
        synth_edit_start_index=2,
    )
    link(cfg)

    if smoke:
        print(f"\n[ampneve] smoke done -> {out_zdl}  (PASS-THROUGH - test only)")
    else:
        print(f"\n[ampneve] done -> {out_zdl}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
