# Custom stages prototype

The standalone prototype owns landing, ground service, takeoff and refueling.
Successful landings continue through braking, missile rearming and departure
in one scene. Standalone departure presets are also available. The arcade
executable and its stage transitions are unchanged. This is an authored extension,
not a port of arcade flight physics. Custom rendering uses a modern RGBA scene
compositor; ROM hardware limits apply only inside the optional asset decoder.

```sh
python3 tools/workflow.py run build/interactive-stages test-custom-stages
./build/interactive-stages --stage runway
./build/interactive-stages --stage carrier-takeoff
./build/interactive-stages --stage refuel
./build/interactive-stages --stage-file assets/custom_stages/runway-landing.json
```

Seven presets: `refuel`, `landing` (carrier), `runway`, `runway-jungle`,
`runway-takeoff`, `carrier-takeoff`, `jungle-takeoff`. Tab / gamepad Y cycles
presets. Enter / Start begins, P / Start pauses, R / F5 retries and reloads a
custom file, H / gamepad X toggles approach assistance. Down / pull back raises
the nose; Up / push forward lowers it. Left/right bank. Space / right trigger
adds power, Shift / left trigger reduces power. Escape / Back exits.
Focus loss pauses; resume explicitly. Input uses the native default mappings,
without importing the main game's saved calibration.

## Authoring

Copy an example from [assets/custom_stages](../assets/custom_stages/) and pass
`--stage-file path.json`. The seven presets also load these JSON files directly;
R / F5 reloads their current contents. All documents require `"version": 1`.
See [the JSON schema](../assets/custom_stages/stage.schema.json) for editor
validation and [a layered example](../assets/custom_stages/layered-runway.json).
Unknown/duplicate fields, invalid types, nonfinite numbers and unsupported
flight dimensions fail. JSON comments and the former key/value format are
not accepted.
`objective` is `landing`, `takeoff` or `refuel`; `environment` is `carrier`,
`runway`, `jungle` or `sea` (refueling only). Dimensions and speeds use meters,
seconds and meters per second; `glideslope` is height / distance. These are
prototype authoring units, not arcade RAM values or a real aircraft simulation.
`"seamless_takeoff": true` is the default; `false` retains a landing-only challenge.

| Field | Meaning |
| --- | --- |
| name | Stage title |
| length, width | Landing/departure surface dimensions |
| start_distance | Distance to landing aim point / tanker |
| start_height | Height above deck/runway, or refueling basket error |
| speed | Initial and neutral-throttle approach airspeed |
| glideslope | Target height / distance, default 0.06 |
| tanker_span | Tanker assembly wingspan, default 50 |
| seamless_takeoff | Continue landing through service/departure, default true |

### Modern scene layers

`layers` is a dynamically sized array. Each layer has a name, numeric `order`,
`space` (`screen` or `world`) and an `elements` array. Arbitrarily many custom
layers can share or fall between the built-in passes. Increasing order draws
later; equal orders preserve file order.

| Order | Built-in pass / custom placement |
| --- | --- |
| below 0 | Behind the built-in environment |
| 0–99 | Above the backdrop, below runway/carrier scenery |
| 100–199 | Above environmental scenery, below guidance |
| 200–299 | Above guidance, below player and ground crew |
| 300–999 | Above aircraft and effects |
| 1000 and higher | Above HUD, with access to the complete canvas |

Screen positions use the 960×600 logical canvas; these are layout units, not
a 320×224 framebuffer or an output-resolution cap. SDL renders at the current
window resolution. World positions use the metric flight camera. World elements are camera-facing billboards; their x/y/z is
the top-left point, with rows extending down the billboard. World layers below
1000 clip to the scene, while layers at 1000 or higher can cover the HUD.

Elements support `"type": "rectangle"` or `"type": "image"`, x/y/z, width/height,
rotation in degrees and `"color": [red, green, blue, alpha]`. Image `asset` paths
resolve relative to the JSON file. PNG (SDL 3.4+) and BMP images load as full
32-bit RGBA textures, without palette/pen conversion, ROM LOD selection or
sprite-frame size restrictions. Color tints and alpha apply to both types.
`"repeat": [columns, rows]` repeats an element as a tile grid. Visible tiles
are culled before drawing; no native tilemap dimensions or tile-index limits
apply. Subpixel repeated cells are culled at the current display resolution.

