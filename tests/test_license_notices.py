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

    def test_current_distribution_excludes_restricted_components(self):
        historical = {"Cannonball.txt", "CannonBall-SE.txt", "MAME-Legacy.txt",
                      "Snes9x.txt", "AfterBurner2-original-noncommercial.txt"}
        self.assertFalse(historical.intersection(package_macos.PROJECT_NOTICES))
        for name in historical:
            self.assertTrue((ROOT / "LICENSES" / name).is_file())
        for path in ("src/main/frontend/scalers/sai.hpp",
                     "src/main/frontend/scalers/2xsai.reference.cpp",
                     "src/main/hwaudio/soundchip.cpp",
                     "src/main/hwaudio/segapcm.cpp", "src/main/hwaudio/ym2151.cpp"):
            self.assertFalse((ROOT / path).exists(), path)
            for build_file in ("CMakeLists.txt", "Makefile"):
                self.assertNotIn(path, (ROOT / build_file).read_text())
        for path in (ROOT / "src/main/frontend/scalers").glob("sprite_pass*.comp"):
            self.assertNotRegex(path.read_text(), r"(?i)snes9x|2xsai|super2xsai")
        self.assertEqual((ROOT / "LICENSE.txt").read_bytes(),
                         (ROOT / "LICENSES/GCC-GPL-3.0.txt").read_bytes())
        self.assertIn("GPL-3.0-or-later", (ROOT / "COPYRIGHT.md").read_text())

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
