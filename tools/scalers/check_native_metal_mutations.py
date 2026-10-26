#!/usr/bin/env python3
"""Retain native Metal baselines and deliberately incorrect filter/effect runs."""
from datetime import datetime, timezone
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = {
    "priority-protection": ("protection.empty()?0:protection[i]", "0"),
    "filter-selection": ("int32_t(filter),int32_t(p.factor)", "int32_t(SpriteFilter::epx),int32_t(p.factor)"),
    "cloud-opacity": ("int32_t(cloud_alpha),int32_t(trail_alpha)", "255,int32_t(trail_alpha)"),
    "trail-opacity": ("int32_t(cloud_alpha),int32_t(trail_alpha)", "int32_t(cloud_alpha),255"),
    "explosion-opacity": ("int32_t(explosion_alpha),int32_t(afterburner_alpha)", "255,int32_t(afterburner_alpha)"),
    "afterburner-opacity": ("int32_t(explosion_alpha),int32_t(afterburner_alpha)", "int32_t(explosion_alpha),255"),
}


def main():
    if sys.platform != "darwin":
        raise SystemExit("Native Metal mutation gate requires macOS; no checks were skipped.")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    destination = ROOT / "build/oracle/native-metal-sprite-mutations" / f"{stamp}-{uuid.uuid4().hex[:8]}"
    inputs = destination / "inputs"
    for name in ("src/main/frontend/native_metal_sprite_filter.hpp", "src/main/frontend/sprite_filter.hpp",
                 "src/main/frontend/native_settings.hpp", "src/main/hwvideo/sprite_scene.hpp",
                 "tests/gpu_sprite_filter_regression.cpp"):
        path = inputs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, path)
    shutil.copytree(ROOT / "src/main/frontend/scalers", inputs / "src/main/frontend/scalers")
    source = (ROOT / "src/main/frontend/native_metal_sprite_filter.mm").read_text()
    for name, (before, _) in MUTATIONS.items():
        if source.count(before) != 1:
            raise SystemExit(f"Native Metal mutation site changed: {name}; no controls were skipped.")
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    options = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))

    def run(name, body):
        folder = destination / name
        folder.mkdir()
        source_path = folder / "native_metal_sprite_filter.mm"
        source_path.write_text(body)
        binary = folder / "check"
        command = [*compiler, *options, "-DAB2_NATIVE_METAL_SPRITES", "-fobjc-arc",
                   "-I" + str(inputs / "src/main"), str(inputs / "tests/gpu_sprite_filter_regression.cpp"),
                   str(source_path), "-o", str(binary), *flags, "-framework", "Foundation",
                   "-framework", "Metal", "-framework", "QuartzCore", "-framework", "CoreVideo"]
        with (folder / "build.log").open("w") as log:
            log.write(shlex.join(command) + "\n");log.flush()
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise SystemExit(f"Native Metal {name} build failed: {folder / 'build.log'}")
        with (folder / "run.log").open("w") as log:
            result = subprocess.run([str(binary)], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        return result.returncode, folder / "run.log"

    status, log = run("baseline", source)
    if status:
        raise SystemExit(f"Native Metal baseline failed: {log}")
    print(f"Native Metal baseline passed: {log.relative_to(ROOT)}", flush=True)
    for name, (before, after) in MUTATIONS.items():
        status, log = run(name, source.replace(before, after))
        if status != 1 or "GPU scaler differs from CPU reference" not in log.read_text():
            raise SystemExit(f"Native Metal mutation did not fail pixel comparison: {name}; see {log}")
        print(f"Native Metal mutation rejected: {name}; full log: {log.relative_to(ROOT)}", flush=True)
    print(f"Native Metal sprite mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped; baseline passed.")


if __name__ == "__main__":
    main()
