# Extending the translated game

The [Three.js prototype](../prototypes/threejs/README.md) exercises a deeper
rendering boundary: the native core publishes partial-clip scene inputs,
individual sprites become meshes, and Three.js composes its own render targets.
The live SDL application and original-mode checks retain their existing path.

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
