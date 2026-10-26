#!/usr/bin/env python3
"""Retained negative controls for the optional Shuffle ROM data policy."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    (
        "scripted-opening-allowed",
        "} while (result.stages.front() == 5 || result.stages.front() == 13\n"
        "             || result.stages.front() == 23);",
        "} while (false);",
        "Shuffle starts with a scripted stage",
    ),
    (
        "lost-inherited-music",
        "if (command == 0x80 && (!position || music != previous_music)) command = music;",
        "if (false) command = music;",
        "Shuffle lost the source stage's inherited music command",
    ),
    (
        "omitted-stage-word-table",
        "{0x32196u, 0x32324u, 0x32452u, 0x30d54u}",
        "{0x32324u, 0x32452u, 0x30d54u}",
        "Shuffle ordinal table differs from canonical source row",
    ),
    (
        "wrong-dip-bank-stride",
        "reorder(canonical, plan, base + bank * 0x40, 2);",
        "reorder(canonical, plan, base + bank * 0x20, 2);",
        "Shuffle ordinal table differs from canonical source row",
    ),
    (
        "retained-old-adjacency-command",
        "if (command != 18) append(canonical, event, 8, cursor);",
        "append(canonical, event, 8, cursor);",
        "Shuffle changed a retained event or its timestamp",
    ),
    (
        "cold-gradient-wrong-endpoint",
        "StageCatalog::Detail::pointer(canonical, 0x140da + first * 4) + 34, 28);",
        "StageCatalog::Detail::pointer(canonical, 0x140da + first * 4) + 2, 28);",
        "Shuffle cold palette source differs from first stage",
    ),
    (
        "seeded-permutation-duplicate",
        "std::swap(result.stages[size - 1], result.stages[value % size]);",
        "result.stages[size - 1] = result.stages[value % size];",
        "Shuffle requires each stage exactly once",
    ),
    (
        "ending-randomized-with-gameplay",
        "for (unsigned size = count - 1; size > 1; --size)",
        "for (unsigned size = count; size > 1; --size)",
        "Shuffle keeps the final stage last",
    ),
    (
        "instruction-byte-alteration",
        "Detail::clone(canonical.maincpu, owned.maincpu, 0x80000);",
        "Detail::clone(canonical.maincpu, owned.maincpu, 0x80000);\n"
        "        owned.maincpu.rom[0] ^= 1;",
        "Shuffle changed code or data outside the declared source tables",
    ),
]


def main():
    logs = ROOT / "build/oracle/shuffle-rom-mutations"
    logs.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "src/main/engine/shuffle_rom.hpp").read_text()
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    for name, before, _, _ in MUTATIONS:
        if source.count(before) != 1:
            raise RuntimeError(f"{name}: mutation anchor changed")

    with tempfile.TemporaryDirectory(prefix="ab2-shuffle-rom-") as temporary:
        overlay = Path(temporary)
        (overlay / "engine").mkdir()
        header = overlay / "engine/shuffle_rom.hpp"
        for name, before, after, witness in [("baseline", "", "", "")] + MUTATIONS:
            header.write_text(source if name == "baseline" else source.replace(before, after))
            binary = overlay / name
            command = compiler + flags + [
                "-I" + str(overlay), "-Isrc/main", "tests/shuffle_rom_regression.cpp",
                "src/main/roms.cpp", "src/main/romloader.cpp",
                "src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp",
                "-o", str(binary),
            ]
            build_log = logs / f"{name}-build.log"
            with build_log.open("w") as log:
                compiled = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            if compiled.returncode:
                raise RuntimeError(f"{name}: build failed; this is not a rejected control: {build_log}")

            variants = [("rom-free", [])]
            if name == "baseline":
                variants.append(("real-rom", ["--roms"]))
            for label, arguments in variants:
                result = subprocess.run([str(binary)] + arguments, cwd=ROOT,
                                        capture_output=True, text=True)
                output = result.stdout + result.stderr
                run_log = logs / f"{name}-{label}.log"
                run_log.write_text(output)
                if name == "baseline":
                    required = "Shuffle ROM-free policy:"
                    if label == "real-rom":
                        required = "Shuffle real-ROM transforms pass;"
                    if result.returncode != 0 or required not in output:
                        raise RuntimeError(f"Shuffle {label} baseline did not pass: {run_log}")
                    print(f"Shuffle ROM mutation baseline ({label}): passes", flush=True)
                else:
                    if result.returncode != 1 or witness not in output:
                        raise RuntimeError(f"{name}: expected semantic rejection missing: {run_log}")
                    print(f"Shuffle ROM mutation {name}: rejected by data-policy assertion", flush=True)
    print(f"Shuffle ROM controls: {len(MUTATIONS)}/{len(MUTATIONS)} rejected; "
          "ROM-free and real-ROM baselines pass", flush=True)


if __name__ == "__main__":
    main()
