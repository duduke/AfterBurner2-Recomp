# Android and Android TV preview

The Android host uses the shared C++ game and SDL3, with an ARM64 APK for
phones, tablets and Android TV. It includes a TV launcher entry, controller
menus and on-screen multi-touch controls on handheld devices. This is an
initial platform preview. A Samsung SM-S908E / Android 16 physical smoke covers
import/relaunch, landscape rendering, touch coin/start/steering/cannon and one
Home/surface-loss/resume cycle; see the [device evidence](ENGINE_NOTES.md#first-physical-android-phone-smoke-2026-11-01).
Sustained performance, broader lifecycle coverage and Android TV remain open.
The 2026-11-02 TCL Smart TV Pro (G08, Android 14) install attempt failed with
`INSTALL_FAILED_NO_MATCHING_ABIS`: its OS exposes only `armeabi-v7a,armeabi`,
while this APK packages only `arm64-v8a`. This TV needs a separately built and
validated 32-bit ARM package before launch or gameplay can be tested. See the
[TCL compatibility record](ENGINE_NOTES.md#tcl-android-tv-abi-compatibility-check-2026-11-02).

## Build on Apple Silicon macOS

The local helper pins Temurin JDK 17.0.21+9, Gradle 8.9, Android Gradle Plugin
8.7.3, SDK platform/build-tools 35, NDK 28.2.13676358, CMake 3.22.1 and SDL
3.4.16. Downloads have SHA-256 checks. SDK Manager validates its package
metadata. Android platform-tools follows Google's current package (ADB is a
host deployment tool, not part of the APK). Minimum Android is 8.0 / API 26;
only `arm64-v8a` is currently packaged. Native code is optimized even in the
debug preview. OpenAL and external Slang effects are excluded from this build;
original graphics and SDL audio are the baseline.

```bash
# The flag accepts the Android SDK package licenses; omit it for interactive prompts.
python3 tools/android.py setup --accept-sdk-licenses
python3 tools/android.py build
python3 tools/android.py check
python3 tools/android.py devices
python3 tools/android.py install --serial DEVICE_SERIAL
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`. This is a debug-signed
sideload preview, not a Play Store release. Installation updates the package
`org.afterburner2.app`; it preserves existing app data. Toolchains, caches and
SDK paths remain in ignored local directories. No system JDK or SDK is required.
A network using a corporate certificate authority may require a private Java
trust store via `JAVA_TOOL_OPTIONS`, using that network's trusted CA certificates.
Do not disable TLS verification.

The helper checks the APK for its ARM64 libraries, notices, source identity and
asset allowlist, launcher manifest, signature, native entrypoint and 16 KiB
ZIP/ELF alignment, and prints its SHA-256. Assets contain licenses and build
identity only. ROMs, oracle captures, soundtrack and private settings are never
packaged. `assets/build-identity.json` identifies the Git revision and a digest
of the working diff and untracked source files; a dirty development build is
not a published source revision. A public release still requires matching source
and applicable dependency source distribution under the retained licenses.

## First launch and controls

Supply your own complete `aburner2` ROM ZIP using Android's document picker.
The import is copied into private app storage, with a 64 MiB limit, and then
checked by the same per-file size/CRC loader as desktop. Subsequent launches
reuse that copy. No broad storage permission or runtime network access is
requested. Settings, control profiles and audio preferences use SDL's private
app preference directory. Uninstalling clears these files and the imported ROM.
Native high-score/NVRAM persistence is still an open shared-host acceptance
item; this preview does not add a guessed board-state save format.

## Custom soundtrack and shader pack imports

Available in `0.1.0-preview.8-android.1` (version code 4), the 7 November 2026
Android refresh of Preview 8. Install it over the existing debug preview to
retain private imports and settings.

Open **Settings → Audio → Music Tracks → Import WAV / FLAC** to select up to
64 files with Android's document picker. Imports are copied into the private
`soundtrack` library and appear in the existing per-track selection controls.
Choose a replacement for each track and preview it there. WAV/FLAC filenames
and container signatures are checked during import; full audio decoding still
occurs when selecting a track. Each file is limited to 256 MiB. Duplicate names
receive a numeric suffix, preserving existing files and track assignments.
The desktop `soundtrack/` behavior is unchanged.

Open **Settings → CRT and Shaders → Shader Packs → Import Shader Pack ZIP**
to copy a ZIP into the private shader library. Presets and their relative
folders, includes and textures are retained together. Imported `.slangp` presets
are listed after import and on later launches. **This Android build does not
include the external Slang renderer:** importing or selecting a listed preset
does not activate it or change the CRT setting. The menu reports that playback
is unavailable; the built-in CRT modes remain the usable Android effects.

ZIP imports allow at most 128 MiB compressed, 256 MiB expanded, 32 MiB per file
and 4,096 entries. They reject traversal, absolute/drive paths, symlinks,
case-insensitive duplicate paths, unsupported/encrypted entries and invalid
ZIP data/CRC. A pack must contain at least one `.slangp`. Packs publish only
when the complete extraction succeeds; filenames alone do not prove shader
compatibility or that every preset dependency is present.

Copying/extraction runs on a frontend worker, with cancellation, failures and
partial music-import counts reported in the menu. Original files remain in
their selected provider; no broad storage permission is added. Private imports
survive app updates and restarts but are removed by uninstalling or clearing
app data. Keep the original files for re-import. Phone/TV provider interaction,
cloud/USB imports and background/resume during copying still need physical
device acceptance. Host tests and an APK cross-build do not close those checks.

On a touchscreen, the lower-left gamepad-style joystick steers. Its small
round cap follows the thumb and returns to center on release. Round GUN and
MSL buttons sit side by side above it and fire independently. The far-right
vertical throttle slider moves upward for boost and downward for brake, with
analog intermediate positions; release or cancellation returns it to the
neutral middle. Round COIN, START and MENU buttons sit at the top center. HIDE, beside MENU,
hides and disables the touch controls and releases held inputs. Only SHOW
remains in that position; tap it to restore the overlay. This choice lasts for
the app session and survives pause/backgrounding; controllers and Android Back
remain available. Several
fingers can hold steering, throttle and weapons together. Fingers keep ownership
of the control they first touch until release; cancellation and backgrounding
clear held inputs. A connected controller remains usable alongside touch. See the
[touch layout validation](ENGINE_NOTES.md#android-touch-layout-revision-2026-11-01).
MENU opens the paused frontend. The joystick and slider each retain one finger
until release; a second finger cannot take them over. Tap menu rows, swipe vertically to reach more
rows, and use Android Back to return through menus. Android TV hides the touch
overlay and uses the existing controller mapping. Remote D-pad/center and Back
navigate menus; gameplay is intended for a game controller. Menu and Back open
the frontend during play. Use the menu's Quit action to leave the application.

TV ROM import requires a device document provider usable with its remote or
controller; provider availability varies by TV. A TV without a document
provider has not yet been validated. Import, all menu pages, controller
reconnection, safe areas, cutouts and display modes need physical checks.

SDL handles Android surfaces and pause/resume. Background/focus loss opens the
pause menu, stops feedback and clears queued audio; returning leaves gameplay
paused until Resume. Renderer reset events recreate frontend resources. These
paths have one physical phone smoke; broader interruption/recreation cases and
TV behavior remain unverified.

## Widescreen horizon wedges

For 16:9, 16:10 or 21:9, use **Settings → Graphics → Ground → Corrected**.
Corrected is the default for new preferences on every platform. An explicitly
saved Original choice bypasses the existing horizon fill corrections and can
expose the old tears/wedges during rolls in wider views. The correction is
independent of the aspect-ratio setting and persists
when switching ratios. Original remains available for the native reference.
The first phone's reported wedges were investigated with `ground=0`; enabling
Corrected uses the retained silhouette/ROM-based fixes. See the
[phone follow-up](ENGINE_NOTES.md#android-widescreen-wedge-settings-follow-up-2026-11-01).

## Validation and remaining device gate

```bash
python3 tools/workflow.py run main
python3 tools/workflow.py run test-native-touch test-native-touch-mutations
python3 tools/workflow.py run all
```

The touch regression covers simultaneous flight/weapons/throttle, radial stick
normalization, finger ownership and cancellation, multiple touch-device IDs,
menu edge events, background clearing, controller merging, renderer-state
preservation/readback and private-file import failure. Ten deliberate faults (vertical steering sign, stuck background input, dropped
cannon, reversed throttle, stuck canceled slider, square button hit areas,
retained inputs on hiding, active hidden controls, visible hidden controls and
an unbounded import) must be rejected. Existing ROM loader
negative/CRC tests remain intact. These host tests cannot validate Android's
actual document provider, GLES driver, audio routing or activity recreation.

For each phone and TV separately, record model, Android version, ABI, GPU,
APK SHA/build identity, controller and exact observations. Check clean install,
import/cancel/invalid ZIP, next-launch import reuse, preference persistence,
remote-only menu navigation, multi-touch, controller disconnect/reconnect,
Back/Home/resume, audio interruptions, surface recreation and sustained play.
Compare device-side deterministic runs to independent MAME references. Measure
frame pacing, audio stability and thermal behavior on representative and peak
stages. Android ARM64 now uses cooperative CPU stacks; worker continuations remain
available for differential checks. A 7,200-frame SM-S908E sample reaches the
original 59.64 Hz cadence; broader peak-load, thermal and TV acceptance remain
open. See the performance workflow below.
Follow the roadmap cadence: platform changes use main functionality plus
affected device checks; engine changes require the complete retained oracle and
mutation gate. No physical device or full-game fidelity acceptance is implied.

## Android performance and correctness checks

Measured on the SM-S908E / Android 16 with 16:9, Corrected ground and the
saved original-effects settings, over identical 7,200-frame debug Stage 6 runs:

| Build | FPS | Mean board work | 95th-percentile total work |
| --- | ---: | ---: | ---: |
| Worker baseline | 53.33 | 15.59 ms | 23.12 ms |
| Cooperative stacks | 59.63 | 8.84 ms | 12.83 ms |
| Cooperative, warmed repeat | 59.63 | 10.63 ms | 15.70 ms |

All three final-frame BMPs match exactly. The optimized runs complete work
within the arcade frame budget on 99.67% / 99.56% of measured frames. The worker
run reached thermal status 1; the warmed optimized check returned status 0.
Ordering/thermal differences limit a universal speedup claim. A separate
900-frame Stage 14 / 21:9 sample achieves 59.56 FPS, 14.07 ms work p95.
These are finite workloads, not full-game/TV or all-effects performance acceptance.

The physical arcade frame is 262 scanlines × 64 µs = 16.768 ms (about
59.6374 Hz). The Android continuation swaps AAPCS64 callee-saved registers on
separate stacks instead of waking worker threads at CPU checkpoints. CPU/bus
clocks, audio cadence and the number/order of physical frames remain unchanged.
ASan/TSan and shadow-call-stack builds retain workers. The worker baseline is
also available with `python3 tools/android.py build --worker-continuation`.

The debug APK accepts ADB-only `ab2.arguments` and `ab2.profile` extras for
bounded CLI runs. Release APKs ignore those extras. Keep the phone unlocked;
the helper rejects lockscreen, paused, incomplete and non-contiguous traces.
It returns to the normal paused frontend when done. A focus/background
interruption ends a bounded Android run and retains/rejects its partial trace. Timings buffer in memory
and write to private app storage at exit. No per-frame disk writes are added.

```bash
python3 tools/android_benchmark.py --serial DEVICE_SERIAL --stage 6 --frames 900 \
  --output build/android-tools/perf/stage6
# Optional copied configuration; preserves the normal saved viewport:
python3 tools/android_benchmark.py --serial DEVICE_SERIAL --stage 14 --viewport 21:9 --frames 900 \
  --output build/android-tools/perf/stage14-wide
python3 tools/android_device_checks.py --serial DEVICE_SERIAL
# Complete 781-frame fixture; needs about 5.56 GiB plus working space:
python3 tools/android_device_checks.py --serial DEVICE_SERIAL \
  --fixture whole_host_expanded --clean-device-fixture
python3 tools/check_android_continuation_mutations.py --serial DEVICE_SERIAL
```

The benchmark writes CSV/JSON and a fixed-final-frame BMP under ignored `build/`.
`--stage` is a debug entry for this bounded workload, not evidence of natural
stage progression. Compare identical settings/inputs and final-frame hashes.
Phase timings distinguish board, scene, ground, filter, present and pacing work;
short samples and averages do not guarantee every frame meets its budget.

Device checks compile separate native test executables (excluded from the APK),
copy the user's local ROM ZIP and the complete retained 121-frame independent
MAME fixture to `/data/local/tmp/ab2-checks`, and compare CPU/bus/device/RGB/audio
observations. They require authorization to transfer/run this test payload and
sufficient phone storage. ABI fault checks keep altered headers/binaries/logs
under ignored `build/`; both omitted FP control restore and broken stack unwind
must be rejected. These checks supplement the complete `required` profile for
engine changes and do not replace its retained fixtures or mutations.

The cold 121-frame and expanded 781-frame replays pass on the SM-S908E with
zero differences or skips. The expanded run compares 28,128,435 CPU snapshots,
73,593,282 bus phases, 55,982,080 RGB pixels and 627,841 mixed stereo frames.
Its temporary phone fixture was removed; all host originals remain retained.
