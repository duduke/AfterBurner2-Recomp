# v0.1.0-preview.2 — Windows performance portability

This preview adds an experimental Windows x64 download alongside the updated
Apple Silicon macOS package. Download from
[GitHub Releases](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.2).
Package manifests identify the exact source commit and all payload hashes.

## Changes

- Windows CPU continuations use cooperative OS fibers instead of frequent
  worker-thread handoffs. The worker backend remains available for comparison.
- CMake Release/RelWithDebInfo builds enable checked LTO and `-O3` for GCC/Clang.
  `AB2_ENABLE_IPO=OFF` disables LTO. Floating-point semantics remain unchanged.
- Frame-deadline waits run immediately after presentation with a hybrid sleep/spin
  wait and 1:1 simulation/display pacing; Vulkan presentation disables adaptive
  swapchain VSync (`FIFO_RELAXED_KHR`) and syncs fullscreen window transitions so
  fullscreen toggling and DWM composition do not stall on Windows.
- Failed GPU compute setup permits SDL accelerated presentation with CPU
  filters. Windows compute uses the existing Vulkan/SPIR-V shaders;
  Direct3D 12 compute shaders are not included.
- OpenAL Soft 1.25.2 spatial audio is enabled on Windows and fixes an OpenAL 1.1
  `AL_STOPPED` state bug (`alSourceRewindv` after stopping and unqueueing buffers
  in `AudioOutput::clear_al()` and `AudioOutput::reap()`) that previously muted
  audio after exiting to the pause menu or switching music track previews.
- Cross-platform CPU, widescreen, horizon and GPU-filter optimizations reduce
  per-frame host overhead: `XBoardHost::observe` skips copying 88-byte `Entry`
  structs outside presentation phases, `tile_gen`/`palette_gen` dirty counters
  avoid redundant 32,768-tile and 8,192-palette reloads across partial scanline
  clips, `GameSession` pools `SceneClip` buffers, `WideVisibility::recover` syncs
  only the 256-byte camera/object range instead of 64 KB `memcpy`, `GroundHorizon`
  caches horizon profiles across partial clips, `HWSprites`/`Presentation::decode`
  clip vertical row loops and skip all-transparent 32-bit words, and
  `GPUSpriteScaler` allocates 2 MB minimum / 2x headroom layer buffers.
- `--profile` reports the continuation backend and CPU/GPU filter path.
- Explicit string headers repair two portability issues found by cross-building.

Existing widescreen, ground correction, sprite filters/translucency, remapping,
replacement music and controller feedback are retained. Native gameplay and
object allocation limits are unchanged.

## Packages

**macOS arm64:** Apple Silicon, macOS 26+. SDL3 3.4.16 and OpenAL Soft 1.25.2
included. Ad-hoc signed; not notarized or Developer ID signed. Extract, add your
own ROMs to `roms/`, then open `Launch.command`. *(Note: the uploaded macOS ZIP
was built at commit `0c7f19e` prior to the Windows runtime/performance commit and
should be rebuilt on macOS after running the full macOS regression/oracle suite.)*

**Windows x64:** targets Windows 10/11 x64. Built and runtime-verified on Windows
x64 with w64devkit GCC 16.2.0 (`-O3`, static C++ runtime), SDL3 3.4.18 and OpenAL
Soft 1.25.2 (`SDL3.dll` and `OpenAL32.dll` included; keep them beside the EXE).
Unsigned. Original SDL stereo, replacement music and OpenAL Soft spatial audio
(headphone HRTF, 2.1 and 5.1 profiles) are enabled. Run
`Check-Continuations.cmd` for the two ROM-free continuation tests, then
`Launch.cmd --profile`.

Both ZIPs include project/dependency sources, notices, a manifest and a separate
SHA-256 checksum. Neither includes ROMs, original Sega graphics/audio, captures,
personal settings or soundtracks.

## Validation

- **Windows x64 runtime and performance (verified on Windows 11):** both ROM-free
  continuation tests (`windows-fiber` and `worker-thread`) pass. Native `afterburner2.exe`
  with Vulkan GPU sprite filters (`renderer=gpu`, `filters=gpu`), OpenAL Soft
  1.25.2 headphone HRTF, 16:9 widescreen, Corrected ground, ScaleFX 3x, all four
  translucency toggles, and Integer/Pixel-Art scaling sustains locked
  `simulation_fps=59.6374 display_fps=59.6374` across 420-frame Stage 1 and
  Stage 5 profiles (`board_ms=9.15–10.24 ms`, `wide_scene_ms=1.27–3.94 ms`,
  `filter_submit_ms=0.22–0.45 ms`). Fullscreen toggling (`F11` and menu), repeated
  music track previews and pause-menu resume retain uninterrupted OpenAL/SDL audio.
- **macOS regression baseline (prior to the Windows runtime/performance commit):**
  the complete `required` profile passed at `build/workflow/20261015T202242.843626Z/`
  (`test`, `verify-stage1` and `oracle-test`, including all retained mutations),
  and supplemental joint/reset checks passed at `build/workflow/20261016T034347.883905Z/`.
  Because engine/video/frontend headers were subsequently optimized on Windows
  without running MAME oracle or full regression suites on the Windows host, the
  next macOS session must re-run `python3 tools/workflow.py run required -j 4`
  and GPU/spatial-audio checks on macOS before claiming updated whole-host
  regression acceptance.

Recorded checks do not establish exhaustive widescreen coverage, physical
controller/speaker acceptance on all platforms or continuous whole-credit
fidelity. Standard-credit recording and full-game acceptance remain open in the
[roadmap](ROADMAP.md).
