#!/usr/bin/env python3
"""Prove the exact independent MAME RGB gate rejects primary-renderer faults."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ("shifted-native-center", "const auto source=y*layer_width+viewport->origin();",
     "const auto source=y*layer_width+viewport->origin()+1;"),
    ("missing-native-text",
     "tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/-1, 0x04, top, bottom);",
     "if(!viewport)tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/-1, 0x04, top, bottom);"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--runtime-dir", type=Path)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    capture = args.capture.resolve(strict=True)
    destination = ROOT / "build/oracle/integrated-mame-mutations" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    inputs = destination / "inputs"
    for header in (ROOT / "src/main").rglob("*.hpp"):
        target = inputs / header.relative_to(ROOT / "src/main")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(header, target)
    sources = [f"hwvideo/{n}.cpp" for n in ("screen", "hwroad", "hwtiles", "hwsprites", "hwpalette")]
    sources += ["roms.cpp", "romloader.cpp"]
    sources += [f"engine/{n}.cpp" for n in ("mainbus", "subbus", "omainsprites", "osubsprites", "osubroad")]
    for name in sources:
        shutil.copy2(ROOT / "src/main" / name, inputs / name)
    for name in ("oracle_video.cpp", "oracle_integrated_video.cpp"):
        shutil.copy2(ROOT / "tests" / name, inputs / name)
    source = (inputs / "hwvideo/screen.cpp").read_text()
    for name, before, _ in MUTATIONS:
        if source.count(before) != 1:
            raise SystemExit(f"Mutation site changed: {name}; no checks skipped")
    environment = dict(os.environ)
    if args.runtime_dir:
        environment["PATH"] = str(args.runtime_dir.resolve()) + os.pathsep + environment.get("PATH", "")

    def run(name, body):
        folder = destination / name
        (folder / "build/oracle").mkdir(parents=True)
        # Keep original inputs and every diagnostic image local to this variant.
        shutil.copytree(ROOT / "roms", folder / "roms")
        cpp = folder / "screen.cpp"
        cpp.write_text(body)
        binary = folder / ("probe.exe" if os.name == "nt" else "probe")
        command = [args.compiler, "-std=c++20", "-O2", "-I" + str(inputs),
                   str(inputs / "oracle_integrated_video.cpp"), str(cpp),
                   *(str(inputs / n) for n in sources if n != "hwvideo/screen.cpp"), "-o", str(binary)]
        with (folder / "build.log").open("w") as log:
            log.write(subprocess.list2cmdline(command) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise SystemExit(f"Build failed: {folder / 'build.log'}")
        with (folder / "run.log").open("w") as log:
            result = subprocess.run([str(binary), "398", str(capture)], cwd=folder, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=90)
        return result.returncode, folder / "run.log"

    status, log = run("baseline", source)
    if status or "0 over the limit of 0" not in log.read_text():
        raise SystemExit(f"Independent MAME baseline failed: {log}")
    print(f"PASS independent MAME baseline: {log}", flush=True)
    for name, before, after in MUTATIONS:
        status, log = run(name, source.replace(before, after))
        failures = re.findall(r"(\d+) over the limit of 0", log.read_text())
        if status != 1 or len(failures) != 1 or int(failures[0]) == 0:
            raise SystemExit(f"MAME did not reject renderer fault: {name}; {log}")
        print(f"PASS independent MAME rejected {name}: {log}", flush=True)
    print(f"Integrated independent MAME mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped")


if __name__ == "__main__":
    main()
