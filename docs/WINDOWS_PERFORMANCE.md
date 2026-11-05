# Windows performance status

**Public Windows downloads were withdrawn on 4 November 2026.** The runtime ZIP
and checksum have been removed from the public release repository, and its
download index and Preview 5/6/7 notes mark the withdrawal. The original artifact
is retained privately and locally for diagnosis; no replacement has been released.

## Windows 11 hardware investigation — 4 November 2026

SSH diagnostics on the user's Windows 11 Pro machine (10.0.26300.9818), Intel
i5-13400 and Radeon Pro WX 5100 / driver 31.0.21925.1001 establish two separate
release concerns:

- Windows Code Integrity events **3033 and 3077** explicitly record earlier
  attempts by `afterburner2.exe` to load `librashader.dll` being blocked by the
  signing policy. Shader runtime signing is therefore part of the release
  problem, not just executable signing. The user's installed executable now has
  a locally trusted, self-signed `AfterBurner2 Local Signer` certificate; this
  does not establish trust on a fresh machine. Subsequent development testing
  used Smart App Control Off with the user's explicit approval; this is not a
  release signing solution. No certificate trust stores were changed.
- The original Preview 5 runtime ZIP was downloaded through authenticated
  GitHub access, checked against the release asset digest and accompanying
  checksum (`93a859b66b07ef0a97107dcc7a991f23808e89274d938c97339f4df8c8134757`),
  and extracted separately. Its executable is unsigned. CRT Geom nevertheless
  loads from this extraction on the machine's **current** configuration, both
  over SSH and in the signed-in desktop session. This is not a reproduction of
  the earlier policy block on a fresh Windows installation. Bilinear, CRT Geom
  and S-video also load in short SSH checks of the user's signed package.

Fullscreen startup samples used `--boot-to-flight`, 1,200 displayed frames,
fresh diagnostic settings, Original or 16:9, and sprite filtering and CRT/shaders
off. The scheduled diagnostic processes ran as the ordinary signed-in user in
console session 4 at **1280×720**; SSH session 0 / 1024×768 samples are excluded
from the table. The first 120 trace rows are excluded from timing statistics;
audio counts cover the complete run.

| Package | Aspect | Mean FPS | Slowest rolling 60-frame FPS | Longest frame | Audio underruns |
| --- | --- | ---: | ---: | ---: | ---: |
| Original release ZIP | Original | 59.60 | 55.89 | 55.67 ms | 1 |
| Original release ZIP | 16:9 | 59.61 | 56.91 | 51.36 ms | 1 |
| User's locally signed package | Original | 59.61 | 55.20 | 77.92 ms | 1 |
| User's locally signed package | 16:9 | 59.60 | 52.91 | 106.75 ms | 1 |

In the longest frames, presentation calls account for 49.62/31.79 ms in the
original package and 70.73/98.56 ms in the signed package, respectively.
Average wide-scene work is about 0.8 ms in the desktop 16:9 samples. These
short, sequential samples reproduce pacing/audio faults in **both** aspects;
they do not prove a widescreen-only regression, a driver root cause, a signing
performance effect or whole-game sustained performance. Profile phase timings
do not completely partition every frame. A controlled presentation/CPU trace
and longer representative gameplay were needed to isolate the later fix below.

