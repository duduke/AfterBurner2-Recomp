#!/usr/bin/env python3
"""Prove the production I/O oracle rejects endpoint mutations, without edits."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    capture = Path(sys.argv[1] if len(sys.argv) > 1 else
                   "build/oracle/mainirq_endpoints.bin").resolve()
    if not capture.is_file():
        raise SystemExit(f"Missing capture: {capture}; run make oracle-capture-mainirq")
    source = (root / "src/main/engine/omainio.hpp").read_text()
    mutations = [
        ("mux", "(mux>>2)&7", "(mux>>3)&7"),
        ("instant-read", "return board.adc.read(board.now());",
         "return inputs[(mux>>2)&7];"),
        ("sound-command", "sound_commands.push_back(command);",
         "sound_commands.push_back(command ^ 1);"),
    ]
    work = root / "build/oracle"
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="io-mutations-", dir=work) as tmp:
        overlay = Path(tmp)
        (overlay / "engine").mkdir()
        for name, old, new in mutations:
            if old not in source:
                raise SystemExit(f"Mutation target missing: {name}")
            (overlay / "engine/omainio.hpp").write_text(source.replace(old, new))
            binary = overlay / name
            command = shlex.split(os.environ.get("CXX", "c++"))
            command += shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
            command += [f"-I{overlay}", "-Isrc/main", "tests/oracle_mainirq.cpp",
                        "src/main/engine/omainadc.cpp", "src/main/engine/omainsound.cpp",
                        "src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp",
                        "-o", str(binary)]
            subprocess.run(command, cwd=root, check=True)
            result = subprocess.run([str(binary), str(capture)], cwd=root,
                                    text=True, capture_output=True)
            # A compilation error, crash or missing fixture is not mutation proof.
            if result.returncode != 1 or "Comparator IRQ vs arcade:" not in result.stdout:
                raise SystemExit(f"Mutation {name} did not produce an oracle mismatch:\n"
                                 f"{result.stdout}{result.stderr}")
            print(f"{name}: rejected")
            for line in result.stdout.splitlines():
                if "vs arcade:" in line or "IRQ hardware timeline:" in line:
                    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
