# Custom Stages & Interactive Sequences (Gemini Architecture)

Status: **Implemented prototype** (`CustomStage::RomView` + `CustomStage::InteractiveController` + `GameSession` integration).

This document describes the **Native Board-Driven Custom Stage Architecture** authored by **Gemini**, designed as a direct, hardware-coupled alternative to a standalone frontend mini-engine.

---

## 1. Architectural Philosophy: Board-Driven vs. Standalone Mini-Engine

Instead of replacing `GameSession` with a separate floating-point renderer or pre-recording 600-frame RGB clips (`138 MB` of raw video per clip), this architecture treats custom stages as a first-class extension of the verified X-Board engine:

| Dimension | Standalone Prototype (`codex/custom-stage-prototype`) | Native Board-Driven Architecture (`gemini/custom-stage-prototype`) |
| --- | --- | --- |
| **Execution Owner** | Separate `CustomStage::Session` & `CustomStageView` bypassing `GameSession` in `afterburner2` | Runs directly inside [`GameSession`](../src/main/engine/game_session.hpp) on the live 68000 Main/Sub CPUs |
| **Stage Compilation** | JSON only configures a standalone 60 Hz float simulation | [`CustomStage::RomView`](../src/main/engine/custom_stage_rom.hpp) compiles JSON stages into an isolated `0x80000` upper-ROM view (`0x40000..0x80000`) |
| **Event & Wave Bytecode** | Not connected to X-Board `$DE68` or `$8122` streams | Compiles verified 8-byte `$DE68` -> `$DE90` control events (`special_road_enter`, `special_road_exit`, `set_cloud_control`, `set_horizon_scroll`, `set_scroll_state`, `clear_horizon_flags`, `set_camera_bounds`, `music_transition`, `refuel`, `landing`, `stage_advance`) and `$8122` -> `$815C` enemy wave streams (`enemy_waves`, `enemy_stage`, `scenery_stage`) |
| **Palettes & Ocean** | Drawn as 2D SDL rectangles | Compiles RGB888 ocean/sky colors into 15-bit X-Board RGB555 words in relocated `$140DA` palette blocks and cold-start aliases (`$2BC60`) |
| **HUD Title & Banner** | Custom SDL text only | Encodes stage name into the native 8-byte `$2B6AC` HUD title and 32-byte `$2EA16` stage banner in ROM |
| **Pre-Takeoff Gate** | Pre-runs `XBoardGame` for 600 frames and caches `vector<vector<RGB888>>` | [`InteractiveController::sync_board`](../src/main/engine/custom_stage_interactive.hpp) holds the live 68000 sequence timer in place (`$FFFF90AE` for carrier state 2; `$1122E` `+42` for airbase state 8) until full throttle |
| **Presentation Pipeline** | Custom SDL primitives only | Inherits Widescreen (`16:9`/`16:10`/`21:9`), ScaleFX/xBR, CRT/RetroArch `.slangp` shaders, OpenAL Soft HRTF cockpit audio, and haptics automatically |

---

## 2. Three-Pillar Design

### Pillar 1: Upper-ROM Stage Compiler ([`CustomStage::RomView`](../src/main/engine/custom_stage_rom.hpp))

Extending the proven [`Shuffle::RomView`](../src/main/engine/shuffle_rom.hpp) pattern, `CustomStage::RomView` deep-clones the canonical `Roms` with an expanded `0x80000` (512 KB) Main-CPU ROM buffer while leaving the canonical arcade ROM bytes untouched:

