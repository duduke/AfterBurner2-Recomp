#!/usr/bin/env python3
"""Bounded check summaries; full output remains in gitignored build/workflow/."""
import argparse
from collections import deque
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
PROFILES = {
    "quick": ["test", "verify-stage1"],
    "joint": ["oracle-joint-main-progress"],
    "full": ["oracle-joint-main-progress-full", "oracle-joint-reset-window"],
    "required": ["test", "verify-stage1", "oracle-test"],
}
RESULT = re.compile(r"identical|different|skipped|passed|PASS|FAIL|error:|fatal error|missing|cannot open|No such file", re.I)


def context():
    """Read the authoritative checkpoint without copying historical sections."""
    roadmap = (ROOT / "docs/ROADMAP.md").read_text()
    section = roadmap.split("## Working on the remaining tasks (read this first)", 1)[1]
    section = section.split("## Phase overview", 1)[0]
    print(section.strip())
    print("\nWorking tree:", flush=True)
    subprocess.run(["git", "status", "--short"], cwd=ROOT, check=True)


def run(targets, jobs):
    directory = ROOT / "build/workflow" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    directory.mkdir(parents=True)
    results = []
    for number, target in enumerate(targets, 1):
        # Targets are arguments, never shell text. Reject make options/assignments.
        if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_./-]*", target):
            raise ValueError(f"Invalid make target: {target}")
        log = directory / f"{number:02d}-{target.replace('/', '_')}.log"
        command = ["make", "--no-print-directory", f"-j{jobs}", target]
        print(f"RUN {target}; full log: {log.relative_to(ROOT)}", flush=True)
        started = time.monotonic()
        tail, evidence = deque(maxlen=18), deque(maxlen=8)
        try:
            with log.open("w") as output:
                process = subprocess.Popen(command, cwd=ROOT, stdout=output,
                                           stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    status = process.wait()
                except KeyboardInterrupt:
                    # Stop make and its compiler/test children together.
                    os.killpg(process.pid, signal.SIGTERM)
                    process.wait()
                    status = 130
            with log.open(errors="replace") as output:
                for line in output:
                    tail.append(line.rstrip())
                    if RESULT.search(line) and not line.startswith(("clang", "g++", "c++")):
                        evidence.append(line.rstrip()[:600])
        except OSError as error:
            status = 127
            tail.append(str(error))
        seconds = round(time.monotonic() - started, 2)
        results.append(dict(target=target, exit_code=status, seconds=seconds,
                            log=str(log.relative_to(ROOT)), evidence=list(evidence)))
        (directory / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        print(f"{'PASS' if status == 0 else 'FAIL'} {target} ({seconds:.2f}s)")
        for line in evidence if status == 0 else tail:
            print("  " + line[:600])
        if status:
            return status
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["context", "run"])
    parser.add_argument("targets", nargs="*", help="Make targets, or one profile: " + ", ".join(PROFILES))
    parser.add_argument("-j", "--jobs", type=int, default=min(8, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    if args.action == "context":
        if args.targets:
            parser.error("context takes no targets")
        context()
        return 0
    if not args.targets:
        parser.error("run needs a profile or make targets")
    targets = PROFILES.get(args.targets[0], args.targets) if len(args.targets) == 1 else args.targets
    # Validate everything before starting any build.
    if any(not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_./-]*", t) for t in targets):
        parser.error("targets must be make target names, without options or assignments")
    return run(targets, args.jobs)


if __name__ == "__main__":
    raise SystemExit(main())
