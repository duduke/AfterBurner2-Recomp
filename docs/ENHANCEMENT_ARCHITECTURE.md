# Extending the translated game

Updated **2026-10-15** against the published macOS preview. Implemented interfaces
are listed below; future features and dependencies live in the
[enhancement backlog](ENHANCEMENT_PLAN.md).

The native application uses SDL3's GPU renderer and compute sprite filters.
The Three.js scene prototype is retained only on
[`codex/threejs-game-engine`](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine),
including its browser-specific source and tests.

The translated main/sub ROM logic and original runtime assets are the base.
`XBoardHost` owns gameplay, shared memory, scheduling and hardware. `XBoardGame`
owns the board lifetime. The application uses `GameSession` as its public
boundary; application features do not need access to CPU registers or RAM.

The original mode remains the reference while enhancements are developed.
Cycle accuracy is not a requirement for new features, but removing existing
scheduling must preserve the game's update order and observable behaviour.
The accepted finite MAME corpora remain regression checks. Exhaustive scene and
whole-credit fidelity acceptance remain open; this API does not close them.

## Available interfaces

| Interface | Contract | Phase 5 uses |
| --- | --- | --- |
| `GameControls` | Normalized X/Y/throttle in -1..1, cannon/missile/coin/start buttons; positive Y means up | Keyboard/controller/HOTAS mapping, calibration, deadzones and response curves |
| `GameSession::advance` | Advances one physical board frame; returns false while paused | Frontend pacing, bounded runs and replay drivers |
| `set_paused`, `restart` | Pause freezes all board owners; restart creates a cold board, discards session audio and unpauses | Pause/settings UI and restart commands |
| `stage_count`, `stage`, `enter_practice_stage` | Runtime ROM stage catalog and explicit directed entry after fresh coin/start boot; keeps first-stage accumulated state | Local [Practice](PRACTICE_MODE.md) startup/retry; mode policy and recording exclusion stay in the frontend |
| `frame()` | Read-only completed native 320×224 RGB plus frame number and board time | GPU presentation, scaling, shaders, screenshots and overlays |
| `capture_sprite_scene`, `sprite_scene()` | Opt-in read-only per-pixel ordered fragment chains from published ROM sprite descriptors; clip-frontier palette and priority retained, reset on restart | Independent cloud/trail/explosion/flame opacity and future per-sprite materials |
| `capture_sprite_presentation`, `sprite_presentation()` | Opt-in read-only native-sized ARGB background/sprite copies and tile/shadow protection; valid after the next completed frame, invalidated on disable/restart | Sprite-only frontend filtering, with original native raster retained |
| `capture_wide_scene(enabled, logical_width)`, `wide_scene()` | Immutable partial-render clips with frame/time identity and width-specific detached descriptors; native RGB/PCM/time stay authoritative; width changes clear enhanced visibility history | Implemented 16:9/16:10 renderer, wider projection/scenery and capacity recovery; default width 398 preserves earlier callers |
| `capture_feedback`, `take_feedback()` | Passive verified cue events and continuous-state snapshot; restart clears history | Implemented body/trigger rumble; additional warning/telemetry cues need verification |
| `capture_music`, `take_music()` | Native music/effects stems and typed start/stop/fade events; original `take_audio()` stays intact | Implemented replacement tracks and independent gains |
| `capture_cockpit` with `take_music()` | Optional chip-rate FM/PCM voice ownership/streams | Implemented OpenAL resampling and cockpit output; unknown voices retain stereo fallback |
| `Extensions::frame_complete` | Called once per completed frame, including each catch-up frame | Capture/recording and frame-driven frontend features |
| `Extensions::process_audio` | Processes outgoing 48 kHz signed 16-bit stereo after the original devices have mixed it | Master volume, output effects and supplemental audio mixing |
| `Extensions::restarted` | Called after an explicit restart; registered functions persist | Reset extension playback cursors/history |

Frame/sample spans are borrowed for the callback or until the next board
advance/restart. Copy them for asynchronous work. Callbacks run synchronously
on the session caller's thread; they must not re-enter session methods. Frame
numbers and board time restart at zero. No constructor restart notification
is emitted. Extensions should initialize their own state before construction.
Drain `take_audio()` regularly; it returns samples accumulated since the last
drain. The platform owns its playback queue and must pause it with the session
and clear it on restart. Wall-clock catch-up debt must also be cleared after
pause/restart. The SDL application implements this and exposes P to pause.

For example, master volume lives outside the sound chips:

```cpp
float volume = 1.0f; // configuration owned by the frontend
GameSession::Extensions extensions;
extensions.process_audio = [&](std::span<int16_t> samples, GameSession::FrameInfo) {
    const float gain = std::clamp(volume, 0.0f, 1.0f);
    for (auto& sample : samples) sample = static_cast<int16_t>(sample * gain);
};
GameSession session(roms, std::move(extensions));
```

