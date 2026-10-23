#!/usr/bin/env python3
"""Retain renderer controls for indexed high-resolution HUD alpha coverage."""

from datetime import datetime, timezone
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import uuid


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/main/frontend/native_hud.hpp").read_text()
MUTATIONS = [
    (
        "straight-alpha-blend",
        "SDL_BLENDMODE_BLEND_PREMULTIPLIED",
        "SDL_BLENDMODE_BLEND",
        "custom alpha coverage failed",
    ),
]


def main():
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    logs = ROOT / "build/oracle/native-hud-mutations" / f"{stamp}-{uuid.uuid4().hex[:8]}"
    logs.mkdir(parents=True)
    pkg_command = shlex.split(os.environ.get("PKG_CONFIG", "pkg-config"))
    with (logs / "pkg-config.log").open("w") as log:
        package = subprocess.run(
            [*pkg_command, "--cflags", "--libs", "sdl3"], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=log, text=True, timeout=30,
        )
    if package.returncode:
        raise SystemExit(f"Native HUD SDL flags unavailable; see {logs / 'pkg-config.log'}")
    sdl_flags = shlex.split(package.stdout)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy"}

    # TemporaryDirectory creates a unique owner-only directory. The overlay is
    # the first include root, leaving live headers untouched during other gates.
    with tempfile.TemporaryDirectory(prefix="native-hud-mutations-", dir=ROOT / "build") as temporary:
        overlay = Path(temporary)
        (overlay / "frontend").mkdir()

        def build_and_run(name, source):
            (overlay / "frontend/native_hud.hpp").write_text(source)
            binary = overlay / name
            command = [
                *compiler, *flags, "-I" + str(overlay), "-I" + str(ROOT / "src/main"),
                str(ROOT / "tests/native_hud_regression.cpp"),
                str(ROOT / "src/main/romloader.cpp"), "-o", str(binary), *sdl_flags,
            ]
            build_log = logs / f"{name}.build.log"
            with build_log.open("w") as log:
                log.write(shlex.join(command) + "\n")
                log.flush()
                build = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            if build.returncode:
                raise SystemExit(f"Native HUD {name} build failed; see {build_log}")
            run_log = logs / f"{name}.run.log"
            with run_log.open("w") as log:
                result = subprocess.run(
                    [str(binary)], cwd=ROOT, env=environment,
                    stdout=log, stderr=subprocess.STDOUT, timeout=30,
                )
            return result.returncode, run_log

        status, baseline_log = build_and_run("baseline", SOURCE)
        if status:
            raise SystemExit(f"Native HUD mutation baseline failed; see {baseline_log}")
        print(f"Native HUD ROM-free baseline passed; full log: {baseline_log.relative_to(ROOT)}")

        for name, before, after, expected in MUTATIONS:
            if SOURCE.count(before) != 1:
                raise SystemExit(f"Native HUD mutation site changed: {name}")
            status, run_log = build_and_run(name, SOURCE.replace(before, after))
            if status != 1 or expected not in run_log.read_text():
                raise SystemExit(f"Native HUD mutation did not reject as expected: {name}; see {run_log}")
            print(f"Native HUD mutation rejected: {name}; full log: {run_log.relative_to(ROOT)}")

    print(f"Native HUD mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped; ROM-free baseline passed.")


if __name__ == "__main__":
    main()