1. **Setup, Road, Curve & DIP Tables**: Copies donor stage entries across `$32196` (road setup), `$32324` (sky setup), `$32452` (curve setup), `$30D54` (color), and the four-bank DIP tables (`$32352`, `$32224`), applying optional JSON overrides (`road_setup`, `sky_setup`, `curve_setup`).
2. **Music Continuity (`$2B604`)**: Resolves `0x80` ("continue predecessor song") against the canonical stage order or writes an explicit track command (`maximum_power`, `final_take_off`, `red_out`, `super_stripe`, `after_burner`, `city_202`).
3. **Native Tilemap Text (`$2B6AC` & `$2EA16`)**: Encodes the custom stage name into the 8-byte HUD stage title table at `$2B6AC` and the 32-byte stage-intro banner table at `$2EA16`.
4. **Relocated Palette Blocks & Custom Ocean RGB555 (`$140DA..$1424E`)**: Relocates all five per-stage palette blocks into `0x40000+`. When `presentation.ocean` is specified, converts `sky`, `haze`, and `water` RGB888 triples into X-Board 15-bit RGB555 words across the 14-word horizon/sea gradient (`+34..+61`) and primary sky/ground descriptor words, then updates cold-start palette aliases (`$2BC60`, `$2CFF4`, `$2DBF4`, `$2BF60`).
5. **Compiled 8-Byte Stage Control (`descriptor + 16` -> `$FFFF8A08`) & Enemy Wave (`descriptor + 8` -> `$FFFF8A00`) Streams**:
   - **Control Events (`$00DE68` -> `$00DE90` jump table)**: Validates every command against the translated 68000 `$00DE90` jump table (`0, 1, 3..17, 19, 20`; explicitly rejecting unported command `2` at `$DE98` and reserved boundary-preload command `18`), splices or replaces custom 8-byte event records sorted by 16-bit stage timer tick (`$29C006 >> 4`), guarantees a terminal `command == 12` (`stage_advance`) record, and appends the `0xFFFF` sentinel.
   - **Enemy Waves (`$008122` -> `$00815C` jump table) & Scenery (`$006874` -> `$0068D2`)**: Supports selecting independent donor stages via `enemy_stage` (`descriptor + 8`) and `scenery_stage` (`descriptor + 12`), plus splicing or replacing 8-byte enemy wave records (`enemy_waves`) targeting the `$0003F000` formation table.

### Pillar 2: Live Board-Synchronized Interactive Controller ([`CustomStage::InteractiveController`](../src/main/engine/custom_stage_interactive.hpp))

For interactive `landing`, `takeoff`, `refuel`, and `combat` objectives, `InteractiveController` runs at `GameSession::advance()` boundaries:

- **In-Place Full-Throttle Departure Gate**:
  - **Carrier (`$29C09C == 15`)**: While the player is in `Phase::ready` or `servicing()` during carrier state 2 (`$FFFF90B0 == 2`), `sync_board` holds the state-2 countdown timer `$FFFF90AE` at `0`. Once the player arms and advances throttle to full power (`>= 0.95`), `sync_board` sets `$FFFF90AE = 19` to trigger catapult launch (`$10538`).
  - **Airbase Runway (`$29C09C == 16`, `$111BE` -> `$1122E`)**: Locates the active `$1122E` sequence object in `$FFFFA000..$FFFFDFFF`. When the aircraft is parked in state 8 (`+40 == 8`) and missile reload (`$FFFF9100` / `$FFFF90FF`) has finished, `sync_board` holds the state-8 departure timer (`+42`) at `30` until the player applies full throttle.
- **Seamless Landing -> Rearm Service -> Takeoff Cycle**: Landing stages transition through `approach` -> `rollout` -> `service` (missile reload) -> `taxi` (on carriers) -> `ready` (full-throttle gate) -> `accelerating` -> `climb` -> `success`.
- **Deterministic Tanker Variant Cycle (`CustomStage::VariantPicker`)**: Discovers all 6 canonical refueling stages (`3, 9, 11, 16, 19, 21`) from ROM event streams (`command == 14`) and cycles through shuffled bags without adjacent repeats across cycle boundaries.

### Pillar 3: Declarative Scene Layers & Guidance HUD ([`Frontend::CustomStageHud`](../src/main/frontend/custom_stage_hud.hpp))

Rendered as a non-intrusive SDL3 overlay inside `afterburner2`'s safe viewport without altering hardware registers:

