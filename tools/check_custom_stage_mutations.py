#!/usr/bin/env python3
"""Retained negative controls for CustomStage::RomView and InteractiveController."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

ROM_MUTATIONS = [
    (
        "retain-command-18",
        "if (!def.replace_events && rec.command != 18u)",
        "if (!def.replace_events)",
    ),
    (
        "wrong-hud-title-address",
        "std::copy(title8.begin(), title8.end(), owned.maincpu.rom + 0x2b6acu + target_pos * 8u);",
        "std::copy(title8.begin(), title8.end(), owned.maincpu.rom + 0x2b6acu + (target_pos + 1u) * 8u);",
    ),
    (
        "skip-ocean-palette-write",
        "Shuffle::Detail::write16(owned, relocated + 34u + w * 2u, c.rgb555());",
        "(void)c;",
    ),
    (
        "missing-event-sentinel",
        "EventRecord sentinel_record{0xffffu, 0u, {0, 0, 0, 0, 0}};",
        "EventRecord sentinel_record{0x0000u, 0u, {0, 0, 0, 0, 0}};",
    ),
]

INTERACTIVE_MUTATIONS = [
    (
        "bypass-full-throttle-gate",
        "if (throttle_armed && power >= 0.95) {",
        "if (power >= 0.10) {",
    ),
    (
        "break-carrier-ram-hold",
        "seed16(memory, 0xffff90aeu, 0u);",
        "seed16(memory, 0xffff90aeu, 18u);",
    ),
]


def main():
    logs = ROOT / "build/oracle/custom_stage_mutations"
    logs.mkdir(parents=True, exist_ok=True)
    rom_source = (ROOT / "src/main/engine/custom_stage_rom.hpp").read_text()
    interactive_source = (ROOT / "src/main/engine/custom_stage_interactive.hpp").read_text()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))

    cases = [("baseline", None, "", "")]
    for name, before, after in ROM_MUTATIONS:
        cases.append((name, "rom", before, after))
    for name, before, after in INTERACTIVE_MUTATIONS:
        cases.append((name, "interactive", before, after))

    with tempfile.TemporaryDirectory(prefix="ab2-custom-stage-") as temporary:
        overlay = Path(temporary)
        (overlay / "engine").mkdir()
        rom_header = overlay / "engine/custom_stage_rom.hpp"
        interactive_header = overlay / "engine/custom_stage_interactive.hpp"

        for name, target, before, after in cases:
            if target == "rom":
                if rom_source.count(before) != 1:
                    raise RuntimeError(f"{name}: mutation anchor changed in custom_stage_rom.hpp")
                rom_header.write_text(rom_source.replace(before, after))
                interactive_header.write_text(interactive_source)
            elif target == "interactive":
                if interactive_source.count(before) != 1:
                    raise RuntimeError(f"{name}: mutation anchor changed in custom_stage_interactive.hpp")
                rom_header.write_text(rom_source)
                interactive_header.write_text(interactive_source.replace(before, after))
            else:
                rom_header.write_text(rom_source)
                interactive_header.write_text(interactive_source)

            binary = overlay / name
            command = compiler + flags + [
                "-I" + str(overlay),
                "-Isrc/main",
                "tests/custom_stage_rom_regression.cpp",
                "src/main/roms.cpp",
                "src/main/romloader.cpp",
                "src/main/engine/mainbus.cpp",
                "src/main/engine/subbus.cpp",
                "-o",
                str(binary),
            ]
            with (logs / f"{name}-build.log").open("w") as log:
                subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
            result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
            output = result.stdout + result.stderr
            (logs / f"{name}-run.log").write_text(output)
            if name == "baseline":
                if result.returncode != 0 or "Custom stage ROM & interactive regression passed." not in output:
                    raise RuntimeError(f"Custom stage baseline did not pass:\n{output}")
                print("Custom stage mutation baseline: passed", flush=True)
            else:
                if result.returncode == 0 or "Custom stage regression failed:" not in output:
                    raise RuntimeError(f"{name}: expected custom stage regression rejection missing")
                print(f"Custom stage mutation {name}: rejected as expected", flush=True)


if __name__ == "__main__":
    main()
