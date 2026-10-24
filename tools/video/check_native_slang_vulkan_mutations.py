#!/usr/bin/env python3
"""Windows Vulkan shader controls, with all sources and logs retained locally."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ("milliseconds-instead-of-microseconds", "std::clamp(delta_us,0.,double(UINT32_MAX))",
     "std::clamp(delta_us/1000.,0.,double(UINT32_MAX))", "FrameTimeDelta binds raw microseconds"),
    ("missing-temporal-history-reset", "opt.clear_history=clear_history||p.clear_next;",
     "opt.clear_history=(clear_history||p.clear_next)&&frame!=91;", "temporal RGB matches previous original frame"),
    ("lost-renderer-state", "const bool restored=p.saved.restore(p.renderer);",
     "const bool restored=true;", "target restored"),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cache = dict(re.findall(r"^([^/#\r\n:=]+):[^=\r\n]*=(.*)$", (build / "CMakeCache.txt").read_text(), re.M))
    sdl = Path(cache["SDL3_DIR"]).parents[2]
    destination = ROOT / "build/oracle/slang-vulkan-mutations" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    inputs = destination / "inputs"
    (inputs / "src/main/frontend").mkdir(parents=True)
    (inputs / "vendor").mkdir()
    (inputs / "tests/fixtures").mkdir(parents=True)
    for original, target in (
        ("src/main/frontend/native_slang.hpp", "src/main/frontend/native_slang.hpp"),
        ("src/third_party/librashader/librashader.h", "vendor/librashader.h"),
        ("tests/native_slang_integration.mm", "tests/native_slang_integration.cpp"),
    ):
        shutil.copy2(ROOT / original, inputs / target)
    shutil.copytree(ROOT / "tests/fixtures/native_slang", inputs / "tests/fixtures/native_slang")
    source = (ROOT / "src/main/frontend/native_slang_vulkan.cpp").read_text().replace(
        '#include "../../third_party/librashader/librashader.h"', '#include "librashader.h"')
    for name, before, _, _ in MUTATIONS:
        if source.count(before) != 1:
            raise SystemExit(f"Mutation site changed: {name}; no checks skipped")

    def run(name, body):
        folder = destination / name
        folder.mkdir()
        cpp = folder / "native_slang_vulkan.cpp"
        cpp.write_text(body)
        binary = folder / "probe.exe"
        command = [cache["CMAKE_CXX_COMPILER"], "-std=c++20", "-O2",
                   "-I" + str(inputs / "src/main"), "-I" + str(inputs / "vendor"),
                   "-I" + cache["AB2_VULKAN_INCLUDE_DIR"], "-I" + str(sdl / "include"),
                   str(inputs / "tests/native_slang_integration.cpp"), str(cpp),
                   "-L" + str(sdl / "lib"), "-lSDL3", "-o", str(binary)]
        with (folder / "build.log").open("w") as log:
            log.write(subprocess.list2cmdline(command) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        if result.returncode:
            raise SystemExit(f"Mutation build failed: {folder / 'build.log'}")
        environment = dict(os.environ, PATH=str(build) + os.pathsep + os.environ.get("PATH", ""),
                           AB2_LIBRASHADER=str(ROOT / "build/deps/librashader/librashader.dll"),
                           AB2_SLANG_TEST_OUTPUT=str(folder / "observations"))
        log_path = folder / "run.log"
        with log_path.open("w") as log:
            result = subprocess.run([str(binary)], cwd=inputs, env=environment,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=90)
        return result.returncode, log_path

    status, log = run("baseline", source)
    if status:
        raise SystemExit(f"Vulkan baseline failed: {log}")
    print(f"PASS Vulkan shader baseline: {log}", flush=True)
    for name, before, after, expected in MUTATIONS:
        status, log = run(name, source.replace(before, after))
        failures = [line for line in log.read_text().splitlines() if line.startswith("FAIL:")]
        if status != 1 or len(failures) != 1 or not failures[0].startswith("FAIL: " + expected):
            raise SystemExit(f"Vulkan mutation did not fail at its assertion: {name}; {log}")
        print(f"PASS rejected {name}: {log}", flush=True)
    print(f"Vulkan shader mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped")


if __name__ == "__main__":
    main()
