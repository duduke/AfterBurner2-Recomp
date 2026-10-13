# True widescreen implementation plan

**Status: planned, not implemented.** Render additional scene content left and
right of the original view, preserving object proportions, vertical coverage and
native simulation. Initial target: 16:9; follow with 16:10 and ultrawide. Stretch,
crop-to-fill, repeated border columns and invented scenery are not acceptance
substitutes. Keep Original as the default and regression reference.

## Scope and aspect policy

Separate three choices: simulation resolution (always native 320×224), enhanced
logical viewport, and physical display scaling. The current frontend presents
320×224 logical pixels; that ratio is 10:7, not 4:3. Establish pixel-aspect policy
before selecting wide dimensions rather than silently changing object shapes.

For height `H = 224`, target display ratio `R` and pixel aspect `P` (physical
pixel width/height), use `W ≈ H × R / P`. Round to a documented even logical
width so equal side margins remain integer; fit the resulting image without
additional anisotropic stretching. Illustrative nearest-even widths:

| Target | Current square-pixel policy (`P = 1`) | Optional 4:3 correction (`P = 14/15`) |
| --- | --- | --- |
| 16:9 | 398×224 | 426×224 |
| 16:10 | 358×224 | 384×224 |
| 21:9 | 522×224 | 560×224 |

The 4:3 column is a proposed display policy derived from fitting 320×224 into
4:3, not a newly verified hardware property. Any such correction must apply
consistently to Original and Wide, with its own validation. Small aspect-rounding
residuals may leave narrow bars. Actual ultrawide panels need their measured
aspect, not a universal width labelled 21:9. Cap supported width explicitly.

## Architecture

`native partial-frame snapshots -> read-only wide renderer -> enhanced planes -> SDL presentation`

Retain the native 320×224 render, scheduler, bus access order, sprite feedback and
original RGB/PCM. `GameSession::frame()` keeps its current contract. Add an opt-in
wide presentation result with width, height, stride, native origin and frame/time
identity; copy borrowed data before asynchronous use. Disable capture in Original
mode and clear enhanced history on restart/mode change.

Do not simply construct a wider `Screen` or call the native sprite rasterizer
again on live state. `HWSprites` writes word-7 scaler feedback, while road/tile/
palette/sprite state changes at native partial-render frontiers. Export immutable
snapshots synchronously at those frontiers, including the buffered data actually
used for that row range. A final-frame RAM snapshot alone is insufficient.
The existing `Screen::layers_rendered` and passive `sprite_scene` observer are
starting points, not complete wide scene exports.

Use centered coordinates: `margin = (W - 320) / 2`,
`native_x = wide_x - margin`. Preserve vertical projection, source zoom and native
positions in the center; widen the horizontal visible interval. Define separately
the source address wrapping, signed screen position and destination clipping.
Translating a descriptor must not turn offscreen coordinates into visible wrap
duplicates. The original 320×224 rectangle is the center-crop reference.

## Delivery stages

### 1. Audit and passive scene export

Trace road buffered RAM/control, tile pages/scroll/category, sprite descriptor
publication/feedback, palette changes and native clipping. Identify projection,
visibility rejection, object activation/despawn and stage-specific scenery
producers in ROM. Document what exists outside the native viewport and what the
game never generates. Inspect attract, flight, refuel/carrier, crash and ending.
Unknown behavior remains unimplemented; use independent MAME observations and
rejected mutations for new semantic probes or literal ports.

Define the snapshot and wide-frame contracts, then prove enabled capture changes
no native CPU/device state, time, RGB or PCM. Keep clips and ordering across
multiple updates in one frame. Avoid timed bus reads in presentation callbacks.

### 2. Road, sky and tile backgrounds

Render road/sky across the wide destination using the original per-row control,
color and horizontal phase. Extend sampling with signed `native_x`, not a recentered
or stretched ground texture. Preserve the two-road priority rules, solid-fill
rows, source wrap/exterior behavior and partial-frame palette history.

Render BG/FG tile pages using the actual page map, scrolling, categories and
source wrapping. A large hardware tilemap does not prove populated scenery
beyond the native view. Audit blank/repeated side content and stage transitions;
defer unsupported cases instead of filling them with copied edge pixels.

Require the enhanced center crop to match native RGB with all optional filters/
opacity/HUD relocation off. Side bands must visibly contain genuine additional
road/tile content. This background milestone is not complete widescreen.

### 3. Sprites, priority and effects

