#!/usr/bin/env python3
"""Create a minimal runtime ZIP and a separate matching source companion."""
import package_macos
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import zipfile

from package_windows import SOURCES_NATIVE, SOURCES_CROSS, SYSTEM_DLLS

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def signing_payload(path):
    """Keep every original byte except Authenticode/checksum fields and padding."""
    data = bytearray(path.read_bytes())
    if data[:2] != b'MZ' or len(data) < 64:
        raise SystemExit('Not a PE image: '+str(path))
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe+4] != b'PE\0\0':
        raise SystemExit('Invalid PE header: '+str(path))
    optional = pe+24
    if optional+152 > len(data) or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise SystemExit('Expected a PE32+ runtime image: '+str(path))
    security = optional+112+4*8
    offset, size = struct.unpack_from('<II', data, security)
    if offset or size:
        if offset < security+8 or offset % 8 or size < 8 or offset+size != len(data):
            raise SystemExit('Unexpected certificate-table layout: '+str(path))
        data = data[:offset]
    data[optional+64:optional+68] = b'\0'*4
    data[security:security+8] = b'\0'*8
    data.extend(b'\0'*(-len(data) % 8))
    return data


def verified_signatures(runtime, receipt_path, originals):
    receipt = json.loads(receipt_path.read_text(encoding='utf-8-sig'))
    if receipt.get('Schema') != 1 or receipt.get('Passed') is not True or receipt.get('FullRuntime') is not True:
        raise SystemExit('A passing full-runtime Windows signature receipt is required')
    files = receipt.get('Files', [])
    if len(files) != len(originals) or {item['File'] for item in files} != set(originals):
        raise SystemExit('Signature receipt does not cover the exact runtime PE inventory')
    for item in files:
        path = runtime/item['File']
        if (path.is_symlink() or not path.is_file() or item.get('Passed') is not True or
                item.get('Status') != 'Valid' or item.get('Timestamped') is not True or
                digest(path).lower() != item.get('SHA256', '').lower()):
            raise SystemExit('Signed payload/receipt mismatch: '+item['File'])
        if signing_payload(path) != signing_payload(originals[item['File']]):
            raise SystemExit('Signing changed the built PE payload: '+item['File'])
    return receipt


