#!/usr/bin/env python3
"""Inventory observed scheduler timer owners; this is not an acceptance test.

Input is the read-only AB2_DEVICE_TRACE sidecar from the isolated MAME probe.
No device deadline or CPU state is seeded into a native implementation.
"""
import argparse
from collections import Counter

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('trace')
args = parser.parse_args()
heads, fired = Counter(), Counter()
late = Counter()
with open(args.trace) as stream:
    for line in stream:
        fields = line.rstrip().split(' ', 4)
        if fields[0] == 'H':
            heads[fields[4]] += 1
        elif fields[0] == 'F':
            fields = line.rstrip().split(' ', 3)
            delivered, deadline = map(int, fields[1:3])
            if delivered < deadline:
                raise ValueError('timer fired before its deadline')
            fired[fields[3]] += 1
            late[fields[3]] = max(late[fields[3]], delivered - deadline)
        else:
            raise ValueError('unknown device trace record')
print('Timer owner | Head grants | Callbacks | Maximum delivery delay (attoseconds)')
for name in sorted(heads.keys() | fired.keys()):
    print(f'{name} | {heads[name]} | {fired[name]} | {late[name]}')
print('Observed owners only; YM/PCM execution, device deadlines and RGB ownership require independent native checks.')
