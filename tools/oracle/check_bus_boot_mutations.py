#!/usr/bin/env python3
"""Reject boot/shared-bus regressions against the cold MAME transcript."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    args = ['build/oracle/bus_arbiter_boot.bin', 'build/oracle/arbiter_boot.txt', '--require-boot']
    baseline = subprocess.run(['build/oracle_bus_arbiter', *args], cwd=root,
                              capture_output=True, text=True)
    if baseline.returncode:
        raise SystemExit(f'Baseline must pass before mutations: {baseline.stderr}')
    mutations = [
        ('reset-clears-shared', 'tests/oracle_bus_arbiter.cpp',
         'vector_next[c]=0;', 'vector_next[c]=0; if(c==1)std::fill(ram,ram+SubBus::SHARED_BYTES,0);'),
        ('main-mirror', 'src/main/engine/mainbus.cpp',
         'SHARED + (a < 0x2a0000 ? 0 : 0x4000) + (a & 0x3fff)',
         'SHARED + (a < 0x2a0000 ? 0 : 0x4000) + ((a ^ 2) & 0x3fff)'),
        ('sub-bank-alias', 'src/main/engine/subbus.cpp',
         '(a < 0xa0000 ? 0 : 0x4000) + (a & 0x3fff)', '(a & 0x3fff)'),
        ('main-high-lane', 'src/main/engine/mainbus.cpp',
         'm.bytes[o] = static_cast<uint8_t>(data >> 8);',
         'm.bytes[o] = static_cast<uint8_t>(data);'),
        ('sub-byte-lane', 'src/main/engine/subbus.cpp',
         'shared[ram_offset(a)] = data;', 'shared[ram_offset(a) ^ 1] = data;'),
        ('reverse-grants', 'src/main/hwmath/xboard_bus_arbiter.hpp',
         'for(unsigned cpu=0;cpu<3;++cpu)',
         'for(unsigned order=0;order<3;++order) if(unsigned cpu=2-order;true)'),
        ('extra-budget-clock', 'src/main/hwmath/xboard_bus_arbiter.hpp',
         'auto cycles=(target-s.local[cpu])/cycle;',
         'auto cycles=(target-s.local[cpu])/cycle+1;'),
        ('boost-duration', 'src/main/hwmath/xboard_bus_arbiter.hpp',
         'IRQ_DURATION=100000000000000ULL', 'IRQ_DURATION=60000000000000ULL'),
    ]
    with tempfile.TemporaryDirectory(prefix='bus-boot-mutations-', dir=root / 'build/oracle') as tmp:
        work = Path(tmp)
        for name, path, old, new in mutations:
            source = (root / path).read_text()
            if source.count(old) != 1:
                raise SystemExit(f'Mutation anchor missing or ambiguous: {name}')
            changed = work / name / path
            changed.parent.mkdir(parents=True)
            changed.write_text(source.replace(old, new))
            binary = work / name / 'oracle'
            mainbus = changed if path.endswith('mainbus.cpp') else root / 'src/main/engine/mainbus.cpp'
            subbus = changed if path.endswith('subbus.cpp') else root / 'src/main/engine/subbus.cpp'
            command = shlex.split(os.environ.get('CXX', 'c++'))
            command += shlex.split(os.environ.get('CXXFLAGS', '-std=c++20 -Wall -Wextra -O2'))
            command += [f'-I{work / name / "src/main"}', '-Isrc/main',
                        str(changed if path.endswith('oracle_bus_arbiter.cpp') else root / 'tests/oracle_bus_arbiter.cpp'), str(mainbus), str(subbus), '-o', str(binary)]
            subprocess.run(command, cwd=root, check=True)
            result = subprocess.run([str(binary), *args], cwd=root, capture_output=True, text=True)
            # A build/crash/coverage error cannot substitute for a comparison failure.
            mismatches = ('Arbitrated shared read CPU=', 'Budget CPU=', 'Arbiter base=')
            if result.returncode != 1 or not any(s in result.stderr for s in mismatches):
                raise SystemExit(f'{name} did not produce a comparison mismatch: {result.stdout}{result.stderr}')
            print(f'Boot/shared mutation {name}: rejected by MAME comparison')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
