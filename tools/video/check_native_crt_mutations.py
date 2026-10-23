#!/usr/bin/env python3
"""Prove final CRT pixel/state checks reject retained rendering controls."""

from datetime import datetime, timezone
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import uuid


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/main/frontend/native_crt.hpp").read_text()
MUTATIONS = [
    ("bypass-pass", "if(success)success=SDL_RenderTexture(renderer,overlay.get(),nullptr,&destination);",
     "if(success)success=true;", "pixel ("),
    ("missing-scanlines", "const double amplitude=next.scanlines*.01*",
     "const double amplitude=0.0*", "pixel ("),
    ("missing-mask", "const double dim=1.0-next.mask*.01;",
     "const double dim=1.0;", "pixel ("),
    ("wrong-raster-phase", "const double native_y=(physical_y-.5-next.area.y)*224.0/next.area.h;",
     "const double native_y=(physical_y-next.area.y)*224.0/next.area.h;", "pixel ("),
    ("lost-viewport", "result=SDL_SetRenderViewport(renderer,has_viewport?&viewport:nullptr) && result;",
     "result=SDL_SetRenderViewport(renderer,nullptr) && result;", "CRT changed caller renderer state"),
    ("enabled-when-off", "settings.crt==Crt::off || settings.crt==Crt::retroarch || (!settings.crt_scanlines && !settings.crt_mask)",
     "settings.crt==Crt::retroarch || (!settings.crt_scanlines && !settings.crt_mask)", "CRT no-op retained or rebuilt its cache"),
]


def main():
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    logs = ROOT / "build/oracle/crt-mutations" / f"{stamp}-{uuid.uuid4().hex[:8]}"
    logs.mkdir(parents=True)
    package = subprocess.run(
        [*shlex.split(os.environ.get("PKG_CONFIG", "pkg-config")), "--cflags", "--libs", "sdl3"],
        cwd=ROOT, capture_output=True, text=True, timeout=30,
    )
    (logs / "pkg-config.log").write_text(package.stdout + package.stderr)
    if package.returncode:
        raise SystemExit(f"CRT SDL flags unavailable; see {logs / 'pkg-config.log'}")
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy"}
    with tempfile.TemporaryDirectory(prefix="crt-mutations-", dir=ROOT / "build") as temporary:
        overlay = Path(temporary)
        (overlay / "frontend").mkdir()

        def run(name, source):
            (overlay / "frontend/native_crt.hpp").write_text(source)
            binary = overlay / name
            command = [*compiler, *flags, "-I" + str(overlay), "-I" + str(ROOT / "src/main"),
                       str(ROOT / "tests/native_crt_regression.cpp"), "-o", str(binary),
                       *shlex.split(package.stdout)]
            build_log = logs / f"{name}.build.log"
            with build_log.open("w") as log:
                log.write(shlex.join(command) + "\n")
                log.flush()
                result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            if result.returncode:
                raise SystemExit(f"CRT {name} build failed; see {build_log}")
            run_log = logs / f"{name}.run.log"
            with run_log.open("w") as log:
                result = subprocess.run([str(binary)], cwd=ROOT, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=120)
            return result.returncode, run_log

        status, baseline = run("baseline", SOURCE)
        if status:
            raise SystemExit(f"CRT mutation baseline failed; see {baseline}")
        print(f"CRT ROM-free baseline passed; full log: {baseline.relative_to(ROOT)}")
        for name, before, after, diagnostic in MUTATIONS:
            if SOURCE.count(before) != 1:
                raise SystemExit(f"CRT mutation site changed: {name}")
            status, log = run(name, SOURCE.replace(before, after))
            if status != 1 or diagnostic not in log.read_text():
                raise SystemExit(f"CRT mutation did not reject as expected: {name}; see {log}")
            print(f"CRT mutation rejected: {name}; full log: {log.relative_to(ROOT)}")
    print(f"CRT mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped; ROM-free baseline passed.")


if __name__ == "__main__":
    main()
