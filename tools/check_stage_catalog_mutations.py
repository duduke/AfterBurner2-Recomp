#!/usr/bin/env python3
"""Retained catalog negative controls against independent MAME observations."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    ("stage-ordinal", "return {number, selector, descriptor, events};",
     "return {number - 1, selector, descriptor, events};"),
    ("ordered-selector-offset", "0x32482 + number - 1", "0x32482 + number"),
    ("descriptor-field-offset", "descriptor + 16", "descriptor + 12"),
    ("descriptor-pointer", "return {number, selector, descriptor, events};",
     "return {number, selector, descriptor + 4, events};"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", nargs="?", default="build/oracle/stage_catalog.bin")
    args = parser.parse_args()
    capture = Path(args.capture).resolve()
    if not capture.is_file():
        raise RuntimeError(f"Missing independent MAME capture: {capture}")
    logs = ROOT / "build/oracle/stage_catalog_mutations"
    logs.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "src/main/engine/stage_catalog.hpp").read_text()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    with tempfile.TemporaryDirectory(prefix="ab2-stage-catalog-") as temporary:
        overlay = Path(temporary)
        (overlay / "engine").mkdir()
        header = overlay / "engine/stage_catalog.hpp"
        for name, before, after in [("baseline", "", "")] + MUTATIONS:
            if name != "baseline" and source.count(before) != 1:
                raise RuntimeError(f"{name}: mutation anchor changed")
            header.write_text(source if name == "baseline" else source.replace(before, after))
            binary = overlay / name
            command = compiler + flags + ["-I" + str(overlay), "-Isrc/main",
                "tests/oracle_stage_catalog.cpp", "src/main/roms.cpp", "src/main/romloader.cpp",
                "-o", str(binary)]
            with (logs / f"{name}-build.log").open("w") as log:
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
            result = subprocess.run([str(binary), str(capture)], cwd=ROOT,
                                    capture_output=True, text=True)
            output = result.stdout + result.stderr
            (logs / f"{name}-run.log").write_text(output)
            if name == "baseline":
                if result.returncode != 0 or "original-MAME entries identical, 0 skipped" not in output:
                    raise RuntimeError("Catalog baseline did not pass")
                print("Stage catalog mutation baseline: original-MAME comparison passes", flush=True)
            else:
                if result.returncode != 1 or "Stage catalog differs from original MAME" not in output:
                    raise RuntimeError(f"{name}: expected catalog rejection missing")
                print(f"Stage catalog mutation {name}: rejected against original MAME", flush=True)


if __name__ == "__main__":
    main()
