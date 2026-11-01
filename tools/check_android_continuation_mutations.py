#!/usr/bin/env python3
"""Prove the on-device continuation ABI test rejects Android context faults."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
from android import ROOT, SDK


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--serial',required=True);a=p.parse_args()
    caches=sorted((ROOT/'android/app/.cxx/RelWithDebInfo').glob('*/arm64-v8a/compile_commands.json'),key=lambda x:x.stat().st_mtime,reverse=True)
    commands=json.loads(caches[0].read_text())
    entry=next(x for x in commands if 'android_continuation_stack.dir' in x['command'])
    command=shlex.split(entry['command']);command.remove('-c')
    source=(ROOT/'src/main/hwmath/xboard_cpu_continuation.hpp').read_text()
    start=source.index('#if defined(__ANDROID__)');end=source.index('#endif',start)
    block=source[start:end]
    cases=[('baseline',None,None),('fp-control','msr fpcr,x9','nop'),('stack-unwind','add sp,sp,#176','add sp,sp,#160')]
    directory=ROOT/'build/android-tools/continuation-mutations';directory.mkdir(parents=True,exist_ok=True)
    adb=[str(SDK/'platform-tools/adb'),'-s',a.serial]
    remote='/data/local/tmp/ab2-checks'
    for name,before,after in cases:
        folder=directory/name;(folder/'include/hwmath').mkdir(parents=True,exist_ok=True)
        body=block
        if before:
            if block.count(before)!=1:raise RuntimeError('Mutation anchor drift: '+name)
            body=block.replace(before,after)
        (folder/'include/hwmath/xboard_cpu_continuation.hpp').write_text(source[:start]+body+source[end:])
        exe=folder/'test';compile_args=command.copy();compile_args[compile_args.index('-o')+1]=str(exe)
        compile_args.insert(1,'-I'+str(folder/'include'));compile_args+=['-latomic','-lm']
        with (folder/'build.log').open('w') as log:
            subprocess.run(compile_args,cwd=entry['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
        target=remote+'/mutation-'+name
        subprocess.run([*adb,'push',str(exe),target],check=True,stdout=subprocess.DEVNULL)
        subprocess.run([*adb,'shell','chmod','755',target],check=True)
        result=subprocess.run([*adb,'shell',f'cd {remote} && LD_LIBRARY_PATH={remote} timeout 15 ./mutation-{name}'],capture_output=True,text=True,timeout=25)
        (folder/'run.log').write_text(result.stdout+result.stderr)
        if (result.returncode==0)!=(name=='baseline'):
            raise RuntimeError(f'{name}: unexpected exit {result.returncode}; see {folder}/run.log')
        print(f'{name}: {"passed" if name=="baseline" else "rejected"} (exit {result.returncode})',flush=True)


if __name__=='__main__':main()
