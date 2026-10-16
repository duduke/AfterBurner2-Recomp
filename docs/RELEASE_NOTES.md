# v0.1.0-preview.2 — Windows performance portability

This preview adds an experimental Windows x64 download alongside the updated
Apple Silicon macOS package. Download from
[GitHub Releases](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.2).
Package manifests identify the exact source commit and all payload hashes.

## Changes

- Windows CPU continuations use cooperative OS fibers instead of frequent
  worker-thread handoffs. The worker backend remains available for comparison.
- CMake Release/RelWithDebInfo builds enable checked LTO and `-O3` for GCC/Clang.
  `AB2_ENABLE_IPO=OFF` disables LTO. Floating-point semantics remain unchanged.
- Frame-deadline waits use SDL's precise timer; input polling remains bounded.
- Failed GPU compute setup permits SDL accelerated presentation with CPU
  filters. Windows compute uses the existing Vulkan/SPIR-V shaders;
  Direct3D 12 compute shaders are not included.
- `--profile` reports the continuation backend and CPU/GPU filter path.
- Explicit string headers repair two portability issues found by cross-building.

Existing widescreen, ground correction, sprite filters/translucency, remapping,
replacement music and controller feedback are retained. Native gameplay and
object allocation limits are unchanged.

## Packages

**macOS arm64:** Apple Silicon, macOS 26+. SDL3 3.4.16 and OpenAL Soft 1.25.2
included. Ad-hoc signed; not notarized or Developer ID signed. Extract, add your
own ROMs to `roms/`, then open `Launch.command`.

**Windows x64 experimental:** targets Windows 10/11 x64. Cross-built with
LLVM-MinGW 20261006 / Clang 23.1.3, Release/LTO. SDL3 and C++ runtime DLLs are
included; keep them beside the EXE. Unsigned. Original SDL stereo and replacement
music are enabled; optional OpenAL spatial output is disabled. Run
`Check-Continuations.cmd` for the two ROM-free continuation tests, then
`Launch.cmd --profile`. **Windows execution, Vulkan output, controls and
sustained FPS have not been verified.**

Both ZIPs include project/dependency sources, notices, a manifest and a separate
SHA-256 checksum. Neither includes ROMs, original Sega graphics/audio, captures,
personal settings or soundtracks.

## Validation

The complete `required` profile passed on the final engine tree at
`build/workflow/20261015T202242.843626Z/`: `test`, `verify-stage1` and the complete
`oracle-test`, including all retained mutations. This pass is reused because
engine sources, transitive dependencies, compiler/options, fixtures and harness
have not changed; subsequent release work affects packaging and documentation.
Supplemental joint/reset checks and the native release build pass freshly at
`build/workflow/20261016T034347.883905Z/`.

- All 23 directed stage slots retain native RGB/PCM/time comparisons.
- Four lifecycle replays: 104,452/104,452 ten-column MAME observations match,
  zero skips. Whole-credit CPU/device/RGB/audio comparison remains open.
- Native GPU/menu: 4,128/4,128 cases identical, zero skips. Standalone GPU filters:
  630/630 identical, zero skips; six deliberate mutations rejected.
- Optimized macOS CMake build and both continuation tests pass; GPU compute,
  accelerated fallback and dummy/software smokes pass.
- Windows x64 game and both continuation tests compile/link; runtime execution
  remains unverified. Some legacy oracle fixtures retain documented input-race
  exclusions, reported in their full logs.
- Extracted preflight packages pass manifest/source hashes, ROM exclusions,
  architecture and dependency checks. The macOS package passes signature and
  launch checks with Metal GPU filters and OpenAL HRTF. Corrupt dependency data
  and attempts to overwrite existing artifacts are rejected. Final packages are
  re-extracted and verified before upload.

Recorded correctness checks do not establish sustained Windows performance,
exhaustive widescreen coverage, physical-platform acceptance or continuous
whole-credit fidelity. Standard-credit recording and full-game acceptance
remain open in the [roadmap](ROADMAP.md).
