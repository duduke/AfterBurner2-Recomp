#!/usr/bin/env python3
"""Require every declared recovery PC and both measured branch outcomes."""
from pathlib import Path
import json,sys
root=Path(__file__).resolve().parents[2]
pcs={int(p,16) for p in (root/'tools/oracle/main_recovery_pcs.txt').read_text().splitlines() if p and not p.startswith('#')}
captures=[Path(p) for p in sys.argv[1:]] or [root/'build/oracle/cpu_recovery.bin', root/'build/oracle/cpu_recovery_complete.bin']
observed=set();branches={};cases=0
for capture in captures:
 data=json.loads(Path(str(capture)+'.coverage.json').read_text())
 observed.update(pc for cpu,pc,count in data['records'] if cpu==1 and count>=2)
 cases+=data['cases']
 for cpu,pc,a,b in data['branches']:
  assert cpu==1, 'unexpected recovery CPU'
  old=branches.get(pc,(0,0));branches[pc]=(old[0]+a,old[1]+b)
assert observed==pcs, f'recovery PC coverage differs: missing={pcs-observed}, extra={observed-pcs}'
image=(root/'build/oracle/main_recovery_image.bin').read_bytes()
def is_branch(pc):
 op=int.from_bytes(image[pc:pc+2],'big')
 return (op&0xf000==0x6000 and (op>>8)&15>=2) or op&0xf0f8==0x50c8
declared_branches={pc for pc in pcs if is_branch(pc)}
assert set(branches)==declared_branches, 'recovery branch census differs'
assert all(a and b for a,b in branches.values()), 'recovery branch outcome missing'
print(f"Gameplay recovery coverage: {len(pcs)} PCs, {len(branches)} branches with both outcomes, {cases} original-ROM fixtures across {len(captures)} capture(s)")
