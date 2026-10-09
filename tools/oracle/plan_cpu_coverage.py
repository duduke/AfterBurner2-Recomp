#!/usr/bin/env python3
"""Plan explicit independent PC/branch fixtures from port membership and a passed log."""
from pathlib import Path
import argparse,json,re
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('output',type=Path);p.add_argument('--limit',type=int);p.add_argument('--legacy',action='store_true');p.add_argument('--main-image',type=Path);p.add_argument('--sub-image',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
log=a.baseline.read_text()
if log.lstrip().startswith('{'):
 data=json.loads(log)
 if not data.get('main_closed') or not data.get('sub_continuity'):raise SystemExit('baseline lacks closed main/sub acceptance')
 seen=[{pc for owner,pc,count in data['records'] if owner==cpu and count} for cpu in range(2)]
else:
 seen=[{int(pc,16) for pc in re.findall(rf'Native publication PC CPU={cpu} pc=([0-9a-f]+):',log)} for cpu in range(2)]
if not all(seen):raise SystemExit('baseline has no complete native PC census')
main={int(pc,16) for pc in re.findall(r'0x([0-9a-f]+)',(root/'tools/oracle/joint_main_schedule.lua').read_text())}
sub={int(pc,16) for pc in re.findall(r'\{0x([0-9a-f]+),',(root/'src/main/engine/osubprogress_manifest.hpp').read_text())}
if a.legacy:
 extra=(root/'tools/oracle/main_remaining_pcs.txt').read_text().split('# Additional 1,200-frame physical-input gameplay PCs.',1)[1]
 main-={int(pc,16) for pc in extra.splitlines() if pc and not pc.startswith('#')}
missing=[sorted(main-seen[0]),sorted(sub-seen[1])]
cases=[]
for cpu,pcs in enumerate(missing,1):
 for pc in pcs:cases.append(dict(cpu=cpu,pc=pc,sr=0x2700,d0=0,d1=0,d2=0))
def condition(cc,sr):
 c=bool(sr&1);v=bool(sr&2);z=bool(sr&4);n=bool(sr&8)
 return [True,True,not c and not z,c or z,not c,c,not z,z,not v,v,not n,n,n==v,n!=v,not z and n==v,z or n!=v][cc]
branches=[]
if a.main_image and a.sub_image:
 for cpu,pcs,image in ((1,main,a.main_image.read_bytes()),(2,sub,a.sub_image.read_bytes())):
  for pc in sorted(pcs):
   op=int.from_bytes(image[pc:pc+2],'big')
   if op&0xf000==0x6000 and (op>>8)&15>=2:
    cc=(op>>8)&15
    for take in (False,True):
     sr=next(sr for sr in range(16) if condition(cc,sr)==take)
     cases.append(dict(cpu=cpu,pc=pc,sr=0x2700|sr,d0=0,d1=0,d2=0))
    branches.append(dict(cpu=cpu,pc=pc,kind='condition'))
   elif op&0xfff8==0x51c8:
    for count in (0,1):cases.append(dict(cpu=cpu,pc=pc,sr=0x2700,d0=0,d1=0,d2=0,counter_reg=op&7,counter_value=count))
    branches.append(dict(cpu=cpu,pc=pc,kind='dbf'))
if a.limit:cases=cases[:a.limit]
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text('return {\n'+''.join(' {'+','.join(f'{k}={v}' for k,v in c.items())+'},\n' for c in cases)+'}\n')
a.output.with_suffix('.json').write_text(json.dumps(dict(baseline=str(a.baseline),missing_main=missing[0],missing_sub=missing[1],branches=branches,cases=cases),indent=2)+'\n')
print(f'PC coverage plan: {len(missing[0])} absent main, {len(missing[1])} absent sub; {len(cases)} explicit instruction fixtures')
