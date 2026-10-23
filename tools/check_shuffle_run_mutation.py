#!/usr/bin/env python3
"""Prove the arranged-run comparator rejects a semantic reference change."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
binary = sys.argv[1] if len(sys.argv) > 1 else "./build/oracle_shuffle_run"
result = subprocess.run(
    [binary, "build/oracle/shuffle_run", "build/oracle/shuffle_run.csv", "--bad-reference"],
    cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
)
log = ROOT / "build/oracle/shuffle-run-bad-reference.log"
log.write_text(result.stdout)
if result.returncode != 1 or "Shuffle execution differs from independent MAME at frame 1:" not in result.stdout:
    raise SystemExit(f"Shuffle reference mutation did not fail by observation comparison; see {log}")
print("Shuffle arranged-run changed timestamp rejected by independent observation comparison, 0 skipped.")
