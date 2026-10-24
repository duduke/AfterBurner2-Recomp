#!/usr/bin/env python3
"""Fetch the optional, pinned RetroArch Slang runtime into the local build tree."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import stat
import sys
import tempfile
from urllib.request import Request, urlopen
import zipfile


VERSION = "0.12.0"
TAG = f"librashader-v{VERSION}"
ARCHIVE = f"librashader-aarch64-macos-v{VERSION}-optimized.zip"
SHA256 = "49808004a4904f6a99e0231092dcfdfe52b7b61f68430a4c9f1e165749c4c90e"
UPSTREAM = "https://github.com/SnowflakePowered/librashader"
URL = f"{UPSTREAM}/releases/download/{TAG}/{ARCHIVE}"
SOURCE_URL = f"{UPSTREAM}/archive/refs/tags/{TAG}.tar.gz"
LICENSE_URL = f"{UPSTREAM}/blob/{TAG}/LICENSE.md"
FILE_SHA256 = {
    "librashader.a": "f73f6743b76310ee51c5df639df0bb262eec81b0d470340d62681d3b439f3122",
    "librashader.dylib": "1dbecea0c165fd0fddc2407ed4b8872f9f73f4fd2c3689a80ffc24c87e3fda2a",
    "librashader.h": "5d478897c391af3f60015810b67785ae1a286d262a845485276e36ded9f21e62",
}
REQUIRED_FILES = set(FILE_SHA256)
MAX_DOWNLOAD_BYTES = 64 * 1024 * 1024
MAX_EXTRACT_BYTES = 128 * 1024 * 1024
MANIFEST = "fetch-manifest.json"

WINDOWS_FILES = {
    "librashader.d": "202eced0826947a9e4c67b5b3c7530caa1765ee806872c0b2a1bc114efb37562",
    "librashader.dll": "1890f647c7fbe52d4cc591526db24367caca284996855c4565c6003c7e46f8cc",
    "librashader.dll.exp": "1763f6968bc5c129514c562267945cc8dcddb4449ae840b336fe3ff6a1dc784e",
    "librashader.dll.lib": "eeb9eda4457b4e74425ef585241f0b9301994dde7836b433c81f7626a8f77c3c",
    "librashader.h": "a20da26f079e7e15e4736368fa553bb94dd4cc6039078d0af81cd5427bae5393",
    "librashader.lib": "79a37d7513f0cf349a4519504e5dce443a6e23577b50b4ae4797735f2e2817bf",
    "librashader.pdb": "daf210e258f71d73ff55cbd538717415c1d2338dc0e1fec63cb32f4fb26a3e0c",
}


def runtime_config(windows=False):
    archive = f"librashader-x86_64-windows-v{VERSION}-optimized.zip" if windows else ARCHIVE
    return dict(archive=archive, sha256="521fe0f364bfa705883f9e99fb1733a3a24f0f403bcc5591b6b78f6cff289183" if windows else SHA256,
                files=WINDOWS_FILES if windows else FILE_SHA256,
                url=f"{UPSTREAM}/releases/download/{TAG}/{archive}" if windows else URL,
                platform="x86_64-windows" if windows else "aarch64-macos",
                library="librashader.dll" if windows else "librashader.dylib",
                max_extract_bytes=160 * 1024 * 1024 if windows else MAX_EXTRACT_BYTES)


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def check_archive(path, config=None):
    config = config or runtime_config()
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"Archive must be a regular file: {path}")
    if path.stat().st_size > MAX_DOWNLOAD_BYTES or digest(path) != config["sha256"]:
        raise ValueError(f"Archive checksum mismatch: {path}; expected SHA-256 {config['sha256']}")


def checked_members(archive, config=None):
    """Validate the complete ZIP before writing any of its members."""
    config = config or runtime_config()
    members = archive.infolist()
    names = set()
    total = 0
    for item in members:
        name = item.filename
        relative = PurePosixPath(name)
        mode = item.external_attr >> 16
        if (relative.is_absolute() or ".." in relative.parts or "\\" in name
                or ":" in name or "\x00" in name or name != relative.as_posix()
                or not name or name in names or item.is_dir()
                or (stat.S_IFMT(mode) not in (0, stat.S_IFREG))):
            raise ValueError(f"Unsafe or duplicate archive member: {name!r}")
        names.add(name)
        total += item.file_size
        if total > config["max_extract_bytes"]:
            raise ValueError("Archive exceeds the extraction size limit")
    if names != set(config["files"]):
        raise ValueError(f"Unexpected archive contents: {sorted(names)}")
    return members


def validate_installation(destination, config=None):
    config = config or runtime_config()
    if destination.is_symlink() or not destination.is_dir():
        raise ValueError(f"Installation path is not a regular directory: {destination}")
    manifest_path = destination / MANIFEST
    if manifest_path.is_symlink():
        raise ValueError(f"Installation manifest must not be a symlink: {manifest_path}")
    record = json.loads(manifest_path.read_text())
    if (record.get("version") != VERSION or record.get("archive_sha256") != config["sha256"]
            or record.get("archive_url") != config["url"] or record.get("platform") != config["platform"]
            or record.get("files") != config["files"]):
        raise ValueError("Installation manifest does not match the pinned runtime")
    for name, expected in record["files"].items():
        path = destination / name
        if path.is_symlink() or not path.is_file() or digest(path) != expected:
            raise ValueError(f"Installed file differs from its manifest: {path}")


def download(cache, config=None):
    config = config or runtime_config()
    if cache.exists() or cache.is_symlink():
        check_archive(cache, config)
        return
    cache.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=f".{config['archive']}.", suffix=".part",
                                         dir=cache.parent, delete=False) as output:
            temporary = Path(output.name)
            request = Request(config["url"], headers={"User-Agent": "AfterBurner2-Recomp-dependency-fetcher"})
            with urlopen(request, timeout=60) as response:
                total = 0
                while chunk := response.read(1024 * 1024):
                    total += len(chunk)
                    if total > MAX_DOWNLOAD_BYTES:
                        raise ValueError("Download exceeds the size limit")
                    output.write(chunk)
        check_archive(temporary, config)
        # A second concurrent fetch may have completed while this one downloaded.
        if cache.exists() or cache.is_symlink():
            check_archive(cache, config)
        else:
            # Hard-link publication never replaces another process's cache file.
            try:
                os.link(temporary, cache)
            except FileExistsError:
                check_archive(cache, config)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def install(archive_path, destination, config=None):
    config = config or runtime_config()
    check_archive(archive_path, config)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".librashader-stage-", dir=destination.parent) as folder:
        staging = Path(folder) / "runtime"
        staging.mkdir()
        with zipfile.ZipFile(archive_path) as archive:
            members = checked_members(archive, config)
            for item in members:
                path = staging / item.filename
                with archive.open(item) as source, path.open("xb") as output:
                    shutil.copyfileobj(source, output)
                path.chmod(0o644)
        record = {
            "version": VERSION,
            "platform": config["platform"],
            "archive_url": config["url"],
            "archive_sha256": config["sha256"],
            "source_url": SOURCE_URL,
            "runtime_license": "MPL-2.0 OR GPL-3.0-only; use MPL-2.0 when redistributing with this project",
            "runtime_license_url": LICENSE_URL,
            "public_header_license": "MIT; notice retained inside librashader.h",
            "files": {name: digest(staging / name) for name in sorted(config["files"])},
        }
        (staging / MANIFEST).write_text(json.dumps(record, indent=2) + "\n")
        validate_installation(staging, config)
        # os.rename refuses to replace a populated installation directory. Never
        # repair or delete an existing dependency tree implicitly.
        if destination.exists() or destination.is_symlink():
            raise ValueError(f"Installation path appeared during fetch; rerun to validate it: {destination}")
        staging.rename(destination)


def main(argv=None):
    default = Path(__file__).resolve().parents[2] / "build" / "deps" / "librashader"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=default,
                        help="local installation directory (default: build/deps/librashader)")
    parser.add_argument("--archive", type=Path,
                        help="reuse a local release ZIP after checking its pinned SHA-256")
    args = parser.parse_args(argv)
    windows = platform.system() == "Windows" and platform.machine().lower() in ("amd64", "x86_64")
    macos = platform.system() == "Darwin" and platform.machine().lower() in ("arm64", "aarch64")
    if not (windows or macos):
        parser.error("this optional fetcher currently supports Apple Silicon macOS or Windows x64; "
                     "other platforms need a matching librashader runtime and presentation backend")
    config = runtime_config(windows)
    destination = args.destination.expanduser().absolute()
    try:
        if destination.exists() or destination.is_symlink():
            try:
                validate_installation(destination, config)
            except (OSError, ValueError, KeyError, TypeError) as error:
                raise ValueError(f"Existing installation was not changed: {destination}: {error}. "
                                 "Move it aside or choose another --destination to fetch afresh.") from error
            print(f"Validated librashader {VERSION}: {destination / config['library']}")
            return 0
        if args.archive:
            archive_path = args.archive.expanduser().absolute()
            check_archive(archive_path, config)
        else:
            archive_path = destination.parent / config["archive"]
            print(f"Fetching optional librashader {VERSION} from {config['url']}", flush=True)
            download(archive_path, config)
        install(archive_path, destination, config)
        print(f"Installed librashader {VERSION}: {destination / config['library']}")
        print(f"Source and license metadata: {destination / MANIFEST}")
        return 0
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        print(f"librashader fetch failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
