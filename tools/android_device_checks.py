#!/usr/bin/env python3
"""Build/run Android continuation checks and a retained MAME whole-host replay."""
import argparse
from pathlib import Path
import re
import subprocess
from android import ROOT, SDK


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True)
    parser.add_argument('--fixture',choices=['joint_cold','whole_host_expanded'],default='joint_cold')
    parser.add_argument('--clean-device-fixture',action='store_true',help='remove only this copied fixture after replay; retain all host originals')
    args=parser.parse_args()
    caches=sorted((ROOT/'android/app/.cxx/RelWithDebInfo').glob('*/arm64-v8a/CMakeCache.txt'),key=lambda p:p.stat().st_mtime,reverse=True)
    cache=next(p for p in caches if 'AB2_ANDROID_WORKER_CONTINUATION:BOOL=OFF' in p.read_text())
    content=cache.read_text()
    output=Path(re.search(r'^CMAKE_RUNTIME_OUTPUT_DIRECTORY:[^=]*=(.+)$',content,re.M)[1])
    subprocess.run([str(SDK/'cmake/3.22.1/bin/cmake'),'--build',str(cache.parent),'--target',
                    'android_continuation_stack','android_continuation_worker','android_whole_host','-j4'],check=True)
    adb=[str(SDK/'platform-tools/adb'),'-s',args.serial]
    def call(*words):subprocess.run([*adb,*map(str,words)],check=True)
    remote='/data/local/tmp/ab2-checks'
    call('shell','mkdir','-p',remote+'/roms',remote+'/oracle')
    for name in ['android_continuation_stack','android_continuation_worker','android_whole_host',
                 'libmain.so','libSDL3.so','libc++_shared.so']:
        call('push',output/name,remote+'/'+name)
    rom=ROOT/'build/android-tools/aburner2.zip'
    if not rom.exists():raise RuntimeError('Local ROM ZIP needed; it remains excluded from APK/source control')
    call('push',rom,remote+'/roms/aburner2.zip')
    # Exact original files, no truncation or reduced comparison policy.
    fixture=ROOT/'build/oracle'/args.fixture
    suffixes=['.bin','.txt','.devices','.bin.inputs','.bin.video','.bin.pixels',
              '.bin.screen','.bin.latches','.bin.soundcpu','.bin.audio.fm',
              '.bin.audio.pcm','.bin.audio.speaker']
    files=[Path(str(fixture)+suffix) for suffix in suffixes]
    free=subprocess.check_output([*adb,'shell','df','-k',remote],text=True).splitlines()[-1].split()
    if sum(p.stat().st_size for p in files)+512*1024*1024>int(free[3])*1024:
        raise RuntimeError('Insufficient phone storage for the complete fixture')
    for path,suffix in zip(files,suffixes):call('push',path,remote+'/oracle/'+args.fixture+suffix)
    call('shell','chmod','755',remote+'/android_continuation_stack',remote+'/android_continuation_worker',remote+'/android_whole_host')
    for name in ['android_continuation_stack','android_continuation_worker']:
        call('shell',f'cd {remote} && LD_LIBRARY_PATH={remote} ./{name}')
    call('shell',f'cd {remote} && LD_LIBRARY_PATH={remote} ./android_whole_host oracle/{args.fixture}.bin oracle/{args.fixture}.txt')
    if args.clean_device_fixture:
        call('shell','rm','-f',*[remote+'/oracle/'+args.fixture+suffix for suffix in suffixes])


if __name__=='__main__':main()
