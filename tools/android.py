#!/usr/bin/env python3
"""Pinned macOS ARM64 Android toolchain and ROM-free APK build/install."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / 'build/android-tools'
SDK = TOOLS / 'sdk'
JDK = TOOLS / 'jdk-17.0.21+9/Contents/Home'
GRADLE = TOOLS / 'gradle-8.9/bin/gradle'
SDL = ROOT / 'build/deps/SDL3-3.4.16'
PACKAGES = ['platform-tools', 'platforms;android-35', 'build-tools;35.0.0',
            'ndk;28.2.13676358', 'cmake;3.22.1']
DOWNLOADS = {
    'sdl.tar.gz': ('https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz',
                   '7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68'),
    'jdk.tar.gz': ('https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.21%2B9/OpenJDK17U-jdk_aarch64_mac_hotspot_17.0.21_9.tar.gz',
                   '78b08c7f5a0598242d5719c022e19479cb4a3aa45c37c19726a757a469ed1c15'),
    'gradle.zip': ('https://services.gradle.org/distributions/gradle-8.9-bin.zip',
                   'd725d707bfabd4dfdc958c624003b3c80accc03f7037b5122c4b1d0ef15cecab'),
    'sdk.zip': ('https://dl.google.com/android/repository/commandlinetools-mac-13114758_latest.zip',
                '5673201e6f3869f418eeed3b5cb6c4be7401502bd0aae1b12a29d164d647a54e'),
}


def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)


def environment():
    env = dict(os.environ, JAVA_HOME=str(JDK), ANDROID_HOME=str(SDK),
               ANDROID_SDK_ROOT=str(SDK), ANDROID_USER_HOME=str(TOOLS / 'android-user'),
               GRADLE_USER_HOME=str(TOOLS / 'gradle-cache'))
    env['PATH'] = str(JDK / 'bin') + os.pathsep + env.get('PATH', '')
    return env


def download(name):
    url, expected = DOWNLOADS[name]
    path = TOOLS / name
    if not path.exists():
        temporary = path.with_suffix('.download')
        run(['curl', '--fail', '--location', '--retry', '3', url, '-o', temporary])
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError(f'Checksum mismatch: {path}; remove it and retry')
    return path


def setup(accept):
    if os.uname().sysname != 'Darwin' or os.uname().machine != 'arm64':
        raise RuntimeError('Bundled toolchain setup currently supports Apple Silicon macOS')
    TOOLS.mkdir(parents=True, exist_ok=True)
    if not SDL.exists():
        SDL.parent.mkdir(parents=True, exist_ok=True)
        run(['tar', '-xzf', download('sdl.tar.gz'), '-C', SDL.parent])
    if not JDK.exists():
        run(['tar', '-xzf', download('jdk.tar.gz'), '-C', TOOLS])
    if not GRADLE.exists():
        run(['unzip', '-q', download('gradle.zip'), '-d', TOOLS])
    manager = SDK / 'cmdline-tools/latest/bin/sdkmanager'
    if not manager.exists():
        target = SDK / 'cmdline-tools'
        target.mkdir(parents=True, exist_ok=True)
        run(['unzip', '-q', download('sdk.zip'), '-d', target])
        (target / 'cmdline-tools').rename(target / 'latest')
    options = {'input': 'y\n' * 100, 'text': True} if accept else {}
    run([manager, f'--sdk_root={SDK}', *PACKAGES], env=environment(), **options)


def check_apk(apk):
    with zipfile.ZipFile(apk) as archive:
        names = set(archive.namelist())
        required = {'lib/arm64-v8a/libmain.so', 'lib/arm64-v8a/libSDL3.so',
                    'lib/arm64-v8a/libc++_shared.so', 'assets/LICENSE.txt',
                    'assets/LICENSES/SDL3.txt', 'assets/LICENSES/Android-NDK-runtime.txt',
                    'assets/docs/THIRD-PARTY-NOTICES.md', 'assets/build-identity.json'}
        if not required <= names:
            raise RuntimeError(f'APK missing required files: {required - names}')
        for name in names:
            lower = name.lower()
            if lower.startswith(('assets/roms/', 'assets/oracle/')) or lower.endswith(('.zip', '.bin', '.rom', '.keystore', '.jks')):
                raise RuntimeError(f'Unexpected private/binary asset: {name}')
        # Only approved notices and generated source identity are packaged as assets.
        if any(n.startswith('assets/') and not (n.startswith('assets/LICENSES/') or n in
               {'assets/LICENSE.txt', 'assets/docs/THIRD-PARTY-NOTICES.md', 'assets/build-identity.json'}) for n in names):
            raise RuntimeError('Unexpected APK asset outside the packaging allowlist')
    build_tools = SDK / 'build-tools/35.0.0'
    run([build_tools/'zipalign', '-c', '-P', '16', '4', apk])
    run([build_tools/'apksigner', 'verify', apk], env=environment())
    badging = subprocess.check_output([str(build_tools/'aapt2'), 'dump', 'badging', str(apk)], text=True)
    if "launchable-activity: name='org.afterburner2.app.GameActivity'" not in badging or \
       "leanback-launchable-activity:" not in badging or \
       "uses-feature: name='android.hardware.touchscreen'" in badging or \
       "native-code: 'arm64-v8a'" not in badging:
        raise RuntimeError('Unexpected phone/TV launcher, touchscreen requirement or ABI')
    readelf = SDK / 'ndk/28.2.13676358/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf'
    with tempfile.TemporaryDirectory(prefix='apk-elf-', dir=TOOLS) as temporary:
        with zipfile.ZipFile(apk) as archive:
            for name in sorted(n for n in archive.namelist() if n.endswith('.so')):
                path = Path(temporary) / Path(name).name
                path.write_bytes(archive.read(name))
                headers = subprocess.check_output([str(readelf), '--program-headers', '--wide', str(path)], text=True)
                segments = [line.split() for line in headers.splitlines() if line.strip().startswith('LOAD ')]
                if not segments or any(int(fields[-1], 16) < 16384 for fields in segments):
                    raise RuntimeError(f'{name}: ELF segment alignment is below 16 KiB')
                symbols = subprocess.check_output([str(readelf), '--dyn-syms', str(path)], text=True)
                if name.endswith('/libmain.so') and not any(' SDL_main' in line and ' UND ' not in line for line in symbols.splitlines()):
                    raise RuntimeError('Native SDL_main entrypoint missing')
    print('Phone/TV manifest, ARM64 libraries/entrypoint, signature and 16 KiB ZIP/ELF alignment passed')
    print(f'APK verified: {apk}\nSHA256 {hashlib.sha256(apk.read_bytes()).hexdigest()}')


def build(worker_continuation=False):
    if not GRADLE.exists() or not SDL.exists():
        raise RuntimeError('Run python3 tools/android.py setup first')
    # local.properties is ignored, and SDK paths never enter the tracked build.
    (ROOT / 'android/local.properties').write_text(f'sdk.dir={SDK}\n')
    identity = ROOT / 'android/app/build/notices/build-identity.json'
    identity.parent.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    diff = subprocess.check_output(['git', 'diff', 'HEAD', '--binary'], cwd=ROOT)
    # Include new platform source files that are not yet tracked.
    untracked = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT).split(b'\0')
    source_hash = hashlib.sha256(diff)
    for name in sorted(n for n in untracked if n):
        source_hash.update(name);source_hash.update((ROOT / os.fsdecode(name)).read_bytes())
    identity.write_text(json.dumps({'revision': revision, 'working_tree_sha256': source_hash.hexdigest(),
        'abi': 'arm64-v8a', 'sdl': '3.4.16', 'ndk': '28.2.13676358', 'gradle': '8.9',
        'cpu_continuation': 'worker-thread' if worker_continuation else 'android-arm64-stack',
        'variant': 'debug-preview'}, indent=2) + '\n')
    run([GRADLE, '--no-daemon', '--console=plain', '-Pab2WorkerContinuation='+('ON' if worker_continuation else 'OFF'),
         'assembleDebug'], cwd=ROOT / 'android', env=environment())
    check_apk(ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['setup', 'build', 'install', 'devices', 'check'])
    parser.add_argument('--accept-sdk-licenses', action='store_true', help='accept Android SDK package licenses during setup')
    parser.add_argument('--serial', help='ADB device serial (required when multiple devices are attached)')
    parser.add_argument('--worker-continuation', action='store_true', help='build the worker CPU baseline for comparison')
    args = parser.parse_args()
    apk = ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk'
    if args.command == 'setup': setup(args.accept_sdk_licenses)
    elif args.command == 'build': build(args.worker_continuation)
    elif args.command == 'check': check_apk(apk)
    else:
        adb = [SDK / 'platform-tools/adb'] + (['-s', args.serial] if args.serial else [])
        if args.command == 'devices': run([*adb, 'devices', '-l'])
        else:
            check_apk(apk)
            run([*adb, 'install', '-r', apk])
            run([*adb, 'shell', 'am', 'start', '-n', 'org.afterburner2.app/.GameActivity'])


if __name__ == '__main__':
    main()
