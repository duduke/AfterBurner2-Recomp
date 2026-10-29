#!/usr/bin/env python3
"""Package a committed arm64 runtime and matching source companion ZIP."""
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
AIRCRAFT_ASSETS = (
    "assets/player-aircraft/level-flight-hd-v1.png",
    "assets/player-aircraft/gentle-bank-hd-v2.png",
    "assets/player-aircraft/neutral-gentle-v1.aircraft",
    "assets/player-aircraft/README.md",
)
LIBRASHADER_NOTICES = (
    "src/third_party/librashader/librashader.h",
    "src/third_party/librashader/NOTICE.md",
    "src/third_party/librashader/LICENSE.MPL-2.0",
)
PROJECT_NOTICES = (
    "README.md", "Apache-2.0.txt", "Cannonball.txt", "CannonBall-SE.txt",
    "MAME-Legacy.txt", "MAME-Audio.txt", "MAME-SegaPCM.txt", "YMFM.txt",
    "YMFM-Timers.txt", "Vulkan-Headers.txt", "Z80.txt", "SDL3.txt",
    "SDL_GameControllerDB.txt", "dr_flac.txt", "miniz.txt", "ScaleFX.txt",
    "Snes9x.txt", "librashader-header.txt", "MPL-2.0.txt", "OpenAL-Soft.txt",
    "OpenAL-BSD-3Clause.txt", "OpenAL-fmt.txt", "OpenAL-GSL.txt", "OpenAL-pffft.txt",
    "GCC-GPL-3.0.txt", "GCC-Runtime-Library-Exception.txt", "GCC-Runtime.txt",
)


def require_notice(path):
    if path is None or path.is_symlink() or not path.is_file() or not path.read_bytes().strip():
        raise SystemExit(f"Required license notice must be a nonempty regular file: {path}")


def check_project_notices():
    for filename in PROJECT_NOTICES:
        relative = f"LICENSES/{filename}"
        require_notice(ROOT / relative)
        committed_source(relative)


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def checked_source(path, expected):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f"Source archive checksum mismatch: {path}")
    return path


def committed_source(relative):
    source = ROOT / relative
    if source.is_symlink() or not source.is_file():
        raise SystemExit(f"Package source must be a regular file: {relative}")
    try:
        run("git", "cat-file", "-e", f"HEAD:{relative}")
    except subprocess.CalledProcessError:
        raise SystemExit(f"Package source is not committed: {relative}") from None
    return source


def write_notices(package, sdl_license):
    # Keep every notice, but do not ship source headers or duplicate texts in
    # several directories. The original files also remain in the source ZIP.
    notices = [(f"LICENSES/{name}", (ROOT / "LICENSES" / name).read_bytes())
               for name in PROJECT_NOTICES]
    notices.append(("Bundled SDL3 LICENSE.txt", sdl_license))
    openal_notices = ROOT / "build/deps/openal/share/licenses/openal-soft"
    for name in ("LGPL.txt", "BSD-3Clause.txt", "pffft.txt", "fmt.txt", "gsl.txt"):
        source = openal_notices / name
        require_notice(source)
        notices.append((f"Bundled OpenAL Soft {name}", source.read_bytes()))
    grouped = {}
    for label, content in notices:
        grouped.setdefault(content, []).append(label)
    with (package / "THIRD-PARTY-NOTICES.txt").open("wb") as output:
        for content, labels in grouped.items():
            output.write(("\n===== " + "; ".join(labels) + " =====\n\n").encode())
            output.write(content)
            output.write(b"\n")


