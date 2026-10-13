# Extending the translated game

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
The accepted finite MAME corpora remain regression checks. Full-credit/stage
coverage is still incomplete and is not advanced by this application API.

## Available interfaces

| Interface | Contract | Phase 5 uses |
| --- | --- | --- |
| `GameControls` | Normalized X/Y/throttle in -1..1, cannon/missile/coin/start buttons; positive Y means up | Keyboard/controller/HOTAS mapping, calibration, deadzones and response curves |
| `GameSession::advance` | Advances one physical board frame; returns false while paused | Frontend pacing, bounded runs and replay drivers |
| `set_paused`, `restart` | Pause freezes all board owners; restart creates a cold board, discards session audio and unpauses | Pause/settings UI and restart commands |
| `frame()` | Read-only completed native 320×224 RGB plus frame number and board time | GPU presentation, scaling, shaders, screenshots and overlays |
| `capture_sprite_scene`, `sprite_scene()` | Opt-in read-only per-pixel ordered fragment chains from published ROM sprite descriptors; clip-frontier palette and priority retained, reset on restart | Independent cloud/trail/explosion/flame opacity and future per-sprite materials |
| `capture_sprite_presentation`, `sprite_presentation()` | Opt-in read-only native-sized ARGB background/sprite copies and tile/shadow protection; valid after the next completed frame, invalidated on disable/restart | Sprite-only frontend filtering, with original native raster retained |
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
| Rumble, motion telemetry, near-miss bonuses | Typed semantic events/state read from verified producers, with explicit event ordering and original-mode comparisons |
| Replacement soundtrack with original effects | Verified music/effect command classification and separate audio routing; processing the final mixed PCM alone cannot separate original music |
| True widescreen and replacement sprites | Renderer/asset interfaces before native clipping and composition; stretching the completed raster cannot reveal additional world content |
| Stage selection, shuffled missions and new formations | A verified stage/mission descriptor interface, followed by an explicit enhanced mode and original-mode regression tests |
| New flight/combat rules or wingman play | Readable subsystem adapters extracted from verified player/object/collision routines; maintain native arithmetic and update order in original mode |
| Save states | Complete versioned serialization of board/device and pending CPU continuation state, plus extension state; a RAM dump is insufficient |

Introduce those interfaces at the owning subsystem as each feature is built.
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

## Individual sprite composition and optional transparency

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
rows and all effects Opaque by default. Explosions use 176/255 and player flame
pens use 192/255. These values are enhancement choices.
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

## SDL3 native input adapter

`frontend/native_input.hpp` maps keyboard scancodes, normalized SDL3 gamepad
axes/buttons and raw joystick axes/buttons/hats to the existing `GameControls`.
Frontend-owned deadzones, sensitivity, polarity, full-range throttle and Menu
bindings live in GUID/type profiles beside native settings. The gamepad database
is embedded and loaded before device opening; hotplug/remapping/focus events
stay in the SDL frontend. Digital-only gamepads use D-pad/shoulder defaults.
No timing, arcade ADC conversion, ROM logic or recording format changes are
needed. Device-specific calibration and response curves remain separate work.

### Music presentation

The optional sound-board adapter exposes pre-mix music/effects projections and
ROM-observed lifecycle events through `GameSession::take_music()`. Original
`take_audio()` remains intact; the native frontend uses its exact output at unity
Original settings and applies replacement/gains to presentation stems otherwise.
File decoding and preview belong to `NativeMusic`, outside board execution. See
[MUSIC_REPLACEMENT_PLAN](MUSIC_REPLACEMENT_PLAN.md) for lifecycle and coverage.

Explosion/flame translucency extends the same optional sprite-fragment adapter.
Explosion animation/palette records are read from main ROM $347BE, including ten
types. Player aircraft families come from $28C94 and their flame pixels from
palette F2 pens 1/2/4, identified by the original $14062 palette writer. CPU and
GPU use the same explicit effect kinds, retaining tile/shadow termination and
all-opaque identity. No extra native producer or descriptor writes are introduced.
