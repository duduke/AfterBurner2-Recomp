#!/usr/bin/env python3
"""Prove the Phase 3c MAME checks reject constants changed in temporary ports.

Run from the repository root after make oracle-capture-frame-sprites. Copies,
executables and logs stay under ignored build/oracle; source is never changed.
"""
import os
from pathlib import Path
import subprocess

root = Path("build/oracle/mutations")
root.mkdir(parents=True, exist_ok=True)
common = ["src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp",
          "src/main/roms.cpp", "src/main/romloader.cpp"]
cases = [
    ("mainsprites", "src/main/engine/omainsprites.cpp", ">=0xc0", ">=0xbf",
     "tests/oracle_mainsprites.cpp", "mainsprites_directed", common),
    ("palette", "src/main/engine/omainpalette.cpp", "bank=(bank+1)&7", "bank=(bank+1)&3",
     "tests/oracle_palette.cpp", "palette_directed", common + ["src/main/engine/omainframe.cpp"]),
    ("mainframe", "src/main/engine/omainframe.cpp", "count=previous-1", "count=previous-2",
     "tests/oracle_mainframe.cpp", "mainframe_directed", common),
    ("subsprites", "src/main/engine/osubsprites.cpp", "s(bus.read16(a0)) < 0x1e",
     "s(bus.read16(a0)) < 0x1d", "tests/oracle_subsprites.cpp", "subsprites_directed",
     ["src/main/engine/subbus.cpp", "src/main/roms.cpp", "src/main/romloader.cpp"]),
]
for name, source, old, new, test, capture, dependencies in cases:
    text = Path(source).read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"{name}: mutation anchor changed; inspect the port")
    variant, binary, log_path = root / (name + ".cpp"), root / name, root / (name + ".log")
    variant.write_text(text.replace(old, new))
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-Isrc/main",
                    test, str(variant), *dependencies, "-o", str(binary)], check=True)
    with log_path.open("w") as log:
        result = subprocess.run([str(binary), "build/oracle/" + capture + ".bin"],
                                stdout=log, stderr=subprocess.STDOUT)
    summary = [line for line in log_path.read_text().splitlines() if " identical, " in line]
    if result.returncode != 1 or not summary or ", 0 different," in summary[-1]:
        raise RuntimeError(f"{name}: mutation did not produce a differential failure; see {log_path}")
    print(f"{name}: rejected — {summary[-1]}", flush=True)
