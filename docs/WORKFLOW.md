# Efficient local workflow

Build/play basics are in the [project README](../README.md). Use this reference
for checks, captures and instruction recovery; see the [documentation guide](README.md)
for other topics.

Read the live rules, checkpoint and ordered next tasks:

```sh
python3 tools/workflow.py context
rg -n 'topic|ROM-address' docs/ENGINE_NOTES.md src/main tests tools/oracle
sed -n 'START,ENDp' path/to/relevant/file
```

The context command extracts the rules, current checkpoint and next tasks
from ROADMAP. Follow its instruction-level translation, MAME comparison,
mutation and coverage rules.
Read relevant engine notes before making a port. Search first and read bounded
sections; generated `.inc` tables and entire historical documents are expensive
to put into an assistant's context.

## Checks and logs

```sh
python3 tools/workflow.py run main        # daily main-functionality checks
python3 tools/workflow.py run quick       # alias for main
python3 tools/workflow.py run integration # retained broad tests + Stage 1
python3 tools/workflow.py run joint       # current 120-frame joint oracle
python3 tools/workflow.py run full        # 600-frame joint + reset window
python3 tools/workflow.py run required    # only for native-execution/emulated-hardware changes
python3 tools/workflow.py run oracle-subirq -j 4  # any focused make target
```

Run separate workflow/Make invocations serially: their shared configuration
stamps use fixed temporary paths. Parallelize targets within one invocation
with `-j`; concurrent wrappers can race even when compiler flags are identical.

The runner executes real make targets every time, stops on the first failure,
preserves exit status, and writes full output plus timings/evidence in
`build/workflow/<UTC-run-id>/`. Chat output contains at most eight matching
result lines per successful target or eighteen final lines on failure.
These excerpts are navigation aids: inspect the log for detailed coverage,
unverified paths and diagnostics. JSON records describe checks that actually
ran; absent later targets have not passed. Checks are not cached or skipped.
The `full` profile supplements the engine-change `required` profile.
The `main` profile runs `test-main`, `verify-game-session`, `verify-practice-mode`
and `verify-shuffle-session`. It checks ROM loading/picker, settings, controls,
input, pacing, renderer, HUD/CRT, aircraft, filters, feedback, music/audio
playback and the Arcade, Practice and Shuffle lifecycles. It omits the full
oracle/mutation suite and complete stage/lifecycle sweeps. Add affected geometry,
GPU, extended audio or package checks when those change. `test`, `verify-stage1`, `oracle-test` and all their fixtures
remain intact. **Run the full gate only when a change affects native execution
or emulated hardware behavior.** Engine-header edits, passive observers, metadata,
frontend work, and test/fixture/comparison-only changes do not independently
trigger it. Other work uses `main` plus affected checks; documentation-only work
uses inspection. Releases add package validation and do not independently trigger
the full gate. There is no periodic merge-count full-suite gate.
Profiles do not automatically capture or regenerate missing fixtures.

Choose gates and evidence reuse by the
[ROADMAP validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first).
The runner has no automatic result cache. `oracle-test` includes retained
mutations; `--only` is available for focused mutation work.

The default is `-j min(8, logical CPU count)`; override with `-j N` when
sharing the machine. Do not change emulated clocks or weaken fixture/mutation
coverage to reduce wall time.
`benchmark-cpu-continuation` measures host synchronization overhead, not game FPS.

`make test` and `make verify-stage1` now share incremental C++ objects under
`build/check_obj/`. Compiler dependency files cover transitive headers and
generated includes; changing compiler/options invalidates these objects.
Repeated checks rerun executables without recompiling unchanged sources.
`make clean-checks` removes shared check objects and all top-level regression,
fixture-helper and integration executables named after `tests/*.cpp` / `tests/*.mm`, plus alternate backend
variants, excluding `oracle_*` programs. `make clean-oracles` removes oracle executables and
`build/oracle_obj/`; it preserves the native objects shared with the game.
`make clean-native` removes those native objects and `build/afterburner2-release`.
`make clean` removes the default game's objects and executable. Run these only
when a fresh build is wanted; ordinary checks retain incremental compilation.
All four commands preserve `build/oracle/`, mutation directories, dependency
installations, packages, validation logs and local recovery tools. CMake build
trees have their own `cmake --build build/<tree> --target clean` command.

## ROM ZIP startup checks

Source builds accept `--roms directory|set.zip`; the default source also reads
`roms/aburner2.zip`. Interactive startup offers a ZIP picker if loading fails;
`--frames` checks fail promptly instead. Successful picker paths are stored
beside settings in a `.roms` sidecar. See [ROM setup](../roms/README.md).

```sh
python3 tools/workflow.py run test-romloader test-rom-picker -j 4
python3 tools/workflow.py run verify-native-roms -j 4
```

The first two targets are ROM-free and included in `test`. The native check
uses the user's existing extracted `roms/` to generate an ignored nested ZIP,
then compares 240-frame final software images for directory, explicit ZIP,
remembered ZIP and default ZIP startup. It also tests bounded missing-source
failure. Fixtures, images and logs stay in `build/oracle/rom-zip-startup/`.
The picker callback tests cover selection, cancel, error, quit and late replies;
physical native dialogs still need a usable desktop. Snapshot mutation runners
that compile a copied ROM loader must retain `src/third_party/miniz` at the
loader's relative include path. No new external ZIP library is required.

## Integrated renderer checks

The default source frontend shares primary road/tile layers at native partial
frontiers. Keep `--scene-renderer snapshot` for differential comparisons.
The complete required profile includes the original whole-host oracle plus
`oracle-integrated-host-mutations` at the retained 358/398 widths, and
independent integrated RGB comparisons across four corpora and five widths.

```sh
python3 tools/workflow.py run oracle-integrated-host-mutations -j 4
python3 tools/workflow.py run verify-integrated-lifecycle -j 2
python3 tools/workflow.py run verify-integrated-shader-main -j 4
```

The lifecycle target retains all four natural recordings at 358 and 398 pixels;
its two width targets can execute concurrently. It compares live primary layers
with snapshot reconstruction for every frame, with ground correction both off
and on, while retaining the independent MAME observations and native image
checks. The shader target requires a working desktop GPU and optional shader
runtime; it exercises actual frontend renderer switching and rollback at all
four menu viewports. These supplemental checks do not replace an applicable engine `required` gate or
establish exhaustive platform/performance coverage.

## macOS preview release

The current downloadable preview targets **Apple Silicon, macOS 26+**. This is
the deployment target of the executable and its bundled SDL3/OpenAL libraries,
not a claim of compatibility with older macOS or Intel Macs. It is ad-hoc
signed, without Developer ID signing or notarization.

Before publishing, run the final-tree `main` profile, affected checks and build
`native-release`. Retain the full logs and inspect failures; a focused check
does not substitute for the release gate. Complete any additional checks
required by the roadmap for the affected code (including `required` for engine
changes). Prepare the matching upstream
SDL3 3.4.16 source archive under `build/deps/SDL3-3.4.16.tar.gz` from
[the official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16).
The optional OpenAL builder provides its library, source archive and notices.

After committing the intended tree:

```sh
python3 tools/package_macos.py v0.1.0-preview.5
```

