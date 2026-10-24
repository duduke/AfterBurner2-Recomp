"""Retain negative controls for passive HUD ownership and replacement underlays."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
logs = root / "build/oracle"
logs.mkdir(parents=True, exist_ok=True)
headers = {
    name: (root / "src/main/hwvideo" / name).read_text()
    for name in ("hud_scene.hpp", "wide_scene.hpp")
}
mutations = [
    ("category", "hud_scene.hpp", "(entry&0x8000)", "(entry&0x0000)",
     "HUD ownership disagrees with native sprite priority"),
    ("priority", "hud_scene.hpp", "((native_sprite>>12)&3)!=3", "((native_sprite>>12)&3)!=2",
     "HUD ownership disagrees with native sprite priority"),
    ("shadow", "hud_scene.hpp", "? 1 : 2", "? 2 : 1",
     "HUD ownership disagrees with native sprite priority"),
    ("visible-columns", "wide_scene.hpp", "c.text.begin()+y*64+24", "c.text.begin()+y*64+23",
     "HUD text or per-frontier palette changed"),
    ("effects-palette", "wide_scene.hpp", "hud.colors[64+i]=argb(palette.pen(0x1c00+i+palette.num_entries()))",
     "hud.colors[64+i]=argb(palette.pen(0x1c00+i))", "HUD effects palette missing"),
    ("partial-bottom", "wide_scene.hpp", "hud.top=c.top;hud.bottom=c.bottom;",
     "hud.top=c.top;hud.bottom=c.bottom-1;", "HUD partial glyph clipping lost"),
    ("baked-text", "wide_scene.hpp", "if(original_hud){", "if(true){",
     "original glyph remained in replacement underlay"),
    ("same-frame-cache", "wide_scene.hpp", "if(frame<last_frame || original_hud!=last_original_hud)reset();\n        if(frame==last_frame)return;",
     "if(frame<last_frame)reset();\n        if(frame==last_frame)return;", "original glyph remained in replacement underlay"),
    ("native-bank", "wide_scene.hpp", "decode(c.sprites,sprite_rom,c.top,c.bottom,assets,",
     "decode(bank,sprite_rom,c.top,c.bottom,assets,", "detached sprite changed native HUD ownership"),
    ("disabled-display", "wide_scene.hpp", "hud_occlusion_pixels.begin()+(c.bottom+1)*320,2)",
     "hud_occlusion_pixels.begin()+(c.bottom+1)*320,0)", "disabled display exposed HUD"),
]

with tempfile.TemporaryDirectory(prefix="hud-mutations-", dir=root / "build") as directory:
    overlay = Path(directory)
    (overlay / "hwvideo").mkdir()

    def build_and_run(name, replacements):
        for filename, source in headers.items():
            (overlay / "hwvideo" / filename).write_text(replacements.get(filename, source))
        binary = overlay / name
        command = shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++20", "-O2", "-I" + str(overlay), "-I" + str(root / "src/main"),
            str(root / "tests/hud_scene_regression.cpp"), str(root / "src/main/romloader.cpp"),
            str(root / "src/main/hwvideo/hwroad.cpp"), str(root / "src/main/hwvideo/hwpalette.cpp"),
            str(root / "src/main/hwvideo/hwtiles.cpp"), "-o", str(binary),
        ]
        build = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=120)
        (logs / f"hud_mutation_{name}.build.log").write_text(build.stdout + build.stderr)
        if build.returncode:
            raise SystemExit(f"HUD {name} build failed; see {logs / f'hud_mutation_{name}.build.log'}")
        result = subprocess.run([str(binary)], cwd=root, capture_output=True, text=True, timeout=30)
        (logs / f"hud_mutation_{name}.log").write_text(result.stdout + result.stderr)
        return result

    baseline = build_and_run("baseline", {})
    if baseline.returncode:
        raise SystemExit(f"HUD mutation baseline failed; see {logs / 'hud_mutation_baseline.log'}")
    for name, filename, before, after, expected in mutations:
        source = headers[filename]
        if source.count(before) != 1:
            raise SystemExit(f"HUD mutation site changed: {name}")
        result = build_and_run(name, {filename: source.replace(before, after)})
        if result.returncode == 0 or expected not in result.stderr:
            raise SystemExit(f"HUD mutation did not reject as expected: {name}; see {logs / f'hud_mutation_{name}.log'}")
        print(f"HUD mutation rejected: {name}")
print(f"HUD mutations: {len(mutations)} of {len(mutations)} rejected, 0 skipped; ROM-free baseline passed.")
