# Sprite presentation filters

EPX (Scale2x) and Scale3x in `../sprite_filter.hpp` independently implement the
published rules at https://www.scale2x.it/algorithm, with clamp-to-edge sampling.

`scalefx.hpp` adapts Sp00kyFox's ScaleFX five-pass algorithm to single-precision
CPU intermediate planes. The original MIT-licensed shaders are retained here:
https://github.com/libretro/glsl-shaders/tree/master/scalefx/shaders
Retrieved 2026-10-12. Defaults: CLR=0.5, SAA=1, SCN=1; final scale 3x.
The one intentional change is a distance of 1 between pixels with different
alpha, so opaque black sprites are distinguishable from transparent space.
No color interpolation is introduced by ScaleFX. `verify-scalefx-reference`
compares the adaptation with the upstream GLSL on macOS/OpenGL, including that
explicit alpha change on the transparent fixtures. Fingerprints from that
independent reference are also retained in the portable ROM-free test.

`sai.hpp` adapts `_2xSaI` and `Super2xSaI` from Snes9x to premultiplied ARGB8888,
clamped borders, and separate output pixels. Branch ordering is retained.
The RGB565 reference (only indentation normalized) is `2xsai.reference.cpp`; it is compiled only
by the regression test, using minimal test declarations in
`tests/scalers_reference`. Both that reference and the adaptation are covered
by `SNES9X-LICENSE.txt` (noncommercial, attribution retained).
https://github.com/snes9xgit/snes9x/blob/master/filter/2xsai.cpp
https://github.com/snes9xgit/snes9x/blob/master/LICENSE
Retrieved 2026-10-12. These are the standard SaI-family modes agreed for the
request's “CrispSal / SmoothSal”; they are not the unrelated Pilzprinz modes.

xBRZ is not included: the published Zenju implementation is GPLv3 with named
exceptions that do not include this project. The project's noncommercial
license cannot simply be applied to that implementation. No approximation is
exposed under the xBRZ name. Adding it requires a compatible implementation or
permission from its rights holder; the repository license is unchanged.
https://sourceforge.net/projects/xbrz/

These files contain algorithms and synthetic references only. Runtime assets
still come from the user's ROM files. All game image captures remain ignored.
