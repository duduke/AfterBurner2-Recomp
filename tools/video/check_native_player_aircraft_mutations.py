#!/usr/bin/env python3
"""Retain controls for HD player-aircraft sampling and native coverage."""

from datetime import datetime, timezone
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import uuid


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/main/frontend/native_player_aircraft.hpp").read_text()
PACK_SOURCE = (ROOT / "src/main/frontend/aircraft_pack.hpp").read_text()
MUTATIONS = [
    (
        "canvas-padding",
        "extent={float(left),float(top),float(right-left+1),float(bottom-top+1)};",
        "extent={0,0,float(rgba->w),float(rgba->h)};",
        "aircraft alpha bounds or viewport origin changed",
    ),
    (
        "no-premultiplication",
        "pixel[c]=uint8_t((unsigned(pixel[c])*alpha+127)/255);",
        "pixel[c]=pixel[c];",
        "aircraft alpha was darkened or blended twice",
    ),
    (
        "no-viewport-origin",
        "const float origin=float(viewport_width-320)/2;",
        "const float origin=0;",
        "aircraft alpha bounds or viewport origin changed",
    ),
    (
        "no-reflection",
        "const auto flip=SDL_FlipMode((item.flip_x?SDL_FLIP_HORIZONTAL:0)|(item.flip_y?SDL_FLIP_VERTICAL:0));",
        "const auto flip=SDL_FLIP_NONE;",
        "aircraft texture reflection changed",
    ),
    (
        "ignored-ownership",
        "if(!scene.visibility[index]){++x;continue;}",
        "if(false){++x;continue;}",
        "aircraft filled a native ownership hole",
    ),
    (
        "ignored-partial-rows",
        "for(unsigned y=item.top;y<=item.bottom;++y)",
        "for(unsigned y=unsigned(item.y);y<unsigned(item.y)+item.height;++y)",
        "aircraft ignored partial native row clip",
    ),
]
PACK_MUTATIONS = [
    ("registered-pivot", "native_player_aircraft.hpp",
     "const float sample_x=found->registration.art_pivot.x-native_pivot.x*scale.x;",
     "const float sample_x=found->registration.art_pivot.x;",
     "registered aircraft art/native pivot did not set the authored silhouette origin"),
    ("registered-source-bounds", "native_player_aircraft.hpp",
     "x1=std::min(float(end),art_right);", "x1=float(end);",
     "registered aircraft sampled outside explicit source bounds"),
    ("registered-pose-dispatch", "native_player_aircraft.hpp",
     "pack_textures[pose_index].get()", "pack_textures[0].get()",
     "registered aircraft selected the wrong pose texture"),
    ("registered-lod", "native_player_aircraft.hpp",
     "if(lod!=found->lods.end() && lod->native_pivot.x<=item.source_width && lod->native_pivot.y<=item.source_height) {", "if(false) {",
     "aircraft ignored explicit LOD pivot or scale"),
    ("registered-lod-fallback", "native_player_aircraft.hpp",
     "lod->native_pivot.x<=item.source_width && lod->native_pivot.y<=item.source_height", "true",
     "invalid ROM-sized LOD override did not retain base registration"),
    ("registered-reflection", "native_player_aircraft.hpp",
     "const auto reflected=SDL_FlipMode((item.flip_x?SDL_FLIP_HORIZONTAL:0)|(item.flip_y?SDL_FLIP_VERTICAL:0));",
     "const auto reflected=SDL_FLIP_NONE;",
     "registered aircraft pivot, source padding or flipped alignment changed"),
    ("registered-ownership", "native_player_aircraft.hpp",
     "if(!scene.visibility[item.mask_offset+row*320+column]){++column;continue;}",
     "if(false){++column;continue;}",
     "registered aircraft filled a native ownership hole"),
    ("registered-partial-rows", "native_player_aircraft.hpp",
     "for(unsigned row=item.top;row<=item.bottom;++row)",
     "for(unsigned row=unsigned(item.y);row<=item.bottom;++row)",
     "registered aircraft ignored partial native row clip"),
    ("registered-image-bounds", "native_player_aircraft.hpp",
     "if(pose.source.x+pose.source.w>loaded->w || pose.source.y+pose.source.h>loaded->h)",
     "if(false)", "out-of-image source bounds accepted"),
    ("registered-transaction", "native_player_aircraft.hpp",
     "error.clear();\n        try {", "error.clear();pack_poses.clear();\n        try {",
     "rejected pack changed registered pose availability"),
    ("registered-nozzle-bounds", "aircraft_pack.hpp",
     "if(!inside(nozzle.art,found->source))", "if(false)",
     "out-of-source nozzle accepted"),
    ("registered-duplicate-pose", "aircraft_pack.hpp",
     "if(std::any_of(poses.begin(),poses.end(),[&](const auto& other){return other.root_frame_address==pose.root_frame_address;}))",
     "if(false)", "duplicate aircraft pose accepted"),
    ("registered-tiny-scale", "aircraft_pack.hpp",
     "value.x<1.0f/4096 || value.y<1.0f/4096", "value.x<=0 || value.y<=0",
     "tiny aircraft registration scale accepted"),
    ("software-reflected-detail", "native_player_aircraft.hpp",
     "if(software_renderer && flip!=SDL_FLIP_NONE)", "if(false)",
     "reflected HD aircraft lost detail inside a native mask cell"),
    ("software-fractional-raster", "native_player_aircraft.hpp",
     "if(software_renderer) {\n            if(!quantize_axis(", "if(false) {\n            if(!quantize_axis(",
     "fractional HD aircraft lost an owned output pixel"),
]


