#!/usr/bin/env python3
"""Build unsigned Windows base input from pinned upstream sources/toolchain.

Runs on Windows with Python, CMake and Ninja. No keys, trust-store changes or
release upload operations. Provider signing and protected launch remain separate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from package_windows import SOURCES_CROSS, SOURCES_NATIVE

TOOLCHAIN = 'llvm-mingw-20261006-ucrt-x86_64'
INPUTS = {
    TOOLCHAIN+'.zip': (
        'https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/'+TOOLCHAIN+'.zip',
        '317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a'),
    'SDL3-3.4.18.tar.gz': (
        'https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz',
        SOURCES_NATIVE['SDL3-3.4.18.tar.gz']),
    'openal-soft-1.25.2.tar.gz': (
        'https://codeload.github.com/kcat/openal-soft/tar.gz/refs/tags/1.25.2',
        SOURCES_NATIVE['openal-soft-1.25.2.tar.gz']),
    'Vulkan-Headers-1.4.341.tar.gz': (
        'https://codeload.github.com/KhronosGroup/Vulkan-Headers/tar.gz/refs/tags/v1.4.341',
        '8876aad926b2e72bdefddc34885472d5332e2d20aa3ed57313466f0bc982cf3f'),
}
SOURCE_URLS = {
    'llvm-project-23.1.3.src.tar.xz': 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.3/llvm-project-23.1.3.src.tar.xz',
    'mingw-w64-a3d93999.tar.gz': 'https://codeload.github.com/mingw-w64/mingw-w64/tar.gz/a3d93999ef0521681d45e445c39964a7af0f593f',
    'llvm-mingw-20261006-source.tar.gz': 'https://codeload.github.com/mstorsjo/llvm-mingw/tar.gz/refs/tags/20261006',
}


def fetch(directory, name, url, sha):
    destination=directory/name
    if not destination.exists():
        temporary=destination.with_suffix(destination.suffix+'.partial')
        with urllib.request.urlopen(url) as source, temporary.open('wb') as output:
            shutil.copyfileobj(source, output)
        temporary.rename(destination)
    if hashlib.sha256(destination.read_bytes()).hexdigest()!=sha:
        raise SystemExit('Pinned dependency hash mismatch: '+name)
    return destination


def run(*args):
    print(' '.join(map(str,args)),flush=True)
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deps',type=Path,default=ROOT/'build/windows-base-inputs')
    parser.add_argument('--build',type=Path,default=ROOT/'build/windows-base-hosted')
    parser.add_argument('--package-sources',action='store_true')
    args=parser.parse_args()
    if os.name!='nt':parser.error('Run this hosted-build entry point on Windows')
    deps=args.deps.resolve();build=args.build.resolve();deps.mkdir(parents=True,exist_ok=True)
    for name,(url,sha) in INPUTS.items():
        archive=fetch(deps,name,url,sha)
        if name.endswith('.zip'):
            if not (deps/TOOLCHAIN).is_dir():
                with zipfile.ZipFile(archive) as z:z.extractall(deps)
        else:
            with tarfile.open(archive) as t:
                top=t.next().name.split('/')[0]
                if not (deps/top).is_dir():t.extractall(deps,filter='data')
    if args.package_sources:
        for name,url in SOURCE_URLS.items():fetch(deps,name,url,SOURCES_CROSS[name])
    compiler=deps/TOOLCHAIN/'bin'
    os.environ['PATH']=str(compiler)+os.pathsep+os.environ['PATH']
    sdk=deps/'sdl-static-sdk'
    run('cmake','-S',deps/'SDL3-3.4.18','-B',deps/'sdl-static-build','-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++',
        '-DCMAKE_INSTALL_PREFIX='+str(sdk),'-DSDL_SHARED=OFF','-DSDL_STATIC=ON',
        '-DSDL_TESTS=OFF','-DSDL_EXAMPLES=OFF')
    run('cmake','--build',deps/'sdl-static-build','--parallel','4')
    run('cmake','--install',deps/'sdl-static-build')
    run('cmake','-S',ROOT,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++','-DBUILD_TESTING=ON',
        '-DAB2_REQUIRE_IPO=ON','-DAB2_STATIC_SDL=ON','-DAB2_STATIC_COMPILER_RUNTIME=ON',
        '-DAB2_OPENAL_DYNAMIC=ON','-DAB2_ENABLE_OPENAL=ON',
        '-DAB2_OPENAL_INCLUDE_DIR='+str(deps/'openal-soft-1.25.2/include'),
        '-DSDL3_DIR='+str(sdk/'lib/cmake/SDL3'),
        '-DAB2_VULKAN_INCLUDE_DIR='+str(deps/'Vulkan-Headers-1.4.341/include'))
    run('cmake','--build',build,'--target','afterburner2','openal_runtime_regression','--parallel','4')
    # Start the real EXE from an otherwise empty directory. The invalid option
    # returns before SDL/ROM selection, but Windows must resolve startup imports.
    with tempfile.TemporaryDirectory(prefix='ab2-base-smoke-') as temporary:
        directory=Path(temporary)
        executable=directory/'afterburner2.exe'
        shutil.copy2(build/'afterburner2.exe',executable)
        result=subprocess.run([str(executable),'--dependency-smoke'],cwd=directory,
                              capture_output=True,text=True,timeout=20)
        if result.returncode!=1 or 'Usage: afterburner2' not in result.stderr:
            raise SystemExit('Standalone Windows loader smoke failed: '+str(result.returncode)+' '+result.stderr)
        print('Standalone Windows startup import smoke passed',flush=True)
        # An unsigned negative control must fail without altering system trust.
        (directory/'Launch.cmd').write_text('@echo off\n')
        shutil.copy2(ROOT/'LICENSE.txt',directory/'LICENSES.txt')
        receipt=build/'unsigned-negative-control.json'
        result=subprocess.run(['powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',
            str(ROOT/'tools/windows/verify_release_signatures.ps1'),
            '-RuntimeDirectory',str(directory),'-ReceiptPath',str(receipt)],timeout=60)
        audit=json.loads(receipt.read_text(encoding='utf-8-sig'))
        if (result.returncode!=1 or audit['Passed'] or not audit['FullRuntime'] or
                len(audit['Files'])!=1 or audit['Files'][0]['Status']!='NotSigned'):
            raise SystemExit('Unsigned base negative control was not rejected')
    environment=dict(os.environ,AB2_TEST_INCOMPATIBLE_RUNTIME=str(Path(os.environ['SystemRoot'])/'System32/kernel32.dll'))
    subprocess.run([str(build/'openal_runtime_regression.exe'),'--missing-only'],
                   cwd=ROOT,env=environment,check=True,timeout=30)
    (build/'pinned-inputs.json').write_text(json.dumps(INPUTS,indent=2)+'\n')


if __name__=='__main__':main()
