#!/usr/bin/env python3
"""Reject sound ROM-fetch and latch-value mutations without production edits."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    capture = root / 'build/oracle/sound_boot.bin'
    baseline = subprocess.run([str(root / 'build/oracle_sound_boot'), str(capture)],
                              cwd=root, capture_output=True, text=True)
    if baseline.returncode:
        raise SystemExit(f'Baseline must pass before mutations: {baseline.stderr}')
    source = (root / 'src/main/hwaudio/xboard_sound_cpu.cpp').read_text()
    source = source.replace('"../../third_party/z80.h"',
                            f'"{root}/src/third_party/z80.h"')
    mutations = [
        ('rom-address', 'd=s.program[a];', 'd=s.program[a^1];'),
        ('latch-value', 'd=latch_read();', 'd=latch_read()^1;'),
        ('pcm-read-address', 'd=pcm_read(a&255);', 'd=pcm_read((a^1)&255);'),
    ]
    with tempfile.TemporaryDirectory(prefix='sound-boot-mutations-', dir=root / 'build/oracle') as tmp:
        work = Path(tmp)
        for name, old, new in mutations:
            if source.count(old) != 1:
                raise SystemExit(f'Mutation anchor missing or ambiguous: {name}')
            cpp = work / f'{name}.cpp'
            binary = work / name
            cpp.write_text(source.replace(old, new))
            command = shlex.split(os.environ.get('CXX', 'c++'))
            command += shlex.split(os.environ.get('CXXFLAGS', '-std=c++20 -Wall -Wextra -O2'))
            command += ['-Isrc/main', 'tests/oracle_sound_boot.cpp', str(cpp), '-o', str(binary)]
            subprocess.run(command, cwd=root, check=True)
            result = subprocess.run([str(binary), str(capture)], cwd=root, capture_output=True, text=True)
            if result.returncode != 1 or 'native sound boot differs' not in result.stderr:
                raise SystemExit(f'Mutation {name} failed to produce a comparison mismatch: {result.stdout}{result.stderr}')
            print(f'Sound boot mutation {name}: rejected by arcade comparison')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
