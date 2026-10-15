#!/usr/bin/env python3
"""Package a committed arm64 build; never copy local ROMs or preferences."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def checked_source(path, expected):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f"Source archive checksum mismatch: {path}")
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Release tag, e.g. v0.1.0-preview.1")
    args = parser.parse_args()
    if not re.fullmatch(r"v[0-9][A-Za-z0-9.\-]*", args.version):
        parser.error("Use a version beginning with v and a digit")
    if run("git", "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("Commit the intended source tree before packaging")

    binary = ROOT / "build/afterburner2-release"
    if "arm64" not in run("file", str(binary)):
        raise SystemExit("This packager expects an arm64 native-release build")
    sdl = Path(run("pkg-config", "--variable=libdir", "sdl3")) / "libSDL3.0.dylib"
    if run("pkg-config", "--modversion", "sdl3") != "3.4.16":
        raise SystemExit("Update the pinned SDL source/version before packaging")
    openal = ROOT / "build/deps/openal/lib/libopenal.1.dylib"
    archives = [
        checked_source(ROOT / "build/deps/SDL3-3.4.16.tar.gz",
                       "7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68"),
        checked_source(ROOT / "build/deps/openal-soft-1.25.2.tar.gz",
                       "fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894"),
    ]
    output = ROOT / "build/releases"
    output.mkdir(parents=True, exist_ok=True)
    name = f"AfterBurner2-{args.version}-macOS-arm64"
    package = output / name
    package.mkdir()  # Refuse to replace an existing artifact.
    for directory in ("lib", "roms", "soundtrack", "Sources", "Notices"):
        (package / directory).mkdir()
    app = package / "afterburner2"
    shutil.copy2(binary, app)
    libraries = [sdl, openal]
    for library in libraries:
        shutil.copy2(library, package / "lib" / library.name)

    for target in [app, *(package / "lib" / p.name for p in libraries)]:
        commands = run("otool", "-l", str(target))
        for rpath in re.findall(r"cmd LC_RPATH\n.*?path (.*?) \(offset", commands, re.S):
            run("install_name_tool", "-delete_rpath", rpath, str(target))
        if target != app:
            run("install_name_tool", "-id", f"@rpath/{target.name}", str(target))
        for line in run("otool", "-L", str(target)).splitlines()[1:]:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            filename = Path(dependency).name
            if filename not in {p.name for p in libraries}:
                raise SystemExit(f"Unbundled dependency: {dependency}")
            replacement = f"@executable_path/lib/{filename}" if target == app else f"@loader_path/{filename}"
            run("install_name_tool", "-change", dependency, replacement, str(target))
        run("codesign", "--force", "--sign", "-", str(target))
        run("codesign", "--verify", "--strict", str(target))

    for relative in ("README.md", "roms/README.md", "roms/roms.txt", "LICENSE.txt"):
        shutil.copy2(ROOT / relative, package / relative)
    shutil.copytree(ROOT / "LICENSES", package / "Notices/project")
    shutil.copytree(ROOT / "docs", package / "docs")
    for source, dest in [
        (sdl.parent.parent / "LICENSE.txt", "SDL3.txt"),
        (ROOT / "src/main/frontend/scalers/SNES9X-LICENSE.txt", "Snes9x.txt"),
        (ROOT / "src/main/frontend/scalers/scalefx.hpp", "ScaleFX.hpp"),
        (ROOT / "src/third_party/z80.h", "z80.h"),
        (ROOT / "src/third_party/dr_flac/LICENSE", "dr_flac.txt"),
        (ROOT / "src/main/frontend/controllerdb/LICENSE", "SDL_GameControllerDB.txt"),
    ]:
        shutil.copy2(source, package / "Notices" / dest)
    shutil.copytree(ROOT / "build/deps/openal/share/licenses/openal-soft", package / "Notices/OpenAL-Soft")
    for archive in archives:
        shutil.copy2(archive, package / "Sources" / archive.name)
    source_archive = package / "Sources/AfterBurner2-source.tar.gz"
    run("git", "archive", "--format=tar.gz", "--prefix=AfterBurner2/", f"--output={source_archive}", "HEAD")
    with tarfile.open(source_archive) as archive:
        for entry in archive.getmembers():
            if not entry.isfile():
                continue
            relative = entry.name.removeprefix("AfterBurner2/")
            if relative.startswith(("build/", "soundtrack/")) or (relative.startswith("roms/") and relative not in ("roms/README.md", "roms/roms.txt")):
                raise SystemExit(f"Unexpected source payload: {relative}")
    launcher = package / "Launch.command"
    launcher.write_text('#!/bin/sh\nset -eu\ncd -- "$(dirname -- "$0")"\nexec ./afterburner2 "$@"\n')
    launcher.chmod(0o755)
    (package / "START-HERE.txt").write_text(
        f"AfterBurner2 {args.version} — macOS Apple Silicon preview\n\n"
        "Requires macOS 26 or newer and an Apple Silicon Mac.\n"
        "Extract the complete ZIP into a writable folder. Put your own extracted\n"
        "MAME aburner2 ROM files in roms/ (see roms/README.md).\n"
        "Double-click Launch.command, or run it from Terminal. Keep lib/ alongside it.\n"
        "Enter Arcade starts attract mode; 5 inserts a coin, Enter starts.\n"
        "Escape opens settings. Choose Video > Viewport > 16:9 Preview for widescreen\n"
        "and Ground > Corrected for the enhanced horizon correction.\n"
        "Custom WAV/FLAC music belongs in soundtrack/. Preferences use SDL's\n"
        "Application Support directory and are not included in this download.\n\n"
        "The executable/libraries are ad-hoc signed, not Developer ID signed or\n"
        "notarized. macOS may require approval in System Settings > Privacy & Security.\n"
        "This is an experimental build; see README.md and docs/ROADMAP.md for limits.\n"
        "No ROMs, Sega graphics/audio, captures or personal settings are included.\n"
        "Complete project and bundled library sources are in Sources/; notices\n"
        "are in Notices/ and LICENSE.txt. Shared libraries can be replaced locally\n"
        "with ABI-compatible builds (re-sign changed binaries as necessary).\n"
    )
    files = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(package.rglob("*")) if p.is_file()}
    (package / "manifest.json").write_text(json.dumps({
        "version": args.version, "commit": run("git", "rev-parse", "HEAD"),
        "architecture": "arm64", "minimum_macos": "26.0",
        "libraries": {"SDL3": "3.4.16", "OpenAL Soft": "1.25.2"},
        "sha256": files,
    }, indent=2) + "\n")
    destination = output / f"{name}.zip"
    if destination.exists():
        raise SystemExit(f"Artifact already exists: {destination}")
    with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package.rglob("*")):
            archive.write(path, path.relative_to(output))
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(".zip.sha256").write_text(f"{digest}  {destination.name}\n")
    print(destination.relative_to(ROOT))
    print(f"SHA256 {digest}")


if __name__ == "__main__":
    main()
