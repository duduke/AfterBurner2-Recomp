#!/usr/bin/env python3
"""Retain proof that passive whole-host assertions reject integrated faults."""
from datetime import datetime, timezone
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ("shifted-native-center", "const auto source=y*layer_width+viewport->origin();",
     "const auto source=y*layer_width+viewport->origin()+1;", "whole-host RGB/index/priority differs"),
    ("missing-native-text",
     "tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/-1, 0x04, top, bottom);",
     "if(!viewport)tiles.draw_text(pixels.data(), priority.data(), width, height, /*category=*/-1, 0x04, top, bottom);",
     "whole-host RGB/index/priority differs"),
    ("corrupted-sprite-feedback", "if(layers_rendered)layers_rendered(top,bottom,pixels,priority,palette);",
     "if(viewport && sprite_bank)sprite_bank[14]^=1;\n    if(layers_rendered)layers_rendered(top,bottom,pixels,priority,palette);",
     "native bank bytes differ"),
)


def main():
    directory = ROOT / "build/oracle/integrated-host-mutations" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    directory.mkdir(parents=True)
    test = "tests/oracle_whole_host.cpp"
    result = subprocess.run(["make", "-n", "-W", test, "build/oracle_whole_host"], cwd=ROOT,
                            text=True, capture_output=True, check=True)
    commands = [shlex.split(line) for line in result.stdout.splitlines() if test in line and " -o " in line]
    if len(commands) != 1:
        raise SystemExit("Cannot identify whole-host build command; no controls skipped")
    original = (ROOT / "src/main/hwvideo/screen.cpp").read_text()
    for name, before, _, _ in MUTATIONS:
        if original.count(before) != 1:
            raise SystemExit(f"Mutation anchor changed: {name}; no controls skipped")
    # Retain source inputs alongside the binaries; external ROMs/corpora stay
    # in their existing ignored directories and are never rewritten by this gate.
    shutil.copytree(ROOT / "src", directory / "inputs/src")
    (directory / "inputs/tests").mkdir()
    shutil.copy2(ROOT / test, directory / "inputs" / test)

    def run(name, source, corpus):
        folder = directory / name
        folder.mkdir()
        local = folder / "screen.cpp"
        local.write_text(source)
        binary = folder / "probe"
        command = commands[0].copy()
        command[command.index("src/main/hwvideo/screen.cpp")] = str(local)
        command[command.index("-o") + 1] = str(binary)
        with (folder / "build.log").open("w") as log:
            log.write(shlex.join(command) + "\n")
            log.flush()
            built = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=300)
        if built.returncode:
            raise SystemExit(f"Integrated host control build failed: {folder / 'build.log'}")
        command = [str(binary), f"build/oracle/{corpus}.bin", f"build/oracle/{corpus}.txt", "398"]
        with (folder / "run.log").open("w") as log:
            log.write(shlex.join(command) + "\n")
            log.flush()
            checked = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        return checked.returncode, folder / "run.log"

    status, log = run("baseline", original, "joint_cold_full")
    if status or "zero differences/skips" not in log.read_text():
        raise SystemExit(f"Integrated host baseline failed: {log}")
    print(f"PASS integrated whole-host baseline: {log}", flush=True)
    for name, before, after, expected in MUTATIONS:
        status, log = run(name, original.replace(before, after), "joint_cold_full")
        if status != 1 or expected not in log.read_text():
            raise SystemExit(f"Integrated host did not reject {name} at its assertion: {log}")
        print(f"PASS integrated whole-host rejected {name}: {log}", flush=True)
    print(f"Integrated whole-host mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped")


if __name__ == "__main__":
    main()
