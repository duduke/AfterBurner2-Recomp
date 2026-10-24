#!/usr/bin/env python3
"""Retain and reject deliberate changes to the experimental primary viewport."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ("shifted-native-center",
     "const auto source=y*layer_width+viewport->origin();",
     "const auto source=y*layer_width+viewport->origin()+(viewport->origin()?1:0);",
     "native layer observer changed"),
    ("reversed-native-origin",
     "const int native_left=viewport?-int(viewport->origin()):0;",
     "const int native_left=viewport?int(viewport->origin()):0;",
     "native layer observer changed"),
    ("lost-disabled-underlay-clear",
     "std::fill(viewport->pixels.begin()+top*w,viewport->pixels.begin()+(bottom+1)*w,0);",
     "std::fill(viewport->pixels.begin()+top*w,viewport->pixels.begin()+bottom*w,0);",
     "wide underlay differs or clip wrote outside rows"),
    ("changed-sprite-feedback",
     "if(sprite_bank) sprites.render_bank(sprite_bank, sprite_pixels.data(), width, height, top, bottom);",
     "if(sprite_bank){sprites.render_bank(sprite_bank, sprite_pixels.data(), width, height, top, bottom);if(viewport)sprite_bank[14]^=1;}",
     "native sprite feedback differs"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--runtime-dir", type=Path)
    args = parser.parse_args()
    destination = ROOT / "build/oracle/integrated-renderer-mutations" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    inputs = destination / "inputs"
    hardware = inputs / "hwvideo"
    hardware.mkdir(parents=True)
    names = ("screen", "hwroad", "hwtiles", "hwsprites", "hwpalette")
    for name in names:
        for extension in ("hpp", "cpp"):
            shutil.copy2(ROOT / f"src/main/hwvideo/{name}.{extension}", hardware)
    shutil.copy2(ROOT / "tests/integrated_renderer_regression.cpp", inputs / "regression.cpp")
    source = (hardware / "screen.cpp").read_text()
    for name, before, _, _ in MUTATIONS:
        if source.count(before) != 1:
            raise SystemExit(f"Mutation site changed: {name}; no checks skipped")
    environment = dict(os.environ)
    if args.runtime_dir:
        environment["PATH"] = str(args.runtime_dir.resolve()) + os.pathsep + environment.get("PATH", "")

    def run(name, body):
        folder = destination / name
        folder.mkdir()
        cpp = folder / "screen.cpp"
        cpp.write_text(body)
        binary = folder / ("probe.exe" if os.name == "nt" else "probe")
        command = [args.compiler, "-std=c++20", "-O2", "-I" + str(inputs),
                   str(inputs / "regression.cpp"), str(cpp),
                   *(str(hardware / f"{n}.cpp") for n in names if n != "screen"), "-o", str(binary)]
        with (folder / "build.log").open("w") as log:
            log.write(subprocess.list2cmdline(command) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise SystemExit(f"Build failed: {folder / 'build.log'}")
        with (folder / "run.log").open("w") as log:
            result = subprocess.run([str(binary)], cwd=ROOT, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=90)
        return result.returncode, folder / "run.log"

    status, log = run("baseline", source)
    if status:
        raise SystemExit(f"Integrated renderer baseline failed: {log}")
    print(f"PASS integrated renderer baseline: {log}", flush=True)
    for name, before, after, expected in MUTATIONS:
        status, log = run(name, source.replace(before, after))
        if status != 1 or expected not in log.read_text():
            raise SystemExit(f"Mutation did not fail at its assertion: {name}; {log}")
        print(f"PASS rejected {name}: {log}", flush=True)
    print(f"Integrated renderer mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped")


if __name__ == "__main__":
    main()
