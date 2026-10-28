# Roadmap

Updated **2026-10-28**. [Preview v0.1.0-preview.5](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.5)
refreshes Apple Silicon / macOS 26+ with ROM ZIP loading/picker, reorganized menus,
native Metal filters with Slang presets, Shuffle gameplay openings, corrected
rolling sky/cloud coverage and broader widescreen effect retention. It retains
the integrated renderer, Practice/Shuffle, HUD/CRT, optional aircraft art,
calibration and spatial audio. The
[Windows 10/11 x64 Preview 5 package](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.5)
has a compact 28 October experimental refresh with the Vulkan/HUD performance
changes below; [Windows FPS/audio issues](WINDOWS_PERFORMANCE.md) remain open.
Original remains the regression reference. See
[release notes](RELEASE_NOTES.md#v010-preview5--rom-startup-menus-and-widescreen-effects).

## Summarized roadmap

| Priority | Workstream | Next outcome |
| --- | --- | --- |
| 1 | [Arcade fidelity](FIDELITY_PLAN.md) | Record standard single-credit completion; extend continuous MAME CPU/device/RGB/audio and remaining lifecycle coverage |
| 2 | [Widescreen](WIDESCREEN_PLAN.md) | Extend shared-renderer performance and controller/scene audits; retain snapshot comparisons and sustained 16:9/16:10 gates; ultrawide needs separate acceptance |
| 3 | [Controls/feedback](HAPTICS_PLAN.md) and [audio](SPATIAL_AUDIO_PLAN.md) | Windows/Linux hardware acceptance, HOTAS calibration, physical speaker/HRTF tests and remaining sound classification; [object-based HRTF](SPATIAL_AUDIO_PLAN.md#object-based-hrtf-planned), starting with missile flybys and positioned explosions |
| 4 | Performance and distribution | Measure peak-load pacing/latency; establish additional GPU/platform and macOS packaging compatibility; [Android / Android TV port and real-device ADB testing](#android--android-tv-port-planned); [Linux x86-64 / ARM64 builds and remote testing](#linux-x86-64--arm64-builds-and-remote-testing-planned) |
| 5 | [Display and assets](ENHANCEMENT_PLAN.md#display-and-assets) | [Offline sprite extraction/mapping](SPRITE_EXTRACTION.md), [CRT scanlines/RGB mask and optional external presets](CRT_DISPLAY.md) and an opt-in [registered HD neutral/gentle-bank aircraft pack](SPRITE_REPLACEMENT.md); full replacement animation, built-in CRT glow/curvature, pacing and optional HUD improvements remain |
| 6 | [Gameplay extensions](ENHANCEMENT_PLAN.md#gameplay-and-content) | Extend the Practice/Shuffle interfaces for additional arranged rules; verify object/state contracts before new stages, save states or multiplayer |

This is a work order, not a schedule. Enhancements can proceed alongside fidelity
work; a feature release does not close whole-game or platform acceptance.
See the [feature backlog](ENHANCEMENT_PLAN.md) for implemented increments and
future proposals, and [ENGINE_NOTES](ENGINE_NOTES.md) for historical evidence.

## Working on the remaining tasks (read this first)

These instructions are for anyone picking up the next task, including AI
assistants. The goal is gameplay **identical** to the arcade ROM, so the
bar is "matches MAME byte-for-byte", not "looks right".

**Where things are**

- [`docs/HARDWARE.md`](HARDWARE.md): the X-Board hardware, from MAME's
  `segaxbd.cpp` and the `segaic16` chips.
- [`docs/ENGINE_NOTES.md`](ENGINE_NOTES.md): what's known about the 68000
  game code (addresses, RAM layout, routines), and the write-up of each
  verified port. Read "Oracle testing", "Sub CPU road generator" and
  "Video oracle" before starting.
- `src/main/engine/subbus.*`, `osubsprites.*`, `osubroad.*`: the
  reference examples of a verified port. Copy their structure.
- `tests/oracle_*.cpp` and `tools/oracle/*.lua`: the matching tests and
  MAME capture scripts. Copy these too.
- `docs/STAGE1_FIDELITY.md`: the retained historical reconstruction tests;
  these do not drive the live application.

**Rules**

1. **Never invent behaviour.** Every number, table, timing and branch
   comes from the ROM disassembly. If something isn't understood yet, leave
   it unported and say so; don't fill the gap with a plausible guess. The
   historical Stage 1 reconstruction is retained only for regression checks;
   do not use its guessed rules to fill gaps in the live translated game.
2. **Translate instruction by instruction**, keeping the arcade's memory
   layout: the ported code reads and writes the same addresses through a
   bus object (`SubBus` and `MainBus`). Comment each block
   with the ROM address it came from. Refactoring into readable code comes
   later, once a test pins the behaviour.
3. **Every port gets an oracle test before it's wired into the game.** A
   Lua script captures the state before and the writes after the routine
   in MAME, and a C++ test replays the "before" state through the port and
   compares. Report results as "N of M identical, K skipped (reason)".
4. **Prove each new or changed oracle can fail.** Run relevant deliberate
   mutations against a passing baseline before accepting the port or test
   change. Retain earlier mutations; use the validation cadence below rather
   than rebuilding every unchanged mutant during each edit. Check coverage
   too: a path the capture never exercised is unverified, and must be listed
   as such in `ENGINE_NOTES.md`.
5. **Never commit ROM data or captures.** ROMs live in `roms/`, captures in
   `build/oracle/`; both are gitignored. Don't paste ROM bytes or large
   tables into source files either: read them from the ROM at runtime.
6. **Keep the docs current**: tick roadmap items off with their results,
   and add what you learned to `ENGINE_NOTES.md`.
7. **Validate according to the change and acceptance gate.** Local commits
   require the applicable focused checks in the cadence below. Gameplay/core
   merges, releases and acceptance milestones require `make test`,
   `make verify-stage1` and the complete `make oracle-test`. Preserve all
   fixtures and mutations; a focused pass is not whole-host acceptance.

**Validation cadence**

| Change or gate | Required validation |
| --- | --- |
| Documentation only | `git diff --check`; inspect changed links, commands and policy consistency. No engine suites unless executable behavior also changes. |
| Development and local code commits | Run `test` and affected oracle targets; add `verify-stage1` for engine, game-session or integration changes, and a relevant build/smoke for platform/UI changes. Record the targets and results; do not rerun unrelated long recordings after each edit. |
| New or changed literal ports, fixtures or comparison gates | Passing independent MAME baselines, relevant branch/operand coverage and relevant rejected mutations before wiring/accepting. For recovery ports, retain and replay the complete corpus with its manifest/branch gate; repeat the affected natural recording. |
| Shared CPU/flags/timing, bus, scheduler, device logic, generator implementation, compiler/options or oracle instrumentation changes | Full regression and all retained mutations before accepting the change, plus affected natural recordings. Regenerate captures when their semantics or instrumentation changed. Adding generated instruction bodies alone follows the literal-port row. |
| Gameplay/core merge, release or declared acceptance milestone | Run the complete `required` profile (`test`, `verify-stage1`, `oracle-test`) on the final tree. Milestones also run the supplemental `full` profile and applicable complete natural recordings. |
| Periodic integration checkpoint | Run the complete regression/mutation suite after ten code merges since the last full pass, even when each merge only required narrow checks. Documentation-only merges do not count. This is a validation trigger, not a scheduled automation. |

Reuse an earlier pass only while its relevant sources and transitive
dependencies, compiler/options, fixtures, capture probe and comparison/mutation
harness are unchanged. Cite its log and scope explicitly; do not present it
as a new run or carry it across an affected change. A failure requires repair
and rerunning the failed gate and dependent checks. Before a merge or milestone,
a focused repair does not replace a clean complete run when that gate requires
one. Existing tools do not implement automatic result caching.

The current `oracle-test` includes all retained mutation checks, so the full
regression gates above still execute them. Selective mutations are for focused
work; splitting the full regression and mutation commands or adding validated
hash-based caching is separate tooling work. Never delete fixtures, weaken
comparisons or skip unexplained failures to meet this cadence.

**Commands** (from the repository root)

```bash
make -j4                    # build the game: ./afterburner2
make test                   # ROM-free engine checks
make verify-stage1          # Stage 1 integration suite (needs roms/)
make oracle-capture         # record MAME reference captures into build/oracle/
make oracle-test            # check the ports against those captures

# Disassemble: build a flat CPU image, then a linear listing of a range
python3 tools/build_rom_image.py maincpu "$TMPDIR/main.bin"    # or subcpu
python3 tools/m68k_disasm.py "$TMPDIR/main.bin" 144A2 14820
```

`make oracle-capture` needs a MAME binary and the `aburner2` ROM set: set
`MAME=` and `ROMPATH=` (defaults are in `tools/oracle/capture.sh`). The
scripts were written for MAME 0.287. The disassembler is linear and
deliberately incomplete: "unknown opcode" on a real execution path means
the decoder needs extending; in a data table it's expected.

**Oracle lessons learned (avoid repeating them)**

- Compare **write sets**, not whole RAM snapshots: log only the CPU under
  test's own writes (filter on `CURPC`), so the other CPU's concurrent
  writes can't cause false failures. A record is a race, and is skipped,
  only if the other CPU changed a byte the port actually read.
- Exclude stack areas: exception frames are pushed while the PC still
  points at the interrupted instruction, so no PC filter catches them.
- Keep Lua tap handles in global variables, or they get garbage-collected
  and silently stop firing. `screen:vpos()` isn't available; derive scan
  lines from `manager.machine.time` (64 us per line).
- In a frame notifier, `screen:pixels()` is the image drawn from the state
  at the *previous* notifier, converted through the *current* palette.
- For IRQ mask fixtures, execute a real SR-writing instruction. MAME's
  debugger SR setter does not update pending interrupt selection.
- Start each MAME run with fresh NVRAM and config (`capture.sh` does), or
  boot isn't deterministic.
- The game logic runs at 30 Hz with an interpolated sprite list on the
  in-between frames. Don't port anything at 60 Hz.

## Current checkpoint (2026-10-24)

Integration update (2026-10-28): main consolidates the latest Windows rendering,
audio and pacing work with local license/package maintenance and platform plans.
Standalone custom-stage experiments are isolated on
[codex/custom-stage-prototype](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/custom-stage-prototype),
including their source, fixtures and instructions; they are excluded from the
main-game macOS package. macOS validation also found and repaired an SDL
starved-tail recovery race; see the new Engine Notes consolidation record.

Licensing maintenance (2026-10-26): the central
[license inventory](../LICENSES/README.md) now retains dependency and legacy
audio notices, Vulkan header terms and GCC runtime terms. Native Windows
packaging requires its compiler's MinGW notice; both packagers check required
central notices. This updates future packages, not published artifacts. See
the [validation record](ENGINE_NOTES.md#license-inventory-and-packaging-notices-2026-10-26).

Windows source update (2026-10-24): checked LLVM-MinGW ThinLTO builds and a
native Vulkan RetroArch `.slangp` host are implemented. Practice/Shuffle,
16:10, HUD/CRT, aircraft presentation, calibration and revised spatial audio
are shared with the macOS source; Windows packaging now includes the matching
aircraft assets and shader notices. The published Windows download remains
Preview 2. Local regression/GPU/shader evidence and the earlier full-oracle and
symlink-fixture failures are recorded in
[ENGINE_NOTES](ENGINE_NOTES.md#windows-lto-vulkan-shaders-and-feature-parity-2026-10-24).

Windows NTSC shader follow-up removes immediate GPU waiting and redundant
Off-mode sprite scaling. A bounded Original viewport/HUD run holds 59.64 fps
after startup; busy 16:10 scenes still fall below target. See
[NTSC measurements](ENGINE_NOTES.md#ntsc-preset-performance-follow-up-2026-10-24).

Windows source follow-up (2026-10-27): CMake builds retain native Vulkan GPU
sprite/scene composition with Slang, including ScaleFX. The live primary
renderer reuses the hardware sprite raster for detached HUD ownership. Matched
S-video Stage 5 fullscreen tests with Sharp HUD and corrected ground average
59.49 fps at 16:9 and 59.64 fps at 16:10, with identical final images. These
optimizations are local source changes awaiting a Windows package refresh;
sustained scene/GPU and whole-game acceptance remain open. See the
[implementation and validation record](ENGINE_NOTES.md#native-vulkan-sprite-filters-and-shared-hud-raster-2026-10-27).

Stage 1 takeoff follow-up retains compact horizon/geometry caches, bounded
frame-time diagnostics and terminal-fragment reuse. Earlier Practice runs
excluded takeoff; longer fullscreen runs still show dips. The connected-desktop
follow-up adds bounded host catch-up and SDL starvation recovery; consistent
full speed remains open. See the
[takeoff investigation](ENGINE_NOTES.md#windows-stage-1-takeoff-frame-time-investigation-2026-10-27).

The 28 October user capture confirms 53.29 fps and ten SDL starvation events.
Reordered ground checks and exact native ScaleFX capture reduction preserve
rendered pixels and reduce work. Four local runs average 59.64 fps, but brief
dips and one 16:10 starvation remain; user confirmation and sustained acceptance
are still open. See the
[dropout investigation](ENGINE_NOTES.md#user-operated-windows-dropout-capture-2026-10-28).

The subsequent administrator CPU/GPU capture reproduces roughly 54–58 fps
during active play, with little CPU pacing headroom. The user now confirms
audible dips with repeated synchronized OpenAL underruns, superseding the
earlier smooth-HRTF report. Long graphics waits cluster around pause/focus
transitions; fullscreen presentation alternatives tested worse. A passive
unit-zoom sprite-decoder shortcut and removal of a redundant SDL Vulkan
submission pass focused rendering, regression and stage checks. Windows
OpenAL now uses an 80 ms startup/recovery reserve with passing output controls,
but final normal runs still dip to 53–56 fps with 1/3 underrun events.
Sustained FPS/audio acceptance remains open. See the
[trace investigation](ENGINE_NOTES.md#administrator-cpugpu-trace-and-unit-zoom-decoder-2026-10-28).

Primary renderer integration (2026-10-24): source builds now use the shared
primary road/tile pass for widescreen and detached HUD/aircraft presentation.
It renders at a bounded even logical width, 224 rows and a centered native
origin, retaining native partial-frame palette/priority rules, sprite feedback,
CPU/device clocks and gameplay object pools. The live consumer uses compact
metadata rather than copied tile snapshots. `--scene-renderer snapshot` retains
the previous reconstruction reference. macOS Preview 4 includes this path; the
Windows download remains Preview 2.

The Windows checkpoint passes 3,500/3,500 independent stock-MAME RGB comparisons,
complete `test` and `verify-stage1`; instrumented CPU/device and supplemental
checks were deferred to this Mac. The Mac follow-up passes 3,010 counted
CPU/bus/device/index/priority/RGB/audio frame comparisons at 358 and 398 pixels,
zero supplied execution/state/read seeds or skips, with three rejected faults.
All four retained natural recordings match the live and snapshot enhanced
planes across **208,904/208,904 frames**, zero differences/skips. GPU/software
frontend image pairs and live shader/viewport switching pass at all three menu
viewports. Matched Stage 1/5 NTSC timings reduce board-plus-scene CPU time by
3.70–4.51%, with identical final images; finite intervals still fall below target.
The complete required gates and supplemental `full` profile pass on the final
source. The clean complete oracle rerun retains all 207 recovery controls,
18 geometry controls and the new integrated-host/RGB controls. Refreshed
independent Shuffle recordings also match all 100,000 native replay observations.
This closes the declared Mac/source renderer rollout gate. See the
[Mac acceptance record](ENGINE_NOTES.md#integrated-primary-renderer-macos-acceptance-2026-10-24)
for gate logs, overlap/coverage limits and timing scope.

The bounded API accepts even widths 320–640; 522 is a nominal square-pixel 21:9
test case rounded for symmetric margins, not an enabled or accepted ultrawide
mode. Broader scene/platform and sustained-load acceptance remain open. Custom
stages remain a separate opt-in stage/object-data interface: renderer integration
does not authorize changes to arcade activation, targeting, collisions, stage
scripts or spawn rules. See the
[integration work order](WIDESCREEN_PLAN.md#primary-renderer-integration-work-order-2026-10-24).

Local macOS shader follow-up (2026-10-25): source builds retain native Metal
GPU sprite filters with Slang presets. This removes the CPU ScaleFX fallback
behind the reported NTSC Composite slowdown. Exact CPU-reference comparisons
pass 882/882 cases (157,452,162 pixels), with six rejected controls and all three
live shader/viewport integration checks passing. See the
[Metal sprite-filter record](ENGINE_NOTES.md#native-metal-sprite-filters-with-slang-2026-10-25)
for measurements and validation scope. Included in macOS Preview 5.

Local menu organization (2026-10-25): source builds now group settings into
Display, Graphics, CRT and Shaders, Controls, Audio, Arcade Settings and
Diagnostics. Startup offers all three modes directly; the pause menu adds
Restart Run and Change Mode. Controls separates bindings, analog settings,
Input Monitor and feedback cues/tests. Focused regression, software/Metal menu
and release-build evidence is recorded in the
[menu organization record](ENGINE_NOTES.md#native-menu-organization-2026-10-25).
Included in macOS Preview 5; the Windows download remains Preview 2.

Local ROM startup update (2026-10-25): source builds accept extracted directories
or stored/Deflate ZIPs, automatically try `roms/aburner2.zip`, and open a native
ZIP picker when startup cannot load the ROM set. Successful selections are
remembered; chip sizes/CRCs and interleave remain checked. See the
[ROM startup record](ENGINE_NOTES.md#zip-rom-loading-and-startup-picker-2026-10-25)
and [setup instructions](../roms/README.md). Published packages are unchanged.

Local widescreen visibility update (2026-10-25): source builds now retain
world explosions across the native horizontal retirement boundary in 16:9
and 16:10. Detached animation keeps native pause, hidden-frame and
depth/vertical retirement rules. Independent MAME calls, branch coverage and
rejected controls are recorded in the
[world-explosion record](ENGINE_NOTES.md#wide-world-explosion-retention-2026-10-25).
Other object controllers and broader scene/platform acceptance remain open.

The subsequent source increment adds ground/air hit-effect retention through
their separate horizontal cull, preserving ground drift, scale growth and ROM
animation/palette rules. Independent MAME coverage and integration scope are in
the [hit-effect record](ENGINE_NOTES.md#wide-ground-and-air-hit-effect-retention-2026-10-25).

The following source increment adds horizontal retention for player/enemy
missile smoke and world-projected player damage/death emissions. Visual copies
retain each family's lifetime rules and the native emission-pool reset, with
no changes to missiles, native rings/pools, damage, sound or respawn. See the
[three-family record](ENGINE_NOTES.md#wide-missile-trails-and-player-damage-emissions-2026-10-25)
for independent MAME comparisons and finite coverage limits.

Local sky correction follow-up (2026-10-25): Corrected ground now uses the
published horizon marker ordering when the current roll has crossed an axis
before the displayed strip. This removes the reproduced Stage 6 white sky
triangles at both wide render widths. See the
[fast-roll evidence](ENGINE_NOTES.md#stage-6-fast-roll-sky-triangles-2026-10-25)
for regression scope; included in macOS Preview 5.

The follow-up also closes reproduced blue cloud gaps and further white sky
cuts by prioritizing measured marker ordering and copying only observed,
matching flat-color ground fields inside one render clip. The expanded test
checks both measured sides throughout the Stage 6 replay; see the
[two-sided horizon record](ENGINE_NOTES.md#stage-6-two-sided-horizon-correction-2026-10-25)
for evidence and finite coverage.

macOS release source is tagged `v0.1.0-preview.5`; the package manifests identify the
exact source commit. The SDL3 native game is
on `main`; the separate Three.js prototype remains on
[`codex/threejs-game-engine`](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine).

| Area | Implemented / validated | Still open |
| --- | --- | --- |
| Native game | Cold boot, attract, coin/start, flight, weapons, HUD, original audio and transitions; 12,466 recovery PCs / 78,670 identical fixtures / 207 retained controls | New execution paths, standard-credit recording and continuous whole-credit fidelity |
| Whole host | Expanded 781-frame independent MAME corpus: CPU/bus/device/RGB/audio comparisons pass with zero supplied execution/state/read seeds | Finite input/operand coverage; broader DIP/reset/persistent-state paths |
| Natural lifecycle | Four replays match ten MAME columns and native original-image reconstruction across 104,452 frames, zero skips; ending run visits all 23 stages | Name entry, NVRAM, alternate carrier/collision paths and whole-credit CPU/device/RGB/audio |
| Menus and controls | Video/Controls/Audio/DIP submenus, [Stage Select / Practice](PRACTICE_MODE.md) and [Shuffle](SHUFFLE_MODE.md), live bound-input calibration display, scroll hints, persisted settings, keyboard/gamepad/raw-joystick remapping and hotplug | Service/coin options, full Practice entry-state fidelity and broader physical device acceptance |
| Display and ground | Five GPU filters plus Off, four opacity controls, full-throttle-only flame translucency, optional ground correction, [high-DPI HUD preview](HUD_PRESENTATION.md) with Sharp 3x/custom indexed text tiles and [CRT display presets](CRT_DISPLAY.md) | Semantic sprite instruments, production replacement art, other backends, continuous terrain projection and sustained performance |
| Widescreen | 398×224 and 358×224 previews in macOS Preview 3, detached scenery, queue/depth-drop recovery and horizon/water extensions; 23 directed slots per aspect preserve 22,540 counted native RGB/PCM/time frames | Broader controller/scene audit, ultrawide, other platforms and sustained load; gameplay object pools remain native |
| Music | Six WAV/FLAC mappings, preview/fallback, independent gains, -12 dB replacement baseline and live active-song changes; all six native song contexts and natural fades observed | Replacement-file natural fade/listening acceptance |
| Spatial audio | Chip-rate OpenAL Soft, headphone HRTF, 2.1/5.1 profiles, dry stereo music, reviewed routing and stall recovery; 117,509 exact MAME ownership observations and 104,452 unchanged native lifecycle frames; original Deluxe stereo wiring traced | Revised-cue listening, physical surround/unplug and Windows/Linux checks, end-to-end latency, ABII cabinet-revision evidence; planned object-to-voice ownership and moving HRTF sources |
| Feedback | Cannon/missile/impact/boost cues; 4,200 native frames unchanged; macOS Xbox Bluetooth body and both triggers physically confirmed | Windows/Linux/fallback hardware, full gameplay feel, warning cues and advanced FFB |
| Distribution | ROM-free macOS Preview 5 and runtime-verified Windows Preview 2 ZIPs with SDL3/OpenAL Soft 1.25.2, sources, notices, manifests and checksums | macOS ad-hoc signed; Windows unsigned and awaiting the new features; Intel/older macOS and Linux packages remain open |

Local source update: the viewport selector now offers Original,
16:9 (398×224) and 16:10 (358×224), with width-specific detached visibility and
centered HUD/menu placement. `test`, `verify-stage1`, independent geometry,
18 geometry mutation runs and Metal menu/wide GPU checks pass. Each aspect's
23 directed stage slots preserve 22,540 counted native RGB/PCM/time frames.
See [the local widescreen validation records](WIDESCREEN_PLAN.md#delivered-preview-and-remaining-gates).
The complete `required` gate and supplemental `full` profile pass on the
integrated 2026-10-19 macOS source; see the validation record below. Broader
scene/platform and sustained-load work remains open.
macOS Preview 3 includes this work. The source also has a
controller-only live Calibration screen and revised headphone HRTF gain. The
user confirmed responsive controller values and improved front/rear placement
with External Headphones. See [engine evidence](ENGINE_NOTES.md#input-calibration-and-macos-spatial-audio-recheck-2026-10-18).

Local source addition (2026-10-22): the [HUD presentation preview](HUD_PRESENTATION.md)
adds Original / Sharp 3x / user indexed text art and high-DPI output. Native
RGB/PCM/time, text priority/palette/frontiers, 4K software/Metal layouts and the
complete local `test` / `verify-stage1` checks pass; see
[the HUD evidence](ENGINE_NOTES.md#high-dpi-hud-text-presentation-2026-10-22).
Sprite-based instruments, production replacement art and sustained/platform
acceptance remain open. This does not replace the complete merge/release gate.

Local source addition (2026-10-23): the opt-in
[HD player-aircraft prototype](SPRITE_REPLACEMENT.md) now loads a versioned,
registered pack containing neutral flight and gentle banking in both directions,
including their runtime ROM LOD families. Native RGB/PCM/time is unchanged across
3,004 comparison frames; 1,206 active-flight draws cover both families across
three viewport widths. Software/Metal rendering at 3x and 4K, 35 retained negative
controls and the 160-frame MAME video reference pass. Fixed bounds, pivots,
scales and nozzle review points travel with the art. Other poses and unsupported
overlaps retain Original; complete animation, production registration and
sustained/platform acceptance remain open. See
[the registered-pack evidence](ENGINE_NOTES.md#registered-aircraft-pack-and-gentle-bank-2026-10-23).

Local source addition (2026-10-23): [CRT display](CRT_DISPLAY.md) adds Off /
Subtle / Arcade presets with independent scanline and RGB-mask strengths on the
completed game/HUD image, followed by readable frontend UI. Software/Metal
pixel, renderer-state, 4K, high-DPI resize and six retained negative controls
pass. Native replay preserves 781 RGB/PCM/time frames, zero skips. The later
unlocked-desktop recheck passed actual fullscreen/windowed transitions; an
accepted API request alone is not fullscreen acceptance.
See [CRT evidence and measured limits](ENGINE_NOTES.md#optional-crt-display-presentation-2026-10-23).
These local presentation checks do not extend the previous complete hardware
gate to the changed frontend tree; sustained load and other platform acceptance
remain open. Optional richer effects are covered by the Slang follow-up below.

Local follow-up (2026-10-23): [RetroArch Slang presets](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal)
add an optional macOS Metal `.slangp` host through pinned librashader, native
224-row input, live preset selection and renderer switching. CRT Easymode,
CRT Geom and CRT Royale have been exercised locally. The host retains the
original board framebuffer and audio, and frontend UI draws after the shader.
Windows/Linux shader backends, HDR, parameter UI, first-input mipmaps and sustained
preset performance remain open. Shader packs and runtime binaries are not
committed or added to published packages. See the separate
[shader validation record](ENGINE_NOTES.md#retroarch-slang-presentation-2026-10-23).

On Windows 11 x64, both ROM-free continuation tests (`windows-fiber` and
`worker-thread`) pass, and native `afterburner2.exe` with Vulkan GPU sprite
filters (`renderer=gpu`, `filters=gpu`), OpenAL Soft 1.25.2 headphone HRTF,
fullscreen switching, 16:9 widescreen, Corrected ground, ScaleFX 3x, all four
translucency toggles and Integer/Pixel-Art scaling sustains locked
`simulation_fps=59.6374 display_fps=59.6374` across 420-frame Stage 1 and
Stage 5 profiles (`board_ms=9.15–10.24 ms`, `wide_scene_ms=1.27–3.94 ms`,
`filter_submit_ms=0.22–0.45 ms`). The integrated source subsequently passed
the macOS gate below.
See [Preview 2 release notes](RELEASE_NOTES.md),
[Windows runtime and pacing notes](ENGINE_NOTES.md#windows-runtime-openal-soft-and-widescreengpu-pacing-validation-2026-10-16),
and the focused plans for scope.

## Next acceptance tasks

macOS Preview 5 includes the 25 October ROM startup, menu, Metal sprite-filter,
horizon and widescreen effect updates. The complete final-source `required`
release gate passes; see the [release record](ENGINE_NOTES.md#preview-5-release-2026-10-26).
The remaining acceptance work below stays open.

macOS Preview 4 includes the default integrated renderer and the complete
required/supplemental and natural-lifecycle results recorded in the
[Mac renderer acceptance](ENGINE_NOTES.md#integrated-primary-renderer-macos-acceptance-2026-10-24).
Its package manifest and GitHub release identify the exact source commit and
independent extracted-package checks. This is a macOS refresh; Windows remains
on Preview 2.

The integrated macOS Preview 3 source passed the complete final-source
`required` release gate on 2026-10-23 at
`build/workflow/20261023T185259.944984Z/`: `test`, `verify-stage1` and the
complete `oracle-test`, including all 207 recovery controls. Its complete
Shuffle comparison matches 100,000 independent frame observations. See
[the release record](ENGINE_NOTES.md#preview-3-release-2026-10-23).

The 2026-10-22 macOS checkpoint passed the complete `required` gate: unchanged-source
`test` / `verify-stage1` at `build/workflow/20261022T160051.739352Z/`, clean
`oracle-test` at `build/workflow/20261022T162126.082489Z/`, and supplemental
`full` at `build/workflow/20261022T161927.407826Z/`. The spatial HRTF lifecycle
replays also preserve 104,452 native RGB/PCM/time frames. See
[the scope and gate record](ENGINE_NOTES.md#spatial-audio-routing-and-playback-acceptance-2026-10-22).
macOS Preview 3 refreshes performance, 16:10, calibration, spatial audio,
HUD/CRT, aircraft presentation and Practice/Shuffle. Refreshing the Windows
package and extending platform acceptance remain open. See the
[Preview 3 release record](ENGINE_NOTES.md#preview-3-release-2026-10-23).

On 2026-10-15, the user reported finishing the game from start to finish on
macOS with the saved Normal / three-ship configuration and current enhancements.
See [the manual completion settings](ENGINE_NOTES.md#user-reported-macos-completion-2026-10-15).
This supplies manual completion evidence; an input recording and independent
whole-credit comparison remain outstanding.

1. **Standard single-credit completion:** the uninterrupted Easy / 99-credit
   recording reaches the ending naturally and matches every named observation.
   Obtain a successful default three-life physical recording as well. Keep
   the declared settings; do not substitute `--stage` for progression.
2. **Remaining transition paths:** the default failure, continue and scoring
   recordings pass. Extend collision-cause, high-score name entry, NVRAM
   persistence and alternate carrier/lifecycle coverage, reporting actual
   states/actions rather than assuming requested inputs succeeded.
3. **Close newly exposed instruction paths:** translate from ROM, add
   independent MAME fixtures and rejected mutations, preserve older coverage,
   then repeat the affected gameplay and required regression gates.
4. **Whole-credit comparison:** extend the passive MAME oracle to the same
   sustained physical-input runs; compare CPU/bus, device, RGB and audio state
   without supplied execution or replacement state.
5. **Configuration and lifecycle coverage:** test alternate DIP settings,
   additional operands/branch outcomes, and hardware reset/suspend paths.
6. **Sustained performance:** measure representative and peak-load stages with
   rendering, audio and presentation active. Optimization must preserve update
   order and observed behavior; finite benchmarks do not guarantee 60 fps.

Enhancements can be developed alongside this work using the contracts in
[ENHANCEMENT_ARCHITECTURE.md](ENHANCEMENT_ARCHITECTURE.md). Keep an original
mode as the regression reference; new features do not close full-game coverage.

Local enhancement increment (2026-10-23): [Stage Select / Practice](PRACTICE_MODE.md)
adds an explicit fresh directed startup, F5 retry and cold arcade return.
The runtime ROM catalog matches 23/23 independent MAME entries, zero skipped,
with four rejected catalog mutations. The existing palette check remains
56/56 words identical. Local `test`, complete `verify-stage1` and final SDL
menu/Practice launch checks pass. Complete stage-entry state, Time Attack and natural
whole-credit acceptance remain open; see the
[Practice evidence](ENGINE_NOTES.md#stage-select--practice-first-increment-2026-10-23).

Local Shuffle follow-up (2026-10-25): generated runs cannot open with scripted
stages 5, 13 or 23. All stages remain in the order, with stage 23 fixed last;
seeded retries remain reproducible. Policy regressions and all nine ROM-policy
controls pass; see [the validation record](ENGINE_NOTES.md#shuffle-gameplay-openings-2026-10-25)
for scope and integration results.

Local enhancement increment (2026-10-23): [Shuffle](SHUFFLE_MODE.md) randomizes
gameplay stages 1–22 once each, keeping stage 23 as the native finale. It runs
continuously with normal score/lives, same-order F5 retry, New Shuffle and
canonical Arcade/Practice return. The fixed-finale native run visits all 23
positions and completes ending, ranking and attract across 61,478 physical
frames. The earlier Shuffle checkpoint passed the complete `required` profile,
including all retained mutation checks. Paired-selector cleanup adds 180 MAME
fixtures and 20 rejected controls; a later post-ending credit-prompt continuation
adds 250 fixtures and 19 controls across the complete prompt routine. An attract
controller/callee closure adds 286 independently verified fixtures and 22
rejected controls. The complete retained instruction corpus passes
78,670/78,670 comparisons, zero skipped.
The final arranged-MAME comparison matches all 100,000 frame observations,
including every stage, ending, ranking and the later attract sequence. See
[Shuffle evidence and finite coverage](ENGINE_NOTES.md#shuffle-mode-2026-10-23).

## Object-based spatial audio (planned)

Added 2026-10-26. Extend the fixed front/rear cockpit categories with opt-in
moving HRTF sources. Start with missile flybys and positioned world explosions,
then enemy weapon launches, player missiles and verified aircraft/rear threats;
keep player propulsion anchored behind the listener. A chasing plane should
sound from its actual rear bearing only after its relative position, chase
state and sound ownership are verified. Continuous enemy engine audio requires
an appropriate native voice or a separately specified optional added sound.

The prerequisite is passive object-to-voice ownership through sound delivery
and FM/PCM allocation, with per-family coordinate/lifecycle contracts and
separate rendering of supported voices. Preserve native gameplay, sound timing
and exported PCM; ambiguous ownership retains the existing fallback. This is
future work, separate from current playback acceptance. See the ordered tasks,
MAME/mutation gates and listening/performance requirements in the
[spatial audio plan](SPATIAL_AUDIO_PLAN.md#object-based-hrtf-planned).

## Android / Android TV port (planned)

Added 2026-10-26. Build on this Apple Silicon Mac using the shared C++/SDL3
engine. First milestone: a ROM-free, controller-driven ARM64 APK with original
graphics/audio and user ROM import, running on a real Android device. Android TV
shares the engine and adds TV launcher integration and controller/remote menu
navigation. Touch controls and richer presentation effects are later increments.
No Android build or device acceptance is claimed yet.

- [ ] Set up a working JDK, Android SDK/NDK, Gradle and ADB; pin toolchain and
  dependency versions and document reproducible macOS build/install commands.
- [ ] Add Android activity/manifest and native shared-library packaging through
  SDL3/CMake; package required assets and notices without ROMs or captures.
- [ ] Implement Android ROM import and persistent app storage, settings/NVRAM,
  Back navigation, pause/resume, audio interruption and surface recreation.
- [ ] Add Android TV launcher icon/banner and manifest support without requiring
  a touchscreen. Verify startup, ROM import and all menus with controller/remote
  navigation, plus gameplay mapping and controller disconnect/reconnect.
- [ ] Validate Android rendering and audio backends. Measure the worker-thread
  CPU continuation fallback; add an Android ARM64 cooperative backend if needed
  for performance, preserving CPU/bus timing and the existing oracle contracts.
- [ ] When the user provides ADB access to a real device, install and launch
  builds, collect logs/screenshots and run repeatable startup, storage, lifecycle,
  input and gameplay checks. Record device model, Android version, ABI, GPU,
  build identity and exact test scope; phone and TV acceptance remain separate.
- [ ] Run device-side deterministic replays against independent MAME references
  for CPU/bus/device, RGB and audio observations. Follow the validation cadence
  above, including full regression and retained mutations for shared execution
  changes and the complete required/supplemental gates for acceptance milestones.
- [ ] Measure sustained representative and peak-load gameplay on target hardware,
  including frame pacing, thermal throttling and audio stability. Obtain user
  checks of controller feel, perceived latency, listening quality and TV display
  behavior; ADB checks alone do not close physical hardware acceptance.

Record implementation and device evidence in ENGINE_NOTES as work proceeds.
Lower-cost TV performance, optional shaders/spatial audio and broader device/ABI
support remain unverified until measured on the corresponding hardware.

## Linux x86-64 / ARM64 builds and remote testing (planned)

Added 2026-10-26. Produce separate ROM-free Linux x86-64 and ARM64 packages
using the shared C++/SDL3 engine. Support reproducible builds from this Apple
Silicon Mac through a Linux VM/container and cross-toolchains or emulation, and
native builds on user-provided remote Linux hosts. No Linux package or hardware
acceptance is claimed yet.

- [ ] Establish versioned build environments for both architectures, including
  SDL3 and optional OpenAL Soft dependencies. Declare supported distributions
  and minimum glibc/runtime requirements; document build and packaging commands.
- [ ] Produce architecture-specific packages with required assets, dependency
  notices, source/build identity and checksums; exclude ROMs and captures.
- [ ] When the user provides SSH access, sync the intended source revision,
  build and run regression/oracle checks with locally supplied ROMs and fixtures,
  collect logs and retrieve packages. Keep ROMs/captures ignored and record host
  architecture, distribution, compiler, dependencies and exact test scope.
- [ ] Run native deterministic replays on both x86-64 and ARM64 against
  independent MAME references for CPU/bus/device, RGB and audio observations.
  Apply the validation cadence above, including full regression and retained
  mutations for shared execution changes and complete required/supplemental
  gates for acceptance milestones.
- [ ] Validate Wayland/X11 startup, ROM picking, settings/NVRAM persistence,
  fullscreen/resize, audio routing and controller disconnect/reconnect. Use an
  accessible active desktop/GPU/audio session for graphical remote testing;
  headless SSH checks alone do not establish desktop hardware acceptance.
- [ ] Measure the worker-thread CPU continuation fallback on each architecture;
  implement faster Linux continuation backends if needed without changing
  CPU/bus timing. Validate GPU filters and extend the current Linux Slang stub
  for optional shader support, recording supported backends and limitations.
- [ ] Measure sustained representative and peak-load pacing and audio stability
  on native hardware of both architectures. Obtain physical controller-feel,
  perceived-latency and listening checks; VM, cross-build and emulated results
  do not establish native hardware performance or acceptance.

Record implementation and remote/native evidence in ENGINE_NOTES as work
proceeds. Keep x86-64 and ARM64 acceptance separate, with explicit GPU, desktop
session and hardware coverage limits.

## Phase overview

| Phase | Status |
| --- | --- |
| 1: Scaffold | Complete |
| 2: Video hardware | Implemented; finite MAME raster/ownership coverage passes |
| 3a–3d: buses, scene, input, objects and host | Live translated host; declared independent corpora pass, unobserved paths remain open |
| 3e: stages and game flow | All 23 directed slots and a natural ending transition stream pass; broader lifecycle acceptance remains |
| 3f: full-game equivalence | Continuous whole-credit CPU/device/RGB/audio comparison open |
| 4: Audio | Original Z80/YM2151/SegaPCM and declared MAME audio corpora pass; full-credit contexts remain |
| 5: Enhancements | Practice/Shuffle, filters/opacity, HUD/CRT, remapping, music, basic rumble, OpenAL and 16:9/16:10 previews implemented; broader acceptance remains |

Focused enhancement plans are linked from the [documentation guide](README.md).
Detailed ROM discoveries and earlier checkpoints remain in ENGINE_NOTES and Git
history; ROMs, recordings and captures must remain ignored.
