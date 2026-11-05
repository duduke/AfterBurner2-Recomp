# SDL rumble and force feedback

**Status: basic SDL rumble shipped in the macOS preview; advanced FFB planned.**
Updated **2026-11-05** for local source; packaged previews require rebuilding.
The shared adapter supports Windows, Linux and macOS through SDL 3.4.
Capabilities and API results determine which outputs are available.

## Use

**Settings → Controls → Feedback** offers:

- Enabled (default Off) and explicit output-device selection.
- Independent body/trigger strengths, seven gameplay effect levels (On / Low → On / Med → On / High → Off) and trigger swap.
- Capability status, four finite motor tests and Stop All.

Tests work with gameplay feedback Off and last 650 ms. They stop on leaving
their row/menu or losing focus. Preferences use `AB2HAPTICS3` (versions 1/2 migrate automatically; new sequence levels start at High) in
`settings.cfg.haptics`; `--settings-file` changes the base path. GUID/type
identifies the saved target; live instance ID routes output. Identical devices
require explicit selection after an ambiguous reconnect. Editing an input
profile does not change the feedback target.

## Development validation

Haptic tuning, motor routing and read-only cue observers use the main profile,
affected motor/mixer/lifecycle checks and a relevant build/smoke. For a new or
changed native cue, verify the affected sequence independently in MAME, reject
relevant mutations and compare native RGB/PCM/time with capture enabled/disabled.
Exercise the reported route (for example Practice 22 → 23); a broad passing suite
cannot substitute for reaching that cue. Physical feel remains a separate check.

Changing a passive hook in GameSession or an engine header does not by itself
require the full engine gate. CPU, scheduling, emulated device/bus or native game
logic changes still do. See [the authoritative cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first).

## Motor mapping

On Xbox controllers, the left body motor provides low-frequency, heavier rumble;
the right body motor provides high-frequency, finer vibration. Default assignments:

| Effect | Left body motor (low frequency) | Right body motor (high frequency) | Left trigger | Right trigger |
| --- | --- | --- | --- | --- |
| Cannon shot | Light recoil | Strong recoil | — | Recoil pulse |
| Missile launch | Strong on left launches | Strong on right launches | Left launch pulse | Right launch pulse |
| Player impact/damage | Strong impact | Strong impact | Impact pulse | Impact pulse |
| Full-throttle afterburner | Gentle vibration | Very light vibration | — | — |
| Landing | Approach, wheel impact/rebound, speed-scaled rollout | Uneven runway texture | Wheel-impact accent | Wheel-impact accent |
| Takeoff | Spool, launch kick, rising thrust, airborne fade | Deck/runway texture | Launch accent | Launch accent |
| Refueling | Coupling jolt, individual reload bumps | Coupling/reload accents | Coupling/reload accents | Coupling/reload accents |

**Swap Triggers** reverses the trigger assignments, including directional missiles. Body and
trigger strength settings independently scale these effects. Outputs require
device support; simple-haptic fallback combines the two body channels.

## Verified gameplay cues

`passive native observations → wall-clock mixer → SDL output`

| Cue | Original-ROM observation | Output |
| --- | --- | --- |
| Cannon | `$9EF6` commits Vulcan handler `$A030` after allocation | High body / right trigger |
| Missile | `$9EBC` commits `$9F7C`; successful-launch count `$29C0B8 + 1` predicts the side selected at `$9FC2` | Corresponding body / trigger side |
| Player impact | `$9C20` commits `$E662`; tracked player handler `$E8D0` states 0–4 or `$E94E` state 0 | Sustained both body / both triggers |
| Afterburner | Flight selector `$29C09C == $10`, signed processed throttle `$29C0D2 >= $FD` | Gentle body rumble |

Weapon, impact and afterburner cues require active flight. Landing, takeoff
and refueling use their verified carrier/airbase/nozzle states. The stage-23
carrier finale has its own touchdown flag and continues rollout feedback into
ending modes 17/18, then stops when the aircraft settles. Attract remains
suppressed. Damage sustains through visible intact falling/rolling
and breakup animation, stopping at the hidden decay states. Holding fire without a successful allocation,
attract activity and enemy explosions produce no player recoil/impact. Warning
cues remain disabled pending independent ROM verification. No inputs, bus
transactions, RAM writes, CPU scheduling or original RGB/PCM are changed.
Feedback works independently of speaker availability and music replacement.

`engine/feedback_cues.hpp` emits timestamped events with sequence IDs and a
continuous-state snapshot once per completed board frame. Restart clears the
capture. `frontend/native_haptics.hpp` consumes events once, drops one-shots
older than 100 ms of board time, and combines envelopes using the maximum per
motor before applying gains. Cannon/missile/initial-impact envelopes last 65/180/350 ms. Visible damage
additionally holds continuous impact, refreshed by each board frame. High impact
uses 1.5 body / 1.2 trigger gain before master scaling and saturation: default
50% masters produce 75% body / 60% trigger output, versus the former initial
45%/37.5% body and 22.5% triggers. Per-effect Low/Med/High scales are
35%/65%/100%; Off is zero. Existing body/trigger master gains still apply.
These strengths and envelopes are enhancement tuning, not arcade physics.

