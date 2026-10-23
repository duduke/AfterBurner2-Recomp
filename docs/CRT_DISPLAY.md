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

## RetroArch Slang presets (macOS Metal)

The optional shader host loads **`.slangp`** presets through
[librashader 0.12.0](https://github.com/SnowflakePowered/librashader/tree/librashader-v0.12.0).
Install its pinned Apple Silicon runtime once, then build the game:

```sh
python3 tools/deps/fetch_librashader.py
python3 tools/workflow.py run native-release
./build/afterburner2-release
```

The macOS Preview 3 ZIP includes this host, while the runtime and shader packs
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
Metal shader renderer while the game/menu session remains live. Shader mode
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

This initial backend supports **macOS Metal / SDR / `.slangp`**. The fetcher is
for Apple Silicon. Windows/Linux shader backends, legacy `.glslp`/Cg presets,
HDR output and universal preset compatibility are not implemented. First-input
mipmap generation is also unavailable. CRT Easymode, CRT Geom and CRT Royale
have been exercised with a complete local shader pack. Preset cost varies;
sustained performance must be measured with the chosen preset and settings.

The runtime is optional and loaded dynamically. Search order is an explicit
`--shader-runtime` path (or saved `shader-runtime`), `AB2_LIBRASHADER`, application
library locations, then `build/deps/librashader/librashader.dylib`. It must expose
ABI 2 / API 5. The fetcher verifies a pinned release SHA-256 before extraction
and records source/license metadata. No shader pack or runtime binary is
committed. The pinned source and redistribution notes are in
`src/third_party/librashader/NOTICE.md` in the repository/source archive and
`Notices/librashader/NOTICE.md` in the preview package.

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
