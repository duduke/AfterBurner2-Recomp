#!/usr/bin/env python3
"""Generate physical controls for discovery; never claim successful play."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("output", type=Path)
p.add_argument("--frames", type=int, default=12000)
p.add_argument("--scenario", choices=("neutral", "weave", "continue", "credit-stock"), default="neutral")
args = p.parse_args()
if args.frames <= 0:
    p.error("frames must be positive")
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("w") as out:
    dip = 141 if args.scenario == "credit-stock" else 221
    out.write(f"AB2PHYS1\n{args.frames} 255 255 {dip}\n")
    for f in range(args.frames):
        buttons, axes = 255, [128]*5 + [16]*3
        if (args.scenario == "credit-stock" and 360 <= f < 2760 and f % 24 < 12) or (args.scenario != "credit-stock" and 360 <= f < 372):
            buttons &= ~64
        start = 2820 if args.scenario == "credit-stock" else 420
        if start <= f < start+12:
            buttons &= ~8
        if args.scenario == "continue":
            if 4800 <= f < 4812: buttons &= ~64
            if 4860 <= f < 4872: buttons &= ~8
        if f >= 480 and args.scenario == "weave":
            phase = ((f-480)//120) % 4
            axes[:3] = ([48,128,208,128][phase], [96,160,128,128][phase], 224 if phase%2 == 0 else 64)
            buttons &= ~16
            if f % 90 < 12:
                buttons &= ~32
        out.write(" ".join(map(str, [f, buttons, *axes])) + "\n")
metadata = {"format": "AB2PHYS1", "scenario": args.scenario, "frames": args.frames,
            "initial_persistent_state": "fresh cold board, no NVRAM/config import",
            "switches": [255,255,dip], "sha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
            "rom_files": {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                          for f in sorted(Path("roms").iterdir()) if f.is_file() and f.suffix.lower() not in (".md", ".txt")}}
args.output.with_suffix(args.output.suffix+".json").write_text(json.dumps(metadata, indent=2)+"\n")
