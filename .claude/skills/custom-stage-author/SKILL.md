---
name: custom-stage-author
description: Author, generate, fix and test custom stages for the After Burner II recomp (AfterBurner2-Recomp) — stage.json packs with external art and music, missions, night/searchlight/flak and weather conditions, procedural squadron formations, bosses, carrier landings, palette recolors, ground layouts and run mutators. Use this whenever the user wants a new stage, level, mission, boss fight, theme or "make a stage like X" for this game, wants to tweak or debug an existing pack in assets/custom_stages, asks how the stage format works, or wants to see what a stage looks like — even if they don't say "custom stage".
---

# Custom stage authoring (After Burner II recomp)

A custom stage is a **pack**: a folder (or .zip) with `stage.json` and the art and
audio it names. The game compiles it onto a copy of the arcade ROM in a chosen
**slot**, borrowing a **donor** stage's scenery, then layers native features on
top (sky backdrops, sprites, bosses, squadrons, weather, carrier...). The arcade
engine keeps flying everything, so a pack mostly *describes* — it never ships ROM
data.

The full format lives in `docs/CUSTOM_STAGES.md` (sections 3–6) and
`assets/custom_stages/stage.schema.json`. Read the section you need rather than
guessing field names; `references/stage-format.md` here is a compact map of it.

## Workflow

1. **Pin down the idea** in a sentence: setting, threat, goal, length. Map it to
   features (table below). If the user names a game or film, make an original
   take — own names, own art, own music — and say so.
2. **Pick slot + donor** from `references/donor-stages.md`. The donor decides the
   ground scenery, the arcade's air enemies and special sequences (refuel,
   landing, canyon); avoid refuel/landing donors unless that's the point.
3. **Write a generator script**, not a hand-edited JSON, whenever the stage has
   art, layouts or randomness: `tools/assets/make_<name>_pack.py`, standard
   library only, writing `assets/custom_stages/packs/<name>/`. The existing
   generators are the best templates:
   - `make_procedural_stage.py` — mission/conditions/formations themes (night raid,
     naval assault, urban trench, storm front) with seeds and mutators. For a new
     themed stage, adding a theme here is often the quickest route.
   - `make_boss_stage_pack.py` — a native boss with shaded parts and wrecks.
   - `make_carrier_landing_pack.py` — carrier art, palette patches, blank ground.
   - `make_trench_stage_pack.py` — scenery_patterns walls, sprite reskins, music.
   Draw art procedurally (supersampled shading functions work well) and keep
   generated packs small.
4. **Validate**: `python3 .claude/skills/custom-stage-author/scripts/validate_stage.py <pack>`
   — schema, missing assets, unsafe paths, unreadable on-screen text, missing
   art for enabled features. Fix every ERROR; read every WARN.
5. **Look at it**: `python3 .claude/skills/custom-stage-author/scripts/capture_stage.py <pack> --frames 300 900 1500`
   runs the stage headless and writes PNGs and a contact sheet. Open the sheet
   and check it actually looks right. Run with the game's build present
   (`build/*/afterburner2[.exe]`) and the user's ROMs in `roms/`.
6. **Iterate**, then tell the user how to play it:
   `afterburner2 --roms <roms> --custom-stage <pack> [--stage-mutators storm,night]`
   or pick it in the in-game Custom Stage menu. F5 reloads the pack from disk.

## Feature map

| The stage wants... | Use |
| --- | --- |
| A different sky | `presentation.backdrop` (image keyed into the sky, follows roll) |
| Different ground / runway colors | `palette_patches` (`primary`, `alternate`, `gradient`) |
| Its own ground layout (walls, canyons, cities) | `scenery_patterns` 16x16 grids + `replace_enemy_waves` |
| Reskinned arcade objects | `sprites` (replace a sprite frame with a PNG) |
| Music | `music_file` (WAV/FLAC in the pack) or `music` (arcade track) |
| Title cards, HUD art, overlays | `layers` with `ticks` windows |
| A goal and a score bonus | `mission` (destroy N / survive), `hold_tick` to wait for it |
| Night, searchlights, flak, rain, lightning, fog, turbulence, wind, sea | `conditions` |
| Extra enemy squadrons with varied entry angles, naval targets | `formations` (+ `conditions.art.fighter/ship`) |
| An opening maneuver | `entry`: dive, climb, roll |
| Optional difficulty twists | `mutators` (or `--stage-mutators` at launch) |
| A boss | `boss` (hull + parts: pods, turrets, one core) |
| A carrier landing | `carrier_landing` + `carrier_art`, donor 5, runway painted as sea |

## Things that bite (learned the hard way)

- **Text is drawn in the arcade font**: uppercase letters, digits and a little
  punctuation only. No lowercase, `/` or `:` — they render as garbage. Keep
  names, briefings and boss names short and uppercase.
- **Stream names are misleading**: `enemy_waves` / `enemy_stage` drive *ground
  scenery*; `scenery_stage` picks the *air enemy* stream. Air enemies can be off
  in a donor (stage 8 turns them off with `music_transition` sub-code 12;
  a `music_transition` event with params `[0,0,0,0,0]` at tick 0 turns them on).
- **Time is in stage ticks**, about 776 per second; entering a stage takes about
  1150 ticks. Most stages end around 24576–32768.
- **scenery_patterns**: one 16x16 grid lasts about 992 ticks; its last row
  repeats. Columns can appear mirrored on some donors — check a capture.
- **Slot 1** starts with the carrier catapult sequence; **refuel donors**
  (3, 9, 11, 16, 19, 21) and **landing donors** (5, 13) run those sequences
  first.
- **Packs must stay clean**: relative asset paths only (no `..`), original art
  and music generated or supplied by the user, never ROM dumps or captures.
- **Mutators** that names don't recognize are an error at load; the valid set is
  night, fog, storm, turbulence, flak_alley, ace_pilots, swarm, afterburner.
- **Interactive rearm** (landings, refuels) is a global setting, on by default;
  landing/refuel donors become flyable sequences, not cut scenes.

## Reporting back

Say what the stage is, how to launch it, what you verified (validator clean,
which frames you looked at) and what you didn't (nobody played it by hand,
audio not listened to). Offer the contact sheet path so the user can see it.
