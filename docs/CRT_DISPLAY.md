# CRT display mode

Select **Options → Video → CRT Mode → Off / Subtle / Arcade / RetroArch**. Off is the
default. Subtle starts at **25% scanlines / 15% RGB mask**; Arcade starts at
**50% / 35%**. Selecting an active preset restores both of its starting
strengths. Off retains the last strengths. The two strength rows adjust
independently in 5% steps from 0–100%; adjusting either while Off enables
Subtle using the retained values.

The effect treats the completed game image, including sprite filters,
translucency, wider viewports and Original / Sharp 3x / custom HUD artwork.
Frontend menus and statistics draw afterward for readability. It also works
while paused, and changing strength updates the current frame. It does not
alter the native framebuffer, gameplay, input recording, sound or board time.

Scanlines follow the original **224-row** raster with phase aligned to output
pixel rows so that 2× scaling retains distinct beam and gap rows. Their contrast decreases
below two output pixels per native row and reaches zero at one pixel per row,
reducing visible scanline aliasing in small windows. The RGB mask cycles red, green
and blue at physical output pixel columns, independently of window DPI and
viewport width. Both effects attenuate light; stronger settings intentionally
darken the picture. The built-in modes include scanlines and the RGB mask.
RetroArch presets can provide glow, curvature and other effects through the
separate Slang host below. General frame-pacing changes remain separate work.

<a id="retroarch-slang-presets-macos-metal"></a>

## RetroArch Slang presets (macOS Metal / Windows Vulkan)

