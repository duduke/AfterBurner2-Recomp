#!/usr/bin/env python3
"""Measure native simulation plus scene export, independent of browser refresh.

Loads only the user's ignored ROMs. No frames, graphics or PCM are saved.
"""
import json
from pathlib import Path
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent

def main():
    with subprocess.Popen([str(ROOT / 'build/threejs_bridge')], cwd=ROOT,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, text=True) as bridge:
        def request(command):
            bridge.stdin.write(command + '\n')
            bridge.stdin.flush()
            line = bridge.stdout.readline()
            if not line:
                raise RuntimeError('Native bridge stopped; run it directly for diagnostics')
            return json.loads(line), len(line)
        if not json.loads(bridge.stdout.readline()).get('ready'):
            raise RuntimeError('Native bridge could not load')
        request('demo 600 0')
        times, cores, captures, sizes = [], [], [], []
        for _ in range(60):
            began = time.perf_counter()
            frame, size = request('tick 1 0 0 0 0 0 0 0 0 0')
            times.append(1000 * (time.perf_counter() - began))
            cores.append(frame['performance']['coreMs'])
            captures.append(frame['performance']['captureMs'])
            sizes.append(size)
        bridge.stdin.write('quit\n')
        bridge.stdin.flush()
        bridge.wait(timeout=5)
    print(json.dumps({
        'board_frames': 60, 'board_budget_ms': 16.768,
        'bridge_median_ms': statistics.median(times),
        'bridge_mean_ms': statistics.mean(times),
        'bridge_p95_ms': sorted(times)[56],
        'core_mean_ms': statistics.mean(cores),
        'scene_export_mean_ms': statistics.mean(captures),
        'uncompressed_mean_bytes': statistics.mean(sizes),
    }, indent=2))

if __name__ == '__main__':
    main()
