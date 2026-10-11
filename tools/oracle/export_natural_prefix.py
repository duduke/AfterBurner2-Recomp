#!/usr/bin/env python3
"""Declare a finite observed window, preserving every frame in that prefix."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('request', type=Path, help='Original requested controls and .json ROM metadata')
p.add_argument('reference', type=Path, help='MAME observations and .inputs actual physical pins')
p.add_argument('output', type=Path, help='Output prefix basename')
p.add_argument('--frames', type=int, required=True)
args = p.parse_args()
if args.frames <= 0: p.error('frames must be positive')
lines = Path(str(args.reference)+'.inputs').read_text().splitlines()
if lines[0] != 'AB2PHYS1': p.error('invalid physical version')
header = lines[1].split()
if args.frames > int(header[0]) or len(lines) != int(header[0])+2:
    p.error('requested prefix exceeds complete physical window')
for f, line in enumerate(lines[2:]):
    values = list(map(int, line.split()))
    if len(values) != 10 or values[0] != f or any(not 0 <= v <= 255 for v in values[1:]):
        p.error('invalid physical record')
with args.reference.open() as f:
    reader = csv.DictReader(f); columns = reader.fieldnames; rows = list(reader)
if len(rows) != int(header[0]) or any(int(row['frame']) != i+1 for i,row in enumerate(rows)):
    p.error('reference observation window is incomplete')
args.output.parent.mkdir(parents=True, exist_ok=True)
physical = Path(str(args.output)+'.inputs')
reference = Path(str(args.output)+'.mame.csv')
header[0] = str(args.frames)
physical.write_text(lines[0]+'\n'+' '.join(header)+'\n'+'\n'.join(lines[2:args.frames+2])+'\n')
with reference.open('w') as f:
    writer = csv.DictWriter(f, columns); writer.writeheader(); writer.writerows(rows[:args.frames])
metadata = json.loads(Path(str(args.request)+'.json').read_text())
metadata.update(frames=args.frames, switches=list(map(int,header[1:])),
                source_request=str(args.request), source_observed_pins=str(args.reference)+'.inputs',
                source_request_sha256=hashlib.sha256(args.request.read_bytes()).hexdigest(),
                sha256=hashlib.sha256(physical.read_bytes()).hexdigest(),
                reference_sha256=hashlib.sha256(reference.read_bytes()).hexdigest())
Path(str(physical)+'.json').write_text(json.dumps(metadata,indent=2)+'\n')
print(f'Exported {args.frames} contiguous physical frames and observations; completion not inferred')
