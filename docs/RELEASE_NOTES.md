# Release notes

## v0.1.0-preview.3 — Practice, Shuffle and display enhancements

This release refreshes the **Apple Silicon / macOS 26+** download. Get it from
[GitHub Releases](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.3).
The tag and package manifest identify the exact source commit; the GitHub
release records the commit, validation logs and independent package checks.
The existing Windows download remains on Preview 2, described below.

### Changes

- **Stage Select / Practice** in the main and pause menus: select one of 23
  ROM stages, start a fresh directed session, retry with F5, or return to Arcade.
- **Shuffle** alongside Practice: stages 1–22 appear once in random order,
  followed by stage 23 as the native finale. Score, lives and weapons continue
  between stages. F5 retries the same order; New Shuffle generates another.
  Reproducible launches use `--shuffle-seed N`.
- **Presentation:** 16:10 alongside 16:9, high-DPI Sharp 3x/custom HUD text,
  built-in Subtle/Arcade CRT scanlines and RGB masks, plus an opt-in registered
  HD neutral/gentle-bank aircraft pack. The pack is included under
  `assets/player-aircraft/`; unsupported poses retain the original art.
- **Optional RetroArch Slang host** on macOS Metal. Its runtime and shader
  packs are installed separately; see [shader setup](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal).
- **Controls and audio:** live controller calibration, revised headphone
  effect placement, reviewed cockpit cues and spatial-output stall recovery.
- Verified literal instruction continuations cover the additional cleanup,
  credit-prompt and attract-controller paths exposed by the shuffled run.
- Package notices use SDL's license from its verified source archive, fixing
  a shared-prefix path that could select the Homebrew notice instead.

### Package

**macOS arm64:** Apple Silicon, macOS 26+. SDL3 3.4.16 and OpenAL Soft 1.25.2
are bundled with their sources and notices. The executable and shared
libraries are **ad-hoc signed, not notarized or Developer ID signed**.
Extract the complete ZIP, supply your own extracted ROMs in `roms/`, and open
`Launch.command`. Keep `lib/` beside the executable.

The ZIP includes project/dependency sources, notices, payload hashes and a
separate SHA-256 file. ROMs, original Sega graphics/audio, MAME captures,
personal preferences, soundtracks, the optional librashader runtime and shader
packs are excluded. The two prototype aircraft images are generated artwork.

### Validation and limits

The release gate is the complete final-source `required` profile (`test`,
`verify-stage1`, `oracle-test`, including retained mutations), followed by an
independent extracted-package check. The dated results are recorded in
[Engine Notes](ENGINE_NOTES.md#preview-3-release-2026-10-23).

The complete Shuffle comparison matches **100,000/100,000 independently
executed arranged-MAME frame observations, zero skipped**, including all
gameplay stages, the finale, ending, ranking and later attract sequence.
Those fields cover frame/time, mode, position, lives and raw score; they do
not establish continuous CPU/device/RGB/audio equivalence. The retained
instruction corpus matches **78,670/78,670 fixtures**, with 12,466 recovery PCs
and all 1,262 measured conditional branches exercised both ways.

Practice uses a directed startup and does not establish complete stage-entry
state fidelity. Enhanced modes reject natural input recording. Full HD
aircraft animation, additional shader backends, older/Intel macOS packages,
broader hardware checks and sustained peak-load performance remain open.
Standard single-credit recording and full-game acceptance remain in the
[roadmap](ROADMAP.md).

## v0.1.0-preview.2 — Windows performance portability

This preview adds an experimental Windows x64 download alongside the updated
Apple Silicon macOS package. Download from
[GitHub Releases](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.2).
Package manifests identify the exact source commit and all payload hashes.

### Changes

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

### Packages

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

### Validation

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
