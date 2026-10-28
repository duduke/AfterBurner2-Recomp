# Third-party code

This project includes original X-Board implementations, licensed third-party
cores and MAME-derived device algorithms. See each source header and the
license files linked below for attribution. ROM data is supplied by the user
and is not distributed.

[LICENSES/README.md](../LICENSES/README.md) is the central inventory of license
texts, source-header notices and platform-specific packaging requirements.
Third-party terms apply to their respective components independently of the
project's original-code license.

## macOS preview dependencies

The macOS ZIP bundles dynamically linked SDL3 3.4.16 (zlib license) and OpenAL
Soft 1.25.2. `Notices/` contains their licenses and the linked-code notices;
`Sources/` contains the complete project source at the manifest's commit plus
the pinned upstream SDL/OpenAL source archives. No Homebrew installation is
needed to run that package. The shared libraries remain replaceable by
ABI-compatible local builds. See the [release workflow](WORKFLOW.md#macos-preview-release).

## Experimental Windows preview dependencies

The native Windows x64 ZIP bundles dynamically linked SDL3 3.4.18 (`SDL3.dll`,
zlib license) and OpenAL Soft 1.25.2 (`OpenAL32.dll`, LGPL-2.0-or-later), built
with w64devkit GCC 16.2.0 (`libstdc++`/`libgcc` linked statically). `Notices/`
contains the SDL3, OpenAL Soft (including fmt/GSL/pffft) and MinGW-w64 runtime
licenses, plus the GCC GPLv3 and Runtime Library Exception notices for the
statically linked GCC runtimes; `Sources/` includes the complete project tree, SDL3 source archive and
OpenAL Soft source archive. When built via the alternative macOS LLVM-MinGW
cross-build path, the ZIP instead bundles SDL3 3.4.16 and LLVM 23.1.3
`libc++.dll`/`libunwind.dll` alongside their upstream source archives. Microsoft
system/MSVCRT DLLs are provided by Windows and are not bundled. See the
[Windows release workflow](WORKFLOW.md#experimental-windows-preview-release).

## Native audio path

- `src/third_party/ymfm/`: Aaron Giles's YMFM synthesis core (BSD-3-Clause);
  the live FM owner uses `ymfm::ym2151`. See [YMFM license](../LICENSES/YMFM.txt).
- `src/main/hwaudio/xboard_ym_timers.hpp`: YMFM/MAME-derived timer/status
  behavior; see [timer license](../LICENSES/YMFM-Timers.txt).
- `src/main/hwaudio/xboard_pcm.hpp`: MAME SegaPCM device algorithm, attributed
  to Hiromitsu Shioya and Olivier Galibert (BSD-3-Clause); see
  [SegaPCM license](../LICENSES/MAME-SegaPCM.txt).
- Native audio scheduling/mixing references MAME audio behavior; retained
  attribution is in [MAME audio license](../LICENSES/MAME-Audio.txt).

## Retained Cannonball-derived code

The older generic sound classes remain in the repository; they do not describe
the live YMFM synthesis owner:

- `src/main/hwaudio/ym2151.cpp` / `.hpp` — Yamaha YM2151 FM synthesis chip
  emulator. Original author: Jarek Burczynski, 1997-2002, distributed as
  part of historical MAME. Carried into this project via Chris White's
  Cannonball, which itself adapted it from MAME with only minor
  integration changes.
- `src/main/hwaudio/segapcm.cpp` / `.hpp` — Sega 8-bit PCM sample-playback
  chip emulator. Also historical MAME-derived, carried via Cannonball.
- `src/main/hwaudio/soundchip.cpp` / `.hpp` — a small abstract base class
  (buffer management shared by the above two), original to Chris White's
  Cannonball, not MAME-derived.

Copyright Chris White and the Cannonball team. The YM2151 core retains
copyright 1997–2002 Jarek Burczynski, optimizing ideas credited to Tatsuyuki
Satoh, and copyright 2025 James Pearce for CannonBall-SE modifications.
See each file's own header comment for its specific attribution.

The complete [Cannonball terms](../LICENSES/Cannonball.txt), additional
[CannonBall-SE terms](../LICENSES/CannonBall-SE.txt), and
[historical MAME terms](../LICENSES/MAME-Legacy.txt) are retained. These legacy
files must not be labeled BSD merely because modern MAME components use BSD.
The historical MAME notice is from MAME 0.146; source/license retrieval
references are in the central inventory. This is separate from the live YMFM
and X-Board device implementations described above. Both build systems still
compile the legacy classes, and the source archive includes them.

## Vulkan headers (Windows)

The native Vulkan shader host uses Khronos Vulkan-Headers, with 1.4.341 as the
CMake fallback version. That version's `vulkan.h`, `vulkan_core.h`,
`vk_platform.h` and `vulkan_win32.h` identify Apache-2.0 as their license.
Copyright 2014–2026 The Khronos Group Inc. The complete
[Apache license](../LICENSES/Apache-2.0.txt) and
[header notice](../LICENSES/Vulkan-Headers.txt) are included in both package
notice trees. Vulkan is loaded from the user's driver; no Vulkan loader or
driver library is bundled. An SDK override must retain its own applicable
notices if they differ from this recorded version.

`src/third_party/z80.h` is Andre Weissflog's cycle-stepped Z80 core from
https://github.com/floooh/chips (zlib/libpng license, copyright 2021).
The upstream license and attribution remain in the unmodified header.
Only the sound CPU uses this core; the 68000 game logic is native C++.

Vendored header SHA-256: `6ca70ffd91b1bdbaf00f7a41e7092b326145fd1c95dcdd7bb01183370de734f3`

`src/main/engine/omainirqservice.*` references the bus/internal-cycle and
E-clock synchronization behavior of MAME's `m68000` core (BSD-3-Clause,
Olivier Galibert) for its original bounded IRQ service translation. The
MAME CPU core is not vendored or copied into this project.

## GPU sprite filters

Generated GPU shaders retain ScaleFX's MIT notice and the SaI adaptations'
Snes9x attribution/license references alongside the retained source notices in
`src/main/frontend/scalers/`. Shader conversion uses glslang and SPIRV-Cross
as development tools; no compiler libraries are linked into the application.

The Three.js dependency and its notices are retained on the dedicated
`codex/threejs-game-engine` branch; main no longer includes that prototype.

## Optional RetroArch shader host

`src/third_party/librashader/librashader.h` is the unmodified MIT-licensed C
header from librashader 0.12.0; its complete copyright and permission notice
remain in the header. The optional macOS Metal host loads a separately
installed runtime. The preview ZIP does not bundle that runtime or shader
packs. Its `Notices/` directory includes the header notice and the upstream
MPL text; the complete header is also in the project source archive.

The optional runtime is available under `MPL-2.0 OR GPL-3.0-only`. Redistribution
under the MPL option requires its applicable notices and covered source.
The pinned source and redistribution notes are in
`src/third_party/librashader/NOTICE.md` in the repository/source archive and
`Notices/librashader/NOTICE.md` in the preview package. See
[shader setup](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal).
User-selected shader packs retain their own licenses.

## SDL_GameControllerDB

The offline controller database comes from
[mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB),
commit `c1d5289a1f713b30a2c121e9fe6529d39360be0f`. The unmodified database and
zlib license are retained in `src/main/frontend/controllerdb/`, with hash and
provenance in its README. `database.inc` wraps that same text in a C++ raw
string for packaging; mappings are not modified. SDL3 loads this through
`SDL_AddGamepadMappingsFromIO` before the frontend opens input devices.

## dr_flac

`src/third_party/dr_flac/dr_flac.h` is unmodified from
[mackron/dr_libs](https://github.com/mackron/dr_libs), commit
`dfe8377631000664666519fdb83da193fd8037f4`. This project uses its MIT No
Attribution license option, retained in that directory alongside provenance.
The decoder is linked only into frontend music playback and its tests.

## Optional OpenAL Soft

Spatial output dynamically links [OpenAL Soft](https://openal-soft.org/), tested
with version 1.25.2. Source and binaries are fetched/built only under ignored
`build/deps/`; they are not vendored. The dependency builder pins the release
archive and checks SHA-256. macOS uses Soft's CoreAudio backend, rather than the
legacy Apple OpenAL framework.

Soft's LGPL license is retained in [LICENSES/OpenAL-Soft.txt](../LICENSES/OpenAL-Soft.txt).
When distributing its shared library, retain upstream source/license notices,
including its bundled fmt/GSL/pffft notices. The local dependency builder copies
these notices beside the installation. The default embedded HRTF is based on
MIT KEMAR measurements: **Bill Gardner and Keith Martin, MIT Media Laboratory,
Copyright 1994**. [Dataset attribution and use terms](https://sound.media.mit.edu/resources/KEMAR.html).
No Sega samples or ROM-derived captures are part of this dependency.
