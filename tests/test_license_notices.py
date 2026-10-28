#!/usr/bin/env python3
"""Keep central notices intact and reject incomplete package notice inputs."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_macos


class LicenseNotices(unittest.TestCase):
    def test_central_copies_match_vendored_notices(self):
        copies = {
            "dr_flac.txt": "src/third_party/dr_flac/LICENSE",
            "miniz.txt": "src/third_party/miniz/LICENSE",
            "SDL_GameControllerDB.txt": "src/main/frontend/controllerdb/LICENSE",
            "Snes9x.txt": "src/main/frontend/scalers/SNES9X-LICENSE.txt",
            "MPL-2.0.txt": "src/third_party/librashader/LICENSE.MPL-2.0",
        }
        for dest, source in copies.items():
            with self.subTest(dest=dest):
                self.assertEqual((ROOT / "LICENSES" / dest).read_bytes(),
                                 (ROOT / source).read_bytes())
        for dest, source in {
            "Z80.txt": "src/third_party/z80.h",
            "ScaleFX.txt": "src/main/frontend/scalers/scalefx.hpp",
            "librashader-header.txt": "src/third_party/librashader/librashader.h",
        }.items():
            with self.subTest(dest=dest):
                notice = (ROOT / "LICENSES" / dest).read_text().rstrip("\n")
                self.assertIn(notice, (ROOT / source).read_text())
                self.assertIn("Copyright", notice)
                self.assertIn("*/", notice)

    def test_inventory_links_and_required_files(self):
        with patch.object(package_macos, "committed_source"):
            package_macos.check_project_notices()
        text = (ROOT / "LICENSES/README.md").read_text()
        for target in re.findall(r"\]\(([^)]+)\)", text):
            if not target.startswith("https://"):
                self.assertTrue((ROOT / "LICENSES" / target).is_file(), target)

    def test_missing_central_notice_stops_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(package_macos, "ROOT", Path(directory)):
                with self.assertRaises(SystemExit):
                    package_macos.check_project_notices()

    def test_invalid_external_notice_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            empty = root / "empty"
            empty.write_text(" \n")
            valid = root / "COPYING"
            valid.write_text("Complete compiler runtime notice\n")
            link = root / "link"
            link.symlink_to(valid)
            for path in [None, root / "missing", root, empty, link]:
                with self.subTest(path=path), self.assertRaises(SystemExit):
                    package_macos.require_notice(path)
            package_macos.require_notice(valid)

    def test_native_cli_requires_notice_before_creating_package(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools/package_windows.py"),
             "v0.0.0-license-check", "--native-build", "unused"],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("Native builds require --mingw-notice", result.stderr)


if __name__ == "__main__":
    unittest.main()
