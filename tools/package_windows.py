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

from package_macos import ROOT, checked_source, run


SOURCES = {
    "SDL3-3.4.16.tar.gz": "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68",
    "llvm-project-23.1.3.src.tar.xz": "c44186a7762ed28954be72e5ff6df9808e0779d4f1bf014ecc4e7e211d31ee34",
    "mingw-w64-a3d93999.tar.gz": "c08776647d5744df7f6ae2bf131da4c9c0fd9934f7a11ecc85cd5692d4b01b62",
    "llvm-mingw-20261006-source.tar.gz": "c2420cffca252535942318334c28aa79514671f6df2e02b403f8198d39e0ba31",
}
SYSTEM_DLLS = {
    "advapi32.dll", "gdi32.dll", "imm32.dll", "kernel32.dll", "msvcrt.dll",
    "ole32.dll", "oleaut32.dll", "setupapi.dll", "shell32.dll", "user32.dll",
    "version.dll", "winmm.dll",
}


def member_bytes(archive, suffix):
    matches = [p for p in archive.getmembers() if p.isfile() and p.name.endswith(suffix)]
    if len(matches) != 1:
        raise SystemExit(f"Expected one archive member ending in {suffix}")
    return archive.extractfile(matches[0]).read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--toolchain-archive", type=Path, required=True)
    parser.add_argument("--sdl-archive", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"v[0-9][A-Za-z0-9.\-]*", args.version):
        parser.error("Use a version beginning with v and a digit")
    if run("git", "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("Commit the intended source tree before packaging")
    build = ROOT / "build/windows-cross"
    cache = (build / "CMakeCache.txt").read_text()
    flags = (build / "CMakeFiles/afterburner2.dir/flags.make").read_text()
    if ("CMAKE_BUILD_TYPE:STRING=Release" not in cache or
            "AB2_ENABLE_OPENAL:BOOL=OFF" not in cache or "-flto" not in flags or "-O3" not in flags):
        raise SystemExit("Expected the Release/LTO cross-build with OpenAL disabled")
    sources = [checked_source(ROOT / "build/deps" / name, digest) for name, digest in SOURCES.items()]
    checked_source(args.toolchain_archive, "a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6")
    checked_source(args.sdl_archive, "c7ef65bd72eabac6e5b535411dbd8d5824d0aab24fd62ff8812666b336f18a9c")
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
    for filename in binaries:
        shutil.copy2(build / filename, package / filename)
    # Copy DLL bytes directly from checksum-verified official distributions.
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
    for relative in ("README.md", "roms/README.md", "roms/roms.txt", "LICENSE.txt"):
        shutil.copy2(ROOT / relative, package / relative)
    shutil.copytree(ROOT / "docs", package / "docs")
    shutil.copytree(ROOT / "LICENSES", package / "Notices/project")
    for relative in ("src/main/frontend/scalers/SNES9X-LICENSE.txt", "src/main/frontend/scalers/scalefx.hpp",
                     "src/third_party/z80.h", "src/third_party/dr_flac/LICENSE", "src/main/frontend/controllerdb/LICENSE"):
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
    (package / "START-HERE.txt").write_text(
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
    files = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(package.rglob("*")) if p.is_file()}
    (package / "manifest.json").write_text(json.dumps({
        "version": args.version, "commit": run("git", "rev-parse", "HEAD"),
        "architecture": "x86_64", "target_windows": "10/11", "runtime_verified": False,
        "compiler": "LLVM-MinGW 20261006 / Clang 23.1.3", "configuration": "Release/LTO",
        "libraries": {"SDL3": "3.4.16", "LLVM runtimes": "23.1.3", "OpenAL": False},
        "imports": imports, "source_sha256": SOURCES, "sha256": files,
    }, indent=2) + "\n")
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package.rglob("*")):
            archive.write(path, path.relative_to(output))
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(".zip.sha256").write_text(f"{digest}  {destination.name}\n")
    print(destination.relative_to(ROOT))
    print(f"SHA256 {digest}")


if __name__ == "__main__":
    main()
