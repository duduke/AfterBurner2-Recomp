#!/usr/bin/env python3
"""Run a bounded Android debug benchmark; reject incomplete/paused traces."""
import argparse
import csv
import hashlib
import io
import json
import re
from pathlib import Path
import statistics
import subprocess
import time
from android import SDK

PACKAGE='org.afterburner2.app'
# XBoardTimeline: 64,000 ns per scanline, 262 lines per physical frame.
FRAME_MS=64_000*262/1_000_000

def trace_status(raw,frames,previous=None):
    rows=list(csv.DictReader(io.StringIO(raw.decode())))
    if len(rows)>=frames and raw.endswith(b'\n'):return 'complete',rows
    return ('interrupted' if raw==previous else 'writing'),rows

def summarize(rows):
    if not rows or any(r['paused']!='0' for r in rows):
        raise RuntimeError('Empty or paused benchmark; unlock the phone and repeat')
    if any(int(b['frame'])!=int(a['frame'])+1 for a,b in zip(rows,rows[1:])):
        raise RuntimeError('Non-contiguous benchmark frame numbers')
    result={'frames':len(rows),'target_fps':1000/FRAME_MS,'frame_budget_ms':FRAME_MS}
    for name in ['total_ms','work_ms','board_ms','scene_ms','filter_ms','present_ms','ground_ms']:
        values=sorted(float(r[name]) for r in rows)
        result[name]={'mean':statistics.mean(values),'p95':values[min(len(values)-1,int(.95*len(values)))],
                      'p99':values[min(len(values)-1,int(.99*len(values)))],'max':values[-1]}
    result['fps']=1000/result['total_ms']['mean']
    result['work_within_arcade_budget_percent']=100*sum(float(r['work_ms'])<=FRAME_MS for r in rows)/len(rows)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial',required=True);p.add_argument('--stage',type=int,default=0)
    p.add_argument('--frames',type=int,default=900);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--viewport',choices=['original','16:9','16:10','21:9'],help='benchmark a copied settings file without changing saved preferences')
    a=p.parse_args()
    if not 0<=a.stage<=23 or not 1<=a.frames<=36000:p.error('invalid stage/frame bound')
    adb=[str(SDK/'platform-tools/adb'),'-s',a.serial]
    def call(*args,check=True):
        return subprocess.run([*adb,*args],check=check,capture_output=True)
    package_path=call('shell','pm','path',PACKAGE).stdout.decode().splitlines()[0].removeprefix('package:')
    apk_hash=call('shell','sha256sum',package_path).stdout.decode().split()[0]
    call('shell','am','force-stop',PACKAGE)
    call('shell','input','keyevent','224');call('shell','wm','dismiss-keyguard')
    time.sleep(1)
    policy=call('shell','dumpsys','window','policy').stdout.decode()
    if re.search(r'^\s*showing=true\s*$',policy,re.M):
        raise RuntimeError('Phone is locked; unlock it before benchmarking')
    call('shell','run-as',PACKAGE,'rm','-f','files/frame-timings.csv','files/benchmark-frame.bmp')
    arguments=['--frames',str(a.frames),'--boot-to-flight','--profile','--frame-preview',
               '/data/user/0/'+PACKAGE+'/files/benchmark-frame.bmp']
    settings_file='files/settings.cfg'
    if a.viewport:
        current=call('exec-out','run-as',PACKAGE,'cat',settings_file).stdout.decode()
        if not current.startswith('AB2SETTINGS1\n'):raise RuntimeError('Saved settings unavailable')
        lines=[line for line in current.splitlines() if not line.startswith(('viewport=','widescreen='))]
        lines+=['viewport='+a.viewport]
        a.output.parent.mkdir(parents=True,exist_ok=True)
        local=a.output.with_suffix('.settings.cfg');local.write_text('\n'.join(lines)+'\n')
        call('push',str(local),'/data/local/tmp/ab2-benchmark-settings.cfg')
        settings_file='files/benchmark-settings.cfg'
        call('shell','run-as',PACKAGE,'cp','/data/local/tmp/ab2-benchmark-settings.cfg',settings_file)
        arguments+=['--settings-file','/data/user/0/'+PACKAGE+'/'+settings_file,
                    '--roms','/data/user/0/'+PACKAGE+'/files/aburner2.zip']
    if a.stage:arguments+=['--stage',str(a.stage)]
    call('shell','am','start','-n',PACKAGE+'/.GameActivity','--ez','ab2.profile','true',
         '--esa','ab2.arguments',','.join(arguments))
    deadline=time.monotonic()+180+a.frames*.12
    trace=None;short_trace=None
    while time.monotonic()<deadline:
        time.sleep(2)
        # This file is written only when the bounded native main returns.
        result=call('exec-out','run-as',PACKAGE,'cat','files/frame-timings.csv',check=False)
        if result.returncode==0 and result.stdout.startswith(b'frame,'):
            status,candidate=trace_status(result.stdout,a.frames,short_trace)
            if status=='complete':
                trace=result.stdout;break
            # SDL's Java process can outlive native main. A closed/stable
            # short trace is authoritative; do not wait on PID disappearance.
            if status=='interrupted':
                a.output.parent.mkdir(parents=True,exist_ok=True)
                a.output.with_suffix('.interrupted.csv').write_bytes(result.stdout)
                call('shell','am','force-stop',PACKAGE)
                call('shell','am','start','-n',PACKAGE+'/.GameActivity')
                raise RuntimeError(f'Interrupted benchmark: {len(candidate)} of {a.frames} frames; prefix retained')
            short_trace=result.stdout
    if trace is None:raise RuntimeError('Benchmark timed out; check phone focus/lock state')
    rows=list(csv.DictReader(io.StringIO(trace.decode())))
    if len(rows)!=a.frames:raise RuntimeError('Frame count mismatch')
    info=summarize(rows)
    image=call('exec-out','run-as',PACKAGE,'cat','files/benchmark-frame.bmp').stdout
    if not image.startswith(b'BM'):raise RuntimeError('Missing final-frame BMP')
    info.update(stage=a.stage,viewport=a.viewport or 'saved',frame_bmp_sha256=hashlib.sha256(image).hexdigest(),apk_sha256=apk_hash,
                device_model=call('shell','getprop','ro.product.model').stdout.decode().strip(),
                android_version=call('shell','getprop','ro.build.version.release').stdout.decode().strip())
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.with_suffix('.csv').write_bytes(trace)
    a.output.with_suffix('.bmp').write_bytes(image)
    a.output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
    settings=call('exec-out','run-as',PACKAGE,'cat',settings_file,check=False)
    if settings.returncode==0:a.output.with_suffix('.settings.cfg').write_bytes(settings.stdout)
    # Return to the normal paused frontend, whose SDL window keeps the screen
    # awake, rather than leaving Home idle between comparative runs.
    call('shell','am','force-stop',PACKAGE)
    call('shell','am','start','-n',PACKAGE+'/.GameActivity')
    print(json.dumps(info,indent=2))

if __name__=='__main__':main()
