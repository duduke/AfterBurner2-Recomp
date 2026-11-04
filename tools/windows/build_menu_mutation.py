#!/usr/bin/env python3
"""Build a retained menu fault against an existing Windows CMake build.

Run the emitted executable with --gpu in the baseline menu test environment.
It must exit nonzero with the emitted expected failure; building alone is not
a mutation pass. Production headers, objects and executables stay untouched.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--control", choices=("ground-toggle", "viewport-21x9"), default="ground-toggle")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    target = build / "CMakeFiles/native_menu_integration.dir"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out = ROOT / "build/oracle/menu-mutations" / (stamp + "-" + args.control + "-" + uuid.uuid4().hex[:8])
    (out / "frontend").mkdir(parents=True)
    source = (ROOT / "src/main/frontend/native_menu.hpp").read_text()
    controls = {
        "ground-toggle": ("if(id==8) changed.ground=!settings.ground;", "if(id==8) changed.ground=settings.ground;", "ground correction UI failed"),
        "viewport-21x9": ("(static_cast<int>(settings.viewport)+viewport_count+direction)%viewport_count", "(static_cast<int>(settings.viewport)+3+direction)%3", "21:9 viewport entry missing"),
    }
    needle, replacement, expected = controls[args.control]
    if source.count(needle) != 1:
        raise SystemExit("Menu mutation anchor changed; no control was built")
    (out / "frontend/native_menu.hpp").write_text(source.replace(needle, replacement))
    shutil.copy2(ROOT / "tests/native_menu_integration.cpp", out / "native_menu_integration.cpp")
    flags = (target / "flags.make").read_text()
    compiler = re.search(r"^# compile CXX with (.+)$", flags, re.M)[1]
    options = []
    for key in ("CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"):
        options += shlex.split(re.search(r"^" + key + r" = (.*)$", flags, re.M)[1])
    obj = out / "native_menu_integration.cpp.obj"
    compile_command = [compiler, "-I" + str(out), *options, "-c", str(out / "native_menu_integration.cpp"), "-o", str(obj)]
    archive = out / "objects.a"
    shutil.copy2(target / "objects.a", archive)
    archiver = str(Path(compiler).with_name("llvm-ar"))
    exe = out / "native_menu_mutant.exe"
    link = shlex.split((target / "link.txt").read_text().splitlines()[-1])
    link = [str(archive) if x == "CMakeFiles/native_menu_integration.dir/objects.a" else x for x in link]
    link[link.index("-o") + 1] = str(exe)
    link = ["-Wl,--out-implib," + str(out / "unused-import.a") if x.startswith("-Wl,--out-implib,") else x for x in link]
    commands = [compile_command, [archiver, "r", str(archive), str(obj)], link]
    for i, command in enumerate(commands):
        with (out / f"{i + 1:02d}-build.log").open("w") as log:
            subprocess.run(command, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    receipt = {"directory": str(out), "executable": str(exe), "control": args.control, "expected_failure": expected, "commands": commands}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