Compose multiple processors explicitly in their intended order within this
callback. Menus and configuration belong in `frontend/`; SDL device mapping,
window/fullscreen options and playback belong in the platform layer. Keep
those concerns out of the address-labelled translated routines.

## Features that need a deeper interface

The output boundary is a starting point, not a claim that every Phase 5
feature can be implemented as a postprocessing effect.

| Feature | Next interface needed |
| --- | --- |
| Additional feedback, motion telemetry, near-miss bonuses | Extend the implemented typed feedback events/state with independently verified producers and explicit ordering; basic rumble is already implemented |
| Additional music/effect policies | Use the implemented verified stem/event interface; final mixed PCM alone cannot separate original music |
| Additional widescreen controllers/aspects and replacement sprites | Reuse immutable scene clips and detached visibility; add verified controller coverage and descriptor-to-asset mapping. Basic 16:9 rendering/capacity recovery is already implemented |
| Stage selection, shuffled missions and new formations | Local [Practice mode](PRACTICE_MODE.md) uses a typed ROM catalog and fresh directed startup; [Shuffle](SHUFFLE_MODE.md) owns a remapped ROM data view for continuous seeded runs. Full Practice entry-state fidelity and new formations require further verified interfaces |
| New flight/combat rules or wingman play | Readable subsystem adapters extracted from verified player/object/collision routines; maintain native arithmetic and update order in original mode |
| Save states | Complete versioned serialization of board/device and pending CPU continuation state, plus extension state; a RAM dump is insufficient |

Introduce those interfaces at the owning subsystem as each feature is built.
The [true widescreen plan](WIDESCREEN_PLAN.md) separates passive wide rendering
from ROM-driven projection/culling and enhanced gameplay visibility.
First establish the original path with ROM/MAME evidence; then expose named
data and operations and implement the optional policy beside it. Avoid a
generic RAM-patching callback: it hides ownership and makes original-mode
regressions difficult to diagnose. Do not infer undocumented stage counts,
spawn rules, sound triggers or collision rules to fill gaps.

## Validation

`make test-game-controls` checks the physical mapping, all 16 button
combinations and invalid/out-of-range device axes without ROMs.
`make verify-game-session` compares the session to `XBoardGame` for the same
781-frame physical-input run, including exact RGB/audio identity, pause/resume,
nonzero audio processing and extension restart lifecycle. These checks are
included in `test` and `verify-stage1` respectively. All existing MAME checks
remain required; the session comparison supplements them.

## Native settings implementation

`frontend/native_menu.hpp` owns the ROM-font texture cache, navigation and
presentation-only statistics. `frontend/native_settings.hpp` owns versioned
preferences. SDL options are applied by the application, with rejection retaining
the last accepted configuration. The UI draws after the completed board raster;
it never changes board frame pixels, RAM or device state. Menu entry pauses
`GameSession` and playback, exit restores the previous pause and clears pacing
debt. Menu keys are withheld from game controls until released.

