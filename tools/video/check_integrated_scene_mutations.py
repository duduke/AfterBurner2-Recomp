#!/usr/bin/env python3
"""Retain and reject faults in the live shared-underlay consumer."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ("reconstructed-instead-of-primary",
     "if(primary){\n            std::copy(primary->indices()",
     "if(false && primary){\n            std::copy(primary->indices()",
     "primary clip background differs"),
    ("lost-primary-priority",
     "std::copy(primary->priorities().begin()+c.top*w,primary->priorities().begin()+(c.bottom+1)*w,priority.begin()+c.top*w);",
     "std::fill(priority.begin()+c.top*w,priority.begin()+(c.bottom+1)*w,0);",
     "primary clip priority/protection differs"),
    ("published-before-frame-completion",
     "hud_clips.clear();player_draws.clear();player_visibility.clear();ready=false;",
     "hud_clips.clear();player_draws.clear();player_visibility.clear();ready=true;",
     "partial primary scene published early"),
    ("lost-native-hud-occlusion",
     "hud_words=native_words;",
     "hud_words=hud_raw;",
     "primary clip HUD occlusion differs"),
    ("stale-cached-palette",
     "colors[i]=argb(palette.pen(i));",
     "colors[i]=0xff000000;",
     "primary clip background differs"),
    ("lost-cached-effects-palette",
     "colors[i+palette.num_entries()]=argb(palette.pen(i+palette.num_entries()));",
     "colors[i+palette.num_entries()]=colors[i];",
     "primary clip HUD palette differs"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--runtime-dir", type=Path)
    args = parser.parse_args()
    destination = ROOT / "build/oracle/integrated-scene-mutations" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    inputs = destination / "inputs"
    # Preserve the embedded ZIP reader at romloader.cpp's relative include path.
    shutil.copytree(ROOT / "src/third_party/miniz", destination / "third_party/miniz")
    hardware = inputs / "hwvideo"
    hardware.mkdir(parents=True)
    for header in (ROOT / "src/main/hwvideo").glob("*.hpp"):
        shutil.copy2(header, hardware)
    names = ("screen", "hwroad", "hwtiles", "hwsprites", "hwpalette")
    for name in names:
        shutil.copy2(ROOT / f"src/main/hwvideo/{name}.cpp", hardware)
    for name in ("roms", "romloader"):
        for extension in ("cpp", "hpp"):
            shutil.copy2(ROOT / f"src/main/{name}.{extension}", inputs)
    shutil.copy2(ROOT / "tests/integrated_scene_clip_regression.cpp", inputs / "regression.cpp")
    source = (hardware / "wide_scene.hpp").read_text()
    for name, before, _, _ in MUTATIONS:
        if source.count(before) != 1:
            raise SystemExit(f"Mutation site changed: {name}; no checks skipped")
    environment = dict(os.environ)
    if args.runtime_dir:
        environment["PATH"] = str(args.runtime_dir.resolve()) + os.pathsep + environment.get("PATH", "")

    def run(name, body):
        folder = destination / name
        (folder / "hwvideo").mkdir(parents=True)
        (folder / "hwvideo/wide_scene.hpp").write_text(body)
        shutil.copy2(inputs / "regression.cpp", folder / "regression.cpp")
        binary = folder / ("probe.exe" if os.name == "nt" else "probe")
        command = [args.compiler, "-std=c++20", "-O2", "-I" + str(folder), "-I" + str(inputs),
                   str(folder / "regression.cpp"), *(str(hardware / f"{n}.cpp") for n in names),
                   str(inputs / "roms.cpp"), str(inputs / "romloader.cpp"), "-o", str(binary)]
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
        raise SystemExit(f"Integrated scene baseline failed: {log}")
    print(f"PASS integrated scene baseline: {log}", flush=True)
    for name, before, after, expected in MUTATIONS:
        status, log = run(name, source.replace(before, after))
        if status != 1 or expected not in log.read_text():
            raise SystemExit(f"Mutation did not fail at its assertion: {name}; {log}")
        print(f"PASS rejected {name}: {log}", flush=True)
    print(f"Integrated scene mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped")


if __name__ == "__main__":
    main()
