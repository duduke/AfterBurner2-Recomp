#!/usr/bin/env python3
"""Retained Shuffle palette controls against independently executed MAME."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STAGES = (5, 8, 13, 17)
MUTATIONS = [
    ("gradient-start", "gradient + 34 + word * 2", "gradient + 2 + word * 2"),
    ("sky-repeat-overwrite", "bus.write32(0x122e60, StageCatalog::Detail::pointer(active, alternate + 4));",
     "for (unsigned repeat = 0; repeat < 8; ++repeat) bus.write32(0x122e60 + repeat * 4, StageCatalog::Detail::pointer(active, alternate + 4));"),
    ("auxiliary-source-offset", "alternate + offset));", "alternate + offset + 4));"),
    ("omitted-gradient-word", "word < 14", "word < 13"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-dir", type=Path, default=ROOT / "build/oracle")
    args = parser.parse_args()
    captures = [args.capture_dir.resolve() / f"shuffle_palette_stage{stage}.bin" for stage in STAGES]
    for capture in captures:
        if not capture.is_file():
            raise RuntimeError(f"Missing independent MAME palette capture: {capture}")
    logs = ROOT / "build/oracle/shuffle_palette_mutations"
    logs.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "src/main/engine/shuffle_rom.hpp").read_text()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    with tempfile.TemporaryDirectory(prefix="ab2-shuffle-palette-") as temporary:
        overlay = Path(temporary)
        (overlay / "engine").mkdir()
        header = overlay / "engine/shuffle_rom.hpp"
        for name, before, after in [("baseline", "", "")] + MUTATIONS:
            if name != "baseline" and source.count(before) != 1:
                raise RuntimeError(f"{name}: mutation anchor changed")
            header.write_text(source if name == "baseline" else source.replace(before, after))
            binary = overlay / name
            command = compiler + flags + ["-I" + str(overlay), "-Isrc/main",
                "tests/oracle_shuffle_palette.cpp", "src/main/engine/mainbus.cpp",
                "src/main/engine/subbus.cpp", "src/main/roms.cpp", "src/main/romloader.cpp",
                "-o", str(binary)]
            with (logs / f"{name}-build.log").open("w") as log:
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
            rejected = 0
            for stage, capture in zip(STAGES, captures):
                result = subprocess.run([str(binary), str(capture)], cwd=ROOT,
                                        capture_output=True, text=True)
                output = result.stdout + result.stderr
                (logs / f"{name}-stage{stage}.log").write_text(output)
                if name == "baseline":
                    if result.returncode != 0 or "39 of 39 original-MAME observed words identical" not in output:
                        raise RuntimeError(f"Palette baseline failed at stage {stage}")
                elif result.returncode == 1 and "Shuffle alternate palette differs from original MAME" in output:
                    rejected += 1
                elif result.returncode != 0:
                    raise RuntimeError(f"{name} stage {stage}: unexpected failure, not a palette comparison")
            if name == "baseline":
                print("Shuffle palette mutation baseline: 156 of 156 original-MAME observed words identical, 0 skipped; 100 exact word writes", flush=True)
            else:
                if not rejected:
                    raise RuntimeError(f"{name}: all four MAME comparisons accepted the mutation")
                print(f"Shuffle palette mutation {name}: rejected by {rejected} of 4 original-MAME fixtures", flush=True)


if __name__ == "__main__":
    main()