def write_zip(folder, output):
    destination = output / f"{folder.name}.zip"
    with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(folder.rglob("*")):
            archive.write(path, path.relative_to(output))
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix(".zip.sha256").write_text(f"{digest}  {destination.name}\n")
    return destination, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", help="Release tag, e.g. v0.1.0-preview.1")
    args = parser.parse_args()
    if not re.fullmatch(r"v[0-9][A-Za-z0-9.\-]*", args.version):
        parser.error("Use a version beginning with v and a digit")
    if run("git", "status", "--porcelain", "--untracked-files=normal"):
        raise SystemExit("Commit the intended source tree before packaging")
    check_project_notices()
    aircraft_assets = [(relative, committed_source(relative)) for relative in AIRCRAFT_ASSETS]

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
    # pkg-config may return a shared prefix such as /opt/homebrew/lib. Its
    # parent LICENSE.txt is Homebrew's notice, not SDL's. Use the pinned source.
    with tarfile.open(archives[0]) as source:
        member = source.getmember("SDL3-3.4.16/LICENSE.txt")
        if not member.isfile():
            raise SystemExit("SDL source license must be a regular file")
        sdl_license = source.extractfile(member).read()
    output = ROOT / "build/releases"
    output.mkdir(parents=True, exist_ok=True)
    name = f"AfterBurner2-{args.version}-macOS-arm64"
    package = output / name
    sources = output / f"{name}-sources"
    for folder in (package, sources):
        if folder.exists() or (output / f"{folder.name}.zip").exists():
            raise SystemExit(f"Artifact already exists: {folder}")
    package.mkdir()  # Refuse to replace an existing artifact.
    sources.mkdir()
    for directory in ("lib", "roms", "soundtrack"):
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

    for relative in ("roms/README.md", "roms/roms.txt", "LICENSE.txt"):
        shutil.copy2(ROOT / relative, package / relative)
    write_notices(package, sdl_license)
    for relative, source in aircraft_assets:
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    for archive in archives:
        shutil.copy2(archive, sources / archive.name)
    source_archive = sources / "AfterBurner2-source.tar.gz"
    run("git", "archive", "--format=tar.gz", "--prefix=AfterBurner2/", f"--output={source_archive}", "HEAD")
    with tarfile.open(source_archive) as archive:
        for entry in archive.getmembers():
            if not entry.isfile():
                continue
            relative = entry.name.removeprefix("AfterBurner2/")
            if relative.startswith(("build/", "soundtrack/")) or (relative.startswith("roms/") and relative not in ("roms/README.md", "roms/roms.txt")):
                raise SystemExit(f"Unexpected source payload: {relative}")
    commit = run("git", "rev-parse", "HEAD")
    docs_url = f"https://github.com/duduke/AfterBurner2-Recomp/blob/{commit}/docs/"
    launcher = package / "Launch.command"
    launcher.write_text('#!/bin/sh\nset -eu\ncd -- "$(dirname -- "$0")"\nexec ./afterburner2 "$@"\n')
    launcher.chmod(0o755)
    (package / "START-HERE.txt").write_text(
        f"AfterBurner2 {args.version} — macOS Apple Silicon preview\n\n"
        "Requires macOS 26 or newer and an Apple Silicon Mac.\n"
        "Extract the complete ZIP into a writable folder. Supply your own extracted\n"
        "MAME aburner2 ROM files in roms/, put aburner2.zip there, or choose a\n"
        "ROM ZIP in the startup picker (see roms/README.md).\n"
        "Double-click Launch.command, or run it from Terminal. Keep lib/ alongside it.\n"
        "Enter Arcade starts attract mode; 5 inserts a coin, Enter starts.\n"
        "Practice selects a stage for a fresh run; F5 retries that Practice stage.\n"
        "Shuffle arranges gameplay stages 1-22 once each and keeps stage 23 last.\n"
        "F5 restarts the same Shuffle order; New Shuffle creates another order.\n"
        "Return to Arcade starts a fresh original board. See PRACTICE_MODE.md\n"
        "and SHUFFLE_MODE.md at the documentation URL below for details.\n"
        "Escape opens the pause menu; Settings > Display > Aspect Ratio offers\n"
        "16:10 and 16:9 previews. Restart Run and Change Mode are on the pause menu.\n"
        "These viewports and detached HUD/aircraft use the integrated renderer by default.\n"
        "The Preview labels describe remaining widescreen coverage, not renderer selection.\n"
        "--scene-renderer snapshot selects the retained reconstruction reference.\n"
        "Settings > Graphics > Ground > Corrected enables horizon correction.\n"
        "Settings > CRT and Shaders includes built-in Off / Subtle / Arcade modes.\n"
        "RetroArch Slang presets need a separately installed optional runtime\n"
        "and shader pack; neither is bundled. See CRT_DISPLAY.md online for setup.\n"
        "The optional neutral/gentle-bank aircraft prototype is in assets/player-aircraft/;\n"
        "see SPRITE_REPLACEMENT.md online for its command-line option and limits.\n"
        "Custom WAV/FLAC music belongs in soundtrack/. Preferences use SDL's\n"
        "Application Support directory and are not included in this download.\n\n"
        "The executable/libraries are ad-hoc signed, not Developer ID signed or\n"
        "notarized. macOS may require approval in System Settings > Privacy & Security.\n"
        "This is an experimental build; see ROADMAP.md online for limits.\n"
        "No ROMs, Sega graphics/audio, captures or personal settings are included.\n"
        f"Documentation: {docs_url}\n"
        f"Complete project and bundled library sources: {sources.name}.zip\n"
        "Distribute that matching companion alongside this runtime ZIP.\n"
        "Notices are in THIRD-PARTY-NOTICES.txt and LICENSE.txt; license inventory\n"
        "paths refer to files in the source companion. Shared libraries can be replaced locally\n"
        "with ABI-compatible builds (re-sign changed binaries as necessary).\n"
    )
    files = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(package.rglob("*")) if p.is_file()}
    (package / "manifest.json").write_text(json.dumps({
        "version": args.version, "commit": commit,
        "sources": f"{sources.name}.zip",
        "architecture": "arm64", "minimum_macos": "26.0",
        "libraries": {"SDL3": "3.4.16", "OpenAL Soft": "1.25.2"},
        "sha256": files,
    }, indent=2) + "\n")
    source_files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in sorted(sources.iterdir())}
    (sources / "manifest.json").write_text(json.dumps({
        "version": args.version, "commit": commit, "sha256": source_files,
    }, indent=2) + "\n")
    for folder in (sources, package):
        destination, digest = write_zip(folder, output)
        print(destination.relative_to(ROOT))
        print(f"SHA256 {digest}")


if __name__ == "__main__":
    main()