There are no X-Board layer, 256-sprite, tile-count or indexed-color budgets in
this path. Layer/element storage is dynamic; practical limits are memory,
frame time and the host GPU's texture support. Flight-physics validation ranges
remain separate from rendering. The current renderer uses 8 bits per RGBA
channel and alpha blending, with all 16.7 million RGB colors available.

Set `"presentation": {"rom_environment": false}` to replace the stock backdrop,
runway/carrier and generated scenery with your own layers. ROM images remain
optional source artwork alongside authored textures, and `--no-sprites` still
allows custom images. A layer can, for example, contain:

```json
{
  "name": "high resolution scenery",
  "order": 150,
  "space": "screen",
  "elements": [
    {"type": "image", "asset": "clouds.png", "x": 50, "y": 90,
     "width": 400, "height": 100, "color": [255, 255, 255, 160]},
    {"type": "image", "asset": "water.png", "x": 40, "y": 350,
     "width": 16, "height": 16, "repeat": [55, 8]}
  ]
}
```

The metric perspective camera scales actors and surface geometry with depth.
Default runway dimensions are 2200 by 45; carrier deck dimensions are 300 by 70.
These are editable visual proportions. The carrier approach has a forgiving
14-meter lateral window and a five-meter-per-second sink tolerance. Landing
uses one continuous camera through approach, flare, braking and ground service.
The ROM motorcycle (autumn) or car (jungle) follows the landing/departure run.
Rearming vehicles and crew reload missiles. Fuel stays unchanged; there is no
ground fuel truck, hose or fuel-transfer phase.
After they clear, apply full throttle; if it was held during service, release
it once before reapplying. The carrier crew smoothly returns the aircraft to
the catapult position, with its takeoff pose based on the arcade startup.
Takeoff accelerates and needs pullback above rotation speed. Engine exhaust
stays anchored to the selected aircraft pose. The native takeoff exhaust entry
is selected once, with a small brightness pulse; cycling unrelated plume
geometries no longer makes the flame jump.

Air refueling requires physical basket/probe contact: the probe extends 12
meters forward, and the capture tolerance is 0.45 meters with matched speed and
attitude. On capture, the connection locks and reloads missiles over four
seconds. Stick/throttle input no longer breaks this automatic rearming sequence;
R still retries the stage. Rearming cannot begin while the plane
is short of the nozzle. The metric docking frame is calibrated to the neutral aircraft's opaque
centerline canopy/nose pixel. Pitch/bank sprite changes do not shift the tanker
or hose; the displayed nozzle error still measures the visible aircraft point. The ROM hose tip
meets that silhouette at capture, without an added long probe line. The hose
stays in front of the aircraft during the servicing hold.

## Arcade HUD and instructions

The custom HUD uses the original SCORE/HIT surrounds, speed ruler, STAGE label
and missile symbols. Loading reads the native text entries and live text palettes
from the same temporary board used for terrain preparation, then caches RGBA
widget textures. Their placement and live bindings belong to the custom stage:
score/hits remain zero, the stage number follows the selected preset, and missile
symbols track groups of ten while the exact count changes during rearming.
Distance, height, speed and vertical motion use the authored model's metric values.
The original speed ruler artwork is retained with a continuous custom speed fill;
the numeric speed is explicitly shown in meters per second (MPS).

All prompts, readings, control hints, briefing/completion messages and paused
feedback now stay inside the gameplay rectangle. The external debug header and
footer are removed. Text decodes the original 8x8 planar ROM glyphs, with their
dark outline/counter pen, and uses nearest-neighbor sampling. Labels use uppercase
and spell out units/directions where the ROM punctuation glyph is blank.
Without ROM assets, the geometric fallback retains its development font.

