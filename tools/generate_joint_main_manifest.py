#!/usr/bin/env python3
"""List the existing address-labelled caller ports for joint capture/replay.

This reads checked-in C++ case labels, not ROM opcodes. It adds no behavior.
Unknown work remains a supplied frontier. --check detects stale membership.
"""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = (root / 'src/main/engine/omainirqservice.cpp').read_text()
source = source[source.index('OMainIrqService::Instruction OMainIrqService::caller_step'):]
sources = [source]
for name in ('input', 'flight', 'companion', 'weapon', 'hostcaller', 'irqwrapper', 'producer', 'irqjointcallee', 'remaining'):
    sources.append((root / f'src/main/engine/omain{name}schedule.inc').read_text())
pcs = sorted(int(pc, 16) for s in sources for pc in re.findall(r'case 0x([0-9a-f]+):', s))
pcs = sorted(pcs+[0x138ba])
if len(set(pcs)) != len(pcs):
    raise SystemExit('duplicate caller instruction address')
rows = ['    ' + ','.join(f'0x{pc:x}' for pc in pcs[i:i+10]) + ',' for i in range(0, len(pcs), 10)]
outputs = {
    'src/main/engine/omainjointmanifest.hpp': '#pragma once\n#include <array>\n#include <algorithm>\n#include <cstdint>\n// Generated existing-port membership; no ROM data or instruction behavior.\n'
        + f'inline constexpr std::array<uint32_t,{len(pcs)}> main_joint_pcs{{{{\n'
        + '\n'.join(rows) + '\n}};\ninline bool main_joint_instruction(uint32_t pc) {\n'
        + '    return std::binary_search(main_joint_pcs.begin(),main_joint_pcs.end(),pc);\n}\n',
    'tools/oracle/joint_main_schedule.lua': '-- Generated existing-port membership; no ROM data or instruction behavior.\nreturn {\n'
        + '\n'.join(rows) + '\n}\n',
}
for path, text in outputs.items():
    target = root / path
    if args.check:
        if not target.exists() or target.read_text() != text:
            raise SystemExit('generated file differs: ' + path)
    else:
        target.write_text(text)
print(f'{len(pcs)} existing caller PCs; ' + ('membership matches' if args.check else 'membership generated'))
