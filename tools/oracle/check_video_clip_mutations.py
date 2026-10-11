"""Prove clip bounds and category priority comparisons reject changed behavior."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
(root / "build").mkdir(exist_ok=True)
sources = root / "src/main/hwvideo"
mutations = [
    ("hwroad.cpp", "y <= std::min(height-1,", "y < std::min(height-1,"),
    ("hwtiles.cpp", "priority_mark << entry_category", "priority_mark << 1"),
]
with tempfile.TemporaryDirectory(prefix="video-clips-", dir=root / "build") as directory:
    overlay = Path(directory)
    for name, before, after in mutations:
        text = (sources / name).read_text()
        if text.count(before) != 2:
            raise SystemExit(f"Mutation site changed: {name}")
        mutant = overlay / name
        mutant.write_text(text.replace(before, after))
        binary = overlay / "mutant"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2",
                        "-I" + str(root / "src/main"), str(root / "tests/video_clip_regression.cpp"),
                        str(mutant if name == "hwroad.cpp" else sources / "hwroad.cpp"),
                        str(mutant if name == "hwtiles.cpp" else sources / "hwtiles.cpp"),
                        "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if result.returncode == 0 or not any(message in result.stderr for message in
                ("clip differs from full raster", "combined categories differ")):
            raise SystemExit(f"Mutation did not reject: {name}: {result.stderr}")
        print(f"Video clip mutation passed: {name} change rejected")