- **Ace Combat-Style ILS Symbology**: Localizer vertical bar, glideslope horizontal bar, fixed reference cross, and flight-path velocity vector ring.
- **Refueling Capture Diamonds**: Fixed probe diamond and dynamic tanker offset diamond that expands as closure approaches contact distance (`12.0 m`) and locks green on capture.
- **Declarative Screen & World Layers**: Sorted by `order`, supporting `screen` and `world` projection spaces (`rectangle` and `image` elements), drawn by [`Frontend::NativeCustomStage`](../src/main/frontend/native_custom_stage.hpp). See [External Stages and Assets](#4-external-stages-and-assets).

---

## 3. Loading Custom Stages

### From the menu

Choose **Custom Stage** at startup, or **Change Mode → Custom Stage** in the pause
menu. The page has one row per job:

| Row | What it does |
| --- | --- |
| **Stage** | Left/Right picks a stage: the built-in presets plus anything found in the folders listed below |
| **Open Stage File** | File dialog for a stage `.json` or a stage pack `.zip` |
| **Open Stage Folder** | Folder dialog for a folder holding `stage.json` |
| **Add Assets Folder / Zip** | Adds extra art and audio for the stage to use. Add as many as you like; the newest one wins when two contain the same file |
| **Asset Packs** | Shows what is added. Enter clears the list |
| **Start Custom Stage** | Validates everything, then starts. A bad stage leaves your current run untouched and shows a **Load Error** row with the reason |

Stages are listed from `custom_stages/` next to where you run the game,
`assets/custom_stages/`, `assets/custom_stages/packs/`, and `custom_stages/` in the
game's preferences folder.

During play, **F5 reloads the stage and all its assets from disk** and restarts it,
so you can edit a file and see the result immediately. If the edited stage has a
problem, the reason goes to the console and the last good version restarts instead.

Starting a stage takes several seconds: it boots the board and flies the opening
frames hidden, as Practice does.

### From the command line

```bash
# Built-in presets:
./afterburner2 --custom-stage refuel
./afterburner2 --custom-stage carrier-landing
./afterburner2 --custom-stage runway-landing
./afterburner2 --custom-stage jungle-landing
./afterburner2 --custom-stage runway-takeoff
./afterburner2 --custom-stage carrier-takeoff
./afterburner2 --custom-stage jungle-takeoff
./afterburner2 --custom-stage canyon-combat

# A stage file, a stage folder, or a stage pack ZIP:
./afterburner2 --custom-stage assets/custom_stages/twilight-trench-run.json
./afterburner2 --custom-stage assets/custom_stages/packs/sunset-strike
./afterburner2 --custom-stage my-stage.zip

# Plus extra assets. Repeat for more; the last one has priority:
./afterburner2 --custom-stage my-stage.zip --stage-assets shared-art/ --stage-assets reskin.zip
```

---

## 4. External Stages and Assets

A custom stage can use art and audio from outside the game's ROMs. Try
[`assets/custom_stages/packs/sunset-strike`](../assets/custom_stages/packs/sunset-strike),
which uses most of it, and
[`assets/custom_stages/packs/iron-trench`](../assets/custom_stages/packs/iron-trench),
a run down a metal trench. Their files come from
[`make_example_stage_pack.py`](../tools/assets/make_example_stage_pack.py) and
[`make_trench_stage_pack.py`](../tools/assets/make_trench_stage_pack.py), so they
contain no ROM data.

### Stage packs

A stage pack is a folder or ZIP with `stage.json` plus its files:

```text
my-stage/
  stage.json
  art/sky.png
  art/sprites/crystals.png
  audio/theme.flac
```

A ZIP can hold those at its root or inside one top-level folder. Asset paths are
relative to a pack and cannot be absolute or use `..`. Use forward slashes and match
the file's upper and lower case, because folders are case-sensitive on Linux.

Files are searched in this order; the first match wins:

1. Asset packs added when loading, newest first (the menu or `--stage-assets`).
2. `asset_packs` listed in `stage.json`, relative to that file, such as
   `"../shared-art"`. A stage inside a ZIP cannot use this. Note that this lets a stage
   folder read images and audio from elsewhere on disk to display or play them.
3. The stage's own folder or ZIP.

Since packs added at load time come first, one can reskin a stage by supplying files
with the same names, without editing it.

### What external assets can do

**Sky backdrop** (`presentation.backdrop`). A PNG or BMP replaces the stage's sky,
behind every sprite. It works in every display mode, including widescreen and the
sprite filters. Ground, scenery, enemies and the HUD stay on top.

```json
"presentation": {
  "backdrop": {
    "asset": "art/sky.png",
    "parallax": [0.25, 0.4],
    "drift": [3, 0],
    "follow_roll": true
  }
}
```

The image repeats sideways, so make its left and right edges match. It is scaled so
that its height is 1.4 times the screen height, leaving room to move vertically.
`parallax` is how far it moves with the camera, `drift` adds constant scrolling in
native pixels per second, and `follow_roll` tilts it with the horizon as you bank.

**Image layers** (`layers`). Elements with `"type": "image"` draw a PNG or BMP with
full color and alpha. Elements use a 960×600 canvas, and `x`/`y` give the center.

| Field | Meaning |
| --- | --- |
| `order` | Below `0`: behind gameplay, inside the sky. `0` and above: on top of the finished frame, including the arcade HUD |
| `space` | `screen`, or `world` to place it in meters through the stage's flight model camera. That camera approximates the view and is not the arcade's own projection, so world elements can drift against ROM scenery |
| `rotation` | Degrees, about the element's center |
| `repeat` | `[columns, rows]` tile grid running right and down from the first tile |
| `color` | `[r, g, b, a]` tint and opacity |
| `filter` | `linear` (default), or `nearest` for pixel art |
| `ticks` | `[begin, end]` show only during this part of the stage timer, which runs at about 776 ticks per second. Entering a stage takes about 1,150 ticks of fly-in |

**Replacement sprites** (`sprites`). An image replaces a sprite frame in this
session's private copy of the sprite ROM. Enemies, scenery and effects then draw it
as a real hardware sprite, in every display mode.

```json
"sprites": [
  { "frame": "0x11530", "asset": "art/sprites/flying-wing.png" },
  { "frame": "0x165f0", "asset": "art/sprites/crystals.png", "own_colors": true }
]
```

| Field | Meaning |
| --- | --- |
| `frame` | Frame record, `"0x165f0"` or `"sub_0165f0"`. Name the largest frame of a family |
| `asset` | PNG or BMP of any size. Alpha below 50% is transparent |
| `palette` | `"auto"` (default) matches the image to the palette the game draws that frame with. A number forces a hardware palette |
| `own_colors` | Rewrites that palette with the image's best 14 colors. **Anything else using the palette changes too** |
| `lods` | Also rewrites the four smaller distance versions (default `true`) |
| `fit` | `contain` (default) keeps the aspect ratio; `stretch` fills the frame |

To find frames, run the stage with a sprite log. It lists each frame the game draws
in your stage, with its palette and size:

```bash
./afterburner2 --custom-stage my-stage --stage-sprite-log frames.csv
```

Then see what they look like with [the sprite extractor](SPRITE_EXTRACTION.md), whose
preview names match. An aircraft or object usually has many frames, one for each
angle, and each one you want changed needs its own entry.

**Stage music** (`music_file`). A WAV or FLAC plays instead of the stage's track. It
uses the Music Volume setting and returns to the normal music once the stage ends.

**ROM data patches** (`rom_patches`). Overwrites part of this session's copy of the
`tiles`, `sprites`, `road` or `pcm` data with a binary file, for changes the other
options do not cover, such as ground patterns or sound samples. You need to know the
data format, which [HARDWARE](HARDWARE.md) describes. Program code cannot be patched.

```json
"rom_patches": [ { "region": "road", "offset": 0, "asset": "data/road-rows.bin" } ]
```

### Ground layouts and air enemies

**Scenery patterns** (`scenery_patterns`) let a stage author its own ground layout,
using the same grid format the ROM's stages use. Each pattern is 16 rows of 16 cells.
One cell spawns per bit of distance flown, left to right and top to bottom, so a grid
lasts about 992 ticks; after that its last row keeps repeating until the next pattern.
The column is the object's position across the ground, and center is between columns
7 and 8.

```json
"replace_enemy_waves": true,
"scenery_patterns": [
  {
    "tick": 1184,
    "objects": { "2": "0x3b", "4": "0x3c", "1": "0xcd" },
    "rows": [ "....2.1....4....", "....2......4...." ]
  }
]
```

| Field | Meaning |
| --- | --- |
| `tick` | When the pattern starts |
| `rows` | 1 to 16 strings. `.` is empty, and `1`..`F` name an entry in `objects`. With fewer than 16 rows, the last one repeats |
| `objects` | Maps each digit to an entry in the ROM's object table at `$3F000`. Objects `0x3b` and `0x3c` are the canyon walls, `0xcd` a building. Most have collision |
| `flip` | `none` (default), `columns`, `rows` or `both` |

Without `replace_enemy_waves` your patterns are mixed in with the donor stage's. With
it, the donor's layout is dropped, and any records you still want go in `enemy_waves`.

Two existing names are misleading, because of what the ROM turned out to do:

| Name | What it actually controls |
| --- | --- |
| `enemy_stage`, `enemy_waves` | The stream at descriptor+8: **ground objects and scenery**, not aircraft |
| `scenery_stage` | The stream at descriptor+12: **air enemy waves**, not scenery. Stage 8 has none |
| `music_transition` | Also switches air enemies. Sub-code 12 (the third param) turns them off, and 0 turns them on. It restarts the stage's music command either way |

So to add fighters to a stage that has none, as
[`iron-trench`](../assets/custom_stages/packs/iron-trench/stage.json) does:

```json
"scenery_stage": 2,
"events": [ { "tick": 0, "command": "music_transition", "params": [0, 0, 0, 0, 0] } ]
```

With a backdrop, `presentation.ocean.water` sets the ground color and
`water_band` adds a second one for scrolling bands.

### Limits

- A replacement sprite cannot be larger than the frame it replaces. The ROM banks are
  about 99% full, so frames are rewritten in place. It is scaled to fit, and it is
  limited to the 14 drawable colors of a hardware sprite palette.
- Sprite and ROM changes last for the whole run, including stages after your slot.
  The backdrop, layers and music apply only inside the stage's slot.
- A frame is rewritten the first time the game draws it, since that names its palette,
  so with `"palette": "auto"` it can be invisible for that one frame.
- Only the sky can be replaced with an image. The ground comes from the road hardware.
- A layer below order 0 replaces the sky even without a backdrop. The sky then becomes
  one flat color, `presentation.ocean.sky` or the donor stage's sky color, and the
  stage's original horizon gradient is lost.
- Layers at `order` 0 and above are drawn after the CRT and shader passes, so those
  effects do not apply to them. Below 0 they do, being part of the game image.
- The Sharp HUD and the menus always use the original font, whatever `tiles` patches do.
- Android cannot open folders, so it takes stage packs as ZIPs. That path has not been
  run on a device yet.

### How the sky works

With a backdrop or a layer below order 0, the stage's sky palette entries are compiled
to a reserved color. Each frame, pixels of exactly that color on the background and
sprite planes, before they are composited, are replaced with the image. The horizon
gradient's two lists are blended position by position, so a position counts as ground
if either list has a ground color there.

---

## 5. Interactive Rearm, Carrier Landings and Bosses

### Interactive rearm

In the arcade, landing at the airbase (stages 5 and 13) and hooking up to the
tanker (stages 3, 9, 11, 16, 19 and 21) are cut scenes. With **Settings →
Arcade Settings → Interactive Rearm** on (the default), you fly them, in any
mode, including custom stages that use those sequences. The arcade still draws
and sequences everything; [`Rearm::Controller`](../src/main/engine/interactive_rearm.hpp)
takes over a few values at the end of each frame, and
[`Frontend::RearmHud`](../src/main/frontend/rearm_hud.hpp) draws the guidance.

**Landing**

| Phase | You do | What decides it |
| --- | --- | --- |
| Approach | Slow down; a crosswind is picked for this landing | Briefing only; the arcade lines up the fly-in |
| Final approach | Steer against the wind (stick left/right): the plane slides across the screen and the view pans with it. Hold the glide path (as in flight, pushing the stick dives and pulling climbs; the plane rises and sinks on screen). Keep the airspeed in band (throttle) | Instrument box: LOC needle (lateral), G/S needle (glide path); SPD readout |
| Flare | Ease the stick back so the sink meter stays out of the red | Sink per descent step; floating too long brings the wheels down anyway |
| Touchdown | — | Graded from lateral offset, sink, glide path and airspeed |
| Takeoff | Once the crew has reloaded, push the throttle to full | The aircraft waits on the deck or runway until you do |

| Result | Effect |
| --- | --- |
| Perfect / Good | Full reload |
| Rough | Half the missiles |
| Off the runway / Hard impact | The plane bounces back into the air and is lost (the arcade's own collision, explosion and life loss) |

The descent is stretched to between four and five times its arcade length, so
there is time to fly it. On the final approach the plane banks into turns and
pitches with the stick. It uses the arcade's own player frames: the table at
`$28C94` has one row per pitch band and 16-byte entries indexed by a signed bank
from -5 to 5, with the mirror flag (byte `+0` bit 5) set for one side. The flare
keeps the arcade's poses, because state 4 rewrites `+12` before it draws. Under the hood: the player record's handler is `$111BE`
(fly-in) then `$1122E`; the descent is `$29C322`, the flare depth is `+36` (the
arcade only touches down when it lands exactly on `$20`, so the controller snaps
to it), and state 8 is the parked reload. Each time the handler runs, `$11ED0`
re-derives the world position `+4`/`+6` from the plane's screen offset
`+42`/`+44` (scaled by the depth in `+36`), and the camera follows `+4`. So the
controller writes the lateral position and the matching `+42` together, and the
glide-path height into `+44`; writing `+4` alone made it alternate with the
ROM's value every other frame.

**Carrier landing** (a stage with `"carrier_landing": true`) is graded by where
the wheels meet the deck. The controller tracks the ship moving under the plane:
the approach starts about 300 units astern, the plane is 16 units ahead of the
chase camera, and arriving high on the glide path lands longer.

| Touchdown | Result |
| --- | --- |
| Short of the stern | **Ramp strike**: the aircraft is lost |
| In the wires (34–58 units from the stern) | **Trap** on the 1–4 wire; the 3 wire with a clean approach is perfect |
| Past the wires, or too fast | **Bolter**: a touch-and-go with no reload (the record is handed to the parked state with a one-tick timer, so the arcade starts the departure straight away) |
| Beyond the deck edge | Off the deck: the aircraft is lost |

As on a real carrier there is no flare: hold the descent and fly it into the
wires; easing back floats the plane past them. The ship starts about 450 units
out, a speck on the horizon.

After a landing the parked timer (`+26`) is held just short of zero until the
reload is finished and the throttle is full. On a carrier, the run up the deck
follows the arcade's takeoff speed (`+54`), so the bow passes under the plane
exactly at liftoff (`$140`). As it climbs out, its jet wash churns a wake on the
water below, which thins and disappears with height.

With `carrier_art` the frontend draws the ship itself
([`NativeCustomStage::paint_carrier`](../src/main/frontend/native_custom_stage.hpp)),
painted into the background plane under the arcade's own sky:

- the flight deck in perspective, standing 7 units above the waterline
- the hull below it: the transom and sides, with a red boot-top at the waterline
- the island and parked jets standing on deck
- the ship's own wake astern
- an animated sea from the `water` texture, drifting in two layers like the
  arcade's water stage and fading into haze toward the horizon The horizon is the first row of
that sea, so the stage should paint the arcade runway in sea colors (see
`carrier-recovery`).

```json
"carrier_landing": true,
"carrier_art": { "deck": "art/carrier/deck.png", "island": "art/carrier/island.png",
                 "jet": "art/carrier/parked-jet.png", "water": "art/carrier/water.png" }
```

The deck plan is drawn stern at the bottom. It spans 2.5 times the deck
half-width (28 units) across, so the angled deck and island sponson fit, and the
full deck length (320 units) top to bottom; the wires belong at 34, 42, 50 and
58 units from the stern.

**In-flight refueling**

| Phase | You do |
| --- | --- |
| Closing | Fly the receptacle marker into the boom diamond; turbulence pushes the plane around |
| Contact | The nozzle only reaches the plane while it is steady inside the inner diamond |
| Transfer | Missiles flow only while connected; a time bar shows how long the tanker stays |

Whatever has not transferred when the time runs out is lost. The controller
writes the plane's `+4`/`+50` as the arcade's own per-pose target (`$16C36`)
plus your offset, holds the nozzle timer `$90F2` at pose 9 until contact, keeps
the tanker on station during transfer, and paces the reload sequencer through
its delay byte `$9103` (held negative while disconnected) so the pending count
`$9100` is never lost by mistake.

### Recoloring a stage (`palette_patches`)

Each entry sets one or more color words of the stage's palette:

| `table` | Words |
| --- | --- |
| `primary` | 0–1 sky, 2–3 ground |
| `alternate` | 0–1 sky, 2–3 ground bands, 4–11 the road / runway colors (asphalt shades, center paint, edge lines) |
| `gradient` | The thin horizon strip: 1–14 and 17–30 |
| `road`, `1413a`, `14196`, `1424e` | The other per-stage palette blocks, by their pointer table |

```json
"palette_patches": [
  { "table": "primary", "words": [2, 3], "color": [20, 64, 150] },
  { "table": "alternate", "words": [8], "color": [236, 200, 40] }
]
```

[`carrier-recovery`](../assets/custom_stages/packs/carrier-recovery) uses this
to turn the stage 5 airbase into open sea, with the runway painted out, and
draws its carrier with `carrier_art` (generated by
[`make_carrier_landing_pack.py`](../tools/assets/make_carrier_landing_pack.py)).

### Bosses (`boss`)

A boss is a large aircraft drawn from pack images and fought with your own
guns and missiles ([`CustomStage::BossFight`](../src/main/engine/custom_stage_boss.hpp),
drawn by [`native_boss.hpp`](../src/main/frontend/native_boss.hpp)). It is
painted into the board's background plane, so your plane, enemies, missiles
and explosions stay in front of it. It tilts with the horizon and banks with its
own maneuvers: it overtakes you from overhead, then weaves ahead, lunges closer
and sweeps across. Turrets swing toward you, engines glow and strobes blink.

Everything that can hit you is drawn last, on top of the picture: tracer
streaks, closing red rings where flak will burst, and smoke-trailed missiles.
Alerts name the threat (GUNFIRE, FLAK, MISSILE), and a hit flashes the screen
edge and says what hit you.

```json
"boss": {
  "name": "LEVIATHAN",
  "hull": "art/boss/hull.png",
  "size": [300, 112],
  "appear_tick": 3600,
  "hold_tick": 6000,
  "time_limit": 120,
  "bonus": 200000,
  "parts": [
    { "name": "PORT ENGINE", "kind": "pod", "asset": "art/boss/engine.png",
      "broken": "art/boss/engine-wrecked.png", "at": [-66, 30], "size": [48, 36], "hp": 60 },
    { "name": "BRIDGE", "kind": "core", "asset": "art/boss/bridge.png",
      "at": [0, -42], "size": [60, 42], "hp": 160 }
  ]
}
```

- `at` and `size` are screen pixels (320 × 224) relative to the hull center at full size.
- **pod** parts fire flak while they live; **turret** parts fire tracer bursts at you;
  the **core** is shielded until every pod is down, and destroying it destroys the boss.
  Once two pods are gone, the core also launches slow homing missiles: keep moving
  and they overshoot, or shoot them down with the cannon.
- Hold the cannon on a part under the gun sight; a missile fired while a part
  shows **LOCK** homes onto it.
- The stage timer is held at `hold_tick` while the boss lives, so the stage
  cannot end around it. The stage's own fighters keep flying until then.
- A hit from flak or tracers raises the arcade's collision request, so you lose a
  life exactly as from an enemy missile. You get a few seconds' grace afterwards.
- Destroying it adds `bonus` to the score; outlasting `time_limit` makes it withdraw.

[`leviathan`](../assets/custom_stages/packs/leviathan) is a full example: an
original flying carrier over a storm sky (generated by
[`make_boss_stage_pack.py`](../tools/assets/make_boss_stage_pack.py)).

## 6. JSON Stage Format & Schema

Custom stages are validated against [`assets/custom_stages/stage.schema.json`](../assets/custom_stages/stage.schema.json) by the strict parser in [`src/main/engine/custom_stage_json.hpp`](../src/main/engine/custom_stage_json.hpp). Example ([`assets/custom_stages/canyon-combat.json`](../assets/custom_stages/canyon-combat.json)):

```json
{
  "$schema": "./stage.schema.json",
  "version": 1,
  "name": "CANYON / AMBUSH",
  "objective": "combat",
  "environment": "canyon",
  "slot": 1,
  "source_stage": 8,
  "enemy_stage": 16,
  "scenery_stage": 8,
  "music": "super_stripe",
  "replace_events": false,
  "presentation": {
    "rom_environment": true,
    "player_shadow": true,
    "clouds": true,
    "hud": true,
    "ocean": {
      "sky": [68, 22, 36],
      "haze": [238, 128, 72],
      "water": [54, 24, 18]
    }
  },
  "events": [
    { "tick": 16, "command": "special_road_enter", "params": [0, 0, 0, 0, 0] },
    { "tick": 32, "command": "set_cloud_control",  "params": [1, 0, 0, 0, 0] },
    { "tick": 64, "command": "set_horizon_scroll", "params": [0, 0, 1, 0, 0] },
    { "tick": 96, "command": "special_road_exit",  "params": [0, 0, 0, 0, 0] }
  ],
  "enemy_waves": [
    { "tick": 24, "command": "standard",      "formation": 1, "params": [0, 0, 0, 0] },
    { "tick": 56, "command": "parameterized", "formation": 2, "params": [0, 16, 0, 0] }
  ]
}
```

### Verified `$00DE90` Stage Control Commands (`events`)

| Command | Name | 68000 Handler | Parameter Contract & Hardware Register Effect |
| --- | --- | --- | --- |
| `0` | `music_transition` | `$DE90` -> `$DEE4` | `4(A0)` word in `{0, 4, 8, 12}` selects `$DEEC + D0.W` sound/flag transition |
| `1`, `3` | `clear_horizon_flags`, `clear_horizon_flags_alt` | `$DE94` / `$DE9C` -> `$DF64` | `BCLR #0,($29C201).L; BCLR #0,($8D36).W` |
| `4` | `set_horizon_flag` | `$DEA0` -> `$DF74` | `BSET #0,($29C201).L` |
| `5`, `6` | `nop_5`, `nop_6` | `$DEA4` / `$DEA8` -> `$DF94` / `$DF96` | No-op (`RTS`) |
| `7` | `set_horizon_target` | `$DEAC` -> `$DF98` | Writes 16-bit word `4(A0)` to `$29C07C` |
| `8`, `9` | `set_ground_flag`, `clear_ground_flag` | `$DEB0` / `$DEB4` -> `$DFA2` / `$DFAA` | `BSET` / `BCLR #1,($8B6F).W` |
| `10` | `set_scroll_state` | `$DEB8` -> `$DFB2` | Writes byte `3(A0)` to `$29C0A1` |
| `11` | `set_cloud_control` | `$DEBC` -> `$DFBC` | Writes byte `3(A0)` to `$FFFF8D6A` |
| `12` | `stage_advance` | `$DEC0` -> `$DFC4` | Clears `$29C006`, increments `$29C060`, advances stage |
| `13` | `set_camera_bounds` | `$DEC4` -> `$E022` | Writes byte `3(A0)` -> `$29C0EF`, word `4(A0)` -> `$29C0E6`, word `6(A0)` -> `$29C0E8` |
| `14` | `refuel` | `$DEC8` -> `$E03C` | `CLR.W ($29C0CC).L` |
| `15` | `set_stage_mode` (`landing`) | `$DECC` -> `$E044` | Writes 16-bit word `4(A0)` to `$29C0CC` |
| `16` | `special_road_exit` | `$DED0` -> `$E062` | `MOVE.W #$0402,($29C320).L; BSET #5,($29C200).L` |
| `17` | `special_road_enter` | `$DED4` -> `$E074` | `MOVE.W #$0480,($29C320).L; BCLR #5,($29C200).L` |
| `19` | `set_bank_drift_flag` | `$DEDC` -> `$E0A6` | Writes byte `3(A0)` to `$FFFF8D7E` |
| `20` | `set_horizon_scroll` | `$DEE0` -> `$DF7E` | `BSET #0,($29C201).L; BSET #0,($8D36).W; MOVE.W 4(A0),($8D38).W` |

---

## 7. Verification & Mutation Controls

```bash
# Run the ROM-free custom stage regression & mutation suite:
python3 tools/workflow.py run test-custom-stages check-custom-stage-mutations

# Verify against the user's CRC-checked aburner2 ROM set (including 960-frame GameSession execution):
python3 tools/workflow.py run verify-custom-stages-rom

# Inspect the compiled upper-ROM layout, RGB555 words, event bytecode, 68000 execution, and autopilot timeline:
./build/custom_stage_rom_regression --inspect assets/custom_stages/twilight-trench-run.json
```

- [`tests/custom_stage_rom_regression.cpp`](../tests/custom_stage_rom_regression.cpp): Verifies `CustomStage::RomView` canonical ROM immutability, upper-ROM allocation, setup/DIP/music/title/banner tables, RGB555 ocean palette compilation, event and enemy wave stream sorting/splicing/replacement, real 68000 execution (`OMainIrqService::caller_step` stepping through `$00DE68..$00DE8E` and `$008122..$0082FA`) across all 19 `$00DE90` control commands and all `assets/custom_stages/*.json` definitions, full `GameSession` execution when ROMs are available, `Shuffle::prepare_alternate_palette` compatibility, `VariantPicker` non-repeating cycles, `InteractiveController` autopilot completion, full-throttle gate, live 68000 RAM synchronization (`$FFFF90AE` and `$1122E` `+42`), and `Guidance`/`ShadowState` math.
- [`tools/check_custom_stage_mutations.py`](../tools/check_custom_stage_mutations.py): Verifies that 8 deliberate mutations across `custom_stage_rom.hpp`, `custom_stage_json.hpp`, and `custom_stage_interactive.hpp` (including mapping `special_road_enter` to command `11` or allowing unported command `2`) are rejected.

## Player-controlled flare (`player_landing`)

With `"player_landing": true` on a slot-5 runway landing, the native sequence
model is bypassed and the stick drives the ROM's own flare. In the airbase
object's state 4 (`$1134A`) the ROM subtracts `$40` from `+36(A5)` each frame
and derives the plane's height (`+44`) from it; touchdown is `+36 <= $20`.
After each frame the host replaces that `$40` with `$40 * (1 - stick_y)`
(neutral stick = ROM behaviour; pull back slows or reverses the sink; push
forward sinks faster). Touchdown with a sink delta above `$60` is reported as a
hard landing. Rollout, rearm and takeoff then run in the ROM unchanged.
Not yet driven: lateral position and throttle (player X and the speed fields
have not been identified in the ROM).
