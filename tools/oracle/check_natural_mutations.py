#!/usr/bin/env python3
"""Prove the finite natural-observation gate rejects wrong/missing intervals."""
import argparse
import csv
import tempfile
from pathlib import Path
from check_natural_credit import compare, require_ending

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('inputs'); p.add_argument('native'); p.add_argument('mame')
p.add_argument('--require-ending', action='store_true')
args = p.parse_args()
compare(args.inputs, args.native, args.mame)
with Path(args.native).open() as f:
    reader = csv.DictReader(f)
    columns, rows = reader.fieldnames, list(reader)
assert len(rows) >= 3, 'mutation gate needs a nontrivial observation window'
with tempfile.TemporaryDirectory(prefix='natural-mutations-', dir='build/oracle') as temp:
    target = Path(temp)/'observations.csv'
    for name in ('state', 'frame', 'gap', 'tail', 'extra'):
        changed = [dict(row) for row in rows]
        index = len(changed)//2
        if name == 'state': changed[index]['mode'] = str(int(changed[index]['mode'])^1)
        if name == 'frame': changed[index]['frame'] = str(int(changed[index]['frame'])+1)
        if name == 'gap': changed.pop(index)
        if name == 'tail': changed.pop()
        if name == 'extra': changed.append(changed[-1])
        with target.open('w') as f:
            writer = csv.DictWriter(f, columns); writer.writeheader(); writer.writerows(changed)
        try:
            compare(args.inputs, target, args.mame)
        except ValueError:
            print(f'Natural observation mutation {name}: rejected')
        else:
            raise SystemExit(f'Natural observation mutation {name}: survived')
    if args.require_ending:
        require_ending(args.native)
        for name in ('stage-order', 'ending-mode', 'attract-return', 'carrier-state', 'refuel-state'):
            changed = [dict(row) for row in rows]
            for row in changed:
                if name == 'stage-order' and row['mode'] == '16' and row['stage_index'] == '12':
                    row['stage_index'] = '11'
                if name == 'ending-mode' and row['mode'] == '22': row['mode'] = '21'
                if name == 'carrier-state' and row['carrier_state'] == '12': row['carrier_state'] = '11'
                if name == 'refuel-state' and row['refuel_state'] == '7': row['refuel_state'] = '6'
            if name == 'attract-return': changed[-1]['mode'] = '0'
            with target.open('w') as f:
                writer = csv.DictWriter(f, columns); writer.writeheader(); writer.writerows(changed)
            # Test semantics independently of native/reference equality.
            compare(args.inputs, target, target)
            try:
                require_ending(target)
            except ValueError:
                print(f'Natural ending mutation {name}: rejected')
            else:
                raise SystemExit(f'Natural ending mutation {name}: survived')
