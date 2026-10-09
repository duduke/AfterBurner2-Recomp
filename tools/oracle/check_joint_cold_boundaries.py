#!/usr/bin/env python3
"""Assert continuous cold execution crosses the retained joint fixture edges."""
import argparse,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('cold',type=Path);p.add_argument('retained',type=Path);a=p.parse_args()
phase=struct.Struct('>BQBIHHIBB')
def groups(path):
 with path.open('rb') as f:
  magic=f.read(8);assert magic in (b'AB2ARB09',b'AB2ARB10')
  assert len(f.read(0x27000+65))==0x27000+65
  while h:=f.read(20):
   grant,base,count=struct.unpack('>QQI',h);events=[]
   for _ in range(count):
    record=phase.unpack(f.read(24));kind=record[-1]
    if kind:assert len(f.read(94))==94
    events.append(record)
   yield grant,events
first=[];sub_started=False;last_cpu=[[],[]]
for grant,events in groups(a.retained):
 for cpu in (1,2):
  tail=[e for e in events if e[0]==cpu]
  if tail:last_cpu[cpu-1]=tail
 for event in events:
  if event[0]==2 and not sub_started:
   if event[-1]:sub_started=True
   else:first.append(event[:-1])
assert len(first)==46,'retained initial partial ISR boundary changed'
# Snapshots differ at the first partial ISR by design; program bus records do not.
last=last_cpu[0]+last_cpu[1]
targets={e[:-1] for e in last}|set(first);remaining=targets.copy()
stop=[max(e[1] for e in last if e[0]==cpu) for cpu in (1,2)]
continued=[False,False];next_boundary=[None,None]
for grant,events in groups(a.cold):
 for event in events:
  remaining.discard(event[:-1]);cpu=event[0]-1
  if event[-1] and event[1]>stop[cpu] and next_boundary[cpu] is None:
   next_boundary[cpu]=event[-1];continued[cpu]=event[-1] not in (3,7)
assert not remaining,f'{len(remaining)} retained boundary bus assertions absent from cold run'
assert all(continued),'both retained final CPU continuations must reach a later instruction/IRQ snapshot without a reset replacement'
print(f'Cold fixture boundaries: {len(first)}/46 initial ISR phases and {len(last)}/{len(last)} final-grant phases identical; both CPUs continue beyond retained final grants, 0 skipped')
