#!/usr/bin/env python3
"""Prove the Phase 3c/3d MAME checks reject constants changed in temporary ports.

Run from the repository root after make oracle-capture-frame-sprites and
make oracle-capture-objects. Copies,
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
    ("objectdispatch", "src/main/engine/omainobjects.cpp", "b.read8(r.a[5])&0x80", "b.read8(r.a[5])&0x40",
     "tests/oracle_objects.cpp", "objects_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("compoundinit", "src/main/engine/omainobjects.cpp", "b.write8(p+3,0x46)", "b.write8(p+3,0x45)",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("compoundprepare", "src/main/engine/omainobjects.cpp", "r.a[0]+=0x300", "r.a[0]+=0x340",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("compoundhide", "src/main/engine/omainobjects.cpp", "word(r.d[1],3)", "word(r.d[1],2)",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("soundenqueue", "src/main/engine/omainsound.cpp", "b.write8(0xffff8906,b.read8(0xffff8906)+1)", "b.write8(0xffff8906,b.read8(0xffff8906)+2)",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainobjects.cpp"]),
    ("objectinit", "src/main/engine/omainobjects.cpp", "r.a[1]-44", "r.a[1]-43",
     "tests/oracle_objects.cpp", "objects", common + ["src/main/engine/omainsound.cpp"]),
    ("objectrng", "src/main/engine/omainobjects.cpp", "r.d[1]<<=2", "r.d[1]<<=1",
     "tests/oracle_objects.cpp", "object_leaves_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("soundring", "src/main/engine/omainsound.cpp", "word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x3f);\n        b.write16(0xffff890a", "word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x1f);\n        b.write16(0xffff890a",
     "tests/oracle_objects.cpp", "object_leaves_directed", common + ["src/main/engine/omainobjects.cpp"]),
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