Landing, Takeoff and Refueling are independent options under Cues. They use
separate approach/contact/rollout, spool/acceleration/climb and coupling/reload
layers. Wheel contact and nozzle capture produce finite impulses; refill bumps
follow each actual `$9B` sound-command delivery, with no invented periodic timer.
Refill pulses compensate the reported software playback queue so they do not
lead buffered beeps; unavailable audio keeps them immediate. Separate pending
pulses preserve the cadence, and pause/menu/focus stops discard them.
See [sequence observations and validation](ENGINE_NOTES.md#layered-landing-takeoff-and-refueling-feedback-2026-11-05).

Output refreshes at most every 20 ms, with finite 80 ms SDL watchdogs. Missing
fresh board frames stop continuous feedback after 100 ms. Pause, menu entry,
focus loss, disable, restart, device changes, unplug and shutdown explicitly
stop both motor pairs. Reconnect neither reroutes to another controller nor
replays stale cues. A failed pair is suppressed until reconnect/remap or
explicit retest; working output remains available.

## Devices and ownership

The adapter borrows `NativeInput` gamepad/joystick handles and stops/releases
output before input closes them. It owns any simple-haptic fallback handle;
all SDL output/haptic work stays on the main thread.

Use capability properties from `SDL_GetGamepadProperties` or
`SDL_GetJoystickProperties`. Missing capabilities mean unavailable. Preferred
outputs are `SDL_RumbleGamepad`/`SDL_RumbleGamepadTriggers` or their raw-joystick
counterparts. Each call replaces its motor pair, so the mixer issues one combined
command. Unsupported triggers leave the existing body mix unchanged.

For a raw joystick without body rumble, the optional haptic subsystem tries
`SDL_OpenHapticFromJoystick`, `SDL_HapticRumbleSupported`,
`SDL_InitHapticRumble` and finite `SDL_PlayHapticRumble`. Simple haptics combines
the body channels into one strength. Never drive a device through multiple body
output paths. Unsupported output leaves normal controls usable.

Official references: [body rumble](https://wiki.libsdl.org/SDL3/SDL_RumbleGamepad),
[trigger rumble](https://wiki.libsdl.org/SDL3/SDL_RumbleGamepadTriggers),
[capabilities](https://wiki.libsdl.org/SDL3/SDL_GetGamepadProperties),
[SDL haptics](https://wiki.libsdl.org/SDL3/CategoryHaptic).

## Acceptance

| Platform | Current evidence | Remaining hardware checks |
| --- | --- | --- |
| macOS | Xbox Bluetooth body and both trigger motors physically confirmed | USB, other devices, simple-haptic fallback, full gameplay/lifecycle feel |
| Windows | Shared SDL adapter and automated virtual-device checks | Series X body and both triggers; USB, Bluetooth/adapter; gameplay/lifecycle |
| Linux | Shared SDL adapter and automated virtual-device checks | Physical body/simple haptics, permissions/backends, gameplay/lifecycle |

On 2026-10-13, SDL **3.4.16** identified the user's Bluetooth Xbox controller as
**Xbox One S Controller** and reported both output capabilities. The user
confirmed body vibration and both triggers physically. A controller-only test
was accepted by SDL but produced no felt output; a diagnostic with the SDL video
window/event loop worked. The native game uses that event loop. The host reports macOS 26.7.1; controller firmware and output latency were not
recorded.

Build/automated/lifecycle checks and at least one physical basic-output test are
required per OS. Another OS's result does not close that gate. Record device,
firmware, OS/SDL version, transport, capability/API results and physical feel;
include distribution/kernel on Linux. Test USB/Bluetooth independently.

Windows Series X acceptance requires all four motors. If trigger output is
missing, inspect SDL HIDAPI/XInput and optional GameInput while preserving user
hints and working drivers. GameInput needs a compatible build/runtime; set its
hint before initialization. Do not assume controller names imply capabilities.
References: [SDL Xbox HIDAPI driver](https://github.com/libsdl-org/SDL/blob/release-3.4.16/src/joystick/hidapi/SDL_hidapi_xboxone.c),
[GameInput hint](https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_GAMEINPUT).

Automated coverage includes SDL virtual motor callbacks, overlap/gains,
unsupported output, failures, stale/duplicate suppression, finite tests,
focus/pause/restart, device switching/hotplug and persisted preferences.
Independent MAME witnesses cover actual allocations, initialized missile sides,
visible damage states, player hits, full throttle,
attract suppression and unrelated handler writes. Native enabled/disabled
comparisons require identical RGB/PCM/time. Retain all fixtures and mutations;
virtual callbacks prove calls, not physical feel. See
[WORKFLOW](WORKFLOW.md#controller-feedback) and the
[ROADMAP validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first).

## Current validation checkpoint

The integrated macOS source passed complete `required` and supplemental `full`
on 2026-10-19. Feedback integration preserves 4,200 native RGB/PCM/time frames;
independent MAME witnesses and all eleven feedback controls pass. See the
[gate record](ENGINE_NOTES.md#integrated-macos-acceptance-gate-2026-10-19).
These automated results were obtained on macOS; the shared adapter and virtual
callbacks do not establish Windows/Linux build or physical-output acceptance.

Next: record named Windows/Linux devices and transports, simple-haptic fallback,
and actual gameplay/lifecycle feel. Keep warning cues disabled until their ROM
producer is independently verified; add advanced stick effects separately.

## Remaining force-feedback work

Advanced sticks require capability/axis/effect-limit detection and real hardware
acceptance. Start with conservative condition spring/damper effects, then
periodic recoil and bounded impulses. A spring centers; constant force pushes.
Verify signs at low strength, avoid competing autocenter/application springs,
restore changed state where possible, and keep finite watchdogs/effect limits.
Unsupported advanced effects must not disable basic rumble.

Speed resistance may follow independently verified speed telemetry. Defer
invented stall, turbulence and G-load physics. Trigger vibration is not adaptive
resistance. Moving cabinets, motion-rig telemetry and vendor SDKs are separate
work. Claim advanced FFB only after physical acceptance on a named OS/transport.
