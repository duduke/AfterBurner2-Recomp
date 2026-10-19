# Enhancement backlog

Difficulty ratings assessed **2026-10-17**; feature status updated **2026-10-19**.
The published release is
[v0.1.0-preview.2](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.2);
see the roadmap for platform results and outstanding validation.
Use the [summarized roadmap](ROADMAP.md#summarized-roadmap) for priorities and
[architecture guide](ENHANCEMENT_ARCHITECTURE.md) for implemented contracts.
Future items below are proposals, not release promises or arcade behavior.

## Assessed difficulty scale

Ratings estimate the remaining implementation and validation work from the
current code, including ROM research, integration, Original-mode fidelity and
physical hardware checks where applicable. They are engineering assessments,
not measured effort, delivery dates or priority rankings. Existing dependencies
are assumed available only where already implemented; unfinished prerequisites
are included in the assessment.

| Rating | Level | Typical scope |
| --- | --- | --- |
| 1/5 | Easy | Small, isolated change using an existing interface and focused checks |
| 2/5 | Moderate | Bounded feature using established interfaces, with integration or device checks |
| 3/5 | Hard | New subsystem interface, ROM research or substantial scene/platform validation |
| 4/5 | Very hard | Several coupled systems, broad fidelity work, content tooling or hardware integration |
| 5/5 | Highest | Foundational architecture changes combining multiplayer, deterministic state and recovery |

Ranges reflect grouped proposals. Reassess when scope and platforms are chosen;
ratings apply to future work, not delivered foundations.

## Delivered foundations

| Area | Current implementation | Remaining acceptance / extension |
| --- | --- | --- |
| Menus | Arcade-font initial/pause menus; Video, Controls, Audio, DIP Switches; live bound-input calibration display, scroll hints, mouse/controller navigation, persisted preferences | Service/sound test, coin ratios, high-score persistence and mode selection |
| Input | Keyboard/gamepad/raw-joystick remapping, axes/buttons/hats, deadzone/sensitivity/polarity, full-axis throttle, hotplug and community mappings | Named HOTAS/retro-controller presets, response curves and physical calibration across platforms |
| Feedback | SDL body/trigger rumble, motor tests, device selection, gains, four verified cues and lifecycle stops | See [haptics plan](HAPTICS_PLAN.md): Windows/Linux/fallback hardware, warning cues, stick forces and motion telemetry |
| Presentation | Off/EPX/Scale3x/2xSaI/Super2xSaI/ScaleFX; GPU composition with CPU fallback; independent original-art opacity | Other backends, sustained performance, new assets and richer effects |
| Ground/horizon | Targeted roll-wedge correction; wide-plane corner/clipped-horizon fill, both actual roll directions and terrain protection checked | Broader scene audit and optional continuous ground projection |
| Widescreen | 16:9 preview and local 16:10 source addition with wider road/tiles/sprites, detached scenery, horizon/water extension, native request/depth-drop recovery and dynamic presentation storage | Broader scene/performance/platform acceptance for both widths, ultrawide and optional HUD anchoring; [plan](WIDESCREEN_PLAN.md) |
| Music | Six WAV/FLAC replacements, preview/fallback, separate gains and live active-song changes; user-provided CD soundtrack files supported | Four remaining natural song contexts, natural fades and broader listening checks; [plan](MUSIC_REPLACEMENT_PLAN.md) |
| Spatial output | OpenAL Soft chip-rate interpolation, headphone HRTF, 2.1/5.1 modes and direct stereo music | Voice/cabinet audit, physical channel/localization tests and long-session latency; [plan](SPATIAL_AUDIO_PLAN.md) |
| Distribution | ROM-free Apple Silicon/macOS 26+ and Windows x64 ZIPs, bundled libraries/sources/notices and checksums | macOS package refresh, broader OS/architecture packages and signing/notarization |

The integrated macOS source passed the complete `required` and supplemental
`full` gates on 2026-10-19 after the cross-platform optimizations and local
16:10/calibration additions; see the [current roadmap](ROADMAP.md#next-acceptance-tasks).
Implementation and finite validation do not imply exhaustive visual, hardware
or whole-credit fidelity acceptance. Original gameplay and native object pools
remain unchanged.

## Menus, controls and convenience

| Proposed increment | Assessed difficulty | Dependency / boundary |
| --- | --- | --- |
| Additional persisted options and device-specific calibration aids | 1–2/5 | Existing menu/preferences hooks and live bound-input display; add concrete options as their owning feature is implemented; physical device acceptance remains separate |
| Service/sound-test access, coin ratios and high-score persistence | 3/5 | Verified cabinet/DIP/NVRAM ownership and lifecycle/persistence checks; do not treat existing lives/difficulty/Ship Increase menus as unfinished |
| Controller/HOTAS presets | 2–3/5 | Preset data uses existing remapping; test named devices and transports; generic mappings do not establish calibration for Thrustmaster, Logitech, Turtle Beach, VKB or retro USB controllers |
| Advanced force feedback | 4/5 | Capability/axis/effect limits, conservative spring/damper/recoil and physical acceptance; trigger vibration is not adaptive resistance |
| Moving-cabinet / rig integration | 4/5 | Verified cabinet commands/feedback before external telemetry or vendor/SimHub/UDP integration, plus physical rig validation; separate from basic rumble |
| Save states | 4/5 | Versioned CPU/device/pending-continuation, persistent and extension state; deterministic resume, not RAM-only snapshots |
| Cannonball ecosystem adaptations | 2–4/5 | Scope-dependent placeholder: evaluate concrete QoL ideas from Cannonball, Cannonball-SE and Cannonball DX individually, with compatible licensing; rate each selected adaptation separately |

## Display and assets

| Proposed increment | Assessed difficulty | Dependency / boundary |
| --- | --- | --- |
| CRT shaders and pacing improvements | 2–3/5 | Basic postprocessing uses the presentation backend; scanlines/mask/glow/curvature options and measured frame pacing need backend/load acceptance |
| Sprite extraction CLI | 2/5 | Existing read-only decoding is a foundation; add reproducible ROM frame/asset identities and PNG export; no extraction option is implemented yet |
| Replacement sprites, larger textures and RGB/RGBA assets | 4/5 | Runtime mapping from original descriptors/LODs to user-provided assets; preserve pivots, priority, shadows and Original fallback; includes asset interfaces, not production of a complete art set |
| Soft clouds, fading trails and additional visual particles | 3/5 | Existing ordered fragment interface plus RGBA assets and lifetime events; validate ordering and dense-scene cost; implemented original-art translucency is already separate |
| High-resolution HUD / 4K presentation | 3–4/5 | Semantic text/instrument ownership, user-provided art and high-DPI/layout tests; current window/integer scaling is already available |
| Smoother sprite rotation | 3/5 | Stable pivots, interpolation or additional art/banks without changing native roll logic; preserve alignment and priority across rotation |
| Additional scalers | 2–3/5 | CPU integration can reuse existing filter interfaces; GPU versions and border/readback comparisons add work; license compatibility required; xBRZ/ScaleHQ remain proposals |
| 120/144 Hz presentation interpolation | 4/5 | Scene/state history, object correspondence across spawn/despawn and pacing independent of the original simulation; evaluate before changing gameplay cadence |
| Additional aspect acceptance, ultrawide and optional pixel-aspect correction | 2–3/5 | 16:10 is now in local source and needs broader acceptance; ultrawide increases controller/culling and load coverage; explicit width/aspect policy and independent scene, UI, GPU and performance acceptance |
| Continuous ground/horizon projection | 4/5 | ROM-derived bank/pitch/altitude and layer audit across rolls/inversion; preserve the completed targeted correction and Original terrain behavior |

Dynamic presentation storage already addresses native request/depth bottlenecks.
It does not create new gameplay objects. Denser decorative scenery/particles
belong in the enhanced presentation; expanded combat object counts require
separate allocation, behavior and balance work.

## Gameplay and content

These features need verified subsystem interfaces and a distinct enhanced mode.
They must preserve original arithmetic, update order and MAME regressions when
disabled. Ratings use the same implementation/validation scale as the other
backlog sections; new encounter systems also require content and balance work.

| Proposal | Assessed difficulty | Prerequisites / retained ideas |
| --- | --- | --- |
| Mission/stage selection and Time Attack | 3/5 | Verified stage initialization, lifecycle and timing; current debug stage entry is not a player-facing mission mode |
| Missile near-miss scoring and sound cues | 3–4/5 | Verified trajectories/collision events, explicit bonus/combo policy and spatial whoosh cues |
| Expanded gameplay object counts and basic arranged mode | 4/5 | Allocation/ownership, spawn sequencing, modifiers and balance; seeded/shuffled runs, daily seeds and palette/environment choices |
| Optional 60 Hz gameplay updates | 4/5 | Adapt movement, acceleration, timers, firing, spawning, rounding and collision order; preserve original logic/display cadence as default |
| Procedural environments and formations | 4/5 | Mission/art/behavior interfaces: night/searchlight/flak, sea-skimming naval assault, urban trenches, severe weather, varied entry angles/hazards and optional run mutators |
| Special stages | 4/5 | New content and encounter systems: airborne fortress, ICBM interception, fleet flagship, megacity dash and typhoon interception |
| Interactive landing/refueling | 4/5 | Verified transitions plus explicit optional pitch/bank/throttle docking or touch-and-go rules; current arcade sequences remain intact |
| Local two-player wingman mode | 4/5 | Second player, camera/dual HUD, targeting, collisions, scoring, liveries, competitive statistics and optional friendly fire |
| Networked co-op / rollback | 5/5 | Local multiplayer plus deterministic snapshots, synchronization, prediction and recovery |

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
