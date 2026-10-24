#!/usr/bin/env python3
"""Matched snapshot/integrated application runs; retain logs and final images."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--shader", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=1200)
    parser.add_argument("--stage", type=int, default=1)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.frames < 180 or args.stage < 1:
        parser.error("use at least 180 frames and a positive Practice stage")
    binary, shader = args.binary.resolve(strict=True), args.shader.resolve(strict=True)
    runtime = args.runtime.resolve(strict=True)
    output = args.output or ROOT / "build/oracle/integrated-frontend-perf" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ)
    # Keep external controller state from changing the paired trajectories.
    for hint in ("SDL_JOYSTICK_HIDAPI", "SDL_XINPUT_ENABLED", "SDL_JOYSTICK_DIRECTINPUT",
                 "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_WGI", "SDL_JOYSTICK_GAMEINPUT"):
        environment[hint] = "0"
    report = {"binary": str(binary), "binary_sha256": digest(binary), "shader": str(shader),
              "shader_preset_sha256": digest(shader), "runtime": str(runtime), "runtime_sha256": digest(runtime), "frames": args.frames, "stage": args.stage,
              "warmup_intervals_excluded": 1, "order": ["snapshot", "integrated", "integrated", "snapshot"],
              "viewports": ["16:9", "16:10"], "runs": []}
    for viewport in report["viewports"]:
        images = []
        for index, mode in enumerate(report["order"]):
            folder = output / (viewport.replace(":", "-") + f"-{index+1}-{mode}")
            folder.mkdir()
            settings, preview = folder / "settings.cfg", folder / "frame.bmp"
            settings.write_text(f"AB2SETTINGS1\nviewport={viewport}\nsprites=none\nhud=sharp\nground=1\nstats=0\nfullscreen=0\n")
            command = [str(binary), "--practice-stage", str(args.stage), "--frames", str(args.frames),
                       "--profile", "--scene-renderer", mode, "--settings-file", str(settings.resolve()),
                       "--shader-preset", str(shader), "--shader-runtime", str(runtime), "--frame-preview", str(preview.resolve())]
            (folder / "command.json").write_text(json.dumps(command, indent=2) + "\n")
            print(f"RUN {viewport} {mode} ({index+1}/4); log: {folder / 'run.log'}", flush=True)
            started = time.monotonic()
            with (folder / "run.log").open("w") as log:
                result = subprocess.run(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                        timeout=max(180, args.frames / 10))
            if result.returncode:
                raise SystemExit(f"Application failed ({result.returncode}): {folder / 'run.log'}")
            samples = []
            for line in (folder / "run.log").read_text().splitlines():
                if not line.startswith("Application performance:"):
                    continue
                if f"scene_renderer={mode}" not in line or "shader=active" not in line:
                    raise SystemExit(f"Requested rendering path was not active: {folder / 'run.log'}")
                samples.append({k: float(v) for k, v in re.findall(r"([a-z_]+)=([0-9.]+)", line)})
            if len(samples) < 3 or not preview.is_file():
                raise SystemExit(f"Insufficient timing/image evidence: {folder}")
            measured = samples[1:]
            image_hash = digest(preview)
            drawable_width,drawable_height=struct.unpack_from("<ii",preview.read_bytes(),18)
            images.append(image_hash)
            row = {"viewport": viewport, "mode": mode, "seconds_including_startup": time.monotonic()-started,
                   "folder": str(folder), "final_image_sha256": image_hash, "intervals": len(measured),
                   "drawable_pixels": [drawable_width,abs(drawable_height)],
                   "median_fps": statistics.median(s["simulation_fps"] for s in measured),
                   "min_interval_fps": min(s["simulation_fps"] for s in measured), "samples": samples}
            for key in ("board_ms", "wide_scene_ms", "filter_submit_ms", "crt_submit_ms", "present_wait_ms"):
                row[key] = statistics.mean(s[key] for s in measured)
            row["board_plus_scene_ms"] = row["board_ms"] + row["wide_scene_ms"]
            report["runs"].append(row)
            (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
            print(f"RESULT {viewport} {mode}: median={row['median_fps']:.3f} fps; minimum interval={row['min_interval_fps']:.3f}; board+scene={row['board_plus_scene_ms']:.3f} ms", flush=True)
        if len(set(images)) != 1:
            raise SystemExit(f"Final output images differ at {viewport}; retained all images and timings: {output}")
        print(f"PASS {viewport}: all four final output images byte-identical", flush=True)
    report["final_image_comparisons_identical"] = True
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Matched frontend benchmark complete: {output}", flush=True)


if __name__ == "__main__":
    main()
