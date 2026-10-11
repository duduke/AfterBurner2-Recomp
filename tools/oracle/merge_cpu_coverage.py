#!/usr/bin/env python3
"""Concatenate verified AB2COV02 fixture corpora without changing any record.

Every input must have a passing replay census newer than its capture. The
merged corpus must still pass oracle_cpu_coverage and the full coverage gate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('inputs', nargs='+', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    plans, sources = [], []
    for path in args.inputs:
        path = path.resolve()
        if path == output:
            parser.error('Output must differ from every input; preserve the original corpus')
        census = Path(str(path) + '.coverage.json')
        if not census.exists() or census.stat().st_mtime_ns < path.stat().st_mtime_ns:
            parser.error(f'Replay this input successfully before merging: {path}')
        with path.open('rb') as source:
            header = source.read(12)
            if len(header) != 12 or header[:8] != b'AB2COV02':
                parser.error(f'Unsupported fixture format: {path}')
            count = struct.unpack('>I', header[8:])[0]
            if not count or count > 100000:
                parser.error(f'Invalid fixture plan size: {path}')
            plan = source.read(count * 5)
            if len(plan) != count * 5 or json.loads(census.read_text())['cases'] != count:
                parser.error(f'Incomplete fixture plan/replay: {path}')
        plans.append(plan)
        sources.append((path, header + plan))
    total = sum(len(plan) // 5 for plan in plans)
    if total > 100000:
        parser.error('Merged plan exceeds the replay format acceptance limit')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    provenance = []
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as merged:
            temporary = Path(merged.name)
            merged.write(b'AB2COV02' + struct.pack('>I', total))
            for plan in plans:
                merged.write(plan)
            for path, prefix in sources:
                digest = hashlib.sha256(prefix)
                with path.open('rb') as source:
                    if source.read(len(prefix)) != prefix:
                        raise RuntimeError(f'Input changed during merge: {path}')
                    while chunk := source.read(8 * 1024 * 1024):
                        digest.update(chunk)
                        merged.write(chunk)
                provenance.append(dict(path=str(path), sha256=digest.hexdigest(), bytes=path.stat().st_size))
        os.replace(temporary, output)
        temporary = None
        Path(str(output) + '.merge.json').write_text(json.dumps(dict(cases=total, inputs=provenance), indent=2) + '\n')
        # Never let an earlier output's passing census imply a successful replay.
        Path(str(output) + '.coverage.json').unlink(missing_ok=True)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f'Merged {total} unmodified original-ROM fixtures from {len(sources)} inputs; replay and full coverage checks required')


if __name__ == '__main__':
    main()
