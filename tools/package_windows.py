#!/usr/bin/env python3
"""Package the cross-built Windows x64 preview without ROMs or preferences."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
import zipfile

from package_macos import (ROOT, AIRCRAFT_ASSETS, LIBRASHADER_NOTICES,
                           checked_source, committed_source, run,
                           require_notice, check_project_notices)


SOURCES_CROSS = {
    "SDL3-3.4.16.tar.gz": "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68",
    "llvm-project-23.1.3.src.tar.xz": "c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34",
    "mingw-w64-a3d93999.tar.gz": "c08776647d5744df7f6ae2bf131da4c9c0fd9934f7a11ecc85cd5692d4b01b62",
    "llvm-mingw-20261006-source.tar.gz": "c2420cffca252535942318334c28aa79514671f6df2e02b403f8198d39e0ba31",
}
SOURCES_NATIVE = {
    "SDL3-3.4.18.tar.gz": "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3",
    "openal-soft-1.25.2.tar.gz": "fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894",
}
SYSTEM_DLLS = {
    "advapi32.dll", "avrt.dll", "gdi32.dll", "imm32.dll", "kernel32.dll", "msvcrt.dll",
    "ole32.dll", "oleaut32.dll", "setupapi.dll", "shell32.dll", "user32.dll",
    "version.dll", "winmm.dll",
}


def member_bytes(archive, suffix):
    matches = [p for p in archive.getmembers() if p.isfile() and p.name.endswith(suffix)]
    if len(matches) != 1:
        raise SystemExit(f"Expected one archive member ending in {suffix}")
    return archive.extractfile(matches[0]).read()


def zip_member_bytes(archive, suffix):
    matches = [name for name in archive.namelist() if name.endswith(suffix)]
    if len(matches) != 1:
        raise SystemExit(f"Expected one zip member ending in {suffix}")
    return archive.read(matches[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("--native-build", type=Path, help="Package a native Windows x64 build directory (e.g. build/win)")
    parser.add_argument("--mingw-notice", type=Path, help="Required for native builds: the compiler's complete MinGW-w64 runtime COPYING notice")
    parser.add_argument("--toolchain", type=Path)
    parser.add_argument("--toolchain-archive", type=Path)
    parser.add_argument("--sdl-archive", type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r"v[0-9][A-Za-z0-9.\-]*", args.version):
        parser.error("Use a version beginning with v and a digit")
    if args.native_build:
        if not args.mingw_notice:
            parser.error("Native builds require --mingw-notice pointing to the compiler's complete MinGW-w64 runtime COPYING file")
        require_notice(args.mingw_notice)
    if run("git", "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("Commit the intended source tree before packaging")
    check_project_notices()
    aircraft_assets = [(relative, committed_source(relative)) for relative in AIRCRAFT_ASSETS]
    shader_notices = [committed_source(relative) for relative in LIBRASHADER_NOTICES]

    output = ROOT / "build/releases"
    output.mkdir(parents=True, exist_ok=True)
    name = f"AfterBurner2-{args.version}-Windows-x64-experimental"
    destination = output / f"{name}.zip"
    if destination.exists():
        raise SystemExit(f"Artifact already exists: {destination}")
    package = output / name
    package.mkdir()  # Never replace an existing artifact.
    for directory in ("roms", "soundtrack", "Sources", "Notices"):
        (package / directory).mkdir()
    binaries = ("afterburner2.exe", "cpu_continuation_regression.exe", "cpu_continuation_thread_regression.exe")

    if args.native_build:
        build = args.native_build.resolve()
        cache = (build / "CMakeCache.txt").read_text()
        ninja_rules = (build / "build.ninja").read_text()
        if ("CMAKE_BUILD_TYPE:STRING=Release" not in cache or
                "AB2_ENABLE_OPENAL:BOOL=ON" not in cache or "-O3" not in ninja_rules):
            raise SystemExit("Expected a Release native Windows build with OpenAL enabled")
        source_map = SOURCES_NATIVE
        sources = [checked_source(ROOT / "build/deps" / k, v) for k, v in source_map.items()]
        sdl_zip = checked_source(ROOT / "build/deps/SDL3-3.4.18-mingw.zip",
                                 "cd98158c8d025a816f430a600b4e9526524e6219ee884ec72bd498b80fd3bf58")
        openal_zip = checked_source(ROOT / "build/deps/openal-soft-1.25.2-bin.zip",
                                    "67a0c4b800bd860c93c04f38caf8cbe4875f9c84700ac430efc451f70e265434")
        for filename in binaries:
            shutil.copy2(build / filename, package / filename)
        with zipfile.ZipFile(sdl_zip) as archive:
            (package / "SDL3.dll").write_bytes(zip_member_bytes(archive, "/x86_64-w64-mingw32/bin/SDL3.dll"))
            (package / "Notices/SDL3.txt").write_bytes(zip_member_bytes(archive, "/x86_64-w64-mingw32/share/licenses/SDL3/LICENSE.txt"))
        with zipfile.ZipFile(openal_zip) as archive:
            (package / "OpenAL32.dll").write_bytes(zip_member_bytes(archive, "/bin/Win64/soft_oal.dll"))
        compiler_match = re.search(r"^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$", cache, re.M)
        if not compiler_match:
            raise SystemExit("Missing compiler identity in native build cache")
        compiler = Path(compiler_match[1])
        compiler_desc = run(str(compiler), "--version").splitlines()[0]
        # LLVM-MinGW uses replaceable shared C++/unwind/pthread runtimes.
        # GCC builds may instead link these statically. Validate imports below.
        for filename in ("libc++.dll", "libunwind.dll", "libwinpthread-1.dll"):
            runtime = compiler.parent / filename
            if runtime.is_file():
                shutil.copy2(runtime, package / filename)
        if "clang" in compiler_desc.lower():
            shutil.copy2(compiler.parent.parent / "LICENSE.TXT", package / "Notices/LLVM.txt")
        shutil.copytree(ROOT / "build/deps/openal/share/licenses/openal-soft", package / "Notices/OpenAL-Soft")
        shutil.copy2(args.mingw_notice, package / "Notices/MinGW-w64.txt")
        imports = {}
        for path in [*(package / filename for filename in binaries), *sorted(package.glob("*.dll"))]:
            report = run("objdump", "-p", str(path))
            if "file format pei-x86-64" not in report and "file format pe-x86-64" not in report:
                raise SystemExit(f"Expected Windows x64 PE: {path}")
            imports[path.name] = re.findall(r"^\s*DLL Name:\s+(.+?)\s*$", report, re.M)
            for dependency in imports[path.name]:
                if (dependency.lower() not in SYSTEM_DLLS and
                        not dependency.lower().startswith("api-ms-win-") and not (package / dependency).is_file()):
                    raise SystemExit(f"Unbundled DLL: {path.name}: {dependency}")
            if path.name == "afterburner2.exe":
                for symbol in ("CreateFiberEx", "ConvertThreadToFiberEx", "SwitchToFiber"):
                    if symbol not in report:
                        raise SystemExit(f"Missing Windows fiber import: {symbol}")
        config_desc = "Release (-O3, LTO)" if "-flto" in ninja_rules else "Release (-O3, without LTO)"
        libraries_desc = {"SDL3": "3.4.18", "OpenAL Soft": "1.25.2"}
        runtime_verified = True
        start_here = (
            f"AfterBurner2 {args.version} - Windows x64 preview\n\n"
            "Targets Windows 10/11 x64. Built and runtime-verified on Windows x64\n"
            "with Vulkan GPU sprite filters and OpenAL Soft 1.25.2 cockpit audio.\n"
            "Unsigned. Extract the complete ZIP to a writable folder. Keep SDL3.dll\n"
            "and OpenAL32.dll beside afterburner2.exe. Put your own extracted\n"
            "aburner2 ROMs in roms/.\n"
            "Open Launch.cmd; Enter Arcade starts boot/attract, 5 inserts a coin,\n"
            "Enter starts, and Escape opens settings. No compiler/SDL install needed.\n\n"
            "Run Check-Continuations.cmd to verify the ROM-free continuation tests.\n"
            "Launch.cmd --profile reports continuation=windows-fiber and GPU/CPU\n"
            "filter selection. Vulkan compute requires driver support; Direct3D 12\n"
            "compute is absent. SDL can use accelerated presentation with CPU filters\n"
            "when compute is unavailable. Original SDL stereo, replacement music and\n"
            "optional OpenAL Soft spatial audio (HRTF, 2.1, 5.1) are included.\n\n"
            "No ROMs, captures, personal settings or soundtracks are included.\n"
            "Sources/ contains project, SDL3 and OpenAL Soft upstream sources;\n"
            "licenses are in Notices/ and LICENSE.txt.\n"
        )
    else:
        if not (args.toolchain and args.toolchain_archive and args.sdl_archive):
            parser.error("--toolchain, --toolchain-archive and --sdl-archive are required for cross-builds")
        build = ROOT / "build/windows-cross"
        cache = (build / "CMakeCache.txt").read_text()
        flags = (build / "CMakeFiles/afterburner2.dir/flags.make").read_text()
        if ("CMAKE_BUILD_TYPE:STRING=Release" not in cache or
                "AB2_ENABLE_OPENAL:BOOL=OFF" not in cache or "-flto" not in flags or "-O3" not in flags):
            raise SystemExit("Expected the Release/LTO cross-build with OpenAL disabled")
        source_map = SOURCES_CROSS
        sources = [checked_source(ROOT / "build/deps" / k, v) for k, v in source_map.items()]
        checked_source(args.toolchain_archive, "a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6")
        checked_source(args.sdl_archive, "c7ef65bd72eabac6e5b535411dbd8d5824d0aab24fd62ff8812666b336f18a9c")
        for filename in binaries:
            shutil.copy2(build / filename, package / filename)
        with tarfile.open(args.toolchain_archive) as archive:
            for filename in ("libc++.dll", "libunwind.dll"):
                (package / filename).write_bytes(member_bytes(archive, f"/x86_64-w64-mingw32/bin/{filename}"))
            (package / "Notices/LLVM.txt").write_bytes(member_bytes(archive, "/LICENSE.TXT"))
            (package / "Notices/MinGW-w64.txt").write_bytes(member_bytes(archive, "/x86_64-w64-mingw32/share/mingw32/COPYING"))
        with tarfile.open(args.sdl_archive) as archive:
            (package / "SDL3.dll").write_bytes(member_bytes(archive, "/x86_64-w64-mingw32/bin/SDL3.dll"))
            (package / "Notices/SDL3.txt").write_bytes(member_bytes(archive, "/x86_64-w64-mingw32/share/licenses/SDL3/LICENSE.txt"))
        imports = {}
        for path in [*(package / filename for filename in binaries), *sorted(package.glob("*.dll"))]:
            headers = run(str(args.toolchain / "bin/llvm-readobj"), "--file-headers", str(path))
            if "IMAGE_FILE_MACHINE_AMD64" not in headers:
                raise SystemExit(f"Expected Windows x64 PE: {path}")
            report = run(str(args.toolchain / "bin/llvm-readobj"), "--coff-imports", str(path))
            imports[path.name] = re.findall(r"^  Name: (.+)$", report, re.M)
            for dependency in imports[path.name]:
                if (dependency.lower() not in SYSTEM_DLLS and
                        not dependency.lower().startswith("api-ms-win-") and not (package / dependency).is_file()):
                    raise SystemExit(f"Unbundled DLL: {path.name}: {dependency}")
            if path.name == "afterburner2.exe":
                for symbol in ("CreateFiberEx", "ConvertThreadToFiberEx", "SwitchToFiber"):
                    if f"Symbol: {symbol} (" not in report:
                        raise SystemExit(f"Missing Windows fiber import: {symbol}")
        compiler_desc = "LLVM-MinGW 20261006 / Clang 23.1.3"
        config_desc = "Release/LTO"
        libraries_desc = {"SDL3": "3.4.16", "LLVM runtimes": "23.1.3", "OpenAL": False}
        runtime_verified = False
        start_here = (
            f"AfterBurner2 {args.version} - experimental Windows x64 preview\n\n"
            "Targets Windows 10/11 x64. CROSS-BUILT ONLY: Windows execution, graphics,\n"
            "controls and frame-rate acceptance have not been verified. Unsigned.\n"
            "Extract the complete ZIP to a writable folder. Keep all three DLLs\n"
            "beside the EXE. Put your own extracted aburner2 ROMs in roms/.\n"
            "Open Launch.cmd; Enter Arcade starts boot/attract, 5 inserts a coin,\n"
            "Enter starts, and Escape opens settings. No compiler/SDL install needed.\n\n"
            "Run Check-Continuations.cmd first: both ROM-free continuation tests\n"
            "should pass. Launch.cmd --profile reports continuation=windows-fiber\n"
            "and GPU/CPU filter selection. Vulkan compute requires driver support;\n"
            "Direct3D 12 compute is absent. SDL can use accelerated presentation\n"
            "with CPU filters when compute is unavailable. Original SDL stereo and\n"
            "replacement music are included; optional OpenAL spatial audio is not.\n\n"
            "No ROMs, captures, personal settings or soundtracks are included.\n"
            "Sources/ contains project, SDL, LLVM runtime and MinGW sources plus\n"
            "the pinned toolchain build scripts; licenses are in Notices/.\n"
        )

    for relative in ("README.md", "roms/README.md", "roms/roms.txt", "LICENSE.txt", "COPYRIGHT.md"):
        shutil.copy2(ROOT / relative, package / relative)
    shutil.copytree(ROOT / "docs", package / "docs")
    shutil.copytree(ROOT / "LICENSES", package / "Notices/project")
    for relative, source in aircraft_assets:
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    (package / "Notices/librashader").mkdir()
    for source in shader_notices:
        shutil.copy2(source, package / "Notices/librashader" / source.name)
    for relative in ("src/main/frontend/scalers/scalefx.hpp",
                     "src/third_party/z80.h", "src/third_party/dr_flac/LICENSE", "src/third_party/miniz/LICENSE", "src/main/frontend/controllerdb/LICENSE"):
        shutil.copy2(ROOT / relative, package / "Notices" / relative.replace("/", "_"))
    for path in sources:
        shutil.copy2(path, package / "Sources" / path.name)
    source_archive = package / "Sources/AfterBurner2-source.tar.gz"
    run("git", "archive", "--format=tar.gz", "--prefix=AfterBurner2/", f"--output={source_archive}", "HEAD")
    with tarfile.open(source_archive) as archive:
        for entry in archive.getmembers():
            relative = entry.name.removeprefix("AfterBurner2/")
            if entry.isfile() and (relative.startswith(("build/", "soundtrack/")) or
                    (relative.startswith("roms/") and relative not in ("roms/README.md", "roms/roms.txt"))):
                raise SystemExit(f"Unexpected source payload: {relative}")
    (package / "Launch.cmd").write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\nafterburner2.exe %*\r\n')
    (package / "Check-Continuations.cmd").write_bytes(
        b'@echo off\r\ncd /d "%~dp0"\r\ncpu_continuation_regression.exe\r\nif errorlevel 1 exit /b 1\r\n'
        b'cpu_continuation_thread_regression.exe\r\nif errorlevel 1 exit /b 1\r\n')
    start_here += (
        "\nPractice, Shuffle, 16:10, HUD/CRT controls, calibration and the\n"
        "registered HD aircraft pack are included. Optional RetroArch .slangp\n"
        "presets use native Vulkan; the runtime and shader packs are external.\n"
        "See docs/CRT_DISPLAY.md for setup and backend limits.\n"
    )
    (package / "START-HERE.txt").write_text(start_here)
    files = {str(p.relative_to(package)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(package.rglob("*")) if p.is_file()}
    (package / "manifest.json").write_text(json.dumps({
        "version": args.version, "commit": run("git", "rev-parse", "HEAD"),
        "architecture": "x86_64", "target_windows": "10/11", "runtime_verified": runtime_verified,
        "compiler": compiler_desc, "configuration": config_desc,
        "libraries": libraries_desc,
        "imports": imports, "source_sha256": source_map, "sha256": files,
    }, indent=2) + "\n")
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package.rglob("*")):
            archive.write(path, path.relative_to(output).as_posix())
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(".zip.sha256").write_text(f"{digest}  {destination.name}\n")
    print(destination.relative_to(ROOT))
    print(f"SHA256 {digest}")


if __name__ == "__main__":
    main()
