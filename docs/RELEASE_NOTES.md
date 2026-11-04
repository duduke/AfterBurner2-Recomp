# Release notes

## Windows public download withdrawal (2026-11-04)

At the user's direction, the Windows Preview 5 runtime ZIP and checksum were
removed from the public download repository. Its README and Preview 5/6/7
release notices now mark Windows withdrawn pending signing, shader-runtime
policy compatibility and performance fixes. Other platforms remain available;
the original Windows artifact is retained privately and locally for diagnosis.
See [Windows evidence and remaining work](WINDOWS_PERFORMANCE.md).

A development frontend fix prepares fullscreen before playback and restores
normal pacing after audio/host outages, preserving all native frames and PCM.
It remains private: occasional GPU stalls/HRTF underruns, trusted signing and
whole-game hardware acceptance are still outstanding.

## v0.1.0-preview.7 — Linux and Vulkan Slang (2026-11-03)

[Main repository release](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.7)
· [Public downloads](https://github.com/duduke/AfterBurner2-Recomp-Releases/releases/tag/v0.1.0-preview.7)

- Add separate **Linux x86-64 / Steam Deck** and **Linux ARM64 desktop** packages
  with SDL3 3.4.16, OpenAL Soft 1.25.2, dependency notices, exact source companions
  and SHA-256 inventories. Full project source companions are retained locally
  and in the private development release, not the public download repository.
  x86-64 targets sniper / glibc 2.31; ARM64 targets
  Debian 12 / glibc 2.36. Builds use the existing worker-thread CPU continuation.
- Add **native Vulkan composition for RetroArch Slang**, sharing the Windows
  bridge and generated GPU sprite/scene kernels, including ScaleFX. Shader
  history, cached pause redraws, live renderer switching and failed-load rollback
  preserve the game session. The game engine and board timing are unchanged.
- The **20261103.1 packaging refresh bundles librashader 0.12.0** in each game
  archive, including required notices. There is no separate runtime installation.
  Corresponding upstream/Cargo sources and notices remain available in the
  retained runtime/source archives. Download the [official Slang shader pack](https://buildbot.libretro.com/assets/frontend/shaders_slang.zip),
  extract the entire ZIP with its folders/includes/textures, then choose a
  `.slangp` through Settings → CRT and Shaders → Shader Preset. Shader packs
  remain external. Legacy GLSL/Cg, HDR and first-input mipmaps remain unsupported.

Extract the game and run `Launch.sh`. Steam Deck uses the x86-64 package natively
and a Gamepad template, with fullscreen/16:10 defaults. ARM64 uses windowed /
Original defaults. Existing preferences are preserved. Supply your own verified
ROM directory or ZIP; no ROMs, captures, preferences or music are included.
See [Linux setup and source builds](LINUX.md).

Prior validation on the two architecture-native VMs includes the complete main
profile, finite independent MAME cold replay (121/121, zero skips), Vulkan pixel
references (1,176/1,176 per architecture), all 11 relevant deliberate faults,
six real renderer-switch cases and extracted-package X11/Wayland launches.
Publication reuses those results and the identical tested binaries. At the
user's explicit direction, no new oracle/regression run is performed for this
non-engine release; publication checks cover source identity, archive hashes,
notices and uploaded assets. Physical Deck/ARM64 GPU/audio/controls, suspend and
sustained performance remain unverified. Earlier quiet VM samples reached only
11.7 FPS x86-64 and 5.76 FPS ARM64 on different configurations, with underruns;
these do not establish hardware performance or an architecture comparison.

macOS/Android remain on Preview 6 and Windows remains on experimental Preview 5.
This release also records the earlier TCL TV's 32-bit-only ABI incompatibility;
no 32-bit Android package is supplied.

## v0.1.0-preview.6 — Android and ultrawide (2026-11-01)

[Main repository release](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.6)
· [Public downloads](https://github.com/duduke/AfterBurner2-Recomp-Releases/releases/tag/v0.1.0-preview.6)

- Add an **Android ARM64 preview** for Android 8.0/API 26 or newer, with private
  ROM ZIP import, controller/remote navigation and Android TV launcher support.
- Handheld touch controls include a thumb-sized gamepad-style joystick cap,
  round GUN/MSL buttons above it, a spring-centered right-hand throttle slider,
  round top-center COIN/START/MENU buttons and a HIDE/SHOW toggle beside MENU.
- Android ARM64 cooperative CPU continuations reduce OS handoffs. On the
  SM-S908E / Android 16, identical 7,200-frame workloads improve from 53.33 to
  59.63 FPS against the unchanged 59.637 Hz arcade cadence; a warmed repeat also
  reaches 59.63 FPS. Complete 121-frame cold and 781-frame gameplay MAME replays
  match CPU/bus/video/audio with zero differences or skips. These are finite
  phone workloads; broader device, TV and peak-load/thermal acceptance remain open.
- Add **21:9 Preview (522×224)** alongside 16:9 and 16:10, preserving the centered
  native HUD/menu and existing wide-renderer effects. Fix optional software
  aircraft edge rounding at fractional display scales.
- Make **Corrected ground the default on every platform**, preserving explicit
  saved Original choices. Consolidate the roadmap and use the documented main
  validation profile for frontend work; full engine fixtures/mutations remain.

The macOS ZIP targets **Apple Silicon / macOS 26+**, bundles SDL3 3.4.16 and
OpenAL Soft 1.25.2, and is ad-hoc signed, without Developer ID signing or
notarization. The Android APK is an optimized **debug-signed sideload preview**,
with no Play Store signing/release claim; SDL audio is included, while OpenAL
and external Slang effects are excluded. Its version code is 2. See
[Android setup, controls and limits](ANDROID.md).

Both packages exclude ROMs, captures, user settings and personal soundtracks.
Supply your own complete `aburner2` ROM directory/ZIP on macOS or import a ZIP
on Android. Matching source companions are retained locally; release notes link
exact project/dependency sources. The release records its exact source commit,
checksums, extracted-package validation and finite device scope. Custom-stage
experiments remain separate. Windows retains its existing Preview 5 build and
known FPS/audio issues; this release does not update Windows.

## Unpublished macOS development refresh (2026-10-28)

The local `v0.1.0-preview.6-20261028.2` package consolidates main through the
28 October Windows work, including shared ground/sprite/HUD rendering, SDL
audio recovery and frame pacing. macOS retains its Metal backend; the native
Vulkan compositor remains Windows-specific. The integration also fixes a
same-iteration SDL starvation/tail restart that could bypass the recovery reserve.

The package includes the expanded original/dependency license inventory and
notice preflight checks. Standalone custom landing/refueling stages live on
`codex/custom-stage-prototype` and are excluded from this main-game build.
Android, Linux and object-based HRTF entries remain plans.

This is a local Apple Silicon/macOS 26+ development package, not a new published
GitHub release. Its exact source and validation are recorded in Engine Notes
and the package manifest. Existing Windows performance and validation limits
remain documented below; this refresh does not close them.

## v0.1.0-preview.5 — ROM startup, menus and widescreen effects

This refreshes the **Apple Silicon / macOS 26+** package at
[GitHub Releases](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.5).
An experimental Windows x64 Preview 5 package was added on 2026-10-27; see below
for its validation scope and remaining limits.
The release tag, package manifest and GitHub release identify the exact source
commit and ZIP checksum.

### Changes

- Load your own extracted ROM directory or stored/Deflate `aburner2.zip` directly.
  Startup automatically tries `roms/aburner2.zip`, offers a native ZIP picker
  when necessary and remembers a successful selection. Chip sizes/CRCs remain
  checked; no ROMs are included.
- Direct Arcade, Practice and Shuffle startup choices. Settings are grouped into
  Display, Graphics, CRT and Shaders, Controls, Audio, Arcade Settings and
  Diagnostics. The pause menu adds Restart Run and Change Mode; Controls
  separates bindings, analog setup, Input Monitor and feedback tests.
- Metal GPU sprite filters stay active with external Slang presets, removing
  the CPU ScaleFX fallback behind the reported NTSC Composite slowdown.
- Corrected ground fixes fast-roll sky triangles and reproduced blue/cloud
  side gaps while retaining terrain and foreground protection.
- Widescreen retains world explosions, ground/air hit effects, player/enemy
  missile smoke and world-projected player damage/death emissions after native
  horizontal removal. Private visual copies preserve their ROM animation and
  lifetime rules; gameplay, native pools/rings, sound and respawn stay unchanged.
- Generated Shuffle runs start on gameplay stages rather than scripted stages
  5, 13 or 23. Every stage still appears once, with stage 23 last.
- Retains the integrated renderer, 16:9/16:10, Practice, HUD/CRT, optional
  registered HD aircraft pack, calibration, replacement music and spatial audio.

### Package and limits

Apple Silicon, **macOS 26+**; SDL3 3.4.16 and OpenAL Soft 1.25.2 are bundled
with matching sources/notices. Binaries are **ad-hoc signed, not Developer ID
signed or notarized**. Extract the complete ZIP and run `Launch.command`.
Supply your own ROM directory or ZIP. ROMs, original Sega media, captures,
preferences, personal soundtracks, external shader runtimes and shader packs
are excluded. Intel/older macOS and Linux packaging are unchanged; ultrawide
remains unavailable.

Widescreen, spatial audio, replacement artwork and sustained peak-load/platform
coverage remain experimental. Full-game acceptance and continuous whole-credit
CPU/device/RGB/audio comparison remain open. The required final-source release
gate is recorded in the
[Preview 5 release record](ENGINE_NOTES.md#preview-5-release-2026-10-26);
the GitHub release records independent extracted-package checks.

### Windows refresh (2026-10-27)

The unsigned Windows 10/11 x64 ZIP uses source commit `514c092` with checked
ThinLTO, SDL3 3.4.18 and OpenAL Soft 1.25.2. It includes sources, notices,
payload hashes and a separate checksum. Independently extracted Arcade,
Practice and Shuffle launches, continuation tests, native shader integration
and OpenAL HRTF checks pass. Complete Windows release gates were not closed;
the GitHub release documents the unavailable instrumented MAME fixtures,
standalone Unicode test-argument issue and Windows input-recording header bug.
Input recordings from this package should not become validation fixtures.

The original 27 October ZIP uses CPU sprite/scene composition with Slang.
The 28 October compact refresh below includes the native Vulkan GPU compositor
and shared HUD-raster optimization; measurements and validation are in
the [source performance record](ENGINE_NOTES.md#native-vulkan-sprite-filters-and-shared-hud-raster-2026-10-27).

### Compact Windows refresh (2026-10-28)

The `Windows-x64-20261028-experimental` runtime ZIP includes the latest native
Vulkan sprite compositor, shared HUD raster, ground/palette/decoder work,
exact native ScaleFX capture reduction and removal of a redundant shader
submission. Windows OpenAL uses an 80 ms startup/recovery reserve, adding
55/30 ms over its previous thresholds. Native PCM and clocks are unchanged.

Keep the complete runtime folder together and open `Launch.cmd`. It contains
one game executable, SDL3/OpenAL/C++ runtimes, librashader 0.12.0 and its
DirectX/Microsoft C++ dependency DLLs, plus one consolidated `LICENSES.txt`.
Project/dependency sources and the manifest are in the separate matching source
companion. No regression executables, general docs, ROMs, captures, personal
settings, shader preset packs or optional aircraft assets are in the runtime ZIP.

**Performance remains unresolved.** Fullscreen Stage 1 can dip to 53–56 FPS
with audible gaps and synchronized 13-source OpenAL underruns; windowed dips
are reported less often. The final two 3,600-frame runs average 59.72/59.50 FPS,
with minimum rolling windows 55.92/53.39 and 1/3 underrun events. These finite
runs do not establish smooth audio or locked native speed. See the
[current issue/reproduction record](WINDOWS_PERFORMANCE.md).

Scoped regression/Stage 1, all 23 stages at 16:9/16:10, renderer/shader and
audio checks pass as recorded in Engine Notes. Complete Windows release/oracle
acceptance remains open because of missing/invalid instrumented captures.
The retained Windows recording-header issue also remains open; do not use
recordings from this build as validation fixtures. The refresh is experimental
under the user's documented acceptance of these release limitations.


## v0.1.0-preview.4 — Integrated primary renderer

This release updates the **Apple Silicon / macOS 26+** download at
[GitHub Releases](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.4).
Windows remains on its existing Preview 2 download. The tag and package manifest
identify the exact committed source; the GitHub release records the ZIP checksum
and independent extracted-package verification.

### Changes

- Widescreen and detached HUD/aircraft presentation use the **integrated primary
  renderer by default**, sharing the native partial-frame road/tile pass and
  avoiding redundant tile snapshots/reconstruction.
- `--scene-renderer snapshot` retains the previous reconstruction reference.
  **16:9 Preview / 16:10 Preview** are viewport labels for remaining scene/platform
  acceptance; they do not indicate the renderer. Original remains the default
  viewport, and selecting a wide ratio uses the integrated renderer.
- Retains all Preview 3 features: Practice, fixed-finale Shuffle, 16:10, HUD/CRT,
  optional registered aircraft art, calibration and revised spatial audio.
- Shuffle preserves inherited original music across new stage adjacency,
  including cold entry. Fresh independent MAME recordings cover this data view.

### Validation and limits

The required gates (`test`, `verify-stage1`, complete `oracle-test`) and
supplemental `full` checks pass on the unchanged application sources. The complete
oracle includes all **207 recovery controls** and all retained fixtures.
Integrated-host CPU/bus/device/index/priority/RGB/audio checks match **3,010 counted
frame comparisons**, with zero supplied execution/state/read seeds or skips.
Independent native-center RGB matches **4,300/4,300 frames** across five widths;
live/snapshot enhanced layers match **208,904/208,904 lifecycle frames** across
both wide ratios, zero differences/skips. These capture windows are finite and
include overlapping observations.

The fresh full Shuffle replay matches **100,000/100,000 independent MAME frame
observations**, zero skipped, through all 23 positions, ending, ranking and
attract. Those observation columns do not establish whole-credit CPU/device/RGB/
audio equivalence. Older routine-level input-race skips keep their documented
reasons. See the [acceptance record](ENGINE_NOTES.md#integrated-primary-renderer-macos-acceptance-2026-10-24).

Matched Stage 1/5 tests with a four-pass NTSC preset reduce board-plus-scene CPU
time by **3.70–4.51%**, with identical final images. Median pacing is 59.637 fps,
but some intervals remain below target. Sustained peak-load and broader
scene/platform acceptance remain open; ultrawide is not enabled.

### Package

Apple Silicon, **macOS 26+**. SDL3 3.4.16 and OpenAL Soft 1.25.2 are bundled
with sources/notices; binaries are **ad-hoc signed, not Developer ID signed or
notarized**. Extract the whole ZIP, supply your own ROMs in `roms/`, and run
`Launch.command`. The source archive, payload manifest and separate SHA-256 file
identify the build. ROMs, original Sega media, captures, preferences, soundtracks,
optional shader runtimes and shader packs are excluded. The registered generated
aircraft prototype is included. External shader setup is described in
[CRT display](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal).


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
