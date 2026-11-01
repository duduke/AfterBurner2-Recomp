#!/usr/bin/env python3
"""Timing acceptance must reject paused, missing and interrupted frame sequences."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from android_benchmark import summarize, trace_status, FRAME_MS

keys=['frame','paused','total_ms','work_ms','board_ms','scene_ms','filter_ms','present_ms','ground_ms']
rows=[dict(zip(keys,map(str,[i,0,FRAME_MS,10,8,1,0,1,0]))) for i in range(3)]
result=summarize(rows)
assert result['frames']==3 and abs(result['fps']-59.63740458015267)<1e-10
assert result['work_within_arcade_budget_percent']==100
for bad in [[],[{**r,'paused':'1'} for r in rows],[rows[0],{**rows[1],'frame':'7'},rows[2]]]:
    try:summarize(bad)
    except RuntimeError:pass
    else:raise AssertionError('Invalid measurement accepted')
header=','.join(keys)+'\n'
raw=(header+'\n'.join(','.join(r[k] for k in keys) for r in rows)+'\n').encode()
assert trace_status(raw,3)[0]=='complete'
for short in [header.encode(),(header+','.join(rows[0][k] for k in keys)+'\n').encode()]:
    assert trace_status(short,3)[0]=='writing'
    assert trace_status(short,3,short)[0]=='interrupted'
assert trace_status(raw[:-1],3)[0]=='writing'
print('Android timings: arcade cadence, complete sequence, paused/missing frames and stable interrupted prefixes passed')