Use a fresh version for later releases. The packager requires a clean working
tree, verifies the pinned dependency source hashes and refuses to overwrite an
existing package. It creates an allowlisted folder and ZIP under ignored
`build/releases/`, rewrites library paths, removes build-machine search paths,
ad-hoc signs the binaries and records source commit/file hashes. It creates a
runtime ZIP and a matching `-sources.zip` companion, each with a SHA-256 file
and manifest. The companion contains the complete committed project tree
(including developer docs) and pinned SDL/OpenAL source archives. The runtime
keeps the launch/ROM instructions, assets and one `THIRD-PARTY-NOTICES.txt`,
with identical notice texts stored once, alongside `LICENSE.txt`. Its launch
guide links to documentation at the exact source commit. ROMs, captures,
soundtracks and preferences
are excluded. Keep the shared libraries replaceable for local compatible builds.
The package also copies the four allowlisted player-aircraft pack files into
`assets/player-aircraft/` and includes the librashader header notices. The
optional shader runtime and user shader packs remain external; see
[shader setup](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal).

Before uploading, extract the ZIP into a separate folder. Verify its SHA-256,
executable permissions, signatures and dependency paths, then launch from a
different working directory with temporary settings and user-provided ROMs.
Check a bounded Metal launch and OpenAL output; remove all temporary ROM links
from the extracted smoke copy. Check the companion's manifest/archive hashes
and exact project commit too. Retain both ZIPs and checksums locally. The current
macOS release publishes the runtime ZIP and checksum, with release-note links to
the exact project source commit and pinned upstream dependency sources; the
user requested that its source companion not remain a public release asset.
Release notes must name the exact commit, minimum OS/architecture, validation
scope, missing ROMs, signing status and remaining preview limitations.

## Experimental Windows preview release

Windows public downloads are withdrawn. The retained 28 October Preview 5
refresh is diagnostic-only; see [signing and publication](WINDOWS_SIGNING.md).
`tools/package_windows_runtime.py` produces its separate runtime/source ZIPs,
including librashader and its DirectX/Microsoft C++ runtime dependencies. It
requires a clean committed tree, a Release/OpenAL native build, the pinned
librashader/DirectX source archives and app-local Microsoft C++ runtime files:

```sh
python3 tools/package_windows_runtime.py "$AB2_VERSION" --label "$AB2_LABEL" \
  --build build/windows-base-hosted --deps build/windows-base-inputs --profile base \
  --verified-runtime "$AB2_SIGNED_RUNTIME" --signature-receipt "$AB2_SIGNATURE_RECEIPT"
```

