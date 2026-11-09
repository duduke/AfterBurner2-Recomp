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
- **Declarative Screen & World Layers**: Sorted by `order`, supporting `screen` and `world` projection spaces (`rectangle` and `image` elements).

---

## 3. Running Custom Stages

Launch a built-in preset or JSON stage file directly from `afterburner2`:

```bash
# Built-in presets:
./afterburner2 --custom-stage refuel
./afterburner2 --custom-stage carrier-landing
./afterburner2 --custom-stage runway-landing
./afterburner2 --custom-stage assets/custom_stages/player-runway-landing.json
./afterburner2 --custom-stage jungle-landing
./afterburner2 --custom-stage runway-takeoff
./afterburner2 --custom-stage carrier-takeoff
./afterburner2 --custom-stage jungle-takeoff
./afterburner2 --custom-stage canyon-combat

# Load a declarative JSON stage file:
./afterburner2 --custom-stage assets/custom_stages/twilight-trench-run.json
./afterburner2 --custom-stage assets/custom_stages/canyon-combat.json
./afterburner2 --custom-stage assets/custom_stages/layered-runway.json
```

During play, press `F5` to retry the active custom stage from a fresh board start.

---

## 4. JSON Stage Format & Schema

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

## 5. Verification & Mutation Controls

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