`presentation.hud` defaults true. Set it false for an authored HUD or clean scene
capture; custom JSON layers at order 1000 and above still draw over the built-in
HUD. The renderer exposes a separate visibility switch for isolated guidance
pixel checks, so changing missile/progress readings cannot disguise world motion.

## ROM presentation

Local extracted `roms/` or `roms/aburner2.zip` is discovered automatically;
`--roms directory-or-zip` overrides it. `--no-sprites` selects a geometric fallback.
No ROM data or captures are distributed. Loading prepares the environment's
materials, then destroys the temporary native board. Tanker retries prepare the
new matching variant. Custom gameplay never
advances a native sequence or replays captured approach frames.

The existing verified hardware sprite decoder and palette DAC supply F14,
carrier, ground crew, tanker fuselage/wings, deployed refueling apparatus/nozzle and
exhaust textures. Main `$166CE` entry 9 supplies the deployed hose frame, palette
`$8C`, selected by `$1686A`. Tanker parts share one depth scale. Runtime road
layer samples provide the ROM sky, ground and sea colors/patterns. The runway's
pavement, edges and markings are sampled from the parked original road layer,
deprojected into a reusable strip and projected through the custom camera. The
carrier uses its complete original image, including deck markings, bridge and
parked aircraft; no land runway is drawn over it. Only the explicit ROM-free
fallback draws a generated runway. Camera, stage rules and HUD remain authored.

Original scenery frames/palettes and signed native positions are collected from
native Main `$8AF2` objects
at asset-loading time. Additional native tree and wave art is instanced across
the entire visible ground, independent of arcade sprite slots or depth buckets.
There is no custom catalog limit of 24 or native per-frame sprite cap. Autumn
stages use orange broadleaf trees; jungle stages use green palm groves. World
cells stay fixed as the aircraft moves, with clearance beside the runway.
Non-tree props retain the original native snapshot's lateral positions and
depth ordering under one common metric conversion; they are not repeated or
forced into invented left/right rows.

Carrier stages retain the older arcade look: bright cyan sky, saturated blue
foreground water and the original colors of all seven native water-animation
frames from Main `$3F918`. Horizontal foam patches keep full foreground opacity,
with only the distant edge fading into the horizon. A connected, short blend
joins sky and water through a shared pale color, without a detached stripe,
raster gaps or replacing the whole sea with muted atmospheric shading.
Colors remain editable as RGB arrays in `presentation.ocean.sky`, `.haze` and
`.water` in both carrier JSON files. Waves are not rotated or randomly mirrored.
The tanker stage randomly selects a complete native backdrop from the six original
tanker stages (3, 9, 11, 16, 19 and 21), discovered from the loaded ROM event
programs. Sky, ground colors, scenery frames/palettes and close-range F14 colors
come from the same settled native sequence. No extra aircraft are drawn;
the horizon sits below the docking area. The choice stays fixed during an attempt.
Retries (R/F5 or Enter after completion) and new tanker entries draw another
variant from a shuffled cycle, without adjacent repeats. Landing/departure stages
have no airborne AI aircraft. Only the player and tanker fly in tanker stages.

`presentation.tanker_variant` defaults to `"random"`; an original stage number
pins that backdrop, for example `"tanker_variant": 9`. With ROMs loaded, a pinned
number must identify an actual tanker stage. `--seed 123` reproduces the sequence
of random choices for debugging; normal launches use a fresh seed. Changing
variants clears the previous scenery and material together.
Jungle field instances use only the upright palm family. Land stages retain their original sunset/green sky
material. Background and pavement raster bands use three logical rows per
step, matching the original 160-row scene crop to the 392-row prototype view;
SDL then scales that consistently at larger window sizes.

Large translucent clouds appear across all seven custom sequences. Soft RGBA
cloud masses sit behind aircraft/scenery, respond to the camera and dim for
night tanker variants. They do not consume native sprite or palette slots.
`presentation.clouds` defaults true; set it false in JSON to disable them.
`--no-scenery` disables clouds, props and fields for visual comparisons. The fields,
placements and camera are authored 2.5D presentation; original-game execution
and its sprite budgets remain unchanged.

