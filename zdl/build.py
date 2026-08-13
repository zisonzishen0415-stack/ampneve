#!/usr/bin/env python3
"""Build AmpNeve.ZDL from ampsim_zdl.c + manifest.json.

Requires TI C6000 Code Generation Tools (cl6x). Point AMPNEVE_TI_ROOT at the
compiler install, or set it in the file below. Example:
  AMPNEVE_TI_ROOT=/Applications/ti/ccs2050/ccs/tools/compiler/ti-cgt-c6000_8.5.0.LTS \\
    python3 build.py
"""

from __future__ import annotations
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

CFLAGS = [
    "--c99",
    "--opt_level=2",
    "--opt_for_space=3",
    "-mv6740",
    "--abi=eabi",
    "--mem_model:data=far",                 # critical, see ABI.md
    f"--include_path={ROOT / 'core'}",
    f"--include_path={TI_ROOT / 'include'}",
]


def main() -> None:
    if not CL6X.exists():
        sys.exit(
            f"[ampneve] TI C6000 compiler not found at {CL6X}\n"
            "  Install TI CGT C6000 and set AMPNEVE_TI_ROOT (see zdl/README.md)."
        )

    manifest = json.loads((HERE / "manifest.json").read_text(encoding="utf-8-sig"))
    write_param_header(manifest, HERE / "ampsim_zdl_params.h", "AMPNEVE")

    src_c = HERE / "ampsim_zdl.c"
    obj = HERE / "ampsim_zdl.obj"
    out_zdl = HERE.parent / "dist" / f"{manifest['effect_name']}.ZDL"

    print(f"[ampneve] compiling {src_c.name} -> {obj.name}")
    subprocess.run([str(CL6X), *CFLAGS, "-c", str(src_c), f"--output_file={obj}"],
                   check=True, cwd=HERE)

    for junk in ("compiler.opt", "linker.cmd"):
        p = HERE / junk
        if p.exists():
            p.unlink()

    cfg = LinkerConfig(
        effect_name=manifest["effect_name"],
        audio_func_name=manifest.get("audio_func_name"),
        gid=manifest["gid"],
        fxid=manifest["fxid"],
        params=params_from_manifest(manifest["params"]),
        obj_path=obj,
        output_path=out_zdl,
        fxid_version=manifest.get("fxid_version", "1.00").encode("ascii"),
        flags_byte=manifest.get("flags_byte", 0x01),
        audio_nop=manifest.get("audio_nop", False),
        use_object_edit_handlers=False,
        synthesize_linesel_edit_handlers=True,
        synth_edit_start_index=2,
    )
    link(cfg)
    print(f"\n[ampneve] done -> {out_zdl}")


if __name__ == "__main__":
    main()
