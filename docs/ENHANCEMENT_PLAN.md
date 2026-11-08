# Enhancement backlog

Difficulty ratings assessed **2026-10-17**; feature status updated **2026-10-31**.
The published release is
[macOS v0.1.0-preview.5](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.5)
with an experimental Windows x64 Preview 5 refresh and older downloads retained;
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
| Menus | Arcade-font initial/pause menus; direct Arcade/Practice/Shuffle startup, grouped Display/Graphics/CRT/Controls/Audio/Arcade Settings; live bound-input calibration display, scroll hints, mouse/controller navigation, persisted preferences | Service/sound test, coin ratios, high-score persistence and additional modes |
| Input | Keyboard/gamepad/raw-joystick remapping, axes/buttons/hats, deadzone/sensitivity/polarity, full-axis throttle, hotplug and community mappings | Named HOTAS/retro-controller presets, response curves and physical calibration across platforms |
| Feedback | SDL body/trigger rumble, motor tests, device selection, gains, four verified cues and lifecycle stops | See [haptics plan](HAPTICS_PLAN.md): Windows/Linux/fallback hardware, warning cues, stick forces and motion telemetry |
| Presentation | Off/EPX/Scale3x/2xSaI/Super2xSaI/ScaleFX; GPU composition with CPU fallback; independent original-art opacity; Off/Subtle/Arcade CRT and optional macOS Metal / Windows Vulkan RetroArch `.slangp` presets with an external runtime | Other shader backends, HDR, sustained performance and new assets |
| Ground/horizon | Targeted roll-wedge correction; wide-plane corner/clipped-horizon fill, both actual roll directions and terrain protection checked | Broader scene audit and optional continuous ground projection |
| Widescreen | 16:9/16:10 previews and local-source 21:9 with wider road/tiles/sprites, detached scenery, horizon/water extension, native request/depth-drop recovery and dynamic presentation storage | Broader scene/performance/platform and physical ultrawide acceptance and optional HUD anchoring; [plan](WIDESCREEN_PLAN.md) |
| Music | Six WAV/FLAC replacements, preview/fallback, separate gains and live active-song changes; all six native song contexts/fades observed | Replacement-file natural fade and listening checks; [plan](MUSIC_REPLACEMENT_PLAN.md) |
| Spatial output | OpenAL Soft chip-rate interpolation, headphone HRTF, 2.1/5.1 modes, direct stereo music, reviewed routing, stall recovery and original Deluxe stereo wiring audit | Revised-cue listening, physical surround/unplug, other platforms and end-to-end latency; ABII cabinet-revision evidence; [plan](SPATIAL_AUDIO_PLAN.md) |
| Distribution | ROM-free Apple Silicon/macOS 26+ Preview 5 and experimental Windows x64 Preview 5 ZIPs, bundled libraries/sources/notices and checksums | Windows performance/audio stability, broader OS/architecture packages and signing/notarization |

The 2026-10-22 macOS checkpoint passed the complete `required` and supplemental
`full` gates, including the local spatial-audio and HUD changes. The later CRT
addition has separate local validation; see the
[current roadmap](ROADMAP.md#next-acceptance-tasks).
The integrated Preview 3 source subsequently passed a fresh complete
`required` release gate on 2026-10-23; see
[the release record](ENGINE_NOTES.md#preview-3-release-2026-10-23).
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
| CRT display and pacing improvements | 2–3/5 remaining | Local [CRT display](CRT_DISPLAY.md) includes built-in scanlines/RGB mask and optional macOS Metal / Windows Vulkan RetroArch `.slangp` presets for richer effects; broader shader backend/load acceptance, HDR, parameter UI and general pacing improvements remain |
| Sprite extraction CLI | Implemented offline | [Indexed PNG export and JSON mapping](SPRITE_EXTRACTION.md): 2,931 structural record candidates, stable Sub ROM identities and renderer differential/mutations; semantic labels, palette assignment and general runtime catalog mapping remain open |
| Replacement sprites, larger textures and RGB/RGBA assets | 4/5 remaining | Local [registered HD player-aircraft pack](SPRITE_REPLACEMENT.md) maps neutral/gentle-bank poses and ROM LODs to transparent output-resolution art with explicit bounds/pivots and native ownership; full animation sets, production registration/LOD art and effect-palette support remain |
| Soft clouds, fading trails and additional visual particles | 3/5 | Existing ordered fragment interface plus RGBA assets and lifetime events; validate ordering and dense-scene cost; implemented original-art translucency is already separate |
| High-resolution HUD / 4K presentation | 3–4/5 | Local text-plane preview: Original / Sharp 3x / user indexed atlas, native palette/priority/frontiers and high-DPI output; semantic sprite instruments, production art and sustained/platform acceptance remain open; [scope and usage](HUD_PRESENTATION.md) |
| Smoother sprite rotation | 3/5 | Stable pivots, interpolation or additional art/banks without changing native roll logic; preserve alignment and priority across rotation |
| Additional scalers | 2–3/5 | CPU integration can reuse existing filter interfaces; GPU versions and border/readback comparisons add work; license compatibility required; xBRZ/ScaleHQ remain proposals |
| 120/144 Hz presentation interpolation | 4/5 | Scene/state history, object correspondence across spawn/despawn and pacing independent of the original simulation; evaluate before changing gameplay cadence |
| Additional aspect acceptance, ultrawide and optional pixel-aspect correction | 2–3/5 | 16:9/16:10 ship in Preview 5; 21:9 is available in source and needs broader acceptance; ultrawide increases controller/culling and load coverage; explicit width/aspect policy and independent scene, UI, GPU and performance acceptance |
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
| Mission/stage selection and Time Attack | 3/5 remaining | Local [Stage Select / Practice](PRACTICE_MODE.md) adds a fresh directed session, retry and arcade return; full entry-state fidelity, stage looping and Time Attack remain |
| Missile near-miss scoring and sound cues | 3–4/5 | Verified trajectories/collision events, explicit bonus/combo policy and spatial whoosh cues |
| Expanded gameplay object counts and further arranged rules | 4/5 remaining | Local [Shuffle](SHUFFLE_MODE.md) provides continuous seeded stage permutations; expanded allocation/ownership, spawn sequencing, modifiers, balance, daily seeds and palette/environment choices remain |
| Optional 60 Hz gameplay updates | 4/5 | Adapt movement, acceleration, timers, firing, spawning, rounding and collision order; preserve original logic/display cadence as default |
| Procedural environments and formations | 4/5 | Mission/art/behavior interfaces: night/searchlight/flak, sea-skimming naval assault, urban trenches, severe weather, varied entry angles/hazards and optional run mutators |
| Special stages | 4/5 | New content and encounter systems: airborne fortress, ICBM interception, fleet flagship, megacity dash and typhoon interception; see [Custom stages prototype](CUSTOM_STAGES.md) |
| Interactive landing/refueling | 4/5 | Verified transitions plus explicit optional pitch/bank/throttle docking or touch-and-go rules; current arcade sequences remain intact; see [Custom stages prototype](CUSTOM_STAGES.md) |
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
