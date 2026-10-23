#!/usr/bin/env python3
"""Retained negative controls for passive player-art identity and ownership."""

from datetime import datetime, timezone
from pathlib import Path
import json
import os
import shlex
import subprocess
import tempfile
import uuid


ROOT = Path(__file__).resolve().parents[2]
HEADERS = {
    name: (ROOT / "src/main/hwvideo" / name).read_text()
    for name in ("player_aircraft.hpp", "wide_scene.hpp")
}
MUTATIONS = [
    ("palette-identity", "player_aircraft.hpp", "(d[6]&255)!=0xf2", "false",
     "nonmatching descriptor lost Original fallback"),
    ("source-start", "player_aircraft.hpp", "if(d[1]!=start)continue;", "if(false)continue;",
     "neutral family direction or LOD did not match"),
    ("horizontal-flip", "player_aircraft.hpp", "backwards!=leftwards", "backwards==leftwards",
     "player source flips disagree with hardware directions"),
    ("partial-frontier", "player_aircraft.hpp", "unsigned(clip_top),unsigned(clip_bottom)",
     "unsigned(clip_top),unsigned(clip_bottom-1)", "partial-render clipping moved the player origin"),
    ("native-suppression", "wide_scene.hpp", "player_bank[selected*16]|=0x40;",
     "player_bank[selected*16]|=0;", "detachment did not suppress the original aircraft"),
    ("later-raw-owner", "wide_scene.hpp", "visibility[i]=0;", "visibility[i]=visibility[i];",
     "later raw sprite did not occlude replacement"),
    ("custom-hud-priority", "wide_scene.hpp", "priority[wide]|text_priority[i]", "priority[wide]|0",
     "custom HUD detachment changed native text ownership over player"),
    ("detached-hud-conflict", "wide_scene.hpp", "!original_hud && priority_mark>4", "false",
     "detached HUD priority conflict lost Original fallback"),
    ("shadow-fallback", "wide_scene.hpp", "if(unsupported_effect || std::none_of(visibility.begin()",
     "if(false || std::none_of(visibility.begin()", "later shadow overlap lost Original fallback"),
    ("translucent-fallback", "wide_scene.hpp", "player_translucent_effects&(1u<<unsigned(kind))", "0u",
     "translucent overlap lost Original fallback"),
    ("registered-root-admission", "player_aircraft.hpp",
     "std::find(supported_roots.begin(),supported_roots.end(),frame.root)==supported_roots.end()", "false",
     "missing authored pose lost Original fallback"),
    ("gentle-rom-pointers", "player_aircraft.hpp",
     "pointer(table-16)==0x10230 && pointer(table+16)==0x10230", "true",
     "inconsistent gentle-bank ROM pointers were accepted"),
    ("gentle-root-identity", "player_aircraft.hpp", ",0,frame.root};", ",0,0x100a0};",
     "gentle-bank ROM family identity changed"),
    ("native-boundary-fallback", "wide_scene.hpp",
     "if(draw.x<0 || draw.x+int(draw.width)>320)return bank;", "if(false)return bank;",
     "aircraft crossing native horizontal boundary lost Original fallback"),
]


def main():
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    logs = ROOT / "build/oracle/player-aircraft-mutations" / f"{stamp}-{uuid.uuid4().hex[:8]}"
    logs.mkdir(parents=True)
    (logs / "manifest.json").write_text(json.dumps(MUTATIONS, indent=2) + "\n")
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", "-std=c++20 -Wall -Wextra -O2"))
    sources = [
        "tests/player_aircraft_presentation.cpp", "src/main/romloader.cpp",
        "src/main/hwvideo/hwroad.cpp", "src/main/hwvideo/hwpalette.cpp",
        "src/main/hwvideo/hwtiles.cpp", "src/main/hwvideo/hwsprites.cpp",
    ]
    with tempfile.TemporaryDirectory(prefix="player-aircraft-mutations-", dir=ROOT / "build") as temporary:
        overlay = Path(temporary)
        (overlay / "hwvideo").mkdir()

        def build_and_run(name, replacements):
            for filename, source in HEADERS.items():
                (overlay / "hwvideo" / filename).write_text(replacements.get(filename, source))
            binary = overlay / name
            command = [
                *compiler, *flags, "-DAB2_PLAYER_AIRCRAFT_SYNTHETIC", "-pthread",
                "-I" + str(overlay), "-I" + str(ROOT / "src/main"),
                *(str(ROOT / path) for path in sources), "-o", str(binary),
            ]
            build_log = logs / f"{name}.build.log"
            with build_log.open("w") as log:
                log.write(shlex.join(command) + "\n")
                log.flush()
                built = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=120)
            if built.returncode:
                raise SystemExit(f"Player aircraft {name} build failed; see {build_log}")
            run_log = logs / f"{name}.run.log"
            with run_log.open("w") as log:
                result = subprocess.run([str(binary), "--synthetic"], cwd=ROOT,
                                        stdout=log, stderr=subprocess.STDOUT, timeout=30)
            return result.returncode, run_log

        status, baseline_log = build_and_run("baseline", {})
        if status:
            raise SystemExit(f"Player aircraft mutation baseline failed; see {baseline_log}")
        print(f"Player aircraft ROM-free baseline passed; full log: {baseline_log.relative_to(ROOT)}", flush=True)
        for name, filename, before, after, expected in MUTATIONS:
            source = HEADERS[filename]
            if source.count(before) != 1:
                raise SystemExit(f"Player aircraft mutation site changed: {name}")
            (logs / f"{name}.hpp").write_text(source.replace(before, after))
            status, run_log = build_and_run(name, {filename: source.replace(before, after)})
            if status != 1 or expected not in run_log.read_text():
                raise SystemExit(f"Player aircraft mutation did not reject as expected: {name}; see {run_log}")
            print(f"Player aircraft mutation rejected: {name}; full log: {run_log.relative_to(ROOT)}", flush=True)
    print(f"Player aircraft mutations: {len(MUTATIONS)} of {len(MUTATIONS)} rejected, 0 skipped; ROM-free baseline passed.")


if __name__ == "__main__":
    main()
