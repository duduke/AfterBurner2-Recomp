# stage.json quick map

The authority is `assets/custom_stages/stage.schema.json` (machine-checked by
`scripts/validate_stage.py`) and `docs/CUSTOM_STAGES.md`. This page is a map of
what exists and where it is explained.

## Core

```json
{
  "$schema": "../../stage.schema.json",
  "version": 1,
  "name": "NIGHT RAID",          // arcade font: uppercase, no / or :
  "objective": "combat",         // older prototype field; combat keeps its overlay out of the way
  "slot": 2,                     // which stage of the run is replaced (1-22)
  "source_stage": 2,             // donor: scenery, enemies, special sequences
  "scenery_stage": 2,            // donor of the AIR enemy stream (misleading name)
  "music": "city_202",           // or "music_file": "audio/theme.wav" (WAV/FLAC)
  "events": [ { "tick": 0, "command": "music_transition", "params": [0,0,0,0,0] } ],
  "replace_events": false
}
```

Arcade music names: maximum_power, final_take_off, red_out, super_stripe,
after_burner, city_202.

## Look

| Field | Notes (docs section) |
| --- | --- |
| `presentation.backdrop` | `{asset, parallax:[x,y], drift:[x,y], follow_roll}` sky image (4) |
| `presentation.ocean` | sky/haze/water/water_band RGB for sea-style ground (2, 4) |
| `presentation.hud` | false hides the older prototype guidance overlay |
| `palette_patches` | `{table, word|words, color}`; tables `primary` (0-1 sky, 2-3 ground), `alternate` (0-1 sky, 2-3 ground bands, 4-11 road/runway), `gradient` (horizon strip 1-14, 17-30) (5) |
| `sprites` | `{frame:"0x1d270", asset, own_colors, fit:"stretch"|"contain"}` replaces an arcade sprite frame; `--stage-sprite-log` lists frames seen (4) |
| `layers` | screen/world image or rectangle elements, `order` (below 0 = in the sky), `ticks:[from,to]` (4) |
| `rom_patches` | raw data patches, rarely needed (4) |

## Ground layout

`scenery_patterns`: list of `{tick, rows: 16 strings of 16 chars, objects: {"2":"0x3b", ...}}`.
`.` is empty; digits map to arcade object ids (0x3b/0x3c canyon walls, 0xcd
building, 0xc0 bush, 0x13 sea swell). Use with `replace_enemy_waves: true` and
`enemy_waves: []` to drop the donor's own layout. A blank pattern
(`rows: ["................"] * 16`) clears the ground.

## Gameplay interfaces (5: "Procedural conditions")

```json
"mission":    { "type": "destroy|survive", "briefing": "...", "detail": "...",
                "target": 12, "time_limit": 100, "bonus": 80000, "hold_tick": 20000 },
"conditions": { "night": 0-1, "searchlights": 0-6, "flak": 0-1, "rain": 0-1, "lightning": 0-1,
                "fog": 0-1, "turbulence": 0-1, "wind": -1..1, "sea": bool,
                "art": { "fighter": "art/fighter.png", "ship": "art/ship.png", "water": "art/water.png" } },
"formations": { "seed": 7, "waves": 12, "start_tick": 1600, "spacing": 6,
                "patterns": ["v","line","echelon","diamond"], "entries": ["front","rear","left","right","dive"],
                "size": [2,4], "aggression": 0-1, "ships": 0-4 },
"entry": "level|dive|climb|roll",
"mutators": ["night","fog","storm","turbulence","flak_alley","ace_pilots","swarm","afterburner"]
```

## Boss (5: "Bosses")

```json
"boss": { "name": "LEVIATHAN", "hull": "art/boss/hull.png", "size": [300,112],
          "appear_tick": 3600, "hold_tick": 6000, "time_limit": 120, "bonus": 200000,
          "parts": [ { "name": "PORT ENGINE", "kind": "pod|turret|core", "asset": "...",
                       "broken": "...", "at": [-66,30], "size": [44,34], "hp": 60 } ] }
```
Exactly one `core`; it is shielded until every `pod` is down. Sizes are 320x224
screen pixels at full size.

## Carrier landing (5: "Interactive Rearm")

`"carrier_landing": true` plus
`"carrier_art": {"deck","island","jet","water"}` on donor 5 with the runway
painted as sea (see `make_carrier_landing_pack.py`). The deck plan is drawn stern
at the bottom, 2.5x the 28-unit half-width across, 320 units long, wires at 34,
42, 50, 58.

## Assets and loading (3, 4)

Packs load from a folder, `stage.json` or `.zip`. Extra asset folders/zips:
`--stage-assets` (repeatable) or the menu; the newest mount wins. Search order:
extra assets, then the stage's `asset_packs`, then the pack itself.