The remaining unaccounted average frame work is 9.08 ms in Original and 8.55 ms
in 16:9 for the clean desktop samples. Audio mixing/submission, including the
bounded playback queue wait, was outside the existing phase counters. Diagnostic
source now adds `audio_ms` to the frame CSV and `audio_submit_ms` to `--profile`;
these measure the existing work without changing playback or frame pacing.
The instrumented baseline subsequently ran after the user reused their existing
signing key as `duduke`; Smart App Control still rejected that self-signed build.
The user then explicitly authorized turning Smart App Control Off for development
measurements and approved the Windows UAC prompt. No keys were exported and no
certificate trust was added. Trusted-provider signing of both executable and
runtime DLLs remains a release requirement; GitHub artifact attestations do not
supply Authenticode trust. [SignPath's OSS terms](https://signpath.org/terms)
require external approval and restrict signing of third-party binaries.

### Fullscreen presentation preparation

The instrumented baseline identifies about **8.94 ms/frame** in audio submission
and a full **120 ms pending queue** after cold fullscreen presentation calls of
210.73 and 144.13 ms. Windowed playback submits audio in about 0.017 ms with no
pending queue. Startup pacing tries to repay the renderer setup time after audio
has started; the resulting queue backlog persists through the short run.

The Windows frontend now presents three known-black frames before creating audio
playback and starting pacing, matching the already configured three in-flight
presentation slots. The same preparation runs on fullscreen entry and renderer
rebuild while playback is paused. F11 pauses playback across that transition and
resets pacing before resuming. This does not execute game frames, generate or
drop PCM, change native clocks, increase queue bounds or alter engine code.

A later 21:9 run demonstrated why preparation alone is insufficient: a 140.57 ms
mid-run presentation stall caused an underrun and a lasting 120 ms pending queue.
The final Windows caller also re-anchors host pacing after playback re-primes or
an iteration exceeds the existing 80 ms playback reserve. Ordinary short stalls
retain deadline catch-up; an exhausted/suspended output no longer induces a
burst that fills its queues. Every board frame and native PCM sample is retained.
This can leave emulation behind wall time by the exceptional outage; it does not
hide that outage or make a stalled display continuous.

The initial 1,200-frame 1280×720 prototype comparison (first 120 rows excluded)
reduces 16:9 fullscreen p99 from **22.10 to 16.80 ms**, audio submission from
**8.94 to 0.015 ms**, pending audio from **120 to 0 ms**, and underruns from
**1 to 0**. Native Vulkan/bilinear also runs at native cadence without a persistent
queue. Isolated presentation waits of 44–85 ms remain in these finite samples;
this is not proof of stall-free or whole-game sustained performance.
Evidence: `build/windows-performance-fix/instrumented-baseline.zip`,
`prime-matrix-evidence.zip` and their summaries. The final build also covers F11
transitions and playback outages; final results and limits follow below.


### Final development-build validation

Final Windows executable SHA-256:
`c56180f74d4bd0026a1577778a5dc661d3f5d803242c8351e79a688ea9e79524`.
It is retained privately at `C:\Games\AB2-diagnostics\fixed-validation\afterburner2.exe`.
The original installation and settings were not replaced. No public replacement
or release-signing acceptance is claimed.

Later runs use console session 6 at **1920×1080**. Some diagnostic launches were
background/occluded and logged 8–15 FPS; these are excluded from performance
claims. A Media Player window and activation of console windows interfered with
focus. Final `visible-*` runs activate the SDL window by its title/handle, check
its foreground PID and retain the focus observations. The logical 21:9 viewport
runs on a 16:9 monitor; physical ultrawide-display acceptance remains separate.

The same-path A/B comparison alternates the instrumented baseline and the
startup/transition-preparation build twice, using the same DLLs and 16:9 settings
for 1,200 frames per run. After the first 120 rows, baseline p99 is
**22.37/22.04 ms**, mean audio submission **9.03/9.26 ms**, and final pending
queue **120/120 ms**. Preparation reduces p99 to **16.78/16.77 ms**, submission
to **0.0155/0.0149 ms**, and pending audio to **0/0 ms**. These comparisons do
not use the abnormal background-window samples.

Final foreground measurements below exclude the first 120 rows. Underrun
counts cover the full run. HRTF uses ScaleFX, Sharp HUD and
`ntsc/ntsc-320px-svideo-scanline.slangp`.

| Final case | Frames | Mean FPS | p99 frame | Longest frame | Mean audio submission | Final pending | Recovery observations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 21:9, SDL, filters/shaders Off | 3,600 | 59.637 | 16.77 ms | 52.16 ms | 0.0153 ms | 0 ms | 0 underruns |
| 16:9, ScaleFX/S-video/Sharp HUD/HRTF | 3,600 | 59.381 | 16.79 ms | 185.57 ms | 0.0487 ms | 0 ms | 13 source-underrun counts in two messages; 1 synchronized re-prime |
| F11 windowed → fullscreen, 16:9 SDL | 2,400 | 59.637 | 16.79 ms | 17.21 ms | 0.0170 ms | 0 ms | 0 underruns; saved fullscreen=1 |

A deliberate whole-process pause tests recovery independently of random driver
stalls. The preparation-only control retains a 120 ms pending queue afterward;
its final 900 rows average 9.19 ms audio submission and p99 22.15 ms. The final
build's work-phase pause retains **all 1,800 consecutive frame rows**, reports the
expected single SDL re-prime, and returns to **59.637 Hz / p99 16.77 ms / 0.0154 ms
submission / 0 ms pending** in the last 900 rows. A separate pause landing inside
pacing sleep records **269.74 ms pacing time**, retains **1,200 consecutive rows**,
and returns to **59.637 Hz / p99 16.77 ms / 0.0167 ms submission** in the last 600
rows, with a normal single 20 ms packet pending. The injected pause remains in
whole-run averages; recovery does not pretend it never happened. Earlier
re-prime-only and work-only recovery variants fail the sleep interruption and
are retained as negative controls.

Earlier preparation-build runs also cover Original, 16:9, 16:10 and selected
Stage 7 startup for 3,600 frames each. Their relevant sources/settings and
limits are retained in the evidence; these are not new final-binary runs or
whole-credit/natural peak-load acceptance. Final Original and 16:9 frame-180 BMPs
are byte-identical to their instrumented-baseline counterparts. All eight final
Windows native shader/main transition cases pass native RGB/PCM/time checks.
The unchanged Windows Vulkan comparison passes 1,176/1,176 cases and
167,610,366 identical pixels, 0 skipped; spatial audio passes its sustained
loopback and lifecycle/backpressure checks. Final Mac release/shader checks and
menu GPU checks also pass. Windows menu integration still fails the pre-existing
recording header check; its preceding Ground/21:9 gates and rejected mutants are
recorded separately in [ENGINE_NOTES](ENGINE_NOTES.md#windows-fullscreen-preparation-implementation-2026-11-04).

Evidence is retained under `build/windows-performance-fix/`: `final-summary.json`,
`recovery-gate.json`, `comparison-evidence.zip` and `acceptance-evidence.zip`.
The final ZIP SHA-256 is
`04293f13de0c0e0377395c3b9d9252e67003345c53b46d9136e6870773e47564`.
It contains diagnostic logs/settings/CSVs/receipts, excluding ROMs and images.
`image-identity-final.json` records image digests without exporting captures.

The persistent startup/recovery backlog is fixed within these tests. Long GPU
stalls and occasional HRTF underruns remain; the 185.57 ms shader-case stall is
not hidden by good p99 or steady-state FPS. Trusted executable/DLL signing,
whole-game sustained hardware acceptance, the Windows recording header bug and
the two reported graphical issues remain open. Public Windows downloads stay
withdrawn.

### Additional comparisons with the existing executable

Ten 1,200-frame startup comparisons use the existing locally signed executable,
16:9, filters/CRT off unless stated, and fresh diagnostic settings in desktop
session 4. The first 120 trace rows are excluded below. This is a sequential
diagnostic matrix, not whole-game or controlled repeated peak-load acceptance.

| Configuration | Mean FPS | Slowest 60-frame FPS | 99th-percentile frame | Mean unaccounted work |
| --- | ---: | ---: | ---: | ---: |
| SDL audio, fullscreen | 59.61 | 56.56 | 22.53 ms | 9.02 ms |
| SDL dummy audio, fullscreen | 59.78 | 57.38 | 26.11 ms | 8.58 ms |
| SDL audio, windowed | 59.64 | 58.00 | 16.80 ms | 0.03 ms |
| SDL audio, fullscreen, no `--profile` | 59.60 | 57.12 | 22.19 ms | 9.05 ms |
| Playback device disabled, fullscreen | 59.64 | 58.05 | 16.81 ms | 0.02 ms |
| SDL audio, native Vulkan/bilinear Slang, fullscreen | 59.61 | 55.06 | 22.10 ms | 8.81 ms |
| SDL audio, 256-frame device hint, fullscreen | 59.60 | 57.27 | 22.13 ms | 8.91 ms |
| SDL audio, 512-frame device hint, fullscreen | 59.61 | 57.21 | 22.20 ms | 8.87 ms |
| DirectSound audio, fullscreen | 59.59 | 55.69 | 31.74 ms | 8.67 ms |
| OpenAL Headphones HRTF, fullscreen | 59.60 | 57.13 | 22.52 ms | 8.89 ms |

Fullscreen playback-enabled cases fill the bounded producer queue, while the
windowed case keeps pending audio near zero. Disabling playback removes the
unaccounted work and restores ordinary software pacing. Changing presentation
backend, device-buffer hints, audio backend or FPS logging does not remove the
fullscreen pattern. This narrows the investigation to playback/backpressure and
its interaction with pacing; it does not establish the precise mechanism or
justify dropping PCM, changing native clocks, weakening queue bounds or
disabling security controls. Presentation outliers still occur with playback
disabled (longest frame 44.27 ms); disabling sound is not a fix.

The matrix evidence and computed summaries are retained under
`build/windows-performance-fix/` as `original-matrix-*`, `fallback-matrix-*` and
`audio-matrix-*`. The three evidence ZIP SHA-256 values are respectively
`bf81437fde618d713aea11fe198528d55470394d0d771c860b36a2b748fea5db`,
`f8cb5487ac55c65c14e2d606350f9be0428ed90beaf1b35bdcff50e1a705f1ce`, and
`3fe24c7c2f303248f4d55e265618e1ae4bfd0301a6bb2aab7eb5259e86575b3e`.
Tasks from that earlier matrix were removed; WPR reports no active recording.

Local evidence is retained under `build/windows-clean-diagnostics/`:
`diagnostic-evidence.zip` contains filtered Code Integrity events, shader logs,
desktop-session transcripts and frame CSVs; `summary.json` records the computed
statistics and longest-frame phase observations. The ZIP's SHA-256 is
`2e5f9c0ab6b439f3ae8bd5a5b9bd6157c2bfc1e96e88b33e404b153f6b219dd3`.
Windows copies remain in `C:\Games\AB2-diagnostics`. Temporary scheduled tasks
were removed after the runs. Existing game files and user settings were retained.

The user also reports completing a full Windows 11 playthrough with two minor
graphical issues. Their scene descriptions/captures and an independent
whole-credit comparison remain outstanding. This is manual completion evidence,
not whole-host fidelity acceptance. See the earlier investigation below and
the [validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first).

## Earlier investigation — 28 October 2026

Windows performance and audio stability remain unresolved. User-operated
fullscreen Stage 1 takeoff can drop to 53–56 FPS with audible gaps and repeated
`OpenAL stream underrun: 13 sources; re-priming synchronized output` messages.
The native frame rate is approximately 59.64 Hz. macOS is reported full speed;
that report is not a controlled comparison on identical hardware.

Reported configurations include Original aspect with ScaleFX x3, and 16:9 or
16:10 with sprite filtering and shaders disabled. Fullscreen dips are more
frequent than windowed dips. The S-video preset
`ntsc/ntsc-320px-svideo-scanline.slangp` reproduces the issue with 16:9 and
ScaleFX. The latest listening report supersedes the earlier smooth-HRTF report.

## Earlier measurements — 28 October 2026

The final two 3,600-frame fullscreen runs used 16:9, ScaleFX x3, S-video, Sharp
HUD, saved Headphones HRTF and the FPS overlay, without other regression work:

| Run | Mean FPS after 120 frames | Slowest rolling 60-frame FPS | Longest frame | OpenAL underrun events |
| --- | ---: | ---: | ---: | ---: |
| A | 59.72 | 55.92 | 47.90 ms | 1 |
| B | 59.50 | 53.39 | 68.98 ms | 3 |

Underrun counts cover each whole run; individual events are not assigned to
specific frame rows. Good average FPS does not establish smooth playback.

The zero-loss administrator CPU/GPU trace reproduces low CPU headroom during
active play. Its longest graphics waits occur near pause/focus transitions;
the evidence does not establish a graphics-driver root cause. Tracing itself
adds overhead. Affinity, priority, adaptive/FIFO presentation and near-fullscreen
borderless experiments did not provide a reliable fix.

## Implemented mitigations and limits

Native Vulkan sprite composition, shared HUD raster, cached palette/horizon
work, exact native ScaleFX capture reduction, unit-zoom decoding and removal
of a redundant SDL Vulkan submission reduce presentation work. They do not
eliminate the drops. Windows OpenAL now collects 80 ms at startup/recovery,
adding 55/30 ms buffering over the former thresholds; other platforms retain
their existing policy. The reserve cannot fix sustained underproduction, and
the final Windows runs still underrun. Native clocks, PCM and input/frame
processing are unchanged.

Regression and Stage 1 integration checks, all 23 stages at 16:9/16:10,
pixel comparisons, shader transitions and targeted audio fault tests pass
within their recorded scopes. The complete Windows release/oracle gate is
not closed: missing/invalid instrumented captures and the retained Windows
input-recording header issue remain documented. Recordings from this Windows
build must not become validation fixtures until that issue is repaired.

The compact experimental runtime download includes required DLLs and one
consolidated license file. Project/dependency sources are a separate companion;
regression executables, general docs, personal data and ROMs are excluded.
External shader presets and optional aircraft packs are separate assets.

For reproduction, use Arcade Stage 1 from takeoff with unchanged display and
audio settings. Compare fullscreen/windowed and the configurations above.
See [diagnostics](CRT_DISPLAY.md#validation) and the detailed
[trace and validation record](ENGINE_NOTES.md#administrator-cpugpu-trace-and-unit-zoom-decoder-2026-10-28).


Security restoration: after the user approved Windows UAC, Smart App Control
was verified **On (policy state 1)** at 15:44:47 UTC on 4 November 2026.
All temporary diagnostic scheduled tasks, including the restoration helper,
were removed. The receipt is `build/windows-performance-fix/final-security-state.json`;
the earlier Off observation is retained separately. No certificate trust stores
were changed. The private executable remains unsigned and is not release-ready.


## Release-signing preparation — 4 November 2026

The fixed executable and five bundled libraries remain unsigned; five Microsoft
DLLs already pass public-signature verification. Added full-runtime verification
and an existing-certificate signing script, plus explicit signed/unsigned modes
in the compact packager. Unsigned, ad-hoc and tampered controls are rejected.
The current noncommercial project/dependency licenses do not satisfy SignPath
Foundation's published OSI-license requirement. No signing identity has been
configured and no replacement has been published; see
[the signing prerequisite, candidate and publication steps](WINDOWS_SIGNING.md).


## Public-source and standalone signing input — 5 November 2026

The noncommercial eligibility blocker above describes the withdrawn previews.
Current original code is GPL-3.0-or-later and the source repository is public.
The current tree excludes Snes9x SaI and unused Cannonball/MAME-legacy audio;
the exact historical implementations/fixtures remain on
`archive/noncommercial-filters-and-audio` at `d080e1d` under their original terms.

A Release/O3/LTO LLVM-MinGW cross-build with SDL3 3.4.18 static and static
compiler runtimes imports only Windows system DLLs. OpenAL is genuinely loaded
on demand, with missing/incompatible runtime fallback to SDL. The base packages
only the project's EXE, launcher and notices; librashader and its redistributable
chain are excluded. This does not certify external unsigned runtimes or establish
SignPath approval. No protected Windows launch is claimed for the unsigned
candidate. The earlier performance results remain scoped to their tested EXE;
static-link changes require the final signed-package launch/smoke.

Validation in the isolated `build/public-source-worktree` checkout:

- Main profile: `build/workflow/20261105T175400.755711Z/`: all four targets pass,
  including 781 exact game-session RGB frames, 375 Practice RGB/PCM/time checks,
  660 Shuffle capture-neutral frames and the 7,118-frame natural Shuffle window.
- Native menu: `build/workflow/20261105T175402.585343Z/01-verify-native-menu.log`
  passes, including 781 CRT native RGB/PCM/time frames.
- Remaining GPU filters: `build/workflow/20261105T175636.260456Z/`: 420/420
  comparisons, 35,026,194 pixels identical, zero skipped; all six retained GPU
  mutations rejected. Initial sandboxed attempt had no displays; the actual
  Metal comparison ran with desktop access.
- Settings: `build/workflow/20261105T175027.329964Z/` passes. Final CPU filters
  and fragment mutations: `build/workflow/20261105T180517.770268Z/` pass,
  retaining six independent ScaleFX reference fingerprints and 583,443 terminal
  fragment foreground/background comparisons. Removed filter settings migrate
  to Off while preserving independent preferences.
- Dynamic OpenAL: `build/workflow/20261105T180203.912779Z/` passes missing and
  incompatible-module fallback, original SDL without loading OpenAL, failed-load
  cleanup/retry, and the existing 13-source/60 sample-second loopback suite.
  These macOS loader checks do not substitute for protected Windows acceptance.
- Retained fragment baseline and all three fragment mutations pass:
  `build/workflow/20261105T180517.770268Z/`.
- License inventory and signed-payload/import negative controls pass. Full
  engine oracle/regression was not run: no native/emulated execution changed.

The [signing requirements](WINDOWS_SIGNING.md) and
[provider follow-up draft](SIGNPATH_APPLICATION.md) distinguish completed source,
build and dependency work from external approval/signing still required.
