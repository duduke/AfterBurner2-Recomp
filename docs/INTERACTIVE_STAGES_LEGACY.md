# Retained legacy prototype

Superseded by [custom stages](INTERACTIVE_STAGES.md). Build and run this version
as `build/interactive-stages-legacy`; commands below describe its historical interface.

# Interactive refueling and landing prototype

This first increment is a **standalone native practice challenge**, selected
explicitly by the user. It automatically uses the original aircraft, tanker
and carrier sprites from an available local ROM set. Without a local set it
runs with authored SDL geometry; `--no-sprites` explicitly selects that
ROM-free presentation. Both use the same experimental approach rules. With
ROMs, successful touchdown continues into an isolated native board session's
landing/service/departure animation. The main game and its normal-run stage
transitions are unchanged. No whole-game fidelity or automatic normal-run
integration claim is made. Prototype audio playback is not implemented.

Build and launch from the repository root:

```sh
python3 tools/workflow.py run build/interactive-stages-legacy
./build/interactive-stages-legacy
./build/interactive-stages-legacy --stage landing
./build/interactive-stages-legacy --stage runway
./build/interactive-stages-legacy --stage runway-jungle
# Optional explicit extracted directory or ZIP:
./build/interactive-stages-legacy --roms roms/aburner2.zip
# Geometric, ROM-free presentation:
./build/interactive-stages-legacy --no-sprites
```

`make build/interactive-stages-legacy` builds this retained version. Launch
it directly to start at a briefing and wait for Enter/Start.
Tab/Y cycles refueling, carrier, autumn runway and jungle runway. Runway
continuations use the original Stage 5 and Stage 13 sequences respectively.

| Action | Keyboard | Gamepad |
| --- | --- | --- |
| Pitch / bank | Down raises the nose; Up lowers it; Left/Right bank | Pull back raises the nose; push forward lowers it; left/right bank |
| Boost / brake | Space / Left Shift | Right / left trigger or mapped shoulders |
| Begin / retry result | Enter (retry returns to briefing) | Start or south button |
| Retry immediately | R or F5 (returns to briefing) | Start on result |
| Switch challenge | Tab | North button / Y |
| Pause / resume | P; Enter also resumes | Start; south button also resumes |
| Exit | Escape | Back |

The prototype reuses the native input adapter's default device mappings and
embedded controller database. Bindings/calibration are not loaded from the
main application's settings. Pitch is reversed at the prototype input boundary;
the model retains positive nose-up pitch and the main game's mapping is unchanged.
Focus loss pauses automatically. Reacquiring focus does not
resume automatically. Retry and switching clear all challenge state.

## Original sprite presentation

Run from the repository root to discover extracted `roms/` or
`roms/aburner2.zip`, or pass `--roms` explicitly. The existing loader checks
the user's ROM set. An invalid detected/explicit set reports an error; use
`--no-sprites` to select geometric presentation. The ROM loader currently
loads the full set for native continuation as well as sprite presentation.
No ROM data, extracted pictures or captures are packaged
or checked in.

`InteractiveStageSprites` uses the unchanged `HWSprites` decoder and
`HWPalette` DAC to create cached, transparent, nearest-filtered SDL textures.
The player neutral/gentle-bank roots come from the main ROM's `$28C94`
pitch/bank tables, with player colors initialized by `$13F42`. Carrier hull
`$19C70`/palette `$6F` comes from `$108AC`. Tanker fuselage `$1D770`/palette
`$46` and mirrored wings `$1D780`/palette `$70` come from `$16892..$16910`.
Their static palettes use the ROM's `$13B1C/$2EDF4` initialization source.
The sprite pixels and colors are original game data. The left wing is mirrored
as `$1690C` requests; the right is not. `$167CE..$16846` places their centers
approximately 130 source pixels either side of the fuselage and 20 below it
at unit zoom. Earlier prototype spacing (90/5) and opposite flips were wrong.
The reticle, hose guide and HUD remain prototype graphics. With ROMs, the
wireframe grid and geometric runway are replaced during play by the original
sky, road, sea, trees, buildings, vehicles and aircraft presentation.

`InteractiveSceneCapture` records native approach scenery and an independent
transparent aircraft plane at the real partial-render frontiers. It renders
copies of the published sprite list using a separate `HWSprites` instance;
native RAM, scaler feedback, CPU timing and RGB are unchanged. Player poses
come from both flight `$28C94` and refuel `$16B9E` tables and their Sub-ROM LODs.
The native tanker/hose are removed from the refueling backdrop so the
interactive assembly is not duplicated. Original accompanying aircraft remain.

Up to 256 native approach snapshots are kept in memory. Distance selects the
scene; adjacent backgrounds blend and the native aircraft bounds interpolate
for smooth motion. Steering offsets and bank rotation remain authored practice
presentation. Landing approach, rollout and takeoff all use the same viewport,
palette sources, pixel scale and native poses. At touchdown, a short fade joins
the final approach pose to native continuation rather than switching from
geometric scenery to a different camera presentation. Native HUD bands are
cropped to leave the prototype's arranged missile/status display in control.
The ROM-free option retains the geometric approach.

## Landing, rearming and takeoff continuation

Touchdown begins rollout rather than opening a terminal success panel.
With ROMs, `InteractiveSequencePlayer` owns a separate `XBoardGame` and reuses
the established directed Practice entry for runways. Preparation locates the
active `$1122E` sequence owner at state 8's parked/service countdown
(`$114B2..$114C4`),
then holds that board until the interactive landing succeeds. The approach
uses scenery captured during that preparation. Original instructions drive rollout,
service vehicles, missile reload and departure. No sequence RAM is patched.

