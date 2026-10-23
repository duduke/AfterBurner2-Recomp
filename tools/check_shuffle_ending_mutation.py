#!/usr/bin/env python3
"""Check the recorded Shuffle ending path and reject missing finale coverage."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
command = [sys.argv[1] if len(sys.argv) > 1 else "./build/oracle_shuffle_run", "build/oracle/shuffle_complete",
           "build/oracle/shuffle_complete.csv", "--complete", "--check-coverage-only"]
directory = ROOT / "build/oracle/shuffle-ending-mutation"
directory.mkdir(parents=True, exist_ok=True)
baseline = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(directory / "baseline.log").write_text(baseline.stdout)
if baseline.returncode != 0:
    raise SystemExit(f"Shuffle ending coverage baseline failed; see {directory / 'baseline.log'}")
mutant = subprocess.run(command + ["--bad-ending-coverage"], cwd=ROOT,
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(directory / "missing-finale.log").write_text(mutant.stdout)
if mutant.returncode != 1 or "Complete Shuffle did not observe all 22 gameplay positions and the native finale" not in mutant.stdout:
    raise SystemExit(f"Missing-finale coverage mutation did not fail by the path gate; see {directory / 'missing-finale.log'}")
print("Shuffle ending coverage baseline passed; missing finale rejected by path gate, 0 skipped.")
