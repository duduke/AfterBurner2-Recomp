#!/usr/bin/env python3
"""Offline integrity and extraction checks for the optional Slang dependency."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("fetch_librashader", ROOT / "tools/deps/fetch_librashader.py")
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


class FetchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="librashader-offline-")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.files = {
            "librashader.a": b"fake static runtime",
            "librashader.dylib": b"fake dynamic runtime",
            "librashader.h": b"fake MIT public header",
        }
        self.archive = self.work / "runtime.zip"
        self.make_archive(self.archive, self.files.items())
        self.expected = {name: hashlib.sha256(data).hexdigest() for name, data in self.files.items()}
        # Keep the real format and verification paths, replacing only the
        # release pins so small inert fixtures work on every host, offline.
        for key, value in (("SHA256", fetch.digest(self.archive)), ("FILE_SHA256", self.expected)):
            patcher = patch.object(fetch, key, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.network = patch.object(fetch, "urlopen", side_effect=AssertionError("unexpected network access"))
        self.urlopen = self.network.start()
        self.addCleanup(self.network.stop)
        for name, result in (("system", "Darwin"), ("machine", "arm64")):
            patcher = patch.object(fetch.platform, name, return_value=result)
            patcher.start()
            self.addCleanup(patcher.stop)

    @staticmethod
    def make_archive(path, members):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(path, "w") as archive:
                for name, data in members:
                    archive.writestr(name, data)

    def main(self, *arguments):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as errors:
            result = fetch.main(list(arguments))
        return result, errors.getvalue()

    def test_checksum_rejected_before_extraction(self):
        self.archive.write_bytes(self.archive.read_bytes() + b"changed")
        target = self.work / "target"
        with patch.object(fetch.zipfile, "ZipFile", side_effect=AssertionError("unverified ZIP opened")):
            result, message = self.main("--archive", str(self.archive), "--destination", str(target))
        self.assertEqual(result, 1)
        self.assertIn("checksum mismatch", message)
        self.assertFalse(target.exists())
        self.assertFalse(list(self.work.glob(".librashader-stage-*")))

    def test_archive_paths_extras_duplicates_and_links_rejected(self):
        link = zipfile.ZipInfo("librashader.dylib")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        fixtures = [
            [("../escape", b"bad")], [("/absolute", b"bad")],
            [("C:/drive", b"bad")], [("back\\slash", b"bad")],
            [("a/../escape", b"bad")], [("a//normalized", b"bad")],
            [*self.files.items(), ("unexpected", b"bad")],
            [*self.files.items(), ("librashader.h", b"duplicate")],
            [(link, b"/tmp/outside")], [("directory/", b"")],
        ]
        for index, members in enumerate(fixtures):
            with self.subTest(index=index):
                path = self.work / "unsafe.zip"
                self.make_archive(path, members)
                with zipfile.ZipFile(path) as archive:
                    with self.assertRaises(ValueError):
                        fetch.checked_members(archive)

    def test_extract_limit_and_missing_members_rejected(self):
        with patch.object(fetch, "MAX_EXTRACT_BYTES", 1), zipfile.ZipFile(self.archive) as archive:
            with self.assertRaisesRegex(ValueError, "extraction size limit"):
                fetch.checked_members(archive)
        incomplete = self.work / "incomplete.zip"
        self.make_archive(incomplete, [("librashader.h", b"header")])
        with zipfile.ZipFile(incomplete) as archive:
            with self.assertRaisesRegex(ValueError, "Unexpected archive contents"):
                fetch.checked_members(archive)

    def test_successful_staging_manifest_integrity_and_ready_reuse(self):
        target = self.work / "target"
        result, _ = self.main("--archive", str(self.archive), "--destination", str(target))
        self.assertEqual(result, 0)
        fetch.validate_installation(target)
        record = json.loads((target / fetch.MANIFEST).read_text())
        self.assertEqual(record["files"], self.expected)
        self.assertEqual(record["archive_url"], fetch.URL)
        self.assertEqual(record["source_url"], fetch.SOURCE_URL)
        self.assertEqual(record["runtime_license_url"], fetch.LICENSE_URL)
        for name, data in self.files.items():
            self.assertEqual((target / name).read_bytes(), data)
        before = {p.name: p.read_bytes() for p in target.iterdir()}
        self.assertEqual(self.main("--destination", str(target))[0], 0)
        self.assertEqual({p.name: p.read_bytes() for p in target.iterdir()}, before)
        self.assertFalse(list(self.work.glob(".librashader-stage-*")))
        self.urlopen.assert_not_called()

    def test_tampered_installation_and_manifest_left_untouched(self):
        target = self.work / "target"
        fetch.install(self.archive, target)
        changed = b"changed runtime"
        (target / "librashader.dylib").write_bytes(changed)
        record = json.loads((target / fetch.MANIFEST).read_text())
        record["files"]["librashader.dylib"] = hashlib.sha256(changed).hexdigest()
        (target / fetch.MANIFEST).write_text(json.dumps(record))
        before = {p.name: p.read_bytes() for p in target.iterdir()}
        result, message = self.main("--archive", str(self.archive), "--destination", str(target))
        self.assertEqual(result, 1)
        self.assertIn("Existing installation was not changed", message)
        self.assertEqual({p.name: p.read_bytes() for p in target.iterdir()}, before)
        self.urlopen.assert_not_called()

    def test_existing_destination_never_overwritten(self):
        target = self.work / "target"
        target.mkdir()
        marker = target / "keep.txt"
        marker.write_text("user content")
        with self.assertRaisesRegex(ValueError, "Installation path appeared"):
            fetch.install(self.archive, target)
        self.assertEqual(list(target.iterdir()), [marker])
        self.assertEqual(marker.read_text(), "user content")
        self.assertFalse(list(self.work.glob(".librashader-stage-*")))

    def test_verified_download_and_cache_reuse(self):
        cache = self.work / "downloads" / fetch.ARCHIVE
        self.urlopen.side_effect = None
        self.urlopen.return_value = io.BytesIO(self.archive.read_bytes())
        fetch.download(cache)
        self.assertEqual(cache.read_bytes(), self.archive.read_bytes())
        self.urlopen.assert_called_once()
        fetch.download(cache)
        self.urlopen.assert_called_once()
        self.assertFalse(list(cache.parent.glob("*.part")))

    def test_corrupt_download_and_existing_cache_are_not_published(self):
        cache = self.work / "downloads" / fetch.ARCHIVE
        self.urlopen.side_effect = None
        self.urlopen.return_value = io.BytesIO(b"incorrect release")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            fetch.download(cache)
        self.assertFalse(cache.exists())
        self.assertFalse(list(cache.parent.iterdir()))
        cache.write_bytes(b"preserve corrupt cache for inspection")
        self.urlopen.reset_mock()
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            fetch.download(cache)
        self.assertEqual(cache.read_bytes(), b"preserve corrupt cache for inspection")
        self.urlopen.assert_not_called()

    def test_symlinked_archive_and_installed_files_rejected(self):
        alias = self.work / "alias.zip"
        try:
            alias.symlink_to(self.archive)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"host cannot create test symlinks: {error}")
        with self.assertRaisesRegex(ValueError, "regular file"):
            fetch.check_archive(alias)
        target = self.work / "target"
        fetch.install(self.archive, target)
        runtime = target / "librashader.dylib"
        runtime.unlink()
        outside = self.work / "outside"
        outside.write_bytes(self.files["librashader.dylib"])
        runtime.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, "differs from its manifest"):
            fetch.validate_installation(target)
        self.assertEqual(outside.read_bytes(), self.files["librashader.dylib"])

    def test_unsupported_platform_has_clear_diagnostic(self):
        with patch.object(fetch.platform, "system", return_value="Linux"):
            with self.assertRaises(SystemExit) as stopped, contextlib.redirect_stderr(io.StringIO()) as errors:
                fetch.main([])
        self.assertEqual(stopped.exception.code, 2)
        self.assertIn("Apple Silicon macOS only", errors.getvalue())
        self.urlopen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