`test-native-settings` covers persistence/defaults/invalid input and save failure.
`verify-native-menu` covers actual SDL filtering, integer presentation, fullscreen,
navigation, rejected changes and preservation of board frames/time and pause state.
See [WORKFLOW](WORKFLOW.md#native-settings-menu) for controls and configuration.

`frontend/native_crt.hpp` owns the optional final CRT display pass. It modulates
the completed SDL game/HUD image with a cached output-resolution texture before
frontend UI, preserving logical presentation/viewport/clip/scale state and
letterbox pixels. It consumes no board state and requires no framebuffer readback
or offscreen game copy. Cache replacement is transactional, capped at 64 MiB;
Off releases it. See [CRT display](CRT_DISPLAY.md) for presets, pixel phase,
small-window contrast reduction, settings and validation.

`frontend/native_slang.hpp` provides a separate optional RetroArch `.slangp`
host. On macOS it dynamically loads librashader and uses public SDL native Metal
and IOSurface APIs. It captures a native-width ×224 game/HUD image, runs the
preset to the physical game rectangle and draws frontend UI afterward. This
reduces HD replacement art/HUD detail to the native raster. The main frontend
recreates renderer resources when switching GPU/Metal while preserving the
GameSession, input, menu and decoded custom HUD. Shader mode uses CPU sprite
filters. Preset loading is transactional; paused output is cached, history is
reset for discontinuities, and errors retain a visible game image. Other
platforms report unsupported and retain the built-in modes. Runtime/shader
binaries remain external; see [setup and limits](CRT_DISPLAY.md#retroarch-slang-presets-macos-metal).

## Sprite-only presentation

GameSession copies indexed background colors at the existing partial-render
observer and determines visible sprite coverage using the hardware raster and
native priority rule. The existing post-render observer resolves sprite colors.
It never reruns the hardware scaler, writes sprite feedback or changes native
composition. Palette shadows are kept on the background plane, with their native
coverage protected; filtered edges cannot cover foreground tiles/text. The
capture is opt-in and survives restart through observer rebinding. Borrowed spans
are invalidated by advance/restart or capture configuration changes.

`frontend/gpu_sprite_filter.hpp` uploads the native background, sprite and
protection planes and runs filtering plus premultiplied alpha composition in
SDL GPU compute passes. The scaled output remains on the GPU and is wrapped
for SDL's 2D GPU renderer; unchanged frames reuse it. ScaleFX uses four float
intermediate passes followed by scaling/composition; the other filters use
one pass. No per-frame GPU readback is needed. `frontend/sprite_filter.hpp`
retains the CPU reference and software/headless fallback. GPU composition also handles Off at native size; software Off with effects
disabled uses the original texture/capture-free path. Existing
SDL window scaling remains a separate final presentation choice. This smooths
visible sprite rasters after native sprite zoom; it is not a replacement asset
pipeline or a change to ROM sprite decoding.

`test-sprite-filters` compares SaI to unchanged upstream RGB565 routines and
ScaleFX to retained synthetic fingerprints from the original GLSL, plus borders,
transparency, priority protection and input immutability. The supplemental macOS
`verify-scalefx-reference` compares all five CPU passes against the actual GLSL
on an offscreen OpenGL context. Source/license notices live in
`frontend/scalers/README.md`. xBRZ remains pending licensing compatibility.

## Optional output-resolution text plane

`WideScene::render(..., original_hud=false)` retains the native scene clips and
exposes a `HudScene` with visible text entries, normal/effects palette pens and
native sprite occlusion. The separate underlay omits text. `NativeHud` draws
ROM-derived Sharp 3x glyphs or user indexed RGBA tiles directly through SDL's
logical-to-output transform, respecting partial glyph clips and native priority.
Native-width presentation reuses the existing scene capture at width 320 with
extended visibility disabled. The hardware renderer and original frame remain
unchanged. Sprite instruments have no new semantic adapter.
See [HUD presentation](HUD_PRESENTATION.md) for usage, limits and checks.

## Individual sprite composition and optional transparency

The opt-in [HD player-aircraft pack](SPRITE_REPLACEMENT.md) uses the same
immutable scene clips. `PlayerAircraftAssets` derives neutral/gentle-bank
frame/LOD identities from runtime ROM tables; `WideScene::player_aircraft`
admits only loaded pose roots, suppresses an unambiguous matching descriptor
and publishes native-coordinate rectangles and per-clip ownership masks.
`NativePlayerAircraft` transactionally loads registered per-pose textures from
a versioned `.aircraft` manifest, preserving source bounds/pivots/scales and
optional LOD registration. It samples art at output resolution before CRT.
Legacy PNG/BMP remains neutral-only. Later shadow or enabled translucent
effect overlap keeps the original aircraft for that clip. This is a single-pose
prototype, with no board mutation or change to native RGB/PCM/time. Detached HUD
also keeps priority-3 aircraft native because the authored alpha ownership is
not available to the separate glyph compositor; observed flight priority 1 is
supported.

Aircraft extending beyond the native 320-column safe area retain Original.
The present HD ownership mask cannot represent the widescreen overhang; whole
descriptor suppression would otherwise remove those margin pixels. Nozzle
annotations aid asset review and never rewrite native flame placement or timing.

`hwvideo/sprite_scene.hpp` passively decodes the published hardware list at the
existing layer observer. It follows native row stepping, nibble order, wrapping,
zoom and clipping without writing word-7 scaler feedback or issuing bus accesses.
Per-row fragment chains retain covered sprites across partial clips; palette
colors and blocked-tile/shadow outcomes are resolved at each native frontier.
GameSession publishes a compact read-only scene after the completed frame.
Native board RGB/audio and CPU/device ownership remain unchanged.

Cloud/trail classification uses known ROM frame families and runtime LOD offsets,
not color guesses. The app's optional policy uses 160/255 opacity for known clouds
and 112/255 for known missile trails, with independent Opaque/Translucent menu
rows and all effects Opaque by default. Explosions use 128/255, including player-crash fire/smoke. Player flame
pixels use 128/255 only at the ROM's maximum throttle during active flight.
Ordinary-throttle flames and aircraft debris remain opaque. These values are enhancement choices.
A GPU compute pass blends front-to-back into a premultiplied foreground plane,
then the existing scaler/background/protection pass presents it. CPU scene
composition is the software fallback and validation reference. Native foreground
protection and hardware shadows retain their original coverage. Tile/shadow
stack termination stays on the background plane so its colors are never
sampled into scaler edges.

This exposes underlying sprites correctly instead of fading the already combined
sprite plane. The extra passive ROM raster decoding and scene packing still run
on CPU when effects are enabled; this is GPU composition, not replacement of the
native hardware renderer. Soft replacement textures, new particles and lifetime
fading need additional asset/event interfaces and are not implemented here.

Explosion/flame translucency extends the same optional sprite-fragment adapter.
Explosion animation/palette records are read from main ROM $347BE, including ten
types. Player-crash streams are resolved through $E89E/$F796; fire/smoke
palettes $A2/$75/$2C are included and aircraft debris stays opaque.
The actual paired afterburner flames come from the two $29018/$2939C exhaust
phases used by $AF88/$B018, with palette $80 from $AE8C/$AEE4. The complete
flame sprites, including white cores, receive optional opacity. The F2 aircraft
and its small animated nozzle glow stay opaque. CPU and
GPU use the same explicit effect kinds, retaining tile/shadow termination and
all-opaque identity. No extra native producer or descriptor writes are introduced.

### Optional ground horizon correction

`capture_ground_horizon` uses passive copies at each native layer frontier.
`hwvideo/ground_horizon.hpp` reads horizon families from ROM $11000 and the
palette $E8, then clips misplaced plain road-0 fill against the published
strip's actual row/column silhouette. Ordered sky/terrain endpoint pens determine
the local direction. Original-width correction remains active-flight-only and
uses those local silhouettes. The wider renderer also corrects attract/takeoff
and other submitted normal-road horizons: the observed envelope and ROM normal
handle clipped/banked corners, while ground-side extension requires an observed
native-row ground/sea color. A guessed fitted plane is not a substitute for the
observed strip. Missing silhouette/color evidence remains unfilled.
Both paths require normal-road control 4 and preserve protected terrain/layers,
partial palettes, sprite ordering and shadows. Original is the default. This
is a targeted fill correction, not a replacement terrain/pitch/roll model.

## Detached widescreen presentation

`SceneClip` captures buffered road/sprite state, tile/text/palette/register data
and detached descriptors before each native partial render. `WideVisibility`
observes verified producers without timed bus reads or native writes. It widens
Sub projection, retains audited scenery against live camera data, extends actual
horizon submissions/takeoff waves and recovers native queue/depth-bucket drops.
Native request counts, hardware banks, object pools and combat stay unchanged.

`WideScene` renders those clips into width-aware background, sprite, protection
and ordered-fragment planes. Its view reports dimensions and native origin;
borrowed spans must be copied before asynchronous use. CPU/GPU composition uses
the same scene contract. Original 320×224 reconstruction remains a strict
reference; additional wide objects may overlap that center in the enhanced view.
Current frontend support is 398×224. See [the widescreen plan](WIDESCREEN_PLAN.md)
for controller/scene, aspect, platform and sustained-load gaps.

## SDL3 native input adapter

`frontend/native_input.hpp` maps keyboard scancodes, normalized SDL3 gamepad
axes/buttons and raw joystick axes/buttons/hats to the existing `GameControls`.
Frontend-owned deadzones, sensitivity, polarity, full-range throttle and Menu
bindings live in GUID/type profiles beside native settings. The gamepad database
is embedded and loaded before device opening; hotplug/remapping/focus events
stay in the SDL frontend. Digital-only gamepads use D-pad/shoulder defaults.
No timing, arcade ADC conversion, ROM logic or recording format changes are
needed. Device-specific calibration and response curves remain separate work.

Implemented feedback uses passive native semantic observations and a separate SDL
output mixer, with explicit device selection and stop/reconnect ownership.
Basic rumble/haptics targets Windows, Linux and macOS with separate physical
acceptance gates on each. Windows Xbox Series X trigger rumble and
force-feedback sticks have additional capability/physical acceptance gates.
macOS Bluetooth Xbox body and both trigger motors are physically confirmed;
Windows/Linux hardware and advanced FFB remain open.
See [HAPTICS_PLAN](HAPTICS_PLAN.md).

### Music presentation

The optional sound-board adapter exposes pre-mix music/effects projections and
ROM-observed lifecycle events through `GameSession::take_music()`. Original
`take_audio()` remains intact; the native frontend uses its exact output at unity
Original settings and applies replacement/gains to presentation stems otherwise.
File decoding and preview belong to `NativeMusic`, outside board execution. See
[MUSIC_REPLACEMENT_PLAN](MUSIC_REPLACEMENT_PLAN.md) for lifecycle and coverage.