The native reload observation uses `$9100` pending missiles, `$90FF` reload
active and `$8D66` ammunition, matching `$16C8A..$16D20`. When pending and
active both clear after a witnessed reload, the native board pauses and the
HUD prompts **FULL THROTTLE TO TAKE OFF**. The original script overlaps its
automatic afterburner with the last part of reloading. To keep that flame out
of the prompt, the prototype observes actual published exhaust descriptors
(palette `$80`, assigned by `$122BA`) before native composition. It holds the
last unlit image and buffers every subsequent original frame while the real
reload finishes. Neutral/partial throttle cannot reveal those burner frames.

Space or the right trigger at least 95% releases the buffered animation in
order, then resumes live native departure. No native frames are invented or
skipped, and no sequence RAM is changed. Up to 600 RGB frames may be buffered;
these are presentation images, not save states or a resumed parent-game session.
Completion appears only after departure.
The board uses its original 16.768 ms physical frame interval, accumulated
separately from the prototype's 60 Hz approach step. Pause/retry/switching also
stop or discard that isolated owner.

The original carrier startup has no service/reloading scene. Carrier practice
therefore continues its original deck approach, adds an explicitly arranged
missile-count service wait at four native frames per missile using the ROM's
ammunition maximum, prompts for full throttle, then resumes the original
carrier takeoff. This carrier service wait is an enhancement, not an arcade
animation claim. Native audio is drained without playback.

Without ROMs, authored rollout (1.5 seconds), rearming (50 to 100 missiles at
15/s), the same full-throttle gate and a three-second takeoff remain usable.
This fallback does not claim to reproduce the original animation. Directed
Practice startup retains fresh first-stage accumulated state, rather than
reconstructing a complete natural credit's history.

## Authored rules

All distances and speeds are abstract prototype units, not measured aircraft
performance or ROM constants. Simulation uses a fixed 60 Hz step independent
of display refresh. This is a separate experiment from the arcade's 30 Hz
logic. Display stalls admit at most six catch-up steps; pausing drops time debt.

**Refueling:** close from 100 units behind the tanker using boost, release it
before contact to settle at 140 U/S, then match the slowly moving basket.
Contact requires range 3–9, horizontal and vertical errors within 2 units,
speed within 3 U/S of the tanker, and nearly level wings (bank command within
0.18). Hold continuously for four seconds to complete transfer. Breaking
contact resets transfer progress, allowing another attempt. Passing more than
3 units ahead fails; the rendezvous expires after 60 seconds. The green reticle
is the aiming point; steer the amber basket toward it.

**Landing:** approach from 900 units at height 60. Neutral throttle settles at
90 U/S; neutral pitch descends at 6 U/S. Follow the height guidance, line up
with the carrier or runway and pull back below height 12 to flare. Flare assist keeps
positive-pitch descent at least 2.5 U/S near the deck, so holding back settles
the aircraft instead of making it climb. At height 2, touchdown permits a
25-unit early grace zone through the first 260 units of deck, within 12 units
of centerline, at 60–120 U/S, descent no more than 5 U/S, pitch at least 0.05
and bank within 0.5. Missing those conditions fails. Passing the landing area
above touchdown height is a bolter; dropping below 58 U/S below height 20 is
an approach failure. Retry starts a fresh approach; bolter does not yet fly a
continuous circuit. Successful touchdown continues through rearming and
takeoff as above. There are no prototype weapons, damage, penalties or score.

## Validation and next increment

```sh
python3 tools/workflow.py run test-interactive-stages verify-interactive-stages
python3 tools/workflow.py run verify-sprite-extraction verify-interactive-stage-sprites
python3 tools/workflow.py run verify-interactive-sequences test verify-stage1
```

Model checks use input-only analog and digital approaches for all four challenges,
disconnect/reacquisition, overshoot, timeout, unflared/off-deck touchdown,
bolter, deterministic replay, reset and non-finite input rejection. SDL checks
use the actual executable with the dummy video driver and `--no-sprites`,
complete all four explicit demo approaches, save BMPs and reject malformed CLI
options. The separate ROM-backed target completes all four sprite demos, checks
that the aircraft scene pixels change and rejects missing ROMs/conflicting
options, and captures a fully rearmed runway holding at the takeoff prompt.
The native continuation test compares exact underlying-board RGB and buffered
presentation frames against direct board execution for all three landing
surfaces. It checks that scenery detachment preserves the scene and aircraft,
holds 120 partial-throttle calls, rejects burner visibility before power and
completes every departure. It is a wrapper comparison, not independent MAME evidence.
The existing extraction differential covers all candidate frames
and retains its negative controls. These checks do not
establish physical controller feel or desktop/GPU acceptance.

For a deterministic presentation demonstration:

```sh
./build/interactive-stages-legacy --stage landing --demo
./build/interactive-stages-legacy --stage runway --demo-hold
./build/interactive-stages-legacy --stage refuel --demo --frames 1500 --screenshot build/oracle/interactive-stages/refuel.bmp
```

`--demo` explicitly enables an input controller. It is never active in normal
practice. `--demo-hold` demonstrates approach/landing/rearming and then gives
the takeoff throttle back to the user; in headless checks it holds at the
prompt. `--frames` uses one fixed simulation step per displayed frame without
real-time pacing or focus-loss suspension, for bounded headless checks. It
still waits at the briefing unless `--demo` is selected. Screenshots require a
bounded frame count. Generated images belong under ignored `build/`.

Next work is user tuning of control feel, perspective/art and tolerance, then
an explicit enhanced-mode entry in the main frontend. Connecting challenges to
native transitions requires a typed, independently MAME-verified sequence
observer and explicit return/result policy. It must preserve Original mode and
must not use generic RAM patch callbacks or replace verified arcade behavior
with these authored rules. Audio and automatic normal-run integration remain
future work.
