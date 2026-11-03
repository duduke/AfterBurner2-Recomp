#!/usr/bin/env python3
"""Record the exact working sources for a Linux preview, excluding ignored data."""
import hashlib
import argparse
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/linux-source.tar.gz')
    args = parser.parse_args()
    if not args.output.name.endswith('.tar.gz'):
        parser.error('--output must end in .tar.gz')
    files = set(subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')) - {''}
    files.update(('tools/build_linux.sh', 'tools/package_linux.py', 'tools/snapshot_linux.py',
                  'tools/linux-arm64.Containerfile', 'tools/deps/build_librashader_linux.sh', 'docs/LINUX.md'))
    target = args.output.resolve()
    target.parent.mkdir(parents=True, exist_ok=True)
    hashes = {}
    with tarfile.open(target, 'w:gz') as archive:
        for relative in sorted(files):
            path = ROOT / relative
            if path.is_symlink() or not path.is_file():
                raise SystemExit(f'Expected regular source file: {relative}')
            if relative.startswith(('roms/', 'build/')) and relative not in ('roms/README.md', 'roms/roms.txt'):
                raise SystemExit(f'Refusing data/build file: {relative}')
            hashes[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
            archive.add(path, arcname=relative, recursive=False)
    manifest = dict(base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    working_tree=subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True),
                    archive_sha256=hashlib.sha256(target.read_bytes()).hexdigest(), files=hashes)
    target.with_name(target.name[:-7] + '.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(target)


if __name__ == '__main__':
    main()
