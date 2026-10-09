# After Burner II — Three.js rendering prototype

Run from the repository root:

```sh
npm --prefix prototypes/threejs install --ignore-scripts --no-audit --no-fund
make threejs-prototype
```

Open http://127.0.0.1:8765 in a WebGL2 browser. Click **Boot to flight** to
execute 600 original board frames with physical coin/start pulses. It pauses
at the flight scene; press P to resume, then use
arrows, Z/X and Space/Shift. P pauses; Restart recreates the native process.
Sound is opt-in. Object outlines show the separate sprite geometry. Stop the
server with Ctrl-C. ROMs are read from the existing ignored `roms/` directory.

This is the direct-scene approach. Each hardware sprite entry becomes a
Three.js mesh with an indexed source texture decoded from the original sprite
ROM. Three.js performs horizontal scaling, ROM-palette selection, sprite
ordering and priority/shadow composition. Ground, scenery and HUD become
batched indexed strip geometry taken from a passive observer of the native
road/tile planes before sprite composition. This avoids decoding those planes
a second time.
There is no native composite framebuffer texture used for display.

The original core continues rendering internally because hardware scaler
writeback feeds gameplay. The local bridge observes each completed native
partial clip without changing memory or clock ownership. Palette and scene
inputs are captured at that clip's native frontier. The GPU output's top-down
RGB FNV-1a checksum can be compared with the native output every board frame
using **Verify pixels (slower)**;
verification defaults off during play to avoid synchronous GPU readback.
**MATCH** and the running mismatch count are diagnostic comparisons, not
whole-game MAME acceptance. Outlines are an overlay after that comparison.

The prototype preserves the arcade's screen-space projection. It does not
infer world coordinates, add 3D terrain/models, or widen the original viewport.
It provides separate sprite geometry and an independent renderer on which
those verified interfaces can be built. Road/tile decoding and scanline
surface generation still run in C++; moving those computations to the GPU
and replacing strips with world geometry are later work. The final Three.js
GPU pass combines its own render targets, not the C++ composite frame.

C++ gameplay runs in a local process; this is not yet a standalone WebAssembly
browser build. The server binds only loopback and serves only explicit frontend
files. Decoded graphics and PCM are generated in memory from user-supplied ROMs;
no original assets are checked in. The pinned Three.js dependency and npm lockfile
contain library code/metadata only; `node_modules/` is ignored.

Known limits:

- Native whole-credit coverage is incomplete. Extended live play still reaches
  unported instructions beyond the finite accepted fixtures. This stops explicitly; Restart or
  Boot to flight starts a fresh process. No substitute behaviour is generated.
  A stop reports its ROM PC and last completed frame. The server saves the
  physical input commands in ignored `build/threejs_failure_replay.txt` so
  the same run can be replayed through `build/threejs_bridge`.
- **Simulation FPS** counts completed physical board frames (target 59.64 Hz).
  **Display FPS** counts independent animation-frame presentations and follows
  the display's refresh rate, including 120 Hz on supported screens. Additional
  display refreshes repeat the latest original state; original gameplay remains
  at its native cadence. There is no invented interpolation.
- Meshes, materials and geometry buffers are reused, scene strips use packed
  little-endian data, unchanged sprite textures/palettes stay cached, and HTTP
  uses compression and a persistent connection. Reload requests a full snapshot.
  One browser tab shares the local server's native game session.
  Apple Silicon uses cooperative CPU stacks; other platforms retain the worker
  backend. Core / export / GPU-submit costs are shown separately in milliseconds.
  The bridge uses separate `-O3 -flto` release objects with ordinary floating
  point semantics. `THREEJS_CXXFLAGS` can override those compiler options.
  Throughput still depends on the hardware, scene and diagnostic options.
- Native audio cadence is preserved, but slow simulation can make browser
  playback discontinuous. Audio scheduling has not been accepted against MAME.
- Palette/priority/shadow and clipping are implemented; full-game renderer
  equality still requires broader natural and directed comparisons.

Validation commands:

```sh
python3 tools/workflow.py run test-threejs-scene test-cpu-continuation-backends
python3 tools/workflow.py run verify-threejs-core benchmark-threejs
python3 tools/workflow.py run test verify-stage1
node --check prototypes/threejs/scene.js
node --check prototypes/threejs/app.js
python3 -m py_compile prototypes/threejs/server.py
```

The ROM-free scene test compares source textures plus mesh transforms against
`HWSprites` for 80 synthetic cases covering zoom, both X/Y directions, nibble
order, signed pitch, address wrapping and clipping. Existing gameplay/MAME
fixtures remain required. Browser testing should check nonzero sprite meshes,
a **MATCH** result with verification enabled, pause, restart, original controls and shader diagnostics.