Generalize the read-only sprite decoder to a signed wide clip and dynamic stride;
remove presentation-only 320-pixel bounds while retaining descriptor semantics,
row pitch, zoom, flip, nibble/address wrapping and end markers. Never change the
native scaler's execution or RAM feedback. Include partially visible sprites at
both edges and preserve overlap, tile priority and palette shadow termination.

Generalize CPU/GPU foreground, background, protection and fragment-chain buffers
to `W×224`, including Off composition, all five filters and opacity effects.
Keep the existing effect identities and full-throttle flame predicate. Validate
GPU dispatch bounds, texture limits and border sampling. Under identical feature
settings, compare wide GPU output with the wide CPU reference. Filters can sample
new neighboring pixels at the old border; exact center identity is required for
unfiltered/opaque mode, not assumed for every enhanced filter setting.

### 4. Projection and gameplay visibility

The first render-only increment preserves native object positions, spawning,
collision, targeting, weapon limits and scoring. It cannot reveal objects culled
before publication or guarantee waves use the added space. Do not call that
increment complete if objects disappear at invisible native boundaries.

For full widescreen, identify the owning ROM projection/culling producers and
expose a separate enhanced policy. Keep center scale/focal geometry and vertical
coverage; expand horizontal frustum and scenery visibility. If necessary, retain
otherwise offscreen objects for rendering without activating attacks/collisions
early. This requires owning object state, not just changing a draw clip.

Changing spawn regions, movement bounds, lock-on or despawn timing is gameplay
work and must be explicitly isolated to the enhanced mode. Prefer preserving
native combat bounds initially, document visible-but-untargetable objects, and
evaluate whether that is acceptable before release. Do not promise unchanged
gameplay if those policies change. Do not increase hardware descriptor/game
request capacity to compensate for missing content without a separate plan.

### 5. HUD, menus and settings

Add Options → Video → Viewport: Original, 16:9, 16:10 and supported ultrawide/custom
aspect. Persist independently from window scaling/fullscreen. Reallocate enhanced
resources at a safe frame boundary; preserve board state and pause status.
Map window resize, high DPI, letterboxing and mouse coordinates consistently.

Default HUD/text to the original centered safe area. Edge-anchored HUD is optional
and needs semantic extraction of score, speed, stage, ammunition and messages;
do not move every text tile, duplicate HUD elements or reposition aiming geometry
as decorative UI. Validate crosshair/lock-on alignment with actual targets.
Keep initial menus, statistics, preview/capture paths and non-flight screens
readable. Until a screen has valid extra scene content, use an explicitly documented
centered Original fallback rather than stretching it.

## Validation and acceptance

- **Native identity:** Original mode remains exact against retained MAME corpora.
  Passive capture/render-only Wide also preserves native state/RGB/PCM/time;
  original-mode regressions remain mandatory for later enhanced gameplay policy.
- **Geometry:** Native center matches at Off/opaque/centered HUD; symmetric extra
  world is shown without stretching, border copies or vertical cropping. Test
  fractional zoom, both sprite directions, source wrap, edge crossing and priority.
- **Scene coverage:** Both roll directions, steep/inverted banks, altitude/pitch,
  road/sky transitions, representative stage backgrounds, weapons, crash, refuel,
  carrier and ending. The existing blue roll wedge matches MAME; correcting it is
  separate enhanced ground work, not an automatic consequence of wider output.
- **Backend/settings:** All filters and opacity combinations against CPU/GPU
  references; supported aspect/width limits, restart, resize, fullscreen, pause,
  repeated mode switches, menu input and capture dimensions.
- **Negative controls:** Wrong origin/road phase, lost frontier palette, sprite
  wrap duplicates, ignored wide edge, tile-priority loss, feedback writes and
  incorrect GPU bounds. Preserve earlier fixtures/mutations.
- **Performance:** Measure sustained board/filter/presentation cost, memory and
  frame pacing at each supported aspect with representative and peak-load scenes.
  Record tested Metal/Vulkan/software paths separately; finite smoke is not
  every-stage performance acceptance.

Use [ROADMAP's validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first)
and `python3 tools/workflow.py run <target> -j 4`: focused checks/oracles/mutations,
`verify-stage1` for session integration, relevant native/GPU builds and smokes;
complete `required` for core merges/releases/acceptance. Keep ROM-derived captures
and screenshots under ignored `build/oracle/`.

Ship 16:9 only after background, sprite, visibility, HUD and lifecycle gates pass;
then validate 16:10 and ultrawide independently. Record supported scenes/aspects,
fallbacks, enhanced gameplay differences and unverified cases. True widescreen
does not establish full-credit arcade equivalence.