The optional shader host loads **`.slangp`** presets through
[librashader 0.12.0](https://github.com/SnowflakePowered/librashader/tree/librashader-v0.12.0).
On Apple Silicon, install its pinned runtime once, then build the game:

```sh
python3 tools/deps/fetch_librashader.py
python3 tools/workflow.py run native-release
./build/afterburner2-release
```

The macOS Preview 4 ZIP includes this host, while the runtime and shader packs
remain separate downloads. Run the fetcher from the included project source
archive or a checkout, optionally choosing `--destination /path/to/librashader`.
Then launch the extracted package with explicit paths:

```sh
./Launch.command --shader-runtime "/path/to/librashader/librashader.dylib" \
  --shader-preset "/path/to/shaders_slang/crt/crt-geom.slangp"
```

Choose **Options → Video → Shader Preset**, then select a `.slangp` from a
complete RetroArch `shaders_slang` directory. Successful selection activates
RetroArch mode and saves the path. Keep the preset's relative shader files,
includes and lookup textures together. The built-in scanline/mask strength
controls are disabled in this mode; the selected preset controls its effect.
Preset parameter overrides are honored. An in-game parameter editor is not
included; edit a user preset and select it again to reload it.

For an isolated preview:

```sh
./build/afterburner2-release --settings-file build/slang-preview.settings \
  --shader-preset "/path/to/shaders_slang/crt/crt-geom.slangp"
```

The application can switch between its normal SDL GPU renderer and the native
Metal or Vulkan shader renderer while the game/menu session remains live. Shader mode
uses the existing CPU sprite-filter fallback. Loading and compilation can pause
presentation briefly. A failed replacement keeps the working preset/display;
startup failure uses Off and reports the reason. Selecting an unsupported format
or an SDR-incompatible HDR preset reports a readable error.

The shader input is **320×224**, **358×224** or **398×224**, matching the selected
viewport. It includes the game and HUD; the shader scales this native raster to
the physical game rectangle, then frontend UI draws afterward. Consequently,
Sharp/custom HUD and HD aircraft are reduced to the native raster in this mode.
The native framebuffer, board clocks and PCM remain separate from presentation.
Paused redraws reuse a completed shader output instead of advancing shader
history. Restart, resize and presentation changes invalidate that output.

The Metal bridge shares IOSurface-backed textures with SDL, synchronizes their
GPU work explicitly, and waits for completion before SDL samples the result.
It performs no per-frame CPU framebuffer readback. Source and final output
images each have a 64 MiB cap; intermediate textures and history are allocated
by the preset/runtime and are additional to those caps. The pinned runtime has
a Metal history-clear bug for chains without history; a requested history reset
recreates the chain and reapplies parameter values rather than calling that
broken path. This can cause a compilation stall on reset.

The backends support **macOS Metal or Windows Vulkan / SDR / `.slangp`**.
The fetcher supports Apple Silicon and Windows x64. Linux shader support, legacy `.glslp`/Cg presets,
HDR output and universal preset compatibility are not implemented. First-input
mipmap generation is also unavailable. CRT Easymode, CRT Geom and CRT Royale
have been exercised with a complete local shader pack. Preset cost varies;
sustained performance must be measured with the chosen preset and settings.

The runtime is optional and loaded dynamically. Search order is an explicit
`--shader-runtime` path (or saved `shader-runtime`), `AB2_LIBRASHADER`, application
library locations, then `build/deps/librashader/librashader.dylib` (macOS) or
`build/deps/librashader/librashader.dll` (Windows). It must expose
ABI 2 / API 5. The fetcher verifies a pinned release SHA-256 before extraction
and records source/license metadata. No shader pack or runtime binary is
committed. The pinned source and redistribution notes are in
`src/third_party/librashader/NOTICE.md` in the repository/source archive and
`Notices/librashader/NOTICE.md` in the preview package.

### Windows Vulkan

Build with SDL3 3.4.18 and Vulkan headers; see the [Windows LTO build](../README.md).
The optional runtime fetcher selects the checksum-pinned Windows x64 0.12.0 ZIP:

```powershell
python tools/deps/fetch_librashader.py
./build/windows-lto/afterburner2.exe --shader-preset "C:/RetroArch/shaders/shaders_slang/crt/crt-easymode.slangp"
```

In a packaged build, supply `--shader-runtime "C:/path/to/librashader.dll"`
or place that DLL beside the executable. Shader packs and runtime binaries
remain external. Select presets through the same Video menu used on macOS.

Shader mode uses SDL's **native Vulkan renderer**, sharing its device, graphics
queue and BGRA target images with librashader. Target unbinding submits SDL's
source work; explicit image barriers order output sampling on the shared queue.
The completion fence is waited before command/chain/target reuse, rather than
immediately after submission, so CPU work and presentation can overlap the
external pass. There is no per-frame CPU readback. History clears in-place on Vulkan.
This differs from SDL's GPU renderer running on Vulkan: the latter retains
compute sprite filters for Off/Subtle/Arcade, while shader mode uses the existing
CPU sprite filters. Frontend menus are drawn after the shader.
CPU ScaleFX can be much slower here than the normal GPU implementation; the
local combined ScaleFX/16:10/HD-aircraft smoke measured roughly 10–11 fps.
Use Sprite Filter Off when measuring the external preset's own cost.

Explicit checks require a working Vulkan display and the optional runtime:

```powershell
cmake --build build/windows-lto --target verify-native-slang-mutations verify-native-shader-main -j4
```

The Windows authored reference covers channel mapping, raw microsecond timing,
resize through 4K, renderer-state restoration, cached redraws and real previous
frame history/reset. Three retained mutations reject timing conversion, lost
history reset and lost renderer state. CRT Easymode also passes the pipeline
checks. This finite coverage does not establish universal preset compatibility
or sustained frame pacing.

With Sprite Filter Off, the final bounded Windows Practice/HD-aircraft smoke
runs near 59.6 fps using CRT Easymode. Shader mode uses the same VSync-off
choice as normal Windows Vulkan rendering. The separate built-in CRT GPU
regression currently encounters an SDL 3.4.18 viewport assertion; software CRT
checks pass, but that supplemental GPU gate remains open.

The four-pass `ntsc/ntsc-320px-composite-scanline.slangp` follow-up removed
immediate host-side GPU waiting and redundant Off-mode CPU scaling. A 1,200-frame
windowed Practice run holds 59.64 fps after startup with Original viewport/HUD
and ground off. The same preset with 16:10/sharp HUD/ground still drops to
approximately 53–55 fps in later busy scenes, where CPU scene reconstruction
reaches 5.5 ms. Sprite Filter Off alone does not guarantee the frame budget.
See [measurements and checks](ENGINE_NOTES.md#ntsc-preset-performance-follow-up-2026-10-24).

Source builds and macOS Preview 4 use the integrated primary-renderer frontend by default;
`--scene-renderer snapshot` selects the retained reconstruction reference.
Matched NTSC runs reduce board-plus-scene CPU time
by roughly 4–5%, with identical final images, but still have busy intervals below
59.6 fps. The Mac follow-up adds direct integrated-host and full lifecycle checks;
its finite timings do not establish exhaustive preset/platform performance.
See the [Mac acceptance record](ENGINE_NOTES.md#integrated-primary-renderer-macos-acceptance-2026-10-24)
and [Windows matched measurements](ENGINE_NOTES.md#frontend-adapter-matched-ntsc-timing-and-independent-mame-rgb-2026-10-24).

## Settings and rendering

The normal `AB2SETTINGS1` file saves `crt=off|subtle|arcade|retroarch`,
`crt-scanlines=0..100` and `crt-mask=0..100`. Older files default to Off.
Malformed strengths are ignored. A mode-only Arcade entry uses its 50/35
defaults; explicit valid strength entries override defaults regardless of
line order. `--settings-file` can isolate a preview from personal preferences.
The optional shader paths use `shader-preset=` and `shader-runtime=` entries.

`frontend/native_crt.hpp` draws one multiplicative SDL texture after game/HUD
composition. The texture is generated only when output geometry or strength
changes; steady frames require no framebuffer readback, image copy or texture
upload. A cosine scanline envelope is evaluated per output row, and its gain
is combined with RGB stripe weights. The overlay uses nearest sampling at
physical output resolution and covers only the logical game rectangle.
Letterbox pixels and the caller's logical presentation, scale, viewport,
clip, draw color and blend state are preserved.

One accepted overlay is retained, capped at **64 MiB** of pixel data. Off or
two zero strengths release it. A resize or strength change builds a replacement
before discarding the accepted texture. If allocation or drawing is unavailable,
the application reports the SDL error and switches CRT Off for the session.
The existing SDL renderer provides GPU and software support; no new shader
compiler or runtime dependency is required. `--profile` reports
`crt_submit_ms`, `crt_cache_bytes` and cumulative `crt_rebuilds`; submission
time alone does not measure GPU completion or total presentation latency.

## Validation

```sh
python3 tools/workflow.py run test-native-settings test-native-crt test-crt-mutations
python3 tools/workflow.py run verify-native-menu verify-gpu-crt verify-gpu-native-menu native-release
# Optional Metal/runtime gate (needs the runtime installed above):
python3 tools/workflow.py run test-librashader-fetch verify-native-slang-mutations verify-native-shader-main
```

GPU checks require an unlocked desktop. An accepted fullscreen API request
does not prove that the window entered fullscreen. The synthetic renderer
reports the actual window flag and any unavailable observation; the visible
menu gate requires actual fullscreen and windowed transitions. The later
unlocked-desktop recheck passed both transitions; see the shader follow-up
record in [ENGINE_NOTES](ENGINE_NOTES.md#retroarch-slang-presentation-2026-10-23).

The renderer regression uses independent synthetic pixel expectations for
Off/zero identity, scanline phase and attenuation, RGB mask ordering, clipping,
letterboxing, all three viewport widths, integer/fractional scaling, high-DPI
output, renderer state and cache lifecycle. The menu integration checks native
RGB/PCM/time while the feature is active. Retained deliberate mutations must
fail against a passing baseline. The normal `test` includes the CRT checks;
follow [the project validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first)
for other changes and merge/release gates. Detailed measured results and
platform limits belong in [ENGINE_NOTES](ENGINE_NOTES.md).
