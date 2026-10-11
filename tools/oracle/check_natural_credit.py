#!/usr/bin/env python3
"""Compare every declared transition observation, including unchanged frames.

This gate covers the named RAM words and frame timestamps only. CPU/bus,
device, RGB and audio comparisons remain separate required acceptance work.
"""
import argparse
import csv
from itertools import zip_longest
from pathlib import Path


def compare(inputs, native, mame):
    with Path(inputs).open() as f:
        if f.readline().strip() != "AB2PHYS1":
            raise ValueError("invalid physical recording version")
        count, *dips = map(int, f.readline().split())
        if count <= 0 or len(dips) != 3 or any(not 0 <= v <= 255 for v in dips):
            raise ValueError("invalid input metadata")
        rows = 0
        for rows, line in enumerate(f, 1):
            values = list(map(int, line.split()))
            if len(values) != 10 or values[0] != rows-1 or any(not 0 <= v <= 255 for v in values[1:]):
                raise ValueError(f"invalid physical input frame {rows-1}")
        if rows != count:
            raise ValueError("input window incomplete")
    columns = ['frame', 'time_ns', 'mode', 'stage_index', 'lives', 'death_state', 'score', 'credits_bcd', 'carrier_state', 'refuel_state']
    with Path(native).open() as a, Path(mame).open() as b:
        na, mb = csv.DictReader(a), csv.DictReader(b)
        if na.fieldnames != columns or mb.fieldnames != columns:
            raise ValueError("observation schema differs")
        rows = 0
        for rows, (left, right) in enumerate(zip_longest(na, mb), 1):
            if left is None or right is None:
                raise ValueError(f"missing observation at frame {rows}")
            if left != right or int(left['frame']) != rows:
                raise ValueError(f"observation differs at frame {rows}: native={left}, MAME={right}")
        if rows != count:
            raise ValueError(f"observation window incomplete: {rows}/{count}")
    return count


def require_ending(path):
    """Assert the observed ROM mode sequence, without extending stream scope."""
    with Path(path).open() as f:
        rows = list(csv.DictReader(f))
    stages, modes = [], []
    flight = False
    for row in rows:
        mode, stage = int(row['mode']), int(row['stage_index'])
        if mode == 16:
            flight = True
            if not stages or stages[-1] != stage:
                stages.append(stage)
        if flight and (not modes or modes[-1] != mode):
            modes.append(mode)
    if stages != list(range(23)):
        raise ValueError(f'ordered flight stages incomplete: {stages}')
    if modes != [16,17,18,19,20,21,22,25,26,0,1]:
        raise ValueError(f'ending/ranking/attract sequence incomplete: {modes}')
    carrier = {int(r['carrier_state']) for r in rows if int(r['mode']) in (15,16,17,18)}
    refuel = {int(r['refuel_state']) for r in rows if int(r['mode']) == 16}
    if not set(range(13)) <= carrier or not set(range(8)) <= refuel:
        raise ValueError(f'carrier/refuel progress incomplete: carrier={sorted(carrier)}, refuel={sorted(refuel)}')
    return stages, modes


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs'); p.add_argument('native'); p.add_argument('mame')
    p.add_argument('--require-ending', action='store_true')
    args = p.parse_args()
    count = compare(args.inputs, args.native, args.mame)
    print(f"Natural transition observations: {count} of {count} identical, 0 skipped; ending not inferred")
    if args.require_ending:
        require_ending(args.native)
        print('Observed all 23 ordered stages, carrier states 0–12, refuel states 0–7, ending modes 17–22, ranking and return to attract; named observations only')


if __name__ == '__main__':
    main()