Use `--unsigned-signing-input` instead of the two signing arguments only for
nonpublic staging. The packager checks receipt coverage and signed-file hashes.
Set `AB2_VERSION`/`AB2_LABEL` to a fresh version/date label and
`AB2_MSVC_REDIST` to the directory containing the runtime DLLs. Use verified
dependency files; package directories
are never overwritten. The runtime has no tests, general docs or optional
asset packs. The source companion retains covered sources and the manifest.
Inspect both archives and launch an extracted runtime; the current release's
scope and unresolved performance/recording limits are in
[release notes](RELEASE_NOTES.md#compact-windows-refresh-2026-10-28) and
[Windows status](WINDOWS_PERFORMANCE.md). The full-layout packager below remains
available for development/distribution variants.

The current standalone base is built with
`python tools/windows/build_base.py --package-sources` on Windows, or the manual
`.github/workflows/windows-base.yml` hosted workflow. Static SDL/compiler
runtimes and dynamically loaded optional OpenAL eliminate upstream startup DLL
imports. The compact packager defaults to `--profile base`; see
[the current signing, source and launch requirements](WINDOWS_SIGNING.md).
The full-layout commands below describe historical development packages and
must not be used to restore Windows downloads without those publication checks.

Preview 5 includes an experimental **Windows 10/11 x64** package. `tools/package_windows.py`
supports both a native Windows x64 Release build (`--native-build build/win`,
with SDL3 3.4.18 and OpenAL Soft 1.25.2 enabled) and the macOS LLVM-MinGW
cross-build (`build/windows-cross`). The artifact is unsigned.

Current Windows source also includes the macOS Preview 3 features and native
Vulkan RetroArch shaders. Use the [LTO-capable Windows build](../README.md) and
[shader setup](CRT_DISPLAY.md#windows-vulkan). w64devkit GCC 16.2.0 was built
without LTO; LLVM-MinGW 20261020 / Clang 23.1.4 supports checked ThinLTO.
`AB2_REQUIRE_IPO=ON` makes an unavailable LTO toolchain a configuration error.
The packager now copies the same four allowlisted aircraft assets and
librashader notices as macOS and includes detected LLVM-MinGW runtime DLLs.
Preview 5 has a compact 28 October Windows refresh; older Preview 2 assets
remain retained. A new package requires a clean committed tree and the current
main-functionality/affected-check release policy; engine changes also require
the complete `required` gate. See [Windows limitations](WINDOWS_PERFORMANCE.md).

To package a native Windows x64 build on Windows (with `SDL3-3.4.18.tar.gz`,
`SDL3-3.4.18-mingw.zip`, `openal-soft-1.25.2.tar.gz` and
`openal-soft-1.25.2-bin.zip` in `build/deps/`):

```sh
cmake -S . -B build/win -G Ninja -DCMAKE_BUILD_TYPE=Release -DAB2_ENABLE_OPENAL=ON \
  -DSDL3_DIR="$PWD/build/deps/SDL3-3.4.18/x86_64-w64-mingw32/lib/cmake/SDL3" \
  -DOpenAL_DIR="$PWD/build/deps/openal"
cmake --build build/win --target afterburner2 cpu_continuation_regression cpu_continuation_thread_regression -j4
python3 tools/package_windows.py v0.1.0-preview.2 --native-build build/win \
  --mingw-notice "$AB2_MINGW_NOTICE"
```

Set `AB2_MINGW_NOTICE` to the complete MinGW-w64 runtime COPYING notice supplied
by the compiler distribution (for example `COPYING.MinGW-w64-runtime.txt` in
w64devkit, or the target's `share/mingw32/COPYING` in LLVM-MinGW). Native
packaging rejects an omitted, empty, missing or symlinked notice before creating
an artifact. The cross-build path extracts its notice from the pinned toolchain
archive. Preserve additional notices required by a different compiler/runtime;
the [license inventory](../LICENSES/README.md) records the supported paths.

Alternatively, `tools/package_windows.py` packages the committed, clean
`build/windows-cross` Release/LTO cross-build using LLVM-MinGW 20261006 / Clang
23.1.3 and the official SDL3 3.4.16 MinGW SDK:

```sh
cmake -S . -B build/windows-cross -G "Unix Makefiles" \
  -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release -DAB2_ENABLE_OPENAL=OFF \
  -DCMAKE_CXX_COMPILER="$AB2_TOOLCHAIN_ROOT/bin/x86_64-w64-mingw32-clang++" \
  -DCMAKE_RC_COMPILER="$AB2_TOOLCHAIN_ROOT/bin/x86_64-w64-mingw32-windres" \
  -DSDL3_DIR="$AB2_SDL_ROOT/x86_64-w64-mingw32/lib/cmake/SDL3"
cmake --build build/windows-cross --target afterburner2 cpu_continuation_regression cpu_continuation_thread_regression -j4
python3 tools/package_windows.py v0.1.0-preview.2 \
  --toolchain "$AB2_TOOLCHAIN_ROOT" \
  --toolchain-archive "$AB2_TOOLCHAIN_ARCHIVE" --sdl-archive "$AB2_SDL_ARCHIVE"
```

The packager checks pinned SHA-256 digests for all bundled DLL and source
archives, verifies x64 PE headers, recursive DLL dependencies and the game's
Windows fiber imports, rejects unbundled non-system DLLs, and includes both
continuation tests.

The ZIP contains sources, notices, setup/launch scripts, payload hashes and a
separate checksum. No Microsoft system DLLs, ROMs, captures or personal settings
are bundled. Independently extract and verify all manifest hashes and DLL closure
before uploading the original ZIP/checksum. Preserve earlier releases.

## Capture discipline

For widescreen capacity and stage/lifecycle work, use focused targets first:

```sh
python3 tools/workflow.py run oracle-wide-requests verify-wide-visibility-mutations -j 4
python3 tools/workflow.py run verify-wide-stages verify-wide-lifecycle-mutations -j 4
```

`verify-wide-stages` covers all 23 ordered ROM slots, both bank-input windows,
pitch/weapons, pause, repeated viewport switches and restart. Its stage skips are
directed setup, not natural progression. `verify-wide-lifecycle` replays the
retained ending, neutral, continue and weapons inputs through the wider renderer;
it compares all ten MAME transition columns on every frame and checks row coverage
and terrain/protected-layer preservation. `verify-wide-lifecycle-mutations` also
rejects an altered MAME mode. It needs the ignored `natural_*` recordings and
`natural_*_sequences.mame.csv` files. Whole-credit CPU/device/RGB/audio equivalence
remains a separate acceptance scope. Acceptance follows ROADMAP's scope-based cadence; complete `required`/`full`
runs are reserved for engine changes.

Lifecycle replay uses **observed MAME pins**, preserving input-update latency:
`natural_ending_sequences.inputs` for ending, and each other capture's
`natural_*_sequences.mame.csv.inputs` companion. The earlier requested
`natural_*.inputs` files are retained but are not interchangeable with those
observed inputs. No frames are shifted or omitted to make a comparison pass.

Reuse a valid capture when its inputs, probe and capture script still apply.
After changing capture semantics or relevant MAME instrumentation, regenerate
that target and record the change. Use short/focused captures during iteration
and the full acceptance corpus for final evidence. Never weaken oracle flags,
mutation checks or coverage requirements to shorten a run.

Use stable target filenames instead of accumulating debug copies. Keep any
new exploratory outputs under `build/oracle/`; remove them once their findings
are recorded and the canonical fixture has replaced them. Keep referenced
fixtures and hand-written local tools. Inspect space with `du -hd 1 build/oracle`
before launching large captures. No cleanup runs automatically.

The 2026-10-15 local storage audit consolidated eleven historical recovery
snapshots into `build/oracle/cpu_recovery_complete.bin`: their plan entries and
fixture payloads were verified byte-for-byte as prefixes of that retained
corpus before removing the duplicate files. No unique fixtures, coverage
reports, merge provenance or mutation inputs were removed. Historical filenames
in earlier notes can be reconstructed with:

```sh
python3 build/cleanup/restore_recovery.py cpu_recovery_before_enemy_overflow.bin
```

The ignored `build/cleanup/recovery-prefix-audit.json` records original hashes,
sizes, timestamps and reconstruction ranges; `retained-corpus.json` identifies
the retained corpus. Keep these local recovery tools and manifests alongside
the captures. Restoration verifies the original SHA-256 before publishing a
file. Preserve the canonical corpus, or an append-only successor containing its
unchanged plan/fixture prefixes; a changed prefix fails restoration.

The same audit shared storage for 36 byte-identical captures/traces using APFS
copy-on-write clones. All existing names and modification timestamps remain;
each file can still be rewritten independently. `build/cleanup/shared-exact-files.json`
records the original hashes and peer paths. File-size tools such as `du` may
count the shared extents repeatedly; filesystem available space reflects the
actual savings. These clones need no special restore step or test-loader changes.

No additional service or dependency is needed for this workflow: Python,
make and compiler dependency files provide the improvements locally. Full
logs, ROM images and captures remain under gitignored `build/`.

## Native settings menu

Build with `make native-release` and run `./build/afterburner2-release`
from the repository root. The initial menu offers Arcade, Practice, Shuffle,
Settings and Quit. Arcade resumes the cold board without hidden frames or
automatic inputs,
so the original boot and attract sequence is displayed. Insert a coin and press
Start to play (keyboard 5, then Enter; gamepad Back, then Start). Esc or F1 opens
the menu during play. Use arrows and Enter/Space, or click a row; Left/Right
changes an option. Esc returns one page, restoring its selected row and scroll
position, then resumes the previous session from the root. The pause menu offers
Resume, Restart Run, Change Mode, Settings and Quit. Change Mode offers Arcade,
Practice and Shuffle; launching a mode ends the current run. Restart Run retries
the active Practice stage or Shuffle order, or restarts the Arcade board.
Opening the menu pauses the board and audio and preserves a pre-existing pause.
P still pauses/resumes and F5 restarts outside the menu.

Settings groups the existing frontend options into short pages:

| Page | Options |
| --- | --- |
| Display | Display Mode, Aspect Ratio, Output Scaling |
| Graphics | Sprite Filter, HUD Style, Ground, Transparency submenu |
| CRT and Shaders | CRT Mode, Scanlines, RGB Mask, Shader Preset |
| Controls | Input Device, Button Bindings, Analog Settings, Input Monitor, Feedback |
| Audio | Music/Effects Volume, Output Setup, Music Tracks, Track Volumes, Reset Volumes |
| Arcade Settings | Lives/Credit, Difficulty, Ship Increase, Apply and Restart |
| Diagnostics | Performance overlay |

Controls and Audio appear when their frontend services are available. Feedback
separates cue toggles and finite motor tests from the strength/device settings.
Input Monitor displays raw and mapped controller axes; it does not perform an
automatic calibration. Settings still save immediately through the existing
preferences; Arcade Settings require Apply and Restart for the active board.

Display provides nearest-neighbour, smooth (SDL linear), integer nearest-neighbour
and SDL Pixel Art scaling (available with SDL 3.4+), plus desktop fullscreen.
Graphics provides the separate sprite filter; Diagnostics offers Off/FPS/Detailed
statistics. Original scaling preserves the native 320×224 aspect ratio;
integer mode uses whole-number multiples. F11 toggles fullscreen and F3 cycles
statistics during play. Detailed statistics show simulated and displayed FPS,
wall time per displayed frame, CPU time per simulated frame, sprite filter/upload
time and presentation time (including vsync wait). Values refresh once per second.

The Graphics → Sprite Filter row selects Off (default), EPX 2x, Scale3x or
ScaleFX 3x. These filter the visible hardware sprite plane after native zoom,
then compose it over nearest-scaled road/tiles. Sprite-drawn scenery is included;
foreground tile/text coverage and palette shadows remain native. Window scaling
still applies to the completed image, so choose Nearest or Integer to retain a
crisp HUD. xBRZ awaits a compatible licensing route; it is not offered as an
unimplemented or approximate mode. The SaI pair uses standard names rather than
the unrelated CrispSal/SmoothSal names. ScaleFX uses GPU compute on the native GPU path, with a CPU/software fallback.
Use the detailed Filter timing to compare modes; backend and load affect performance.

Graphics → HUD Style offers Original (default), Sharp 3x ROM-tile smoothing and Custom
indexed PNG artwork. The replacement text plane uses output-resolution drawing
and native palette/priority, centered at all four source viewport widths. The window
requests high pixel density; fullscreen uses the desktop resolution. See
[HUD presentation](HUD_PRESENTATION.md) for atlas format, `--hud-atlas`, fallback
and validation scope. Sprite-based instruments retain their native artwork.

CRT and Shaders → CRT Mode offers Off (default), Subtle, Arcade and optional RetroArch. Scanlines and RGB Mask
adjust independently in 5% steps. Presets reset both strengths; Off retains them.
The cached effect covers the final game/HUD image and leaves frontend menus/stats
readable. See [CRT display](CRT_DISPLAY.md) for pixel alignment, small-window
behaviour, persistence and resource limits. Run
`python3 tools/workflow.py run test-native-crt test-crt-mutations verify-gpu-crt`
for synthetic software/real-GPU pixel and state checks. GPU checks need a desktop
display; the normal `test` includes the software baseline and retained mutations.

On macOS Metal, **Shader Preset** selects a RetroArch `.slangp` from a complete
user shader pack and activates it after successful loading. Install the optional
pinned runtime with `python3 tools/deps/fetch_librashader.py`; normal builds and
built-in modes need no runtime. `--shader-preset path.slangp` selects it at launch;
`--shader-runtime path.dylib` overrides runtime discovery. Shader mode uses native
224-row input (including reduced-resolution HD art/HUD), CPU sprite filters and
SDR output. See [setup and limits](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal).
The explicit `verify-native-slang-mutations` and `verify-native-shader-main` gates
require that runtime and a usable Metal display; the latter needs ROMs. They
exercise deterministic authored shader references, timing/history controls,
GPU/Metal switching, preset rollback and unchanged paused board output.
`test-librashader-fetch` runs offline and is included in normal `test`.

Settings save immediately to `settings.cfg` in SDL's per-user preference directory
for organization `AfterBurner2-Recomp`, application `AfterBurner2`.
`--settings-file path` overrides that location. Unsupported or invalid settings
fall back to defaults; failed changes leave the previous selection and show a
notice. ROM fonts are decoded at runtime for the menu and overlay, independently
of the board's text RAM and palette. No extracted assets are checked in.

The **Practice** menu (startup, or pause → Change Mode) selects any runtime ROM stage, starts a fresh session
with normal lives/damage, and offers Return to Arcade. F5 retries the active
Practice starting stage; cancelling stage browsing preserves that target.
`--practice-stage 8` launches the same mode directly. Practice is unavailable
while recording. See [Practice mode](PRACTICE_MODE.md) for directed-startup scope
and validation.

The **Shuffle** menu (startup, or pause → Change Mode) starts a continuous run through stages 1–22 once
in random order, followed by the native stage-23 finale. F5 retries the same order, New Shuffle draws a new seed, and
Return to Arcade restores the canonical board. `--shuffle` launches directly;
`--shuffle-seed 12345` reproduces an order. See [Shuffle mode](SHUFFLE_MODE.md)
for its explicit run-position and palette policy, commands and evidence.

`--boot-to-flight`, `--stage`, `--practice-stage`, `--shuffle` and bounded `--frames` runs bypass the initial menu.
`--menu-preview image.bmp` renders Settings and exits for visual inspection.
`--frame-preview image.bmp --frames count` saves the last presented bounded frame;
use an ignored path under `build/oracle/` for any ROM-derived image.
Use `python3 tools/workflow.py run test-native-settings test-sprite-filters verify-native-menu`
for persistence and ROM-backed SDL menu checks; the latter uses dummy video/audio.
These cover navigation, SDL options, rejection, Start/Quit and pause/frame isolation.

For native desktop play use `make play-native`; this builds the release binary,
opens the normal arcade launcher and shows measured FPS. Automatic coin/start
is opt-in through `--boot-to-flight`; selected-stage debug launches retain their
explicit shortcut. Run
`python3 tools/workflow.py run benchmark-native benchmark-native-extended`
with other heavy tests stopped to measure board/render costs. The extended
profile covers 960 flight/weapon frames; it excludes window presentation and
does not prove full-credit gameplay. `verify-native-release-recovery` checks
directed instruction fixtures with the same release compiler options.

Recovery gates retain `build/oracle/cpu_recovery.bin` and also require the
current complete manifest capture `build/oracle/cpu_recovery_complete.bin`.
If the latter is missing or its plan changed, run
`python3 tools/workflow.py run oracle-capture-recovery oracle-recovery`.
The capture target preserves an existing baseline. Both captures must replay
successfully before their combined coverage/mutations count as evidence.


## Focused oracle targets

These retained subsystem gates help investigate failures. They supplement the
required full suite and keep their original fixture boundaries; see the matching
ENGINE_NOTES sections for inputs, skipped races, mutations and capture details.

| Area | Replay targets |
| --- | --- |
| Z80 execution | `oracle-sound-execution`, `oracle-sound-execution-full`, `oracle-sound-irq`, `oracle-sound-execution-mutations`, `oracle-joint-sound-full` |
| Audio/device deadlines | `test-audio-deadlines`, `oracle-joint-audio-full`, `oracle-sound-audio-irq`, `oracle-sound-audio-timer-b`, `oracle-audio-deadline-mutations` |
| Mixed speaker | `oracle-speaker`, `oracle-speaker-edges`, `oracle-joint-speaker-full`, `oracle-speaker-mutations` |
| Video ownership | `oracle-joint-video-owned`, `oracle-joint-video-owned-directed`, `oracle-joint-video-mutations` |
| Instruction/exception coverage | `oracle-cpu-coverage`, `oracle-cpu-coverage-check`, `oracle-cpu-coverage-legacy`, `oracle-cpu-nested-irq`, `oracle-main-sub-coverage` |
| Cold boot | `oracle-joint-cold`, `oracle-joint-cold-full`, `oracle-cold-generated-check`, `oracle-joint-cold-boundaries`, `oracle-joint-cold-mutations` |

Find the specific capture target in Makefile before regenerating; names are
not uniformly derived from the replay target. Finish captures before dependent
comparisons. Cold captures must retain CPU phases through machine stop: exiting
from a CPU tap consumes the executing CPU's remaining clocks and invalidates
continuation evidence. Never seed a replacement context to hide a mismatch.

## Autonomous whole host and live game

```sh
python3 tools/workflow.py run oracle-whole-host
python3 tools/workflow.py run oracle-whole-host-full
python3 tools/workflow.py run oracle-whole-host-expanded
python3 tools/workflow.py run oracle-whole-host-mutations
python3 tools/workflow.py run verify-whole-host
python3 tools/workflow.py run verify-whole-host-headless
```

The first two reuse task 5's cold captures. Generate the expanded physical
ADC/weapon fixture with `oracle-capture-whole-host-expanded` and wait for the
capture to finish before replaying it. No capture file drives the host's
instruction, grant, deadline or state selection. All three positive runs and
fifteen owner mutations are required by `oracle-test`. `verify-stage1` keeps
the reconstruction regression suite and also runs `verify-whole-host`.
The live executable uses the same board host; 5 inserts a coin, Enter starts,
F5 recreates a cold board, and arrow/Z/X/Space/Left Shift feed physical inputs.
The 121-frame cold fixture contains only silence, so the speaker gain mutant
uses the 603-frame gameplay fixture's nonzero stream. A silent gain mutation
is not counted as a successful rejection. CMake exposes the new oracles under
`ORACLE_TESTS` and the native game integration under `STAGE1_ROM_TESTS`.

The resolved throttle, roll and missile stops retain their exact-PC fixtures
and mutations. Focused input smokes are `benchmark-native-full-throttle`,
`benchmark-native-rolls` and `benchmark-native-missiles`; each covers 960 flight
frames and reports observed entries. These are finite discovery/performance
runs, not whole-credit acceptance.

For later-stage instruction discovery, run
`python3 tools/workflow.py run stage-input-sweep`. Each ordered ROM stage
slot gets a fresh cold board and a planned 600-frame (~10.06-second) window:
75 frames each of full up/down, full bank left/right, opposite-endpoint
roll gestures left/right, Vulcan and repeated missile presses. Later slots
use test-only operands for the original ROM stage-advance event and stage
initializer; they are directed discovery fixtures, not natural progression
or MAME acceptance. Entry failures stop that case before any input window.
Diagnostics read backing memory directly: diagnostic bus reads would change
CPU continuation timing. `RESULT` and `COVERAGE` lines distinguish completed
input frames, observed roll-angle frames and weapon routine entries.
To reproduce one slot, use
`STAGE_SWEEP_ARGS='--stage 3' python3 tools/workflow.py run stage-input-sweep`.
The target succeeds when discovery finishes, even if cases report `status=stop`;
inspect those lines before claiming gameplay coverage.

To play a selected stage, use `make play-native STAGE=7`, or launch
`./build/afterburner2-release --stage 7 --profile` directly. Stage numbers
are 1-based ordered ROM slots (1..23 for this set). `--stage` automatically
boots with physical coin/start pulses, waits for active flight, and invokes
the same directed original-ROM transition as the sweep. F5 restarts at the
selected stage. Without `STAGE`/`--stage`, normal startup and F5 stay unchanged.
This is a debug skip, not natural progression. Any newly reached unported
path still stops with its original missing-PC diagnostic.

Selected-stage startup also executes a preceding alternate-road palette event
when the previous stage's ROM program contains one, then waits for the original
IRQ blend to finish. This avoids unset canyon colors after skipping directly
to stages 5, 8, 13 or 17. Compare these fourteen-word gradients against original
MAME with `python3 tools/workflow.py run capture-native-stage-palette verify-native-stage-palette`.
Reuse the local captures with `verify-native-stage-palette`; the focused check
does not replace the complete engine-change gate when that cadence applies.


## Natural recordings and transition observations

Record manual play from cold boot with
`./build/afterburner2-release --record build/oracle/manual.inputs`.
Coin/Start and every control are recorded against physical board-frame
numbers, including automatic `--boot-to-flight` inputs when requested.
Pausing adds no frames; F5 starts a separate `.restartN` recording.
`--stage`, `--practice-stage`, `--shuffle` and the Practice/Shuffle menus are rejected when recording. A stopped recording retains the
attempted failing frame. Its JSON sidecar identifies the loaded ROM regions
by length/FNV-1a digest and declares fresh persistent state and switch bytes.

`AB2PHYS1` is a text format: magic line; declared frame count followed by
three decimal switch bytes; then exactly one row per frame containing the
zero-based frame index, active-low button byte and eight raw ADC bytes.
Recordings and observations belong under ignored `build/oracle/`.
There is no gameplay RAM, CPU context, stage selector or invulnerability input.

Generate a discovery attempt and observe it in MAME:

```sh
python3 tools/natural_inputs.py build/oracle/neutral.inputs --scenario neutral --frames 12000
NATURAL_INPUT=build/oracle/neutral.inputs NATURAL_REFERENCE=build/oracle/neutral.mame.csv \
  python3 tools/workflow.py run capture-natural-credit
python3 tools/workflow.py run build/natural_credit
NATURAL_INPUT=build/oracle/neutral.mame.csv.inputs NATURAL_OUTPUT=build/oracle/neutral.native.csv \
  python3 tools/workflow.py run natural-credit-replay
NATURAL_INPUT=build/oracle/neutral.mame.csv.inputs NATURAL_OUTPUT=build/oracle/neutral.native.csv \
  NATURAL_REFERENCE=build/oracle/neutral.mame.csv python3 tools/workflow.py run oracle-natural-transitions
```

MAME field overrides have input-update latency. Keep the original requested
controls separate from the actual port observations in `.mame.csv.inputs`;
native comparison uses those observed physical pins. The replay script
rejects unsupported service buttons/ADC channels and mismatched switch values.
The `continue-multi-credit` scenario inserts three coin pulses during the
first life-exhaustion/continue window, exercising the plural-credit display
when two credits remain at a later continue screen. Use `--frames 10000`
and the same capture/replay/comparison commands above. The original `continue`
scenario remains unchanged.
`natural-credit` builds then replays; build once and use
`natural-credit-replay` for independent concurrent runs because ordinary
make invocations share release configuration/object files.

The CSV compares **every** frame's timestamp, mode, ordered stage index,
lives, damage-controller state, raw score word, packed credit byte and the
carrier/refuel selectors at `$FFFF90B0/$FFFF90E0` (ROM dispatch reads
`$103F2/$1624E`).
Native observations use backing memory, not timed bus transactions.
`.events.csv` separately retains the first carrier/refuel, shared `$E224`, stage-advance,
lives and weapon producer entry per stage; entry does not establish sequence
completion or every action requested. The comparison rejects missing,
extra, shifted or changed intervals, with five deliberate gate mutations.
An equal CSV does not establish whole-credit CPU/bus/device/RGB/audio identity,
collision cause, high-score name entry or NVRAM persistence. No ending is
inferred from a completed input window or from directed stage discovery.

Other discovery scenarios are `continue`, `weave` (weapons and controls),
and `credit-stock` (Easy / 3× Credits, repeated physical coins before Start).
The latter is an unusual multi-credit configuration, not default-credit
acceptance. `NATURAL_SECONDS` bounds the emulated MAME window; use 1515 for
90000 frames. `tools/oracle/export_natural_prefix.py` can declare a
contiguous prefix ending at an observed transition while preserving its
source input/ROM metadata and hashes. Retain the original discovery attempts
and report any stops beyond that window separately.

For a successful ending capture, declare the window through its first return
to attract, replay that entire prefix from cold boot, then require the actual
mode/stage sequence as well as equal observations:

```sh
python3 tools/oracle/export_natural_prefix.py build/oracle/stock.inputs \
  build/oracle/stock.mame.csv build/oracle/ending --frames 74452
NATURAL_INPUT=build/oracle/ending.inputs NATURAL_OUTPUT=build/oracle/ending.native.csv \
  python3 tools/workflow.py run natural-credit-replay
NATURAL_INPUT=build/oracle/ending.inputs NATURAL_OUTPUT=build/oracle/ending.native.csv \
  NATURAL_REFERENCE=build/oracle/ending.mame.csv python3 tools/workflow.py run oracle-natural-ending
```

The ending gate requires ordered flight slots 0–22, modes 17–22, ranking
25/26 and return 0/1, without gaps, plus observed carrier selectors 0–12 and
refuel selectors 0–7. Five additional mutations remove a stage, ending mode,
attract return or terminal sequence selector while keeping both CSVs
equal; all must fail the semantic check. This remains acceptance of the named observation
stream, not continuous whole-host acceptance. The endpoint above applies to
the documented `credit-stock` recording; inspect a different run's actual
transitions before declaring its endpoint.

For passive instruction discovery across a natural window, use
`capture-natural-census` with the same original requested controls and
`NATURAL_REFERENCE`/`NATURAL_SECONDS` settings. The isolated MAME probe
retains a 32-bit-word bitmap per 68000 and exports `.main.pcs` / `.sub.pcs`
at the endpoint. It observes instruction entry without writing game RAM,
registers, pins or clocks. It mirrors PCs to physical 22-bit addresses and
retains every even site, including RAM. The original MAME build is untouched.
Compare its actual pins and every named frame observation against the
ordinary reference before using its PC list to extend literal ports. This
is discovery coverage, not a CPU/bus/timing comparison. Reset and interrupt
vector sites plus unchanged observations are checked in the short smoke.

## Extending instruction coverage after a live stop

Read the original-ROM continuation and add only literal instruction sites to
`tools/oracle/main_recovery_pcs.txt`; generate the recovery translation and
capture metadata with `tools/generate_input_schedule.py --graph recovery`.
For a focused independent MAME capture of new sites:

```sh
RECOVERY_PC_FILTER="$PWD/build/oracle/new_pcs.txt" \
RECOVERY_INCREMENT_CAPTURE=build/oracle/new_instructions.bin \
python3 tools/workflow.py run oracle-capture-recovery-increment build/oracle_cpu_coverage
./build/oracle_cpu_coverage build/oracle/new_instructions.bin
python3 tools/oracle/check_cpu_coverage_mutations.py --jobs 3 \
  --only mutation-name --recovery build/oracle/new_instructions.bin
```

`RECOVERY_PC_FILTER` is a newline-delimited hexadecimal PC manifest. It must
select nonempty sites already in the recovery manifest. It changes only the
focused capture; the normal coverage gate still requires every declared PC
and both measured branch outcomes. `--only` also accepts comma-separated
names; full recovery mutation acceptance runs all retained mutations. Concurrent
mutations use separate source overlays and binaries, with passing baselines
checked first. No mutation is accepted merely because its baseline fails.

The full recovery capture defaults to `RECOVERY_CAPTURE_SECONDS=96`. Its
64,656 cases run sixteen per video frame after forty boot frames, requiring
about 68 emulated seconds. The earlier 64-second limit was insufficient for
regenerating the complete plan; focused increments keep their 32-second limit.

After the new instructions and mutations pass, run the directed stage sweep,
refresh the complete recovery capture with `oracle-capture-recovery`, and run
the checks required by the validation cadence above. Engine-change merge and milestone
gates include `test`, `verify-stage1` and the complete `oracle-test`. The
full capture allows 96 emulated seconds for the expanded fixture roster;
focused captures keep their bounded window. Keep older captures and coverage.


To retain a verified complete corpus and add independent increments without
recapturing its earlier fixtures, first preserve the complete capture and its
passing `.coverage.json`, then use:

```sh
python3 tools/oracle/merge_cpu_coverage.py build/oracle/merged.bin \
  build/oracle/retained.bin build/oracle/new_instructions.bin
./build/oracle_cpu_coverage build/oracle/merged.bin
```

The merger requires passing input replay censuses newer than their captures,
keeps the exact original plan entries and record bytes, writes atomically,
and records input hashes in an ignored `.merge.json`. It removes any stale
output census. The merged file is not accepted until replay and the complete
manifest/branch gate pass; relevant mutations and whole-host checks at the
applicable acceptance gate remain required.
A fresh `oracle-capture-recovery` can always recreate the complete manifest.


## Native SDL3 platform

The native application requires SDL3 3.4 or newer. Make uses `pkg-config sdl3`
and CMake uses `find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3)` with
`SDL3::SDL3`. SDL3 supplies the window, keyboard events, GPU rendering/compute
and audio stream; `GameSession` still owns board clocks, RGB and PCM.

Use SDL3's driver environment names for native headless checks:

```sh
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./build/afterburner2-release --stage 7 --frames 90 --profile
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=unavailable ./build/afterburner2-release --frames 120
```

`tools/oracle/capture.sh` runs the external MAME executable and retains its
SDL2 driver environment names. Native rendering requests vsync, letterboxes
the original logical resolution and explicitly uses nearest texture sampling.
Audio queues signed 16-bit stereo presentation PCM into an SDL3 playback stream;
Original at unity retains exact native output, while optional music settings use
verified stems and replacement files.
the input-side queue still bounds latency to approximately 100 ms. P pauses
the stream device, F5 clears queued PCM and restarts the selected board, and
a missing audio driver permits gameplay without a speaker.

For the stage-1 full-throttle/cannon regression window, generate physical inputs
with `python3 tools/natural_inputs.py build/oracle/cannon_boost.inputs --frames 3600 --scenario cannon-boost`. This holds the same actions as
Space+Z from frame 480 and retains cold boot/default switches. It is a bounded
discovery window, not a recording of a user's exact input trajectory.

`--scenario ground-awards --frames 12000` uses the existing Easy/99-credit stock
boot, then full throttle/cannon, varying stick position and missile pulses for
ground-target discovery. It changes physical inputs/DIP settings only. Verify
actual award-queue execution before claiming ground-target coverage; it does
not reproduce a user's exact trajectory by itself.

### Experimental 16:9 preview

Display → Aspect Ratio → 16:9 Preview selects 398×224 presentation with a centered
native HUD/menu. Original is the default. Published sprites and wider road/tile
samples are rendered from immutable partial-frame snapshots; gameplay stays
native. Detached scenery retention, wider Sub projection and extra horizon
segments cover the identified native-edge gaps. Native gameplay object limits
remain unchanged; complete scene/platform acceptance remains open.
See [WIDESCREEN_PLAN](WIDESCREEN_PLAN.md) for the remaining acceptance work.

```sh
python3 tools/workflow.py run verify-wide-geometry verify-wide-scene verify-wide-stages verify-wide-mutations verify-wide-visibility-mutations oracle-wide-projection -j 4
python3 tools/workflow.py run verify-wide-gpu
```

The GPU check requires a real device/display and compares all six filters under
opaque/translucent policies. `verify-wide-scene` generates its ignored local
fixture; CMake GPU checks also need that fixture first. Geometry/scene/stage checks are
part of `verify-stage1`, both mutation groups of `oracle-test`. All old fixtures remain.
Captures stay under `build/oracle/widescreen/`; never commit runtime ROM data.
World-explosion retention has additional focused gates:

```sh
python3 tools/workflow.py run oracle-capture-wide-explosions
python3 tools/workflow.py run verify-wide-world verify-wide-explosion-mutations verify-wide-visibility-mutations
python3 tools/workflow.py run verify-wide-explosions-natural
```

Reuse the supplemental capture while its operands/probe remain unchanged.
`verify-wide-world` retains both older world corpora and requires visible,
hidden, retired, both horizontal edges, near-depth, below-screen, pause and
animation advancement in the new independent ROM calls. `oracle-test` includes
that gate and all new mutations. Natural checks replay the complete retained
weapons recording through integrated/snapshot rendering at both widths; report
the actual retained-explosion count rather than assuming it exercises recovery.
Ground/air hit-effect retention uses the same comparison discipline:

```sh
python3 tools/workflow.py run oracle-capture-wide-hit-effects
python3 tools/workflow.py run verify-wide-hit-effects verify-wide-hit-effect-mutations verify-wide-visibility-mutations
python3 tools/workflow.py run verify-wide-hit-effects-natural
```

The helper gate retains `effects_directed.bin` and checks the supplemental
ground/air corpus with separate per-family lifetime, animation, age, sway and
ground-drift coverage requirements. Both the new gate and its mutations are in
`oracle-test`; old captures/controls remain. The natural target reuses the
complete weapons recording at both widths and reports ground and air retained
descriptor observations separately. A zero observation count is not recovery
coverage. Captures and logs stay ignored under `build/oracle/` and
`build/workflow/`.
`--profile` reports the added CPU scene cost as `wide_scene_ms`; compare Original
and Wide with the same audio/device setup before attributing pacing changes.

For player/enemy missile smoke and player damage/death emission retention:

```sh
python3 tools/workflow.py run oracle-capture-wide-trail-effects
python3 tools/workflow.py run verify-wide-trail-effects verify-wide-trail-effect-mutations
python3 tools/workflow.py run test verify-stage1 verify-wide-visibility-mutations
python3 tools/workflow.py run verify-wide-trails-natural
```

The capture target retains both immediate-frontier and 2 ms delayed profiles.
Only actual other-CPU writes to consumed inputs count as races. The delayed
corpus has per-entry lifetime/branch gates; the frontier corpus and older
missile, enemy-effect and companion recordings stay in the focused replay.
The divisor mutation deliberately also uses the retained missile corpus's
nonzero-Y operands. `oracle-test` includes the new comparisons/controls. The
natural target replays complete weapons and continue recordings at both widths,
reporting player/enemy/damage descriptor observations and native pool-reset
entries. Observe the counts before claiming a path was exercised.

### Widescreen viewports

Settings → Display → Aspect Ratio cycles Original → 16:9 (398×224) →
16:10 (358×224) → 21:9 (522×224) in either direction. Preview 5 includes the
first three; 21:9 is local source. Legacy `widescreen=1` still selects 16:9;
new files additionally persist `viewport=original|16:9|16:10|21:9`.

```sh
python3 tools/workflow.py run test-native-settings verify-wide-geometry -j 4
python3 tools/workflow.py run verify-wide-stages-21-9 -j 4
python3 tools/workflow.py run verify-wide-gpu-21-9 -j 4
```

The 23-stage 21:9 sweep generates Stage 1/5 fixtures; the GPU target consumes
those retained fixtures and compares six filters and both opacity policies on
a real GPU without repeating the sweep.
The retained 16:10 targets are `verify-wide-stages-16-10` and
`verify-wide-gpu-16-10`; the latter consumes existing fixtures. All three stage
sweeps are in `verify-stage1`; geometry mutations exercise all three widths.
They are affected-feature checks, outside the smaller daily `main` profile.
Physical display/platform, broader scene and sustained-load acceptance remain
open. See [the widescreen plan](WIDESCREEN_PLAN.md).

### SDL GPU sprite filtering

The native app requests an SDL GPU device with Metal/MSL or Vulkan/SPIR-V
support, then creates an SDL GPU 2D renderer. Vulkan/SPIR-V is also the Windows
compute path; Direct3D 12 compute shaders are not embedded. GPU compute shaders
implement all five sprite filters and protected background composition. If GPU
compute is unavailable, SDL selects an available renderer for presentation while
filters run on the CPU. This permits accelerated presentation where available;
dummy-video checks still use software. `--profile` reports both selections and
the CPU continuation backend.
The CPU board renderer and sprite feedback remain authoritative.

```sh
python3 tools/workflow.py run verify-gpu-sprite-mutations verify-gpu-native-menu
make shader-build
```

GPU checks require a real device/display service and fail if unavailable; they
do not silently skip. Ordinary `test` retains the CPU/reference coverage and
`verify-native-menu` uses dummy/software rendering. Shader rebuilding needs
`glslangValidator` and `spirv-cross`; normal Make/CMake builds use the embedded
shader header and need neither tool. Rebuild shaders after changing scaler
algorithms and repeat GPU comparison/mutations. GPU filter/upload statistics
measure CPU upload/command submission time, not isolated GPU execution time.

`verify-gpu-native-menu` additionally compares all five filters on sampled
native boot/launch/gameplay planes and retains the first 320x224 native fixture
under ignored `build/oracle/`. Use
`python3 tools/workflow.py run verify-gpu-native-fixture` to replay that retained
fixture after creating it with the native check. Existing fixtures are preserved;
new failures get separate frame/filter filenames.

### Optional original-art sprite transparency

Settings → Graphics → Transparency → Clouds, Trails, Explosions and Afterburner independently select Opaque
or Translucent; all default to Opaque and persist in the existing settings file. These options
work with Sprites Off or any scaler. Known original cloud art uses 160/255
opacity, missile-trail art uses 112/255, explosions use 176/255 and player flame
pens use 192/255; gameplay and native board
pixels remain unchanged. GPU composition retains covered sprites, foreground
tile priority and native shadow coverage. A CPU/software fallback is available.

`verify-gpu-sprite-mutations` includes opaque, zero-opacity and translucent
layer overlaps plus separate rejected cloud/trail/explosion/afterburner opacity mutations.
`verify-gpu-native-menu` exercises all sixteen option combinations and all six
scaler modes on sampled native boot/attract/launch/gameplay scenes, with actual
cloud/trail/explosion/player-flame observations required. `verify-game-session` checks individual
scene capture against a separate native host across 781 frames, audio and the
pause/toggle/restart lifecycle. This is finite coverage; other ROM art families,
whole-credit visuals and peak-load performance remain open.

### Native input remapping

Use Settings → Controls → Button Bindings for keyboard, gamepad and joystick
bindings. Analog Settings holds dead zone, sensitivity and throttle mode. The SDL3
gamepad database is embedded for offline deployment. Preferences live beside
`settings.cfg` in `settings.cfg.inputs`; `--settings-file` also relocates them.
Controls → Input Monitor is enabled after selecting a gamepad or joystick. It
displays bound stick/throttle values before and after dead zone and sensitivity,
including full-axis throttle; unplugging the selected device shows a disconnect
notice and returns Controls to Keyboard.
`python3 tools/workflow.py run test-native-input verify-native-menu native-release`
checks SDL virtual-device discovery/remapping/hotplug, UI and native compilation.
`test-native-input` is included in `test` and CMake CTest. Physical hardware
testing remains separate from virtual-device coverage. See README for bindings,
axis capture, full throttle, GUID sharing and reserved system shortcuts.

Music frontend checks: `python3 tools/workflow.py run test-native-music
verify-music-presentation oracle-music-routing oracle-music-mutations`. Generate
independent routing fixtures with `oracle-capture-music-routing` when absent or
when capture semantics change. The 1,800-frame fixture is required to exercise
actual FM effects voice ownership. Shared audio changes still require `required`.


### Optional ground horizon correction

Settings → Graphics → Ground offers Corrected (default on every platform) and Original. This removes
misplaced ground fill on the sky side of the native horizon strip in eligible
normal-road flight; other modes retain Original. It does not change gameplay or
implement continuous 3D terrain. Focused checks:

```sh
python3 tools/workflow.py run test-ground-horizon verify-ground-horizon verify-ground-fixture oracle-video-rolls verify-ground-mutations -j 4
python3 tools/workflow.py run verify-ground-gpu -j 4
```

The GPU gate needs a working graphics backend. The native gate retains generated
before/after images and a corrected-plane fixture under `build/oracle/horizon/`;
1560 scripted frames test both roll directions and original RGB/PCM/time identity.

`verify-ground-fixture` additionally replays the retained MAME roll corpus with
actual sky/terrain witnesses for both bank directions. It requires the capture;
ordinary ROM-backed integration does not. Ambiguous endpoint lines retain Original.

### Controller feedback

```sh
python3 tools/workflow.py run test-native-haptics verify-feedback verify-feedback-fixture verify-feedback-mutations verify-native-menu -j 4
# Regenerate compact passive MAME witnesses when needed:
python3 tools/workflow.py run capture-feedback capture-feedback-details -j 4
# Reuses the retained cold-boot natural ending input recording:
python3 tools/workflow.py run capture-feedback-sequences -j 4
python3 tools/workflow.py run verify-feedback-sequences verify-feedback-observer-mutation -j 4
```

`test` includes virtual SDL output/mixer checks; `verify-stage1` includes the
4,200-frame native RGB/PCM/time comparison. `oracle-test` includes MAME cue
witnesses, the retained attract-mode bus fixture and feedback mutations.
Sequence capture uses the retained `natural_ending_sequences.inputs` request file
and records the actual MAME pins in `feedback_sequences.txt.inputs` for native
replay. `verify-feedback-observer-mutation` runs the 17,000-frame native baseline
then rejects restoring the limited presentation filter. Captures stay in ignored
`build/oracle/`; never commit them. Physical acceptance
is separate: test every exposed motor in Settings → Controls → Feedback, then
exercise gameplay, pause, focus loss, restart and unplug on each target OS.
macOS Xbox Bluetooth body and both triggers are confirmed; Windows/Linux and
simple-haptic fallback hardware remain open.

## Optional spatial audio checks

```sh
python3 tools/workflow.py run test-spatial-audio verify-spatial-mutations -j4
python3 tools/workflow.py run test-spatial-sdl-only -j4
python3 tools/workflow.py run verify-spatial-game -j4
python3 tools/workflow.py run oracle-spatial-routing verify-spatial-ownership-mutations -j4
# Extended milestone checks, reusing the retained physical recordings:
python3 tools/workflow.py run verify-spatial-lifecycle -j4
AB2_SPATIAL_SUSTAIN_SECONDS=600 python3 tools/workflow.py run test-spatial-audio -j4
```

Soft loopback needs the optional OpenAL Soft dependency, without physical audio
hardware. `capture-spatial-routing` regenerates the independent 1,800-frame MAME
fixture under `build/oracle/spatial-audio/`. Unit/playback checks are part of
`test`; ownership and retained mutation checks are part of `oracle-test`.
Physical headphone/2.1/5.1 acceptance is separate from offline loopback.
The lifecycle target replays ending, game-over, continue and weapons inputs
through HRTF while comparing native RGB/PCM/time with an untapped board.
`--profile` reports software audio queues, stopped-source counts, synchronized
re-primes and backend recoveries; these exclude hardware latency. The SDL-only
target deliberately omits OpenAL headers/linking and checks truthful fallback,
pause/resume, native PCM and profile switching.


## Object audio tracking checks

```sh
python3 tools/workflow.py run test-object-audio oracle-object-audio verify-object-audio-mutations -j 4
python3 tools/workflow.py run verify-object-audio verify-object-audio-wire-mutation -j 4
# Regenerate only when the retained independent recording needs updating:
python3 tools/workflow.py run capture-object-audio
# Only changes affecting native execution or emulated hardware behavior
# require this complete gate; passive ownership metadata uses focused checks:
python3 tools/workflow.py run required -j 4
```

`capture-object-audio` reuses `build/oracle/natural_weapons.inputs` for 8,000
frames and writes `build/oracle/object-audio/weapons-v4.txt` plus the actual
MAME input-pin sidecar. The native integration uses that sidecar, not requested
inputs. The capture checks main writes, sound execution, raw missile coordinates
and frame timestamps; its header is `AB2OBJ02`. ROMs and recordings stay ignored.
The oracle compares all 2,244,500 records, with no skipped rows. This recording
contains `$D8` owned PCM launches, but no owned FM or `$6E/$6F` flyby commands;
protocol-only coverage must not be reported as natural-path acceptance.

`test-object-audio` is in `test`/`main`; `verify-object-audio` is in
`verify-stage1`; the oracle and ten metadata mutations plus the detached native
sound-observer mutation are in `oracle-test`. The wire mutation first requires
the unchanged 8,000-frame native baseline. All controls use header overlays and
retain full logs under `build/oracle/object-audio/mutations/`.


## Final carrier landing feedback checks

```sh
python3 tools/workflow.py run test-native-haptics verify-feedback-finale verify-feedback-finale-mutations -j 4
python3 tools/workflow.py run verify-feedback-finale-practice verify-feedback-finale-native verify-feedback-finale-wire-mutation -j 4
# Regenerate the separate, complete natural-finale witness only when needed:
python3 tools/workflow.py run capture-feedback-finale
```

The retained `feedback_sequences.txt` remains unchanged. The separate
`build/oracle/feedback_finale.txt` (`AB2FINH1`) captures 74,452 natural frames and
its actual-input sidecar. The fixture independently scans active finale aircraft
and checks the contact-flag edge, repeated TAS suppression and post-flight
rollout/stop. Native replay checks mode/time throughout and final-family
motion/events wherever the independently scanned aircraft exists. The paired
Practice cases exercise direct 23 and 22 → 23 with ordinary physical controls;
the latter steers to avoid losing all three lives before the finale.

`verify-feedback-finale-native` and `verify-feedback-finale-practice` are in
`verify-stage1`. The new fixture, seven metadata controls and detached flags-export
control are in `oracle-test`; the wire control first requires the passing
Practice baseline. Logs stay under `build/haptics/finale-mutations/`.
These passive presentation observers use `main` plus the affected checks above
and a relevant build/smoke. Their location in engine headers does not trigger
the full `required` gate; follow ROADMAP's core-execution boundary. Retain all
existing fixtures and mutation targets in the aggregate gate.


Object rendering extensions use these focused checks (no complete engine gate):

```sh
python3 tools/workflow.py run oracle-object-audio-families verify-object-audio-world verify-object-audio-mutations verify-object-audio-wire-mutation verify-spatial-game verify-spatial-mutations
```

`families-v3.txt` is the 8,000-frame expanded natural ownership/trajectory capture;
`world-v1.txt` is the 12,000-frame ground-award-route capture. Both use
`AB2OBJ03`. The second name describes the input route, not proof of naturally
sounding world explosions: its extended positive families are missiles, firing
aircraft and target-lock attachments. World coordinates instead use the retained
independent directed `world_directed.bin` projection witnesses.
Regenerate expanded captures with `AB2_OBJECT_FAMILIES=1`, plus
`OBJECT_AUDIO_INPUT`, `OBJECT_AUDIO_FRAMES` and `OBJECT_AUDIO_OUTPUT` environment
variables before `python3 tools/workflow.py run capture-object-audio`.
Keep all earlier fixtures and controls. Positive natural `$6E/$6F` remains open;
355 actual comparison executions in the second recording have zero eligible
unsigned depth conditions. Do not synthesize a command to claim natural coverage.
