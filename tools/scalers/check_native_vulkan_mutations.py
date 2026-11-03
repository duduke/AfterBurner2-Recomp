#!/usr/bin/env python3
"""Retain native Vulkan CPU-reference baselines and rejected sprite faults."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = {
    "priority-protection": ("protection.empty()?0:protection[i]", "0"),
    "filter-selection": ("int32_t(filter),int32_t(p.factor)", "int32_t(SpriteFilter::epx),int32_t(p.factor)"),
    "cloud-opacity": ("int32_t(clouds),int32_t(trails)", "255,int32_t(trails)"),
    "trail-opacity": ("int32_t(clouds),int32_t(trails)", "int32_t(clouds),255"),
    "explosion-opacity": ("int32_t(explosions),int32_t(afterburner)", "255,int32_t(afterburner)"),
    "afterburner-opacity": ("int32_t(explosions),int32_t(afterburner)", "int32_t(explosions),255"),
    "queued-read-order": ("if(!renderer_reads_submitted)p.drain();else p.wait();", "p.wait();"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--compiler", help="Host probe compiler when testing an SDK build")
    parser.add_argument("--sdl-prefix", type=Path, help="Host path to the SDK-built SDL installation")
    parser.add_argument("--vulkan-include", help="Host Vulkan header directory")
    args = parser.parse_args()
    cache = dict(re.findall(r"^([^/#\r\n:=]+):[^=\r\n]*=(.*)$", (args.build_dir / "CMakeCache.txt").read_text(), re.M))
    sdl = args.sdl_prefix.resolve() if args.sdl_prefix else Path(cache["SDL3_DIR"]).parents[2]
    if args.compiler:
        cache["CMAKE_CXX_COMPILER"] = args.compiler
    if args.vulkan_include:
        cache["AB2_VULKAN_INCLUDE_DIR"] = args.vulkan_include
    compiler = Path(cache["CMAKE_CXX_COMPILER"])
    destination = ROOT / "build/oracle/native-vulkan-sprite-mutations" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    inputs = destination / "inputs"
    for name in ("src/main/frontend/native_vulkan_sprite_filter.hpp", "src/main/frontend/sprite_filter.hpp",
                 "src/main/frontend/native_settings.hpp", "src/main/hwvideo/sprite_scene.hpp", "tests/gpu_sprite_filter_regression.cpp"):
        target = inputs / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, target)
    shutil.copytree(ROOT / "src/main/frontend/scalers", inputs / "src/main/frontend/scalers")
    source = (ROOT / "src/main/frontend/native_vulkan_sprite_filter.cpp").read_text()
    for name, (before, _) in MUTATIONS.items():
        if source.count(before) != 1:
            raise SystemExit(f"Native Vulkan mutation site changed: {name}; no checks skipped.")
    environment = os.environ.copy()
    environment["PATH"] = str(compiler.parent) + os.pathsep + str(sdl / "bin") + os.pathsep + environment.get("PATH", "")

    environment["LD_LIBRARY_PATH"] = str(sdl / "lib") + os.pathsep + environment.get("LD_LIBRARY_PATH", "")

    def run(name, body, test_body=None):
        folder = destination / name
        folder.mkdir()
        source_path = folder / "native_vulkan_sprite_filter.cpp"
        source_path.write_text(body)
        test_path = inputs / "tests/gpu_sprite_filter_regression.cpp"
        if test_body is not None:
            test_path = folder / "regression.cpp"
            test_path.write_text(test_body)
        binary = folder / "check.exe"
        command = [str(compiler), "-std=c++20", "-O2", "-DAB2_NATIVE_VULKAN_SPRITES",
                   "-I" + str(inputs / "src/main"), "-I" + cache["AB2_VULKAN_INCLUDE_DIR"],
                   "-I" + str(sdl / "include"), str(test_path),
                   str(source_path), "-o", str(binary), "-L" + str(sdl / "lib"), "-lSDL3"]
        with (folder / "build.log").open("w") as log:
            log.write(subprocess.list2cmdline(command) + "\n");log.flush()
            result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise SystemExit(f"Native Vulkan {name} build failed: {folder / 'build.log'}")
        with (folder / "run.log").open("w") as log:
            result = subprocess.run([str(binary)], cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        return result.returncode, folder / "run.log"

    status, log = run("baseline", source)
    if status:
        raise SystemExit(f"Native Vulkan baseline failed: {log}")
    print(f"Native Vulkan baseline passed: {log.relative_to(ROOT)}", flush=True)
    for name, (before, after) in MUTATIONS.items():
        status, log = run(name, source.replace(before, after))
        expected = "queued texture reuse/resize changed prior output" if name == "queued-read-order" else "GPU scaler differs from CPU reference"
        if status != 1 or expected not in log.read_text():
            raise SystemExit(f"Native Vulkan mutation did not fail pixel comparison: {name}; see {log}")
        print(f"Native Vulkan mutation rejected: {name}; full log: {log.relative_to(ROOT)}", flush=True)
    capture_test = (inputs / "tests/gpu_sprite_filter_regression.cpp").read_text()
    anchor = "const SDL_FRect capture_dst{0,0,float(w),float(h)};"
    assert capture_test.count(anchor) == 1, "native capture alignment fault site"
    status, log = run("native-capture-half-pixel", source,
                      capture_test.replace(anchor, "const SDL_FRect capture_dst{0.5f,0,float(w),float(h)};"))
    if status != 1 or "Native capture changed ScaleFX centre samples" not in log.read_text():
        raise SystemExit(f"Native capture alignment fault did not fail pixel comparison: {log}")
    print(f"Native capture alignment mutation rejected; full log: {log.relative_to(ROOT)}", flush=True)
    print(f"Native Vulkan sprite mutations: {len(MUTATIONS)+1} of {len(MUTATIONS)+1} rejected, 0 skipped; baseline passed.")


if __name__ == "__main__":
    main()
