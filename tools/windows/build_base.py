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
            import shutil
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
        '-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++','-DBUILD_TESTING=OFF',
        '-DAB2_REQUIRE_IPO=ON','-DAB2_STATIC_SDL=ON','-DAB2_STATIC_COMPILER_RUNTIME=ON',
        '-DAB2_OPENAL_DYNAMIC=ON','-DAB2_ENABLE_OPENAL=ON',
        '-DAB2_OPENAL_INCLUDE_DIR='+str(deps/'openal-soft-1.25.2/include'),
        '-DSDL3_DIR='+str(sdk/'lib/cmake/SDL3'),
        '-DAB2_VULKAN_INCLUDE_DIR='+str(deps/'Vulkan-Headers-1.4.341/include'))
    run('cmake','--build',build,'--target','afterburner2','--parallel','4')
    (build/'pinned-inputs.json').write_text(json.dumps(INPUTS,indent=2)+'\n')


if __name__=='__main__':main()
