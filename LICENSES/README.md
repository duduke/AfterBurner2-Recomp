# Third-party license inventory

The files in this directory collect the notices for code used by this project.
Original project code is covered by `LICENSE.txt` at the repository root;
third-party components retain their own terms. Source headers and vendored
license files remain intact. See `docs/THIRD-PARTY-NOTICES.md` in the source
tree for component usage and attribution. macOS runtime packages consolidate
these texts and bundled dependency notices in `THIRD-PARTY-NOTICES.txt`, storing
identical texts once. Their matching `-sources.zip` companion contains the
complete project tree and dependency source archives and is retained locally.
The macOS release notes link the exact project commit and pinned dependency
sources; the source companion is not a public release asset. The Windows
packager retains `Notices/project/` and
its source archives; see `docs/WORKFLOW.md` for each packaging path.

| Component | License / notice in this directory |
| --- | --- |
| YMFM live YM2151 core | [YMFM.txt](YMFM.txt), BSD-3-Clause |
| YMFM/MAME timer behavior | [YMFM-Timers.txt](YMFM-Timers.txt), BSD-3-Clause |
| Live MAME-derived SegaPCM | [MAME-SegaPCM.txt](MAME-SegaPCM.txt), BSD-3-Clause |
| MAME-derived video/math/I/O/CPU timing references | [MAME-Devices.txt](MAME-Devices.txt), BSD-3-Clause |
| Live MAME-derived audio scheduling | [MAME-Audio.txt](MAME-Audio.txt), BSD-3-Clause |
| Andre Weissflog's Z80 core | [Z80.txt](Z80.txt), zlib/libpng notice extracted verbatim from `src/third_party/z80.h` |
| SDL3 | [SDL3.txt](SDL3.txt), zlib |
| SDL_GameControllerDB | [SDL_GameControllerDB.txt](SDL_GameControllerDB.txt), zlib |
| dr_flac | [dr_flac.txt](dr_flac.txt), upstream dual license; this project selects MIT No Attribution |
| miniz | [miniz.txt](miniz.txt), MIT |
| ScaleFX | [ScaleFX.txt](ScaleFX.txt), MIT notice extracted verbatim from `scalefx.hpp` |
| librashader public C header | [librashader-header.txt](librashader-header.txt), MIT |
| Optional external librashader runtime | [MPL-2.0.txt](MPL-2.0.txt), MPL option; runtime not bundled |
| OpenAL Soft | [OpenAL-Soft.txt](OpenAL-Soft.txt), LGPL-2.0-or-later; [OpenAL-BSD-3Clause.txt](OpenAL-BSD-3Clause.txt) for applicable upstream files |
| OpenAL bundled fmt, GSL and pffft | [OpenAL-fmt.txt](OpenAL-fmt.txt), [OpenAL-GSL.txt](OpenAL-GSL.txt), [OpenAL-pffft.txt](OpenAL-pffft.txt) |
| Khronos Vulkan headers used on Windows | [Vulkan-Headers.txt](Vulkan-Headers.txt), [Apache-2.0.txt](Apache-2.0.txt) |
| GCC libgcc / libstdc++ runtimes in GCC Windows builds | [GCC-Runtime.txt](GCC-Runtime.txt), [GPLv3](GCC-GPL-3.0.txt) with [GCC Runtime Library Exception 3.1](GCC-Runtime-Library-Exception.txt) |

OpenAL's default HRTF uses MIT KEMAR measurements: Bill Gardner and Keith
Martin, MIT Media Laboratory, Copyright 1994. See the upstream
[dataset attribution and use terms](https://sound.media.mit.edu/resources/KEMAR.html).
User-selected ROMs, music and external shader packs are not distributed and
retain their own terms. glslang, SPIRV-Cross, MAME and Python are development
or verification tools; their compiler/emulator libraries are not linked into
the application. System frameworks and driver libraries are not bundled.

## Windows compiler runtime notices

Compiler runtime licenses depend on the compiler used for the package.
LLVM-MinGW packages include the compiler distribution's complete `LICENSE.TXT`
as `Notices/LLVM.txt` and the target's MinGW `COPYING` as
`Notices/MinGW-w64.txt`. Native builds require `--mingw-notice` with the
compiler's complete runtime COPYING file, including when GCC runtimes are
linked statically. Cross-builds extract both notices from the pinned
toolchain archive. The corresponding upstream source archives are included
as described in `docs/WORKFLOW.md`. These toolchain-specific notices are
collected at packaging time, rather than substituting a generic license here.
GCC builds additionally retain the central GCC runtime notice, GPLv3 text and
Runtime Library Exception for their statically linked libgcc/libstdc++ code.

## Provenance and updates

New upstream texts retrieved on 2026-10-26:

- [Cannonball license](https://github.com/djyt/cannonball/blob/27493ebf62be3498dff93ed6a45e8e2db819bae1/docs/license.txt).
- [CannonBall-SE additional terms](https://github.com/J1mbo/cannonball-se/blob/bc34b18fe34921d977d09e7459d796e9976ec267/docs/CannonBall-SE-license.txt).
- [Historical MAME 0.146 license](https://github.com/mamedev/mame/blob/mame0146/docs/license.txt).
  This preserves the historical notice referenced by the retained legacy
  sound files; it is not a claim that they were imported from that exact tag.
  Jarek Burczynski (1997–2002), Tatsuyuki Satoh, Chris White and James Pearce
  remain credited in the source and third-party inventory. Modern MAME license
  labels must not be applied retroactively to these files.
- [Vulkan-Headers 1.4.341 Apache text](https://github.com/KhronosGroup/Vulkan-Headers/blob/v1.4.341/LICENSES/Apache-2.0.txt).
- GCC's unmodified [COPYING3](https://github.com/gcc-mirror/gcc/blob/master/COPYING3)
  and [COPYING.RUNTIME](https://github.com/gcc-mirror/gcc/blob/master/COPYING.RUNTIME)
  for the compiler runtimes; GPL version 3 and exception version 3.1.

Other central copies match the retained vendored licenses or header notice
blocks. SDL3's text comes from the SHA-256-pinned 3.4.16 source archive used by
the macOS packager; Windows also extracts the actual bundled SDL version's
license. OpenAL notices come from the pinned 1.25.2 source archive, including
fmt 11.2.0 and GSL. Component versions, hashes and original-source references
remain in the vendored READMEs, dependency builder and package scripts.

When updating a dependency, update its notices here and at its original source
location together. Preserve upstream texts verbatim. Packaging-specific
notices must match the libraries actually shipped; copying this directory
does not replace source-redistribution requirements.

## Historical notices only

The following texts document components removed from the current source and
runtime builds. Their code and validation fixtures remain, under their original
terms, on [archive/noncommercial-filters-and-audio](https://github.com/duduke/AfterBurner2-Recomp/tree/archive/noncommercial-filters-and-audio).
They are not included in the current runtime notice selection.

| Historical component | Original terms |
| --- | --- |
| Retained Cannonball sound classes | [Cannonball.txt](Cannonball.txt), noncommercial/source redistribution terms |
| Retained CannonBall-SE modifications | [CannonBall-SE.txt](CannonBall-SE.txt), additional upstream terms |
| Historical MAME sound code carried through Cannonball | [MAME-Legacy.txt](MAME-Legacy.txt), historical noncommercial/source redistribution terms |
| Snes9x SaI filters | [Snes9x.txt](Snes9x.txt), Snes9x terms |

The earlier original-code grant is retained in
[AfterBurner2-original-noncommercial.txt](AfterBurner2-original-noncommercial.txt).
