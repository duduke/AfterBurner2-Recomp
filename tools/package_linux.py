#!/usr/bin/env python3
"""Package a pinned Linux build and its exact, ROM-free source snapshot."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SDK = 'registry.gitlab.steamos.cloud/steamrt/sniper/sdk:3.0.20261021.266025'
SDK_DIGEST = 'sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e'
SOURCES = {
    'SDL3-3.4.16.tar.gz': '7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68',
    'openal-soft-1.25.2.tar.gz': 'fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894',
}

SLANG_BUNDLES = {
    'x86_64': '4ea9bdc1c2156d9d05aff583a6c628bc17e43de3ec276af5aa1b26654f56b58c',
    'arm64': '5b97babca026d566d1e73159739c1531771b658cb1c156ceca4f370da1db30d3',
}
SHADER_PACK_URL = 'https://buildbot.libretro.com/assets/frontend/shaders_slang.zip'


def load_slang_bundle(path, architecture):
    """Verify the retained runtime/source/notice archive before copying any files."""
    if digest(path) != SLANG_BUNDLES[architecture]:
        raise SystemExit('Pinned librashader runtime/source archive checksum mismatch')
    prefix = f'librashader-0.12.0-Linux-{architecture}-Vulkan'
    files = {}
    with tarfile.open(path) as archive:
        for member in archive.getmembers():
            relative = PurePosixPath(member.name)
            if relative.is_absolute() or '..' in relative.parts or relative.parts[0] != prefix:
                raise SystemExit('Unsafe librashader archive path')
            if not member.isdir() and not member.isfile():
                raise SystemExit('Expected only regular librashader files/directories')
            if member.isfile():
                name = str(relative.relative_to(prefix))
                if name in files:
                    raise SystemExit('Duplicate librashader archive file')
                files[name] = archive.extractfile(member).read()
    for line in files['SHA256SUMS'].decode().splitlines():
        expected, name = line.split('  ', 1)
        if name not in files or hashlib.sha256(files[name]).hexdigest() != expected:
            raise SystemExit(f'librashader internal checksum mismatch: {name}')
    manifest = json.loads(files['build-manifest.json'])
    if (manifest['version'], manifest['abi'], manifest['api'], manifest['architecture']) != (
            '0.12.0', 2, 5, 'aarch64' if architecture == 'arm64' else 'x86_64'):
        raise SystemExit('Incorrect librashader version/ABI/API/architecture')
    if hashlib.sha256(files['liblibrashader_capi.so']).hexdigest() != manifest['runtime_sha256']:
        raise SystemExit('librashader runtime does not match its build manifest')
    if not any(name.startswith('sources/cargo-crates/') for name in files):
        raise SystemExit('Missing librashader corresponding dependency sources')
    return manifest, files


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('label')
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--platform', choices=('sniper-x86_64', 'debian12-arm64'),
                        default='sniper-x86_64')
    parser.add_argument('--environment', type=Path, help='ARM64 SDK image/package manifest')
    parser.add_argument('--librashader-bundle', type=Path, required=True,
                        help='Pinned runtime/source/notice archive from the retained Linux build')
    args = parser.parse_args()
    arm64 = args.platform == 'debian12-arm64'
    architecture = 'arm64' if arm64 else 'x86_64'
    slang_manifest, slang_files = load_slang_bundle(args.librashader_bundle, architecture)
    slang_source_name = args.librashader_bundle.name
    slang_source_url = ('https://github.com/duduke/AfterBurner2-Recomp/releases/download/'
                        'v0.1.0-preview.7/' + slang_source_name)
    compiler = 'clang++-14' if arm64 else 'g++-14'
    maximum_glibc = (2, 36) if arm64 else (2, 31)
    environment = None
    if arm64:
        if args.environment is None:
            parser.error('--environment is required for ARM64')
        environment = json.loads(args.environment.read_text())
        if environment['recipe_sha256'] != digest(ROOT / 'tools/linux-arm64.Containerfile'):
            raise SystemExit('ARM64 SDK recipe does not match its manifest')
    if not re.fullmatch(r'[A-Za-z0-9.\-]+', args.label):
        parser.error('Use an alphanumeric artifact label')
    identity = json.loads(args.manifest.read_text())
    if digest(args.snapshot) != identity['archive_sha256']:
        raise SystemExit('Source archive hash mismatch')
    for relative, expected in identity['files'].items():
        path = ROOT / relative
        if path.is_symlink() or digest(path) != expected:
            raise SystemExit(f'Source changed since snapshot: {relative}')
    for name, expected in SOURCES.items():
        if digest(ROOT / 'build/deps' / name) != expected:
            raise SystemExit(f'Dependency checksum mismatch: {name}')
    binary = ROOT / 'build/linux/afterburner2'
    cache_path = ROOT / 'build/linux/CMakeCache.txt'
    cache = cache_path.read_text()
    for setting in ('CMAKE_BUILD_TYPE:STRING=Release', 'AB2_ENABLE_OPENAL:BOOL=ON',
                    'AB2_ENABLE_IPO:BOOL=ON', 'AB2_REQUIRE_IPO:BOOL=ON'):
        if setting not in cache.splitlines():
            raise SystemExit(f'Expected release configuration: {setting}')
    os_release = Path('/etc/os-release').read_text()
    if arm64:
        if 'VERSION_ID="12"' not in os_release.splitlines() or 'bookworm' not in os_release:
            raise SystemExit('Package inside the pinned Debian 12 ARM64 SDK')
    elif 'sniper' not in os_release or '3.0.20261021.266025' not in os_release:
        raise SystemExit('Package inside the pinned sniper SDK')
    expected_elf = 'AArch64' if arm64 else 'Advanced Micro Devices X86-64'
    if expected_elf not in output('readelf', '-h', str(binary)):
        raise SystemExit(f'Expected a {architecture} ELF executable')
    compiler_path = re.search(r'^CMAKE_CXX_COMPILER:(?:STRING|FILEPATH)=(.+)$', cache, re.M)
    if not compiler_path or Path(compiler_path[1]).name != compiler:
        raise SystemExit(f'Expected compiler {compiler}')
    report = output('ldd', str(binary))
    # SDL/OpenAL are installed in this prefix while packaging in the SDK.
    if 'not found' in report:
        raise SystemExit(report)
    libraries = {}
    for name, path in re.findall(r'^\s*(\S+) => (\S+) \(', report, re.M):
        if name.startswith(('libSDL3', 'libopenal', 'libstdc++', 'libgcc_s', 'libatomic')):
            libraries[name] = Path(path)
    if not any(n.startswith('libSDL3') for n in libraries) or 'libopenal.so.1' not in libraries:
        raise SystemExit('Expected shared SDL3 and OpenAL dependencies')
    suffix = 'arm64-preview' if arm64 else 'x86_64-SteamDeck-preview'
    name = f'AfterBurner2-{args.label}-Linux-{suffix}'
    releases = ROOT / 'build/releases'
    releases.mkdir(exist_ok=True)
    package = releases / name
    package.mkdir()  # Never replace an earlier artifact.
    (package / 'lib').mkdir()
    (package / 'roms').mkdir()
    shutil.copy2(binary, package / 'afterburner2')
    (package / 'lib/liblibrashader_capi.so').write_bytes(slang_files['liblibrashader_capi.so'])
    for soname, path in libraries.items():
        shutil.copy2(path.resolve(), package / 'lib' / soname)
    shutil.copytree(ROOT / 'assets', package / 'assets')
    shutil.copytree(ROOT / 'LICENSES', package / 'LICENSES')
    shutil.copy2(ROOT / 'LICENSE.txt', package / 'LICENSE.txt')
    for relative, data in slang_files.items():
        if relative.startswith('notices/'):
            notice = package / 'LICENSES/librashader' / relative.removeprefix('notices/')
            notice.parent.mkdir(parents=True, exist_ok=True)
            notice.write_bytes(data)
    (package / 'LICENSES/librashader/build-manifest.json').write_bytes(slang_files['build-manifest.json'])
    (package / 'LICENSES/librashader/SOURCES.txt').write_text(
        'Bundled librashader 0.12.0 uses the MPL-2.0 option.\n'
        'Corresponding upstream/Cargo sources and complete notices: ' + slang_source_url + '\n'
        'Archive SHA-256: ' + SLANG_BUNDLES[architecture] + '\n')
    shutil.copy2(ROOT / 'roms/README.md', package / 'roms/README.md')
    # Retain the actual license files from the pinned dependency sources too.
    shutil.copy2(ROOT / 'build/deps/SDL3-3.4.16/LICENSE.txt', package / 'LICENSES/SDL3-source.txt')
    for notice in ('COPYING', 'BSD-3Clause', 'LICENSE-pffft'):
        shutil.copy2(ROOT / 'build/deps/openal-soft-1.25.2' / notice,
                     package / 'LICENSES' / f'OpenAL-source-{notice}.txt')
    compiler_notices = ('clang-14', 'llvm-14', 'g++-12', 'gcc-12') if arm64 else ('g++-14', 'gcc-14')
    for dependency in ('libstdc++6', 'libgcc-s1', *compiler_notices):
        copyright_file = Path('/usr/share/doc') / dependency / 'copyright'
        shutil.copy2(copyright_file, package / 'LICENSES' / f'{dependency}-copyright.txt')
    if any(n.startswith('libatomic') for n in libraries):
        shutil.copy2('/usr/share/doc/libatomic1/copyright', package / 'LICENSES/libatomic1-copyright.txt')
    (package / 'Launch.sh').write_text('''#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/AfterBurner2-Recomp/AfterBurner2"
mkdir -p "$data_dir"
settings="$data_dir/settings.cfg"
if [ ! -e "$settings" ]; then
    # Atomic first-run defaults; preserve every existing user preference.
    (umask 077; set -C; printf '%s\\n' AB2SETTINGS1 fullscreen=1 viewport=16:10 scaling=linear ground=1 > "$settings") 2>/dev/null || :
fi
export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$app_dir"
exec "$app_dir/afterburner2" --settings-file "$settings" "$@"
'''.replace('fullscreen=1 viewport=16:10 scaling=linear',
            'fullscreen=0 viewport=original scaling=nearest' if arm64 else
            'fullscreen=1 viewport=16:10 scaling=linear'))
    (package / 'Launch.sh').chmod(0o755)
    installation = ('''Extract the entire archive and run ./Launch.sh on an ARM64 Linux desktop.
This is an ARM64 executable. First-run settings use a window, Original viewport
and nearest scaling; fullscreen/16:10 remain available in the Display menu.''' if arm64 else
'''Extract the entire archive into a folder in Desktop Mode. Add Launch.sh as a
Non-Steam Game. Use the Gamepad controller template, then launch in Gaming Mode.
Run natively; do not select Proton. First-run settings use fullscreen/16:10.
Menus can change both.''')
    platform_description = ('Debian 12 (bookworm), ARM64, maximum required glibc 2.36.' if arm64 else
                            'Steam Linux Runtime 3 (sniper), x86-64, maximum required glibc 2.31.')
    limitations = ('Physical ARM64 hardware, controller feel, suspend/resume and audio latency need testing.' if arm64 else
                   'Physical Steam Deck performance, controller feel, suspend/resume and audio latency need testing on a Deck.')
    (package / 'README.txt').write_text(f'''After Burner II Recomp - Linux {architecture} preview

{installation}
Keep lib/ beside the executable. Existing preferences are preserved.

Supply your own verified After Burner II ROM set: put aburner2.zip in roms/,
use the initial file picker in Desktop Mode, or pass --roms /absolute/path.zip.
No game ROMs, recordings, captures, music or saved preferences are included.
Settings and controller bindings are stored under XDG_DATA_HOME (by default
~/.local/share/AfterBurner2-Recomp/AfterBurner2).

Built in {platform_description}
SDL3/OpenAL, librashader and any shared C++ runtimes are replaceable files in lib/. The
SDK may link its newer libstdc++ statically; GCC runtime notices are included. System
graphics, window-system and audio drivers remain supplied by the Linux host/runtime.
The exact project snapshot and dependency source archives are supplied in the
sources companion. BUILD.json records the base commit and snapshot hashes.

Preview limitations: {limitations} Linux currently
uses the existing worker-thread CPU continuation backend. RetroArch Slang
uses native Vulkan composition with librashader 0.12.0 already bundled in lib/.
No separate shader runtime installation is needed.

To install RetroArch Slang shaders:
1. Download the official Libretro shader pack: {SHADER_PACK_URL}
2. Extract the entire ZIP into a folder you can keep. Preserve its subfolders,
   shader includes and lookup textures together.
3. Open Settings > CRT and Shaders > Shader Preset and select a .slangp preset
   from that extracted pack. Selection activates RetroArch mode.
Shader packs are external downloads; they are not included in this game archive.
Corresponding bundled-runtime sources and notices: {slang_source_url}
Legacy GLSL/Cg presets, HDR and first-input mipmap generation remain unsupported.
''')
    requirements = {}
    for path in [package / 'afterburner2', *sorted((package / 'lib').iterdir())]:
        if expected_elf not in output('readelf', '-h', str(path)):
            raise SystemExit(f'{path.name} has the wrong architecture for {architecture}')
        versions = re.findall(r'\bGLIBC_(\d+)\.(\d+)\b', output('readelf', '--version-info', str(path)))
        highest = max((tuple(map(int, v)) for v in versions), default=(0, 0))
        if highest > maximum_glibc:
            raise SystemExit(f'{path.name} requires glibc {highest}, newer than {maximum_glibc}')
        requirements[str(path.relative_to(package))] = '.'.join(map(str, highest))
    build = dict(source=identity, architecture=architecture,
                 sdk='Debian 12 ARM64 snapshot 20261103T000000Z' if arm64 else SDK,
                 sdk_digest=environment['image_id'] if arm64 else SDK_DIGEST,
                 environment=environment, compiler=output(compiler, '--version'),
                 sdk_os_release=os_release, cmake_cache_sha256=digest(cache_path),
                 glibc_requirements=requirements, dependencies=SOURCES,
                 shader_runtime=slang_manifest,
                 shader_runtime_sources=dict(url=slang_source_url, sha256=SLANG_BUNDLES[architecture]),
                 limitations=limitations)
    (package / 'BUILD.json').write_text(json.dumps(build, indent=2) + '\n')
    companion = releases / f'{name}-sources'
    companion.mkdir()
    shutil.copy2(args.snapshot, companion / 'project-source.tar.gz')
    shutil.copy2(args.manifest, companion / 'SOURCE.json')
    shutil.copy2(args.librashader_bundle, companion / slang_source_name)
    if args.environment:
        shutil.copy2(args.environment, companion / 'BUILD-ENVIRONMENT.json')
    for dep in SOURCES:
        shutil.copy2(ROOT / 'build/deps' / dep, companion / dep)
    for directory in (package, companion):
        files = sorted(p for p in directory.rglob('*') if p.is_file())
        (directory / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.relative_to(directory)}\n' for p in files))
        archive = releases / f'{directory.name}.tar.gz'
        if archive.exists():
            raise SystemExit(f'Artifact already exists: {archive}')
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(directory, arcname=directory.name)
        archive.with_suffix(archive.suffix + '.sha256').write_text(f'{digest(archive)}  {archive.name}\n')
        print(archive)


if __name__ == '__main__':
    main()
