# True widescreen implementation plan

**Status: experimental 16:9 and 16:10 previews released for macOS; exhaustive acceptance remains open.**
Updated **2026-10-23** for
[macOS Preview 3](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.3).
The existing Windows Preview 2 download includes 16:9.

Render additional scene content left and
right of the original view, preserving object proportions, vertical coverage and
native simulation. Initial target: 16:9; 16:10 now uses the same pipeline, with ultrawide still future work. Stretch,
crop-to-fill, repeated border columns and invented scenery are not acceptance
substitutes. Keep Original as the default and regression reference.

## Implemented preview

**Options → Video → Viewport** cycles Original, **16:9 Preview** (398×224,
39 additional columns per side) and **16:10 Preview** (358×224, 19 per side).
Both wide modes render additional scene content with square-pixel proportions.
Widths are rounded to even numbers for symmetric margins. Original remains the
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
Broader 16:10 acceptance, ultrawide and sustained performance remain open. Windows
has finite Stage 1/5 runtime evidence; the new 16:10 mode still needs physical
Windows/Linux checks.
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

## Delivered preview and remaining gates

macOS Preview 3 provides **Original, 16:9 and 16:10**, with 224 logical rows,
current square-pixel proportions, native combat rules and a centered HUD.
Windows Preview 2 provides Original and 16:9. These are experimental increments;
exhaustive widescreen acceptance remains open. See the
[Preview 3 release gate](ENGINE_NOTES.md#preview-3-release-2026-10-23).

| Milestone | Current state | Remaining acceptance |
| --- | --- | --- |
| 1. Scene audit/export | Immutable partial-frame snapshots and passive capture implemented | Audit additional object controllers/unsupported scenes |
| 2. CPU backgrounds | Wide road/sky/tiles and exact legacy reconstruction implemented | Broader stage/input/scene trajectories |
| 3. Sprites and visibility | Wider projection, retained scenery, horizon/wave extension and queue/depth-drop recovery implemented | Remaining controller/culling and visible-but-untargetable cases |
| 4. Existing enhancements | Wide CPU/GPU filters, translucency and corrected ground implemented | Other backends and peak-load behavior |
| 5. Frontend integration | Persisted Original/16:9/16:10 viewport, centered HUD/menu, capture and switching implemented | Complete physical high-DPI/resize/fullscreen/input matrix |
| 6. Release/acceptance | macOS preview release gates pass for the recorded scope | Broader scene audit and sustained performance for both widths; ultrawide separately |

The 2026-10-15 Preview 1 integration run covers all 23 directed slots: 22,494 counted native
RGB/PCM/time frames unchanged and 8,280 exact legacy reconstructions. It also
covers 840 attract frames and a 720-frame inversion tail with both actual roll
directions. Four natural lifecycle replays match ten MAME observation columns,
legacy images and terrain/protected-layer gates across 104,452 frames, zero skips.
The unchanged seven Metal fixtures retain 84 passing filter/opacity cases;
this is reused GPU evidence, not a new readback run. See
[the complete release checkpoint](ENGINE_NOTES.md#macos-preview-packaging-2026-10-15).

The 16:10 implementation included in macOS Preview 3 shares the renderer and width-derived visibility
margin with 16:9. `viewport=original|16:9|16:10` is persisted; legacy
`widescreen=1` migrates to 16:9. The explicit viewport wins regardless of file
order. Switching widths rebuilds presentation resources and clears detached
visibility history without advancing native time; fresh clips arrive on the
next completed frame. HUD/menu width remains 320.

Local validation on 2026-10-17:

| Check | Result / local record |
| --- | --- |
| ROM-free tests and optimized native build | Pass; `build/workflow/20261017T202212.268264Z/` |
| Independent geometry at 398×224 and 358×224 | Both pass; `build/workflow/20261017T202530.947304Z/` |
| Geometry negative controls | 18/18 rejected (nine per width); `build/workflow/20261017T202625.741852Z/` |
| Native GPU/menu | 4,128/4,128 cases identical, zero skips; both viewport selections, centered pointer coordinates and wrap navigation pass; `build/workflow/20261017T202419.403322Z/` |
| Stage 1 integration and directed stage sweeps | `verify-stage1` passes in 454.91 s; all 23 slots at each width preserve 22,540 counted native RGB/PCM/time frames per width, zero differences/skips; `build/workflow/20261017T202233.433957Z/01-verify-stage1.log` |
| 16:9 GPU retained scene | 12/12 filter/opacity cases identical to CPU, zero skips; `build/oracle/widescreen/aspect-398-gpu-final.log` |
| 16:10 GPU Stage 1/5 fixtures | 24/24 filter/opacity cases identical to CPU, zero skips; final pass `build/workflow/20261017T203029.378955Z/` |
| Existing MAME projection corpora | 240/240, 1,199/1,200 and 1,198/1,200 passes identical; zero differences; respectively 0, 1 and 2 input-race skips; `build/workflow/20261017T202729.742570Z/` |
| 16:10 native launch | 120-frame Metal and 60-frame dummy/software smokes pass; `build/oracle/widescreen/viewport-16-10-{metal,smoke}.log`; finite launch checks, not sustained performance |

GPU checks require desktop display access: sandbox-only attempts could not
enumerate a display; their reruns with desktop access pass. On 2026-10-19 the
complete `required` gate, including retained mutations, passed on the integrated
source at `build/workflow/20261019T042949.062480Z/`; the supplemental `full`
profile passed at `build/workflow/20261019T050739.004809Z/`. Earlier
release/lifecycle results above retain their historical scope. These checks do
not declare a new release or exhaustive scene acceptance.

Next: integrate the primary renderer in the work order below while retaining
controller/scene and sustained-load audits for both widths. Establish named
Windows/Linux/GPU coverage before accepting ultrawide. Optional
pixel-aspect correction, edge-anchored HUD and wider gameplay rules are separate
features. Do not promote finite lifecycle coverage into exhaustive visual coverage.

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

### Primary-renderer integration work order (2026-10-24)

The preview's isolation makes it a useful comparison reference, but duplicates
CPU work. With Sprite Filter Off and the NTSC scanline preset, the measured
16:10/sharp-HUD/ground path spends 2.7–5.5 ms reconstructing scenes in addition
to approximately 10–12 ms of board execution. This motivates sharing rendering
work; the measurements do not attribute all that cost to viewport width alone.
See [the bounded evidence](ENGINE_NOTES.md#ntsc-preset-performance-follow-up-2026-10-24).

Target pipeline:

`native partial-render frontier -> primary variable-width layers -> native center + enhanced presentation -> SDL`

1. **Shared road/tile pass:** experimental `Screen::render_integrated_partial`
   accepts reusable `Screen::ViewportLayers`, renders the underlay once at the
   selected width and copies its center into normal native composition. Native
   text, sprite raster dimensions and feedback stay unchanged. It is a testable
   first slice; the snapshot frontend remains the default comparison reference.
2. **Live frontier integration (experimental):** the host can now select the
   shared pass at its existing partial-render boundary. An optional synchronous
   consumer on `GameSession::capture_wide_scene` supplies pre-feedback metadata
   and borrowed primary layers to `WideScene::render_primary_clip`.
   `finish_primary_frame` publishes the enhanced output only after the native
   board tick. The optional completion callback also covers hidden startup frames.
   Native observers and `GameSession::frame()` remain intact. The frontend adapter
   is selectable with `--scene-renderer integrated`; `snapshot` remains the default
   until full acceptance. The setting is a launch-time experimental override.
   Retained snapshots support comparisons; compact capture reuses one metadata
   slot, omits the unused tile snapshot and makes `wide_scene()` unavailable.
3. **Enhanced composition (experimental):** the live consumer uses the existing
   extended descriptors, ground correction, shadows, fragment chains, detached
   HUD and aircraft policy while bypassing duplicate road/tile rasterization and
   tile/road-device reloads. Paired-session and snapshot comparisons cover the
   recorded 358/398/522 trajectories and lifecycle changes. Retain the snapshot
   reference and native sprite feedback pass; remaining scene, GPU, custom-art
   and sustained-load acceptance precede removing further work.
4. **Acceptance and rollout:** compare native RGB/PCM/time and feedback with
   independent MAME fixtures, reject relevant mutations, compare enhanced output
   with the retained preview, and benchmark matched settings and busy trajectories.
   Run complete required gates before promoting the path to the playable frontend.
   A synthetic pass is not MAME or whole-host acceptance.
   Windows independent RGB passes 3,500/3,500 comparisons; complete `test` and
   `verify-stage1` pass. Instrumented CPU/device and supplemental full gates are explicitly
   deferred to the user's Mac; this keeps default promotion open.
5. **Additional ratios:** keep logical width independent of menu presets. The
   initial API follows the existing bound of even widths 320–640 and 224 rows;
   522 exercises nominal 21:9 geometry. Wider visibility, actual panel ratios,
   scene coverage, HUD placement and performance require separate validation.
   No new viewport selection is enabled by this prototype.

Matched frontend timing uses the same executable, viewport/settings, shader
runtime and neutral controller policy in snapshot/integrated/integrated/snapshot
order. Compare `board_ms + wide_scene_ms`: primary background work moves into the
board tick, while enhanced-consumer time is reported separately. The runner keeps
every interval/log/final image, excludes the first timing interval from summaries,
and rejects different final images or inactive requested rendering paths. Example:

```powershell
python tools/video/benchmark_integrated_frontend.py --binary build/windows-lto/afterburner2.exe --shader build/playable/shaders/ntsc/ntsc-320px-composite-scanline.slangp --runtime build/playable/librashader.dll
```

Stop unrelated builds/captures while timing. These finite Practice measurements
do not replace sustained full-stage/fullscreen acceptance. Independent MAME RGB
comparisons validate the native center only; physical hardware has no outer band.

Custom stages are a complementary content workstream: document stage progression,
road/scenery placement, formations, spawn timing and transition contracts, then
build an opt-in stage-data interface using understood routines. New behavior
requires an explicit extension contract. Widening the renderer does not provide
stage authoring or justify changing the verified arcade execution path.

### Retained preview architecture

`native partial-frame snapshots -> read-only wide renderer -> enhanced planes -> SDL presentation`

Retain the native 320×224 render, scheduler, bus access order, sprite feedback and
original RGB/PCM. `GameSession::frame()` keeps its current contract. The implemented
`capture_wide_scene` / `wide_scene()` interface supplies immutable clips and frame/time
identity; `WideScene::view()` exposes width, height, native origin and tightly
packed planes. Copy borrowed data before asynchronous use. Capture is opt-in;
clear enhanced history on restart/mode change.

Do not simply construct a wider `Screen` or call the native sprite rasterizer
again on live state. `HWSprites` writes word-7 scaler feedback, while road/tile/
palette/sprite state changes at native partial-render frontiers. Export immutable
snapshots synchronously at those frontiers, including the buffered data actually
used for that row range. A final-frame RAM snapshot alone is insufficient.
The implemented `SceneClip` export includes buffered road/sprite data, tile/text/
palette/register state and detached descriptors. A native-sized sprite plane
alone does not supply this complete wide scene contract. Compose wide capture with the
existing ground/effect observers; do not overwrite their callbacks. Capture must
serve the same published data and clip-frontier palette across all consumers.

Use centered coordinates: `margin = (W - 320) / 2`,
`native_x = wide_x - margin`. Preserve vertical projection, source zoom and native
positions in the center; widen the horizontal visible interval. Define separately
the source address wrapping, signed screen position and destination clipping.
Translating a descriptor must not turn offscreen coordinates into visible wrap
duplicates. The original 320×224 rectangle is the center-crop reference.

## Delivery contracts

The following contracts describe implemented stages and constraints for extending
them. Remaining audits are identified explicitly; imperative wording does not
mean the existing snapshot, renderer or menu still needs to be built.

### 1. Audit and passive scene export

**Current state:** Implemented for the recorded paths; further controller/scene audit remains.

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

**Current state:** Implemented at 398×224; exhaustive scene/aspect coverage remains.

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

**Current state:** Implemented, including detached capacity and both-edge/roll checks; broader backend/load acceptance remains.

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

**Current state:** Wider Sub projection and audited scenery retention are implemented; native combat rules remain unchanged.

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

**Current state:** Original/16:9/16:10 settings and centered safe area ship in macOS Preview 3; ultrawide and optional HUD relocation remain.

Options → Video → Viewport offers Original, 16:9 and 16:10. Add
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

The released macOS 16:9/16:10 previews pass their declared background, sprite,
visibility, HUD and lifecycle gates. Broader scene/performance acceptance remains
open for both widths; ultrawide requires separate validation. Record supported scenes/aspects,
fallbacks, enhanced gameplay differences and unverified cases. True widescreen
does not establish full-credit arcade equivalence.
