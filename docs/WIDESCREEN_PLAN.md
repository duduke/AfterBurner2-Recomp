# True widescreen implementation plan

**Status: experimental 16:9 preview implemented 2026-10-14; full acceptance remains open.**

Render additional scene content left and
right of the original view, preserving object proportions, vertical coverage and
native simulation. Initial target: 16:9; follow with 16:10 and ultrawide. Stretch,
crop-to-fill, repeated border columns and invented scenery are not acceptance
substitutes. Keep Original as the default and regression reference.

## Implemented preview

**Options → Video → Viewport → 16:9 Preview** selects 398×224 square-pixel
presentation, with 39 additional columns on each side. Original remains the
default. The HUD/menu stay in the centered 320×224 safe area. Window resizing,
pointer conversion, full-width captures, saved preferences, existing sprite
filters, translucency and optional corrected ground use the selected viewport.
The SDL GPU path and CPU/software fallback share the same wide scene planes.

`GameSession::capture_wide_scene` copies immutable road/tile/text/palette/sprite
state before each native partial render. `WideScene` renders these snapshots
without widening the native renderer or writing sprite feedback. Native gameplay,
object limits, audio and timing remain authoritative.

### Wider visibility and roll coverage

A detached presentation observer now recovers requests hidden by the Sub CPU's
native horizontal bounds. It uses the verified `$7B8..$9E2` projection with wider
bounds and unwrapped X positions; ROM art, zoom, flips, pitch and interpolation
remain the source of the image.

Scenery deleted by `$8B94..$8BB8` is retained in private object records. The
verified `$8AF2..$8C14` update advances those records against current camera
inputs, including the live shared rotation/position pair, until they leave the
wider bounds or pass the original depth limit. Retention also covers attract
and takeoff. Takeoff repeats adjacent cells of the native water field using its
wave animation and world projection, so the extra width includes wave artwork.
Native allocator reuse cannot overwrite them. These objects never enter native
collision, targeting, sound or sprite-request state.

