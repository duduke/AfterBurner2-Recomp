# Enhancement backlog

Updated **2026-10-15** against published macOS preview
[v0.1.0-preview.1](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.1).
Use the [summarized roadmap](ROADMAP.md#summarized-roadmap) for priorities and
[architecture guide](ENHANCEMENT_ARCHITECTURE.md) for implemented contracts.
Future items below are proposals, not release promises or arcade behavior.

## Delivered foundations

| Area | Current implementation | Remaining acceptance / extension |
| --- | --- | --- |
| Menus | Arcade-font initial/pause menus; Video, Controls, Audio, DIP Switches; scroll hints, mouse/controller navigation, persisted preferences | Calibration displays, service/sound test, coin ratios, high-score persistence and mode selection |
| Input | Keyboard/gamepad/raw-joystick remapping, axes/buttons/hats, deadzone/sensitivity/polarity, full-axis throttle, hotplug and community mappings | Named HOTAS/retro-controller presets, response curves and physical calibration across platforms |
| Feedback | SDL body/trigger rumble, motor tests, device selection, gains, four verified cues and lifecycle stops | See [haptics plan](HAPTICS_PLAN.md): Windows/Linux/fallback hardware, warning cues, stick forces and motion telemetry |
| Presentation | Off/EPX/Scale3x/2xSaI/Super2xSaI/ScaleFX; GPU composition with CPU fallback; independent original-art opacity | Other backends, sustained performance, new assets and richer effects |
| Ground/horizon | Targeted roll-wedge correction; wide-plane corner/clipped-horizon fill, both actual roll directions and terrain protection checked | Broader scene audit and optional continuous ground projection |
| Widescreen | 16:9 preview with wider road/tiles/sprites, detached scenery, horizon/water extension, native request/depth-drop recovery and dynamic presentation storage | Exhaustive scene/performance/platform acceptance, 16:10/ultrawide and optional HUD anchoring; [plan](WIDESCREEN_PLAN.md) |
| Music | Six WAV/FLAC replacements, preview/fallback, separate gains and live active-song changes; user-provided CD soundtrack files supported | Four remaining natural song contexts, natural fades and broader listening checks; [plan](MUSIC_REPLACEMENT_PLAN.md) |
| Spatial output | OpenAL Soft chip-rate interpolation, headphone HRTF, 2.1/5.1 modes and direct stereo music | Voice/cabinet audit, physical channel/localization tests and long-session latency; [plan](SPATIAL_AUDIO_PLAN.md) |
| Distribution | ROM-free Apple Silicon/macOS 26+ ZIP, bundled libraries/sources/notices and checksum | Broader OS/architecture packages and signing/notarization |

The release passed the complete `required` and supplemental `full` gates.
Implementation and finite validation do not imply exhaustive visual, hardware
or whole-credit fidelity acceptance. Original gameplay and native object pools
remain unchanged.

## Menus, controls and convenience

| Proposed increment | Dependency / boundary |
| --- | --- |
| Additional persisted options and calibration visualizers | Existing menu/preferences hooks; add concrete options as their owning feature is implemented |
| Service/sound-test access, coin ratios and high-score persistence | Verified cabinet/DIP/NVRAM ownership; do not treat existing lives/difficulty/Ship Increase menus as unfinished |
| Controller/HOTAS presets | Test named devices and transports; generic mappings do not establish calibration for Thrustmaster, Logitech, Turtle Beach, VKB or retro USB controllers |
| Advanced force feedback | Capability/axis/effect limits, conservative spring/damper/recoil and physical acceptance; trigger vibration is not adaptive resistance |
| Moving-cabinet / rig integration | Verified cabinet commands/feedback before external telemetry or vendor/SimHub/UDP integration; separate from basic rumble |
| Save states | Versioned CPU/device/pending-continuation, persistent and extension state; deterministic resume, not RAM-only snapshots |
| Cannonball ecosystem adaptations | Evaluate concrete QoL ideas from Cannonball, Cannonball-SE and Cannonball DX individually, with compatible licensing |

## Display and assets

| Proposed increment | Dependency / boundary |
| --- | --- |
| CRT shaders and pacing improvements | Presentation backend, scanlines/mask/glow/curvature options and measured frame pacing |
| Sprite extraction CLI | Reproducible ROM frame/asset identities and PNG export; no extraction option is implemented yet |
| Replacement sprites, larger textures and RGB/RGBA assets | Runtime mapping from original descriptors/LODs to user-provided assets; preserve pivots, priority, shadows and Original fallback |
| Soft clouds, fading trails and additional visual particles | Existing ordered fragment interface plus RGBA assets and lifetime events; implemented original-art translucency is already separate |
| High-resolution HUD / 4K presentation | Semantic text/instrument ownership, user-provided art and high-DPI/layout tests; current window/integer scaling is already available |
| Smoother sprite rotation | Stable pivots, interpolation or additional art/banks without changing native roll logic |
| Additional scalers | License compatibility and reference comparisons; xBRZ/ScaleHQ remain proposals |
| 120/144 Hz presentation interpolation | Scene/state history and pacing independent of the original simulation; evaluate before changing gameplay cadence |
| 16:10, ultrawide and optional pixel-aspect correction | Explicit width/aspect policy and independent scene, UI, GPU and performance acceptance |
| Continuous ground/horizon projection | ROM-derived bank/pitch/altitude and layer audit; preserve the completed targeted correction and Original terrain behavior |

Dynamic presentation storage already addresses native request/depth bottlenecks.
It does not create new gameplay objects. Denser decorative scenery/particles
belong in the enhanced presentation; expanded combat object counts require
separate allocation, behavior and balance work.

## Gameplay and content

These features need verified subsystem interfaces and a distinct enhanced mode.
They must preserve original arithmetic, update order and MAME regressions when
disabled. Difficulty estimates describe implementation/validation effort only.

| Proposal | Difficulty | Prerequisites / retained ideas |
| --- | --- | --- |
| Mission/stage selection and Time Attack | Hard | Verified stage initialization, lifecycle and timing; current debug stage entry is not a player-facing mission mode |
| Missile near-miss scoring and sound cues | Hard–very hard | Verified trajectories/collision events, explicit bonus/combo policy and spatial whoosh cues |
| Expanded gameplay object counts and basic arranged mode | Very hard | Allocation/ownership, spawn sequencing, modifiers and balance; seeded/shuffled runs, daily seeds and palette/environment choices |
| Optional 60 Hz gameplay updates | Very hard | Adapt movement, acceleration, timers, firing, spawning, rounding and collision order; preserve original logic/display cadence as default |
| Procedural environments and formations | Very hard | Mission/art/behavior interfaces: night/searchlight/flak, sea-skimming naval assault, urban trenches, severe weather, varied entry angles/hazards and optional run mutators |
| Special stages | Very hard | New content and encounter systems: airborne fortress, ICBM interception, fleet flagship, megacity dash and typhoon interception |
| Interactive landing/refueling | Very hard | Verified transitions plus explicit optional pitch/bank/throttle docking or touch-and-go rules; current arcade sequences remain intact |
| Local two-player wingman mode | Very hard | Second player, camera/dual HUD, targeting, collisions, scoring, liveries, competitive statistics and optional friendly fire |
| Networked co-op / rollback | Highest | Local multiplayer plus deterministic snapshots, synchronization, prediction and recovery |

Custom stages are **not required** for wider rendering of the original stages.
They would allow formations, scenery and full-width combat designed for the new
space, after the stage/object interfaces and enhanced rules exist.

## Delivery order and validation

First finish the acceptance gaps in the implemented features. Smaller follow-ups
include concrete menu additions, hardware presets and measured CRT/pacing work.
Asset extraction precedes replacement assets; verified stage/event interfaces
precede new gameplay; deterministic serialization precedes rollback/netplay.
These dependencies replace an outdated easiest-to-hardest list that mixed
already implemented rumble/widescreen work with future features.

Follow [ROADMAP's validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first).
New ports/probes need independent MAME fixtures and rejected mutations. Preserve
all existing fixtures, controls and Original-mode coverage. Store ROM-derived
exports/captures and personal assets only in ignored local directories.
