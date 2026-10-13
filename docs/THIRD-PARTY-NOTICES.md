# Third-party code

This project includes original X-Board implementations, licensed third-party
cores and MAME-derived device algorithms. See each source header and the
license files linked below for attribution. ROM data is supplied by the user
and is not distributed.

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
  part of MAME (BSD-3-Clause). Carried into this project via Chris White's
  Cannonball, which itself adapted it from MAME with only minor
  integration changes.
- `src/main/hwaudio/segapcm.cpp` / `.hpp` — Sega 8-bit PCM sample-playback
  chip emulator. Also MAME-derived (BSD-3-Clause), carried via Cannonball.
- `src/main/hwaudio/soundchip.cpp` / `.hpp` — a small abstract base class
  (buffer management shared by the above two), original to Chris White's
  Cannonball, not MAME-derived.

See each file's own header comment for its specific attribution.

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
