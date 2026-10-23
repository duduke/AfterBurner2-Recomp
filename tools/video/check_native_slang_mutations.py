#!/usr/bin/env python3
"""Explicit macOS Metal gate: prove retained Slang timing/history controls fail.

Requires an unlocked usable Metal display environment and the optional
librashader ABI 2 / API 5 runtime. Never used by the ordinary headless test
suite. All staged sources, binaries, observations and complete logs are retained
under a unique gitignored build/oracle/slang-mutations directory.
"""

import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import uuid


ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    (
        "milliseconds-instead-of-microseconds",
        "std::clamp(delta_us,0.,double(UINT32_MAX))",
        "std::clamp(delta_us/1000.,0.,double(UINT32_MAX))",
        "FAIL: FrameTimeDelta binds raw microseconds:",
    ),
    (
        "missing-temporal-history-reset",
        "if(p.ever_rendered&&(clear_history||p.clear_next))p.reset_history();",
        "if(p.ever_rendered&&(clear_history||p.clear_next)&&frame!=91)p.reset_history();",
        "FAIL: temporal RGB matches previous original frame:",
    ),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    if sys.platform != "darwin":
        raise SystemExit("Slang mutation gate requires macOS native Metal; no checks were skipped.")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    destination = ROOT / "build/oracle/slang-mutations" / f"{stamp}-{uuid.uuid4().hex[:8]}"
    destination.mkdir(parents=True)
    inputs = destination / "inputs"
    (inputs / "src/main/frontend").mkdir(parents=True)
    (inputs / "vendor").mkdir()
    (inputs / "tests/fixtures").mkdir(parents=True)
    shutil.copyfile(ROOT / "src/main/frontend/native_slang.hpp", inputs / "src/main/frontend/native_slang.hpp")
    shutil.copyfile(ROOT / "src/third_party/librashader/librashader.h", inputs / "vendor/librashader.h")
    shutil.copyfile(ROOT / "tests/native_slang_integration.mm", inputs / "tests/native_slang_integration.mm")
    shutil.copytree(ROOT / "tests/fixtures/native_slang", inputs / "tests/fixtures/native_slang")
    package = subprocess.run(
        [*shlex.split(os.environ.get("PKG_CONFIG", "pkg-config")), "--cflags", "--libs", "sdl3"],
        cwd=ROOT, capture_output=True, text=True, timeout=30,
    )
    (destination / "pkg-config.log").write_text(package.stdout + package.stderr)
    if package.returncode:
        raise SystemExit(f"Slang SDL flags unavailable; see {destination / 'pkg-config.log'}")
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    source = (ROOT / "src/main/frontend/native_slang.mm").read_text()
    include = '#include "../../third_party/librashader/librashader.h"'
    if source.count(include) != 1:
        raise SystemExit("Slang runtime vendor-header include changed; update retained staging recipe.")
    source = source.replace(include, '#include "librashader.h"')
    for name, before, _, _ in MUTATIONS:
        if source.count(before) != 1:
            raise SystemExit(f"Slang mutation site changed: {name}; no controls were skipped.")

    def run(name, body):
        staged = destination / name
        staged.mkdir()
        path = staged / "native_slang.mm"
        path.write_text(body)
        binary = staged / "native_slang_integration"
        command = [
            *compiler, *flags, "-fobjc-arc", "-I" + str(inputs / "src/main"),
            "-I" + str(inputs / "vendor"),
            str(inputs / "tests/native_slang_integration.mm"), str(path),
            "-o", str(binary), *shlex.split(package.stdout),
            "-framework", "Foundation", "-framework", "Metal", "-framework", "QuartzCore",
            "-framework", "CoreVideo",
        ]
        build_log = staged / "build.log"
        with build_log.open("w") as log:
            log.write(shlex.join(command) + "\n")
            log.flush()
            try:
                result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
            except subprocess.TimeoutExpired:
                raise SystemExit(f"Slang {name} build timed out; see {build_log}")
        if result.returncode:
            raise SystemExit(f"Slang {name} build failed; see {build_log}")
        run_log = staged / "run.log"
        environment = {**os.environ, "AB2_SLANG_TEST_OUTPUT": str(staged / "observations")}
        if environment.get("AB2_LIBRASHADER"):
            runtime = Path(environment["AB2_LIBRASHADER"])
            environment["AB2_LIBRASHADER"] = str(runtime if runtime.is_absolute() else ROOT / runtime)
        elif (ROOT / "build/deps/librashader/librashader.dylib").is_file():
            environment["AB2_LIBRASHADER"] = str(ROOT / "build/deps/librashader/librashader.dylib")
        with run_log.open("w") as log:
            try:
                result = subprocess.run([str(binary)], cwd=inputs, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=180)
            except subprocess.TimeoutExpired:
                raise SystemExit(f"Slang {name} execution timed out; see {run_log}")
        return result.returncode, run_log

    status, baseline = run("baseline", source)
    if status:
        raise SystemExit(f"Slang mutation baseline failed; see {baseline}")
    print(f"Slang Metal baseline passed; full log: {baseline.relative_to(ROOT)}", flush=True)
    for name, before, after, expected in MUTATIONS:
        status, log = run(name, source.replace(before, after))
        failures = [line for line in log.read_text().splitlines() if line.startswith("FAIL:")]
        if status != 1 or len(failures) != 1 or not failures[0].startswith(expected):
            raise SystemExit(f"Slang mutation did not reject at its specific assertion: {name}; see {log}")
        print(f"Slang mutation rejected: {name}; full log: {log.relative_to(ROOT)}", flush=True)
    print(f"Slang mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped; Metal baseline passed.")


if __name__ == "__main__":
    main()