The landing aid follows the attached Ace Combat reference: a long vertical
localizer bar slides sideways, while a long horizontal glideslope bar slides
vertically. Both have end caps and center on a fixed ring when the aircraft is
aligned with the approach. Each bar independently becomes three pixels thick
inside its three-pixel centering window. The bars respond independently to lateral/height
error, without moving the center ring with camera pitch. A separate winged
velocity-vector circle responds to actual lateral/vertical velocity,
rather than the aircraft's nose. Air refueling instead uses a large tanker
diamond with an inner diamond that grows almost to the outer outline with
closure. The same acquisition predicate that captures the aircraft makes both
outlines coincide, become bold
green and hold position throughout the automatic rearming sequence.
The nozzle's flap is drawn over the aft-right fuselage, so its open panel remains
visible. H toggles the aids.

The [refueling diamond reference](https://encrypted-tbn0.gstatic.com/images?q=tbn:ANd9GcRQMd38FfDtS4gDBMnGJWCx0qiZUIbOqpdqLTXquuUgJw&s=10)
was inspected, as was the subsequently attached landing screenshot. HUD geometry is authored rather than
copied image artwork.

## Player shadow

A soft, translucent F-14 silhouette projects onto the landing surface. Its
world-space sunlight offset increases with altitude; the footprint becomes
smaller, lighter and softer higher up, then draws closer and sharper during
touchdown. Bank slightly foreshortens it. Perspective clipping keeps the shadow
inside the runway/deck boundary, and the player/crew draw above it. It also
continues through rollout and departure when the aircraft is over the surface.
Air refueling casts no surface shadow.

The mask comes from the ROM aircraft's opaque pixels and uses a separate,
filtered texture, preserving the player's original pixel art. Set
`"presentation": {"player_shadow": false}` in a stage JSON to disable it.

## Smoke and audio

Every landing (carrier, autumn runway and jungle) uses the ROM's original
landing-smoke silhouette at both main wheels. The native shape is toned neutral
gray and alpha-fades as it expands/drifts during braking. The landing renderer
selects all five native aircraft pitch poses from the runtime ROM table.
Tanker flight uses those pitch/bank poses too, with its own live native palette
and the same continuous pitch displacement as landing. Mirroring follows the
selected bank pose, avoiding a flip partway through a gentle left bank. Exhaust
orientation and engine anchors share the aircraft's mirror transform. Gear
proximity no longer freezes the airborne aircraft pose; a stopped aircraft
retains its grounded pose. Live play includes original ROM music,
missile-reload beeps and launch effects through the existing sound-board runtime,
plus an authored quiet engine bed and a tire spin-up chirp followed by braking
friction. No dedicated tire-squeal command was identified in the inspected ROM
landing routine; its approach command is not substituted for tire noise. M toggles audio;
`--mute` starts muted. Pausing clears queued sound and freezes its clock.
Retries reset the sound session. ROM-free mode retains procedural engine and
tire audio. Fast `--frames` runs skip audio unless `--audio-wav build/oracle/name.wav`
is supplied; that option records 48 kHz stereo PCM for inspection.

## Validation and retained legacy prototype

`test-custom-stages` covers seven demo completions, rollout, seamless service/departure cycles, independent takeoff,
full-power gating, invalid touchdown, projection scaling, stage files and original
runway pixel provenance.
`verify-custom-stages` adds executable runs, screenshots, strict file rejection,
ROM-free and ROM-backed presentations, seven scenery-on/off image comparisons,
original runway use, carrier/runway isolation, fields beyond 256 drawn sprites
band thickness/gradient in saved pixels, physical docking, serviced cold holds
and all five exhaust attachment frames. Captures stay in ignored
`build/oracle/`.

The earlier native-continuation prototype is retained as
`build/interactive-stages-legacy`. Its original `verify-interactive-stages`,
`verify-interactive-stage-sprites`, `verify-interactive-sequences` targets retain
all existing cases. [Legacy implementation and evidence](INTERACTIVE_STAGES_LEGACY.md).