def main():
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    logs = ROOT / "build/oracle/native-player-aircraft-mutations" / f"{stamp}-{uuid.uuid4().hex[:8]}"
    logs.mkdir(parents=True)
    pkg_command = shlex.split(os.environ.get("PKG_CONFIG", "pkg-config"))
    with (logs / "pkg-config.log").open("w") as log:
        package = subprocess.run(
            [*pkg_command, "--cflags", "--libs", "sdl3"], cwd=ROOT,
            stdout=subprocess.PIPE, stderr=log, text=True, timeout=30,
        )
    if package.returncode:
        raise SystemExit(f"Native aircraft SDL flags unavailable; see {logs / 'pkg-config.log'}")
    sdl_flags = shlex.split(package.stdout)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy"}

    # Each helper invocation owns an include overlay and never rewrites the
    # shared source. Retain generated headers alongside the complete logs.
    with tempfile.TemporaryDirectory(prefix="native-aircraft-mutations-", dir=ROOT / "build") as temporary:
        overlay = Path(temporary)
        (overlay / "frontend").mkdir()

        def build_and_run(name, source, pack_source=PACK_SOURCE):
            (logs / f"{name}.hpp").write_text(source)
            (overlay / "frontend/native_player_aircraft.hpp").write_text(source)
            (overlay / "frontend/aircraft_pack.hpp").write_text(pack_source)
            binary = overlay / name
            command = [
                *compiler, *flags, "-I" + str(overlay), "-I" + str(ROOT / "src/main"),
                str(ROOT / "tests/native_player_aircraft_regression.cpp"),
                "-o", str(binary), *sdl_flags,
            ]
            build_log = logs / f"{name}.build.log"
            with build_log.open("w") as log:
                log.write(shlex.join(command) + "\n")
                log.flush()
                build = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            if build.returncode:
                raise SystemExit(f"Native aircraft {name} build failed; see {build_log}")
            run_log = logs / f"{name}.run.log"
            with run_log.open("w") as log:
                result = subprocess.run(
                    [str(binary)], cwd=ROOT, env=environment,
                    stdout=log, stderr=subprocess.STDOUT, timeout=30,
                )
            return result.returncode, run_log

        status, baseline_log = build_and_run("baseline", SOURCE)
        if status:
            raise SystemExit(f"Native aircraft mutation baseline failed; see {baseline_log}")
        print(f"Native aircraft ROM-free baseline passed; full log: {baseline_log.relative_to(ROOT)}")

        for name, before, after, expected in MUTATIONS:
            if SOURCE.count(before) != 1:
                raise SystemExit(f"Native aircraft mutation site changed: {name}")
            status, run_log = build_and_run(name, SOURCE.replace(before, after))
            if status != 1 or expected not in run_log.read_text():
                raise SystemExit(f"Native aircraft mutation did not reject as expected: {name}; see {run_log}")
            print(f"Native aircraft mutation rejected: {name}; full log: {run_log.relative_to(ROOT)}")

        for name, filename, before, after, expected in PACK_MUTATIONS:
            source = SOURCE if filename == "native_player_aircraft.hpp" else PACK_SOURCE
            if source.count(before) != 1:
                raise SystemExit(f"Native aircraft mutation site changed: {name}")
            mutated = source.replace(before, after)
            (logs / f"{name}.hpp").write_text(mutated)
            status, run_log = build_and_run(name, mutated, PACK_SOURCE) if filename == "native_player_aircraft.hpp" else build_and_run(name, SOURCE, mutated)
            if status != 1 or expected not in run_log.read_text():
                raise SystemExit(f"Native aircraft mutation did not reject as expected: {name}; see {run_log}")
            print(f"Native aircraft mutation rejected: {name}; full log: {run_log.relative_to(ROOT)}")

    count = len(MUTATIONS) + len(PACK_MUTATIONS)
    print(f"Native aircraft mutations: {count} of {count} rejected, 0 skipped; ROM-free baseline passed.")


if __name__ == "__main__":
    main()
