#!/usr/bin/env python3
"""Require source membership and both branch outcomes in passed MAME fixtures."""
import json,re,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
plan=json.loads(Path(sys.argv[1]).read_text());result=json.loads(Path(sys.argv[2]).read_text())
baseline=Path(plan['baseline']).read_text()
if baseline.lstrip().startswith('{'):
 b=json.loads(baseline)
 assert b['main_closed'] and b['sub_continuity']
 seen=[{pc for cpu,pc,n in b['records'] if cpu==owner and n} for owner in range(2)]
else:
 assert '0 different, 0 skipped' in baseline and '2 supplied entry contexts' in baseline
 seen=[{int(pc,16) for pc in re.findall(rf'Native publication PC CPU={cpu} pc=([0-9a-f]+):',baseline)} for cpu in range(2)]
members=[{int(pc,16) for pc in re.findall(r'0x([0-9a-f]+)',(root/'tools/oracle/joint_main_schedule.lua').read_text())},
 {int(pc,16) for pc in re.findall(r'\{0x([0-9a-f]+),',(root/'src/main/engine/osubprogress_manifest.hpp').read_text())}]
assert result['cases']==len(plan['cases']) and result['cases']>=len(plan['missing_main'])+len(plan['missing_sub'])
covered=[{pc for cpu,pc,n in result['records'] if cpu==owner+1 and n} for owner in range(2)]
for cpu in range(2):
 missing=members[cpu]-seen[cpu]
 assert missing==set(plan['missing_main' if cpu==0 else 'missing_sub']), 'stale fixture plan'
 assert missing<=covered[cpu] and members[cpu]<=seen[cpu]|covered[cpu], 'unexercised source PC'
branches={(cpu,pc):(untaken,taken) for cpu,pc,untaken,taken in result['branches']}
assert len(plan['branches'])==995, 'branch census changed; regenerate/review plan'
for b in plan['branches']:assert all(branches.get((b['cpu'],b['pc']),(0,0))), f'missing branch direction {b}'
print(f"Main/sub coverage: {len(members[0])}/{len(members[0])} main, {len(members[1])}/{len(members[1])} sub PCs; {len(plan['branches'])} branches with both directions; independent fixtures supplement retained baseline")
