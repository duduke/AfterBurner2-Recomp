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

JSON_MUTATIONS = [
    (
        "allow-unported-command-2",
        "if (command == 2u)\n            throw std::runtime_error(\"Stage control command 2 ($DE98) is unused in canonical ROM and unported\");",
        "(void)0;",
    ),
    (
        "swap-special-road-enter-to-cmd-11",
        "if (c == \"special_road_enter\") return 17u;",
        "if (c == \"special_road_enter\") return 11u;",
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
    json_source = (ROOT / "src/main/engine/custom_stage_json.hpp").read_text()
    interactive_source = (ROOT / "src/main/engine/custom_stage_interactive.hpp").read_text()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    prebuilt_objs = sorted(str(p) for p in (ROOT / "build/check_obj/src").rglob("*.o"))
    if not prebuilt_objs:
        raise RuntimeError("Missing build/check_obj/src/*.o; run make test-custom-stages first")

    cases = [("baseline", None, "", "")]
    for name, before, after in ROM_MUTATIONS:
        cases.append((name, "rom", before, after))
    for name, before, after in JSON_MUTATIONS:
        cases.append((name, "json", before, after))
    for name, before, after in INTERACTIVE_MUTATIONS:
        cases.append((name, "interactive", before, after))

    with tempfile.TemporaryDirectory(prefix="ab2-custom-stage-") as temporary:
        overlay = Path(temporary)
        (overlay / "engine").mkdir()
        rom_header = overlay / "engine/custom_stage_rom.hpp"
        json_header = overlay / "engine/custom_stage_json.hpp"
        interactive_header = overlay / "engine/custom_stage_interactive.hpp"

        for name, target, before, after in cases:
            rom_header.write_text(rom_source)
            json_header.write_text(json_source)
            interactive_header.write_text(interactive_source)
            if target == "rom":
                if rom_source.count(before) != 1:
                    raise RuntimeError(f"{name}: mutation anchor changed in custom_stage_rom.hpp")
                rom_header.write_text(rom_source.replace(before, after))
            elif target == "json":
                if json_source.count(before) != 1:
                    raise RuntimeError(f"{name}: mutation anchor changed in custom_stage_json.hpp")
                json_header.write_text(json_source.replace(before, after))
            elif target == "interactive":
                if interactive_source.count(before) != 1:
                    raise RuntimeError(f"{name}: mutation anchor changed in custom_stage_interactive.hpp")
                interactive_header.write_text(interactive_source.replace(before, after))

            binary = overlay / name
            command = (
                compiler
                + flags
                + [
                    "-pthread",
                    "-I" + str(overlay),
                    "-Isrc/main",
                    "tests/custom_stage_rom_regression.cpp",
                ]
                + prebuilt_objs
                + ["-o", str(binary)]
            )
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