The horizon extends the `$10ED8` generator's exact 16.16 segment increments at
both ends, using its current ROM frame/flips and previous-frame positions. The
**Ground → Corrected** option measures these full-width sprites. It
corrects eligible plain ground fill on the sky side. The enhanced wide plane also
fills confirmed ground-side sky gaps from an observed native-row ground/sea
index, including edge holes exposed by translucent clouds. Tiles, terrain,
aircraft and layer protection remain intact. A clipped marker may use the ROM
normal across the enhanced wide plane when a real silhouette edge remains
visible, including central sky holes during inversion. Missing
silhouettes/source colors remain unfilled. Observed horizon-plane bounds also
classify banked corners without a local row/column intersection. Original ground
remains available. See the [screenshot-driven checks](ENGINE_NOTES.md#screenshot-driven-outer-background-coverage-2026-10-15)
and [rolling scenery/takeoff follow-up](ENGINE_NOTES.md#rolling-scenery-takeoff-waves-and-banked-cloud-corners-2026-10-15).

Native gameplay and spawning remain unchanged. The wider renderer now observes
the producer before its signed **192-request rejection** and keeps rejected
requests in detached, dynamic presentation lists. It also recovers accepted
requests lost by the native 30-entry depth buckets, removing repeated stale
order entries from the enhanced bank. Native request counts, 256-descriptor
hardware banks, scaler feedback, object pools and combat rules stay intact.
This expands rendering capacity, not native spawning or gameplay capacity.

All 23 ordered stage slots and retained natural lifecycle recordings have
dedicated validation targets (see [coverage and results](ENGINE_NOTES.md#widescreen-capacity-and-stagelifecycle-checks-2026-10-15)). These are finite input
windows and recordings, not proof of every possible object/input trajectory.
16:10/ultrawide, sustained performance and Windows/Linux GPU checks remain open.
Other object controllers still require audit before promising complete visibility
in every possible scene.

Focused checks include independent MAME projection/scenery comparisons, native
RGB/PCM/time identity, both wide edges and roll directions, allocator reuse,
wide-edge retirement, partial palettes, pause/reset and combined feedback
observers. The legacy snapshot renderer still reconstructs the exact native
center. Extended visibility may add sprites that overlap that center; it is a
separate presentation policy. GPU readback checks all six filters and both
opacity policies against the wide CPU reference. See ENGINE_NOTES for counts,
logs and exclusions.

## Proposed first release

Start with **Original and 16:9**, retaining current square-pixel proportions,
224 logical rows, native combat rules and a centered HUD. Add 16:10 and measured
ultrawide ratios after 16:9 passes. Original remains the default. Pixel-aspect
correction, relocated HUD and gameplay changes are separate opt-in follow-ups.

The first deliverable was an **audit and passive snapshot contract**, followed by
the implemented CPU-rendered 16:9 preview. A background-only preview is a development milestone;
release requires sprites, visibility, layers, effects and lifecycle gates too.
Do not ship unseen native-edge disappearance as complete widescreen.

| Milestone | Deliverable | Gate before continuing |
| --- | --- | --- |
| 1. Scene audit/export | Immutable road, tile, sprite and palette data at each partial-render frontier; clipping/culling inventory | Capture on/off preserves native CPU/device state, RGB, PCM and time |
| 2. CPU backgrounds | Genuine additional sky/road/tile content in 398×224, with 39 columns per side | Original 320×224 center matches at unfiltered/opaque/Original ground settings; side content and both roll directions checked |
| 3. Sprites and visibility | Signed wide clipping, layer priority, shadows and coverage of pre-render rejection | Sprites cross both edges correctly; source wrap and native-boundary popping accounted for |
| 4. Existing enhancements | Wide CPU/GPU planes, all sprite filters, translucency and corrected ground | GPU matches the wide CPU reference; original presentation and terrain-preservation checks remain intact |
| 5. Frontend integration | Viewport setting, centered HUD/menu coordinates, resize/fullscreen/capture handling | Pause/restart/mode switches, high DPI, pointer alignment and restored arcade attract startup pass |
| 6. 16:9 acceptance | Supported-scene/aspect matrix and sustained performance measurements | Relevant fixtures and rejected mutations, then the complete required profile for release |

Current progress: snapshot export, CPU rendering, published-sprite clipping,
CPU/GPU effects and the frontend setting are implemented. Milestone 3 now includes
detached scenery retention and wider Sub projection; milestone 4 includes added
horizon segments and full-width correction. Complete object/scene coverage and
full release acceptance in milestone 6 remain open.

Each milestone records unsupported scenes and dependencies. New probes or literal
ports need independent MAME witnesses; side bands have no automatic MAME oracle,
so validate their addressing/geometry independently and use deliberate mutations.

## Scope and aspect policy

Separate three choices: simulation resolution (always native 320×224), enhanced
logical viewport, and physical display scaling. The Original viewport presents
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
starting points, not complete wide scene exports. Compose wide capture with the
existing ground/effect observers; do not overwrite their callbacks. Capture must
serve the same published data and clip-frontier palette across all consumers.

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

The corrected-ground implementation now has width-aware silhouettes and column
arrays in the enhanced renderer, preserving its sky/terrain role checks.
Test sea/terrain at the lower-left and lower-right
edges during rolls in both directions; a wider horizon must not erase terrain.
Retain Original ground as the strict center-crop reference.

Generalize CPU/GPU foreground, background, protection and fragment-chain buffers
to `W×224`, including Off composition, all five filters and opacity effects.
Keep the existing effect identities and full-throttle flame predicate. Validate
GPU dispatch bounds, texture limits and border sampling. Under identical feature
settings, compare wide GPU output with the wide CPU reference. Filters can sample
new neighboring pixels at the old border; exact center identity is required for
the legacy snapshot reference, not assumed after extending object visibility or
for every enhanced filter setting.

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

Initially add Options → Video → Viewport: Original or 16:9. Add 16:10 and
supported ultrawide/custom aspects after their separate acceptance gates. Persist independently from window scaling/fullscreen. Reallocate enhanced
resources at a safe frame boundary; preserve board state and pause status.
Map window resize, high DPI, letterboxing and mouse coordinates consistently.

Maintain separate gameplay and menu coordinate spaces: menu content stays at
320×224 in a centered safe area, including its scrollbars. Transform mouse input
through that same placement. Statistics and screenshots use explicit dimensions.

Default HUD/text to the original centered safe area. Edge-anchored HUD is optional
and needs semantic extraction of score, speed, stage, ammunition and messages;
do not move every text tile, duplicate HUD elements or reposition aiming geometry
as decorative UI. Validate crosshair/lock-on alignment with actual targets.
Keep initial menus, statistics, preview/capture paths and non-flight screens
readable. Until a screen has valid extra scene content, use an explicitly documented
centered Original fallback rather than stretching it.

## Sprite capacity and custom stages

The hardware sprite bank remains 256 descriptors and the native request capacity
remains 192. The opt-in wider renderer bypasses those presentation bottlenecks
with detached producer copies and dynamic descriptor storage. A 512-rejected-
request fixture exceeds both native limits; a 5,001-descriptor fixture checks the
dynamic decoder tail. An actual Sub sorter fixture recovers 34 of 64 requests
from a full final depth bucket without repeating stale native order entries.
Independent MAME producer fixtures verify the request bytes and signed capacity
boundary. More render capacity does not recover objects never spawned or already
rejected by an unaudited object controller; gameplay object allocation stays native.

Custom stages are a later opportunity to design formations, scenery and pacing
for the added width. Original stages can supply the initial wider view. New spawn
areas or full-width combat need a verified stage/object interface and explicit
enhanced gameplay rules; they are outside this rendering plan's first release.

## Validation and acceptance

- **Native identity:** Original mode remains exact against retained MAME corpora.
  Passive capture/render-only Wide also preserves native state/RGB/PCM/time;
  original-mode regressions remain mandatory for later enhanced gameplay policy.
- **Geometry:** Native center matches at Off/opaque/centered HUD; symmetric extra
  world is shown without stretching, border copies or vertical cropping. Test
  fractional zoom, both sprite directions, source wrap, edge crossing and priority.
- **Scene coverage:** Both roll directions, steep/inverted banks, altitude/pitch,
  road/sky transitions, representative stage backgrounds, weapons, crash, refuel,
  carrier and ending. The targeted roll-wedge correction is already implemented
  as an optional enhancement. Keep its both-direction/terrain-preservation
  regression coverage when adapting it to wide output.
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
