# Donor stages

`source_stage` borrows a stage's ground scenery, palettes and special sequences;
`scenery_stage` borrows its air-enemy stream. What is known from captures and
the ROM's event streams (stage numbers are 1-based, as on the HUD):

| Stage | What it is | Notes |
| --- | --- | --- |
| 1 | Open sea | The run starts with the carrier catapult takeoff (mode 15); avoid slot 1 unless that is wanted |
| 2 | City and houses under an orange sky | Good base for night raids and urban looks |
| 3 | Night sea | Opens with an in-flight refuel |
| 4 | Ordinary combat stage | Used as the open-sea base for the naval-assault theme (palette patched to sea) |
| 5 | Autumn airbase | Landing, rearm and takeoff; the carrier-recovery base |
| 6 | Sky over clouds | Plain combat |
| 7 | Jungle | Plain combat; LEVIATHAN's base |
| 8 | Canyon run | Special canyon road; air enemies switched off (event 0 sub-code 12); Iron Trench / Urban Trench base |
| 9 | Combat | Opens with an in-flight refuel |
| 10 | Autumn countryside | Plain combat; storm-front base |
| 11 | Combat | Opens with an in-flight refuel |
| 13 | Jungle airstrip | Landing, rearm and takeoff |
| 16, 19, 21 | Combat | Open with an in-flight refuel |
| 17 | Canyon run | Like 8 |

Refuel donors: 3, 9, 11, 16, 19, 21. Landing donors: 5, 13. With Interactive
Rearm on (the default) those sequences are flown by the player.

When a stage's look is unknown, capture it first:
`afterburner2 --roms roms/aburner2.zip --stage N --frames 400 --frame-preview out.bmp`
(or a throwaway pack with that `source_stage` through `scripts/capture_stage.py`).

Stage lengths: most end with `stage_advance` at tick 32768 (stage 2: 28672,
stage 8: 24576, landing stages 5 and 13: 7936). About 776 ticks per second.