def audit_imports(runtime, binaries, compiler, base):
    allowed_os = SYSTEM_DLLS | {'bcryptprimitives.dll','d3dcompiler_47.dll','ntdll.dll','combase.dll','d3d9.dll','d3dcompiler_43.dll','d3dx9_43.dll'}
    if base:
        # These are optional/vendor dependencies, never Windows OS imports.
        allowed_os -= {'d3dx9_43.dll','d3dcompiler_43.dll'}
    names = {n.lower() for n in binaries}
    imports = {}
    for n in binaries:
        objdump = compiler.parent/('llvm-objdump.exe' if os.name == 'nt' else 'llvm-objdump')
        report = command(str(objdump),'-p',str(runtime/n))
        if 'file format coff-x86-64' not in report and 'file format pei-x86-64' not in report:
            raise SystemExit('Not an x64 PE: '+n)
        imports[n] = re.findall(r'^\s*DLL Name:\s*(\S+)',report,re.M)
        for dependency in imports[n]:
            lower = dependency.lower()
            if lower not in names and lower not in allowed_os and not lower.startswith(('api-ms-win-','ext-ms-win-')):
                raise SystemExit(f'Unbundled dependency {n}: {dependency}')
    return imports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version')
    parser.add_argument('--label', required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--deps', type=Path, default=ROOT/'build/deps')
    parser.add_argument('--profile', choices=('base','legacy'), default='base')
    parser.add_argument('--msvc-redist', type=Path)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--verified-runtime', type=Path,
                      help='Runtime signed and checked on Windows; requires --signature-receipt')
    mode.add_argument('--unsigned-signing-input', action='store_true',
                      help='Create explicitly UNSIGNED staging artifacts; never publish as a fixed release')
    parser.add_argument('--signature-receipt', type=Path)
    args = parser.parse_args()
    if bool(args.verified_runtime) != bool(args.signature_receipt):
        parser.error('--verified-runtime and --signature-receipt must be supplied together')
    if not all(re.fullmatch(r'[A-Za-z0-9.\-]+', x) for x in (args.version, args.label)):
        parser.error('Invalid artifact version/label')
    if command('git', 'status', '--porcelain'):
        raise SystemExit('Commit the intended source before packaging')
    build = args.build.resolve()
    cache = (build/'CMakeCache.txt').read_text()
    if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache or 'AB2_ENABLE_OPENAL:BOOL=ON' not in cache:
        raise SystemExit('Expected Release with OpenAL')
    base = args.profile == 'base'
    if base:
        for option in ('AB2_STATIC_SDL','AB2_STATIC_COMPILER_RUNTIME','AB2_OPENAL_DYNAMIC'):
            if option+':BOOL=ON' not in cache:
                raise SystemExit('Base runtime requires '+option+'=ON')
    elif not args.msvc_redist:
        parser.error('Legacy profile requires --msvc-redist')
    deps = args.deps.resolve()
    compiler = Path(re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$', cache, re.M)[1])
    if not base:
        dx = deps/'microsoft.dxsdk.d3dx-9.29.952.8'
        if digest(deps/'microsoft.dxsdk.d3dx.9.29.952.8.nupkg') != 'ead0906ae8a26c18a7525da7490127a2110f7c58f18293738283e30e97c6ea4b':
            raise SystemExit('D3DX package checksum mismatch')
        source = deps/'librashader-v0.12.0-source.tar.gz'
        if digest(source) != '4bf8cf2489d00848dcabbf2163204093776082da4217d5a5db45e4cbf335cedf':
            raise SystemExit('librashader source checksum mismatch')
    name = f'AfterBurner2-{args.version}-Windows-x64-{args.label}-{args.profile}-experimental'
    if args.unsigned_signing_input:
        name += '-UNSIGNED-signing-input'
    output = ROOT/'build/releases'
    package = output/name
    if base:
        binaries = {'afterburner2.exe':build/'afterburner2.exe'}
    else:
        binaries = {n: build/n for n in ('afterburner2.exe','SDL3.dll','OpenAL32.dll','libc++.dll','libunwind.dll')}
        binaries['librashader.dll'] = deps/'librashader-windows/librashader.dll'
        for n in ('MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll'):
            binaries[n] = args.msvc_redist/n
        for n in ('D3DX9_43.dll','D3DCompiler_43.dll'):
            binaries[n] = dx/'build/native/release/bin/x64'/n
    signatures = None
    if args.verified_runtime:
        signatures = verified_signatures(args.verified_runtime, args.signature_receipt, binaries)
        binaries = {n: args.verified_runtime/n for n in binaries}
    output.mkdir(parents=True, exist_ok=True)
    package.mkdir()  # Preserve every previous artifact; validate signatures first.
    for name_, original in binaries.items():
        shutil.copy2(original, package/name_)
    (package/'Launch.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\n"%~dp0afterburner2.exe" %*\r\n')
    notices = []
    def notice(label, text):
        notices.append('\n\n=== '+label+' ===\n'+text)
    def file_notice(path):
        notice(path.name, path.read_text(encoding='utf-8-sig', errors='replace'))
    file_notice(ROOT/'LICENSE.txt')
    file_notice(ROOT/'COPYRIGHT.md')
    for name in package_macos.PROJECT_NOTICES:
        file_notice(ROOT/'LICENSES'/name)
    for relative in ('src/third_party/dr_flac/LICENSE', 'src/third_party/miniz/LICENSE',
                     'src/main/frontend/controllerdb/LICENSE',
                     'src/third_party/librashader/LICENSE.MPL-2.0'):
        file_notice(ROOT/relative)
    for relative in ('src/main/frontend/scalers/scalefx.hpp','src/third_party/librashader/librashader.h'):
        text = (ROOT/relative).read_text()
        notice(relative, re.search(r'/\*.*?\*/',text,re.S)[0])
    z80 = (ROOT/'src/third_party/z80.h').read_text()
    notice('Z80', z80[z80.index('/*\n    zlib/libpng license'):z80.index('*/',z80.index('/*\n    zlib/libpng license'))+2])
    with tarfile.open(deps/'SDL3-3.4.18.tar.gz') as archive:
        notice('SDL3', archive.extractfile('SDL3-3.4.18/LICENSE.txt').read().decode())
    for path in sorted((deps/'openal/share/licenses/openal-soft').glob('*')):
        file_notice(path)
    file_notice(compiler.parent.parent/'LICENSE.TXT')
    for n in ('COPYING.MinGW-w64-runtime.txt','COPYING.winpthreads.txt'):
        file_notice(compiler.parent.parent/'x86_64-w64-mingw32/share/mingw32'/n)
    if not base:
        for n in ('LICENSE.txt','NOTICE.md'):
            file_notice(dx/n)
        file_notice(deps/'Visual-C-Runtime-2015-2022-License.txt')
        file_notice(deps/'librashader-dependency-NOTICES.txt')
        notice('librashader source', 'Runtime licensed under MPL-2.0. Matching covered source and locked dependency sources are supplied in the source companion on the same release.\nhttps://github.com/SnowflakePowered/librashader/tree/librashader-v0.12.0')
    (package/'LICENSES.txt').write_text('Redistribution licenses and notices. No game ROMs are included.\n'+''.join(notices),encoding='utf-8')
    imports = audit_imports(package, binaries, compiler, base)
    if sorted(p.name for p in package.iterdir()) != sorted([*binaries,'Launch.cmd','LICENSES.txt']):
        raise SystemExit('Unexpected runtime payload')
    manifest = {'commit':command('git','rev-parse','HEAD'),'executable_sha256':digest(package/'afterburner2.exe'),
                'imports':imports,'sha256':{p.name:digest(p) for p in package.iterdir()},
                'profile':args.profile,'signatures':signatures,'unsigned_signing_input':args.unsigned_signing_input,
                'limits':'Experimental Windows build: occasional GPU stalls/HRTF underruns and hardware acceptance remain open. Signing receipts do not replace extracted-package launch/shader checks.'}
    (output/(name+'.manifest.json')).write_text(json.dumps(manifest,indent=2)+'\n')
    companion = output/(name+'-sources')
    companion.mkdir()
    command('git','archive','--format=tar.gz','--prefix=AfterBurner2/',
            '--output='+str(companion/'AfterBurner2-source.tar.gz'),'HEAD')
    source_map = dict(SOURCES_NATIVE)
    if base:
        source_map.update({n:h for n,h in SOURCES_CROSS.items() if not n.startswith('SDL3-')})
    for filename, expected in source_map.items():
        path = deps/filename
        if digest(path) != expected:
            raise SystemExit('Dependency source mismatch: '+filename)
        shutil.copy2(path,companion/filename)
    shutil.copy2(package/'LICENSES.txt',companion/'LICENSES.txt')
    shutil.copy2(output/(name+'.manifest.json'),companion/'manifest.json')
    if not base:
        shutil.copy2(source,companion/source.name)
        crates = json.loads((deps/'librashader-locked-crates.json').read_text())
        (companion/'Rust-crates').mkdir()
        for filename, expected in crates.items():
            path = deps/'librashader-locked-crates'/filename
            if digest(path) != expected:
                raise SystemExit('Locked crate source mismatch: '+filename)
            shutil.copy2(path,companion/'Rust-crates'/filename)
    # Archive whitelist and git archive prevent ROMs, captures and local settings.
    with tarfile.open(companion/'AfterBurner2-source.tar.gz') as archive:
        for entry in archive:
            relative = entry.name.removeprefix('AfterBurner2/')
            if relative.startswith(('build/','soundtrack/')) or (relative.startswith('roms/') and entry.isfile() and relative not in ('roms/README.md','roms/roms.txt')):
                raise SystemExit('Unexpected project source payload: '+relative)
    for folder in (package,companion):
        destination = output/(folder.name+'.zip')
        with zipfile.ZipFile(destination,'x',zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(folder.rglob('*')):
                if path.is_file():
                    archive.write(path,path.relative_to(output).as_posix())
        destination.with_suffix('.zip.sha256').write_text(digest(destination)+'  '+destination.name+'\n')
        print(destination.name,destination.stat().st_size,digest(destination),flush=True)


if __name__ == '__main__':
    main()
