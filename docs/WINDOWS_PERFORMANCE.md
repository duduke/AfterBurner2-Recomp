# Windows performance status — 28 October 2026

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

## Current measurements

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
