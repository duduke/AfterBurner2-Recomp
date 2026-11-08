# Roadmap

Updated **2026-11-04**. [Preview v0.1.0-preview.7](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.7)
adds Linux x86-64/ARM64 packages and native Vulkan Slang composition.
Physical Linux hardware and sustained performance acceptance remain open. [Preview v0.1.0-preview.6](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.6)
adds the Android ARM64 host, touch controls, cooperative CPU continuations and
21:9, and refreshes the Apple Silicon / macOS 26+ runtime. Corrected ground is
the shared default. It retains the integrated renderer, Practice/Shuffle,
HUD/CRT, optional aircraft art, calibration and desktop spatial audio. The
[Windows downloads are withdrawn pending signing/performance fixes](WINDOWS_PERFORMANCE.md).
The compact 28 October experimental refresh is retained for diagnosis.
Original remains the regression reference. See
[release notes](RELEASE_NOTES.md#v010-preview7--linux-and-vulkan-slang-2026-11-03).

## Summarized roadmap

| Priority | Workstream | Next outcome |
| --- | --- | --- |
| 1 | [Arcade fidelity](FIDELITY_PLAN.md) | Record standard single-credit completion; extend continuous MAME CPU/device/RGB/audio and remaining lifecycle coverage |
| 2 | [Widescreen](WIDESCREEN_PLAN.md) | Extend shared-renderer performance and controller/scene audits; retain snapshot comparisons and sustained 16:9/16:10/21:9 checks and separate physical ultrawide acceptance |
| 3 | [Controls/feedback](HAPTICS_PLAN.md) and [audio](SPATIAL_AUDIO_PLAN.md) | Windows/Linux hardware acceptance, HOTAS calibration, physical speaker/HRTF tests and remaining sound classification; [object-based HRTF](SPATIAL_AUDIO_PLAN.md#object-based-hrtf-planned), with rendering implemented for supported sources; positive natural flybys, additional families and listening remain |
| 4 | Performance and distribution | Measure peak-load pacing/latency; establish additional GPU/platform and macOS packaging compatibility; [Android / Android TV port and real-device ADB testing](#android--android-tv-port-initial-implementation); [Linux x86-64 / ARM64 builds and remote testing](#linux-x86-64--arm64-builds-and-remote-testing) |
| 5 | [Display and assets](ENHANCEMENT_PLAN.md#display-and-assets) | [Offline sprite extraction/mapping](SPRITE_EXTRACTION.md), [CRT scanlines/RGB mask and optional external presets](CRT_DISPLAY.md) and an opt-in [registered HD neutral/gentle-bank aircraft pack](SPRITE_REPLACEMENT.md); full replacement animation, built-in CRT glow/curvature, pacing and optional HUD improvements remain |
| 6 | [Gameplay extensions](ENHANCEMENT_PLAN.md#gameplay-and-content) | Extend the Practice/Shuffle/[Custom stage](CUSTOM_STAGES.md) interfaces for additional arranged rules; verify object/state contracts before new stages, save states or multiplayer |

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
7. **Validate the affected functionality.** Daily development and presentation,
   UI, packaging and documentation work use the main-functionality checks below.
   At the user's explicit direction (2026-11-05), run the complete gate only
   when the change affects native execution or emulated hardware behavior. Retain all fixtures, comparisons and mutations;
   a main-functionality pass is not whole-host fidelity acceptance.

**Validation cadence**

| Change or gate | Required validation |
| --- | --- |
| Documentation only | `git diff --check`; inspect changed links, commands and policy consistency. |
| Daily work, non-engine merges and releases | Run `python3 tools/workflow.py run main` plus checks for affected functionality and relevant build/smoke. Releases also verify the extracted package, notices, source identity and platform launches. No automatic full oracle/mutation run. |
| Presentation, input, playback or UI changes | Main profile plus affected geometry, image/audio, lifecycle or platform checks. Verify changed comparison gates against a passing baseline and relevant deliberate mutations. |
| Haptics, presentation audio metadata, and read-only presentation observers/hooks (including in GameSession or engine headers) | Main profile, affected cue/sequence and motor/mixer/lifecycle checks, and relevant build/smoke. New or changed native cue interpretation needs focused independent MAME coverage, relevant rejected mutations, and paired native RGB/PCM/time checks for the affected path. No automatic `required`, `oracle-test`, or unrelated CPU/device mutation run. |
| Changes affecting native execution or emulated hardware behavior: CPU/flags/timing, bus, scheduler, emulated devices, native game logic, literal ports, or generators that alter that execution/behavior | Complete `required` profile (`test`, `verify-stage1`, `oracle-test`) with all retained mutations; independent MAME baselines, relevant operand/branch coverage and affected natural recordings. Engine acceptance milestones also run supplemental `full` and applicable complete natural recordings. |
| New or changed literal ports, fixtures or comparison gates | Relevant independent MAME baselines, coverage and rejected mutations before wiring/acceptance. Recovery ports retain/replay the complete corpus with its manifest/branch gate. The complete gate applies only if the change also affects native execution or emulated hardware behavior. |

**Full-gate trigger: only changes affecting native execution or emulated hardware
behavior.** This is the user's explicit testing preference. Editing an engine
header, a fixture, a comparison gate, or instrumentation is not sufficient by
itself. Test-only changes still require their focused baselines, coverage and
rejected mutations; they do not independently trigger the whole suite.

Classify changes by their effect, not their directory, filename, or use of a
trace callback. Reading existing game state, adding passive cue/ownership metadata,
and tuning rumble or presentation playback do not count as core engine changes
when they leave CPU execution, emulated bus/device state, native game logic,
clocks and exported native PCM unchanged. The stage-23 landing rumble correction
falls in this presentation-observer category. Its full-gate runs are retained as
historical evidence, not a precedent requiring them for future haptic work.
If a task also changes execution, scheduling, bus/device behavior or native
logic, the core engine row still applies to that change.

There is no merge-count, release, milestone, or file-location trigger for full
oracle/mutation testing. Only a change affecting native execution or emulated
hardware behavior triggers it; other work uses the main profile and affected
checks (documentation-only work uses inspection).

Reuse an earlier pass only while its relevant sources and transitive
dependencies, compiler/options, fixtures, capture probe and comparison/mutation
harness are unchanged. Cite its log and scope explicitly; do not present it
as a new run or carry it across an affected change. A failure requires repair
and rerunning the failed gate and dependent checks. When an engine change requires a complete run, a focused
repair does not replace that gate. Existing tools do not implement automatic result caching.

The current `oracle-test` includes all retained mutation checks, so the full
regression gates above still execute them. Selective mutations are for focused
work; splitting the full regression and mutation commands or adding validated
hash-based caching is separate tooling work. Never delete fixtures, weaken
comparisons or skip unexplained failures to meet this cadence.

**Commands** (from the repository root)

```bash
make -j4                    # build the game: ./afterburner2
python3 tools/workflow.py run main  # daily main functionality (needs roms/)
make test                   # retained ROM-free suite
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

## Current checkpoint (2026-10-31)

- 2026-11-05: Local haptics adds separate Landing, Takeoff and Refueling levels,
  layered ground/departure feedback, nozzle coupling and sound-delivery-driven
  refill bumps. Passive MAME coverage includes the carrier and in-game airbase;
  the stage-23 finale now also has touchdown and post-flight rollout feedback.
  Physical feel and rebuilt packages remain open. See
  [sequence feedback](ENGINE_NOTES.md#layered-landing-takeoff-and-refueling-feedback-2026-11-05)
  and the [finale correction](ENGINE_NOTES.md#final-carrier-landing-feedback-correction-2026-11-05).

- 2026-11-04: Local haptics source adds stronger sustained player damage,
  directional missile launch feedback and per-effect Low/Med/High/Off choices.
  Independent MAME witnesses cover both missile sides and intact/breakup damage;
  updated physical feel and rebuilt packages remain open. See the
  [feedback change record](ENGINE_NOTES.md#stronger-and-directional-controller-feedback-2026-11-04).

- 2026-11-04: **Windows public downloads withdrawn; development fullscreen
  backlog/recovery fix implemented.** Preparing presentation before playback and
  rebasing host pacing after playback outages removes lasting audio queue debt
  in foreground-controlled startup, F11 and forced-pause checks. Final 21:9 runs
  at native cadence; shader/HRTF GPU stalls and occasional underruns remain.
  Signing, sustained whole-game acceptance, the Windows recording header bug
  and two user-reported graphical issues remain open. See
  [Windows evidence and exact scope](WINDOWS_PERFORMANCE.md#final-development-build-validation).
  2026-11-05: source is public and original code is GPL-3.0-or-later.
  Noncommercial SaI filters and unused legacy audio were removed from current
  source and retained on a named historical branch. A standalone Windows base
  statically links SDL/compiler runtimes and makes OpenAL optional, removing
  unsigned upstream DLL startup dependencies. Main/affected checks and the
  cross-build/import audit pass; provider approval, trusted signing and protected
  final-package launch remain open. Windows downloads remain withdrawn. See
  [the revised signing path](WINDOWS_SIGNING.md).

- 2026-11-01: **Corrected ground is the shared default on every platform**.
  Fresh settings and files without a ground preference select Corrected; explicit
  saved Original choices remain supported. See the
  [default-setting record](ENGINE_NOTES.md#corrected-ground-default-on-every-platform-2026-11-01).

- Local source adds **21:9 Preview (522×224)** alongside Original, 16:9 and
  16:10. The centered native HUD/menu and width-derived renderer are shared;
  sustained ultrawide performance and physical display acceptance remain open.
  See [widescreen](WIDESCREEN_PLAN.md) and the
  [current change record](ENGINE_NOTES.md#219-and-main-functionality-workflow-2026-10-31).
- Daily validation now uses the `main` profile (`quick` is an alias). Full
  oracle/mutation runs are reserved for engine changes. Full test sources,
  fixtures and mutations remain retained; see the cadence above and
  [workflow](WORKFLOW.md#checks-and-logs).
- macOS and Android packages are **Preview 6**; Windows public downloads are
  **withdrawn**, with Preview 5 retained privately for diagnosis.
  The macOS runtime uses a separate retained source companion;
  the Windows 28 October refresh retains unresolved FPS/audio dips. Source
  changes after those packages require a new build. See
  [release notes](RELEASE_NOTES.md),
  [macOS packaging evidence](ENGINE_NOTES.md#compact-macos-release-refresh-2026-10-29)
  and [Windows status](WINDOWS_PERFORMANCE.md).
- Main includes Practice/Shuffle, integrated primary rendering, GPU sprite
  filters, HUD/CRT, optional aircraft art, calibration, haptics and spatial audio.
  Native gameplay remains the regression reference. Latest complete integration
  evidence is in the
  [28 October gate](ENGINE_NOTES.md#completed-macoswindows-integration-gate-2026-10-28);
  finite checks do not establish whole-game fidelity or sustained performance.
- Custom stage experiments remain isolated on
  [codex/custom-stage-prototype](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/custom-stage-prototype).
  Android/TV host implementation is in progress. Linux Preview 7 x86-64/ARM64
  packages are available; hardware/performance acceptance and remaining object-audio coverage stay open below.

Dated implementation details and earlier checkpoints remain in
[Engine Notes](ENGINE_NOTES.md), [release notes](RELEASE_NOTES.md) and the
feature plans linked in the [documentation guide](README.md).

## Next acceptance tasks

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

Practice and Shuffle are implemented; complete Practice entry-state fidelity,
Time Attack and natural whole-credit acceptance remain open. Retained Shuffle
MAME comparisons cover 100,000 frame observations across all stages, ending,
ranking and attract. See [Practice](PRACTICE_MODE.md), [Shuffle](SHUFFLE_MODE.md)
and their linked evidence for exact finite scope.

<a name="object-based-spatial-audio-planned"></a>
## Object-based spatial audio

Experimental rendering is implemented for supported sources (5 November 2026).
Owned FM/PCM contributions use a bounded 24-direction grid per chip, with causal
movement carried in queued samples. Native PCM, sound triggers and clock/device
behavior stay unchanged. Player missiles, firing aircraft and target-lock
attachments have natural ownership/trajectory evidence; world explosion
coordinates have independent directed MAME projection witnesses.

Natural FM/PCM coverage passes across 8,000- and 12,000-frame recordings, with
paired native RGB/PCM/time and production loopback comparisons. The 12,000-frame
recording observes 355 real flyby comparisons and zero eligible unsigned depth
conditions. Positive natural `$6E/$6F` remains open; no command or engine loop is
invented to fill it. Remaining work includes additional typed families,
verified pursuer/voice contracts, listening and physical platform acceptance.
See [rendering evidence](ENGINE_NOTES.md#playback-aligned-object-hrtf-rendering-2026-11-05)
and the [spatial audio plan](SPATIAL_AUDIO_PLAN.md#object-based-hrtf-planned).

## Android / Android TV port (initial implementation)

2026-11-07: Android music and shader ZIP imports now copy selected provider files
into private libraries. Host import/menu/lifecycle checks and ARM64 packaging
are recorded in [the import record](ENGINE_NOTES.md#android-content-imports-2026-11-07).
External Slang playback remains unavailable on Android; physical provider and
phone/TV import acceptance remain open.

2026-11-02 TCL TV check: wireless ADB pairing/connection succeeded, but the
Android 14 G08 OS supports only 32-bit ARM and rejects the ARM64 Preview 6 APK
with `INSTALL_FAILED_NO_MATCHING_ABIS`. Add and validate `armeabi-v7a` packaging
before gameplay testing on that device; TV acceptance remains open. See the
[compatibility record](ENGINE_NOTES.md#tcl-android-tv-abi-compatibility-check-2026-11-02).

Updated 2026-11-01. The shared C++/SDL3 Android host now has ARM64 packaging,
phone/tablet multi-touch controls, TV launcher integration, controller/remote
menu navigation and private ROM ZIP import. Android Back/background handling
and renderer reset recovery are implemented. See [Android build and device
acceptance](ANDROID.md). A first SM-S908E / Android 16 smoke covers import,
relaunch, touch Stage 1 entry and Home/surface-loss/resume; SDL orientation and
fullscreen issues found there are fixed. Broader phone/TV acceptance, device-side
comparisons and sustained performance remain open (one attract sample: 52.6 FPS). No engine behavior was added for this
platform port.

- [x] Set up a working JDK, Android SDK/NDK, Gradle and ADB; pin build toolchain
  and dependency versions and document reproducible macOS build/install commands
  (`tools/android.py`; platform-tools is a rolling host-only deployment package).
- [x] Add Android activity/manifest and native shared-library packaging through
  SDL3/CMake; package required notices without ROMs or captures. ARM64 APK
  cross-build and manifest/signature/16 KiB packaging checks pass; see the
  [1 November implementation record](ENGINE_NOTES.md#android-host-and-touch-controls-2026-11-01).
- [x] Add handheld multi-touch steering, throttle, weapons, coin/start and menu
  controls, including a spring-centered throttle slider, round top-center buttons
  and a gamepad-style joystick, plus a Hide/Show toggle beside Menu, with host
  regression and ten rejected adapter mutations. Physical
  touch feel, cutout/safe-area and multi-touch hardware checks remain open.
- [ ] Implement Android ROM import and persistent app storage, settings/NVRAM,
  Back navigation, pause/resume, audio interruption and surface recreation.
- [ ] Add Android TV launcher icon/banner and manifest support without requiring
  a touchscreen. Verify startup, ROM import and all menus with controller/remote
  navigation, plus gameplay mapping and controller disconnect/reconnect.
- [ ] Validate Android rendering and audio backends. Measure the worker-thread
  CPU continuation fallback; add an Android ARM64 cooperative backend if needed
  for performance, preserving CPU/bus timing and the existing oracle contracts.
  Android ARM64 cooperative stacks are implemented: the SM-S908E 7,200-frame
  comparison improves 53.33 to 59.63 FPS at the unchanged 59.637 Hz arcade
  cadence. Device continuation/FP/unwind checks and the 121-frame cold plus 781-frame
  expanded independent MAME whole-host replays pass; broader GPU/audio/TV and peak-load acceptance
  remain open. See the [Android optimization record](ENGINE_NOTES.md#android-arm64-cooperative-performance-2026-11-01).
- [ ] When the user provides ADB access to a real device, install and launch
  builds, collect logs/screenshots and run repeatable startup, storage, lifecycle,
  input and gameplay checks. Record device model, Android version, ABI, GPU,
  build identity and exact test scope; phone and TV acceptance remain separate.
- [ ] Run device-side deterministic replays against independent MAME references
  for CPU/bus/device, RGB and audio observations. Follow the validation cadence
  above, reserving full regression and retained mutations for engine changes;
  platform-only acceptance uses main functionality and affected device checks.
- [ ] Measure sustained representative and peak-load gameplay on target hardware,
  including frame pacing, thermal throttling and audio stability. Obtain user
  checks of controller feel, perceived latency, listening quality and TV display
  behavior; ADB checks alone do not close physical hardware acceptance.

Record implementation and device evidence in ENGINE_NOTES as work proceeds.
Lower-cost TV performance, optional shaders/spatial audio and broader device/ABI
support remain unverified until measured on the corresponding hardware.

## Linux x86-64 / ARM64 builds and remote testing

Added 2026-10-26; initial previews built 2026-11-03. Separate ROM-free x86-64
and ARM64 packages use the shared C++/SDL3 engine, exact source companions and
checksums. The Ubuntu 26.04.1 VMware x86-64 build targets Valve's sniper SDK /
glibc 2.31; the Debian forky QEMU ARM64 build targets a pinned Debian 12 SDK /
glibc 2.36. Both pass the complete main profile, continuation and ROM startup
checks, extracted SDK/host/X11/Wayland launches, and an independent MAME cold
replay: **121 of 121 frames identical, 0 skipped per architecture**.
These are finite architecture-native VM checks, not bare hardware acceptance.
Quiet Stage 7 samples are only **11.7 FPS x86-64** and **5.76 FPS ARM64** on
their different VMs/viewports, with audio underruns; neither establishes real-time
playback or a cross-architecture performance comparison. See
[build and validation](LINUX.md), the
[x86-64 record](ENGINE_NOTES.md#linux-x86-64--steam-deck-packaging-2026-11-03) and
[ARM64 record](ENGINE_NOTES.md#linux-arm64-preview-2026-11-03).
Physical Deck/ARM64 controls, lifecycle, audio and sustained performance remain open.

- [x] Establish versioned build environments for both architectures, including
  SDL3 and optional OpenAL Soft dependencies. Declare supported distributions
  and minimum glibc/runtime requirements; document build and packaging commands.
- [x] Produce architecture-specific packages with required assets, dependency
  notices, source/build identity and checksums; exclude ROMs and captures.
- [x] When the user provides SSH access, sync the intended source revision,
  build and run regression/oracle checks with locally supplied ROMs and fixtures,
  collect logs and retrieve packages. Keep ROMs/captures ignored and record host
  architecture, distribution, compiler, dependencies and exact test scope.
- [x] Run native deterministic replays on both x86-64 and ARM64 against
  independent MAME references for CPU/bus/device, RGB and audio observations.
  Apply the validation cadence above, reserving full regression and retained
  mutations for engine changes; platform-only acceptance uses main functionality
  and affected device checks. Initial scope is the 121-frame independent cold
  fixture on each VM; expanded/natural and physical hardware acceptance remain open.
- [ ] Validate Wayland/X11 startup, ROM picking, settings/NVRAM persistence,
  fullscreen/resize, audio routing and controller disconnect/reconnect. Use an
  accessible active desktop/GPU/audio session for graphical remote testing;
  headless SSH checks alone do not establish desktop hardware acceptance.
- [ ] Measure the worker-thread CPU continuation fallback on each architecture;
  implement faster Linux continuation backends if needed without changing
  CPU/bus timing. CMake Linux builds now share the Windows Vulkan Slang and
  GPU composition backend; validate sustained shader performance on physical
  hardware and retain the documented optional runtime/backend limitations.
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
| 5: Enhancements | Practice/Shuffle, filters/opacity, HUD/CRT, remapping, music, basic rumble, OpenAL and 16:9/16:10/21:9 source previews implemented; broader acceptance remains |

Focused enhancement plans are linked from the [documentation guide](README.md).
Detailed ROM discoveries and earlier checkpoints remain in ENGINE_NOTES and Git
history; ROMs, recordings and captures must remain ignored.
