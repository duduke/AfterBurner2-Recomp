# Per-track music replacement

**Status: shipped in the native frontend; natural-context acceptance remains open.**
Updated **2026-10-15** against [v0.1.0-preview.1](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.1).
Six mappings, -12 dB balance and live active-song changes are implemented.
Original is the default for every track.

## Using the menu

1. Put WAV/FLAC files directly in `soundtrack/` under the startup working directory.
2. Open Options → Audio → Music Tracks; Left/Right cycles Original and files,
   Enter advances. The list refreshes on entry, sorts alphabetically and ignores
   subfolders. Extensions are case-insensitive; no file picker opens.
3. Space/gamepad North previews while paused. Space, leaving the menu or changing
   selection stops preview. Original is heard during gameplay.

The six tracks are listed in the command table below. Long filenames scroll;
spaces/Unicode are stored, though the ROM font has a limited character set.

| Change | Behavior |
| --- | --- |
| Current song's loaded mapping | Applies on resume; new file starts at sample zero with the current fade |
| Return to Original | Resumes the native sequencer's existing position |
| Inactive/stopped song's mapping | Applies at its next genuine native start; does not start music |
| Invalid selection | Keeps the prior choice and shows an error |
| Saved file missing/not ready at cue | Uses Original for that instance; retains the filename for recovery |
| All Original / Reset Volumes | Clears mappings / restores global and track volumes, respectively |

Music, effects and six track volumes persist at 0–100% in 5% steps, default 100%.
Zero mutes without stopping position. Volume changes ramp over 10 ms. Preview
and gameplay use the same gain: replacement music × 0.25 (-12 dB) × global music
volume × track volume × native fade. Original music/effects get no added baseline
attenuation. Global 50% × track 80% therefore means 40% of the source baseline.
This is fixed balance, not loudness normalization; the final PCM sum still clips
if it exceeds available headroom.

## Routing and lifecycle

The sound ROM contains commands and descriptors, rather than literal song titles.
The user-supplied labels are corroborated by the [SEGA soundtrack catalog](https://sst.sega.jp/catalog/a004.html)
and an [authored Hoot playback configuration](https://w.atwiki.jp/hoot2ch/pages/245.html).
The catalog maps these commands to stable settings keys:

| Command | Track | Settings key |
| --- | --- | --- |
| 0x92 | Maximum Power | maximum_power |
| 0x91 | Final Take Off | final_take_off |
| 0x94 | Red Out | red_out |
| 0x93 | Super Stripe | super_stripe |
| 0x95 | After Burner | after_burner |
| 0x96 / 0x97 alias | City 202 | city_202 |

### Sega Saturn (SEGA AGES) soundtrack tracklist

The Japanese Sega Saturn release of *SEGA AGES: After Burner II* (Catalog `GS-9109`,
September 27, 1996) is structured as a mixed-mode CD with a Mode 1 data track
(`Track 01`) and 17 Redbook CD-DA audio tracks (`Track 02` through `Track 18`).
These audio tracks correspond directly to the in-game Extended Music Test menu
(accessible via `R x 3` on the Saturn Options screen) across three sound suites:
- **REV.B / ARCADE** (Tracks 02-07): arcade-accurate arrangements without lead melody
- **REV.A** (Tracks 08-13): arrangements featuring lead melodies
- **AB1** (Tracks 14-18): original *After Burner 1* arcade sequences (omits *City 202*)

Because forward slashes (`/`) and double quotes (`"`) are reserved in Windows and
POSIX filenames, the recommended filenames replace the menu's slash with a hyphen
(` - `) and preserve the track numbers for sorted alphabetical display in the menu.

| Track | In-game sound test title | Recommended file basename | Duration | Target arcade cue |
| --- | --- | --- | --- | --- |
| 02 | `MAXIMUM POWER REV.B/ARCADE` | `Track 02 - MAXIMUM POWER (REV.B - ARCADE)` | 2:05 | 0x92 Maximum Power |
| 03 | `FINAL TAKE OFF REV.B/ARCADE` | `Track 03 - FINAL TAKE OFF (REV.B - ARCADE)` | 6:25 | 0x91 Final Take Off |
| 04 | `RED OUT REV.B/ARCADE` | `Track 04 - RED OUT (REV.B - ARCADE)` | 4:08 | 0x94 Red Out |
| 05 | `SUPER STRIPE REV.B/ARCADE` | `Track 05 - SUPER STRIPE (REV.B - ARCADE)` | 4:08 | 0x93 Super Stripe |
| 06 | `AFTER BURNER REV.B/ARCADE` | `Track 06 - AFTER BURNER (REV.B - ARCADE)` | 5:22 | 0x95 After Burner |
| 07 | `CITY 202 REV.B/ARCADE` | `Track 07 - CITY 202 (REV.B - ARCADE)` | 2:39 | 0x96 City 202 |
| 08 | `MAXIMUM POWER REV.A` | `Track 08 - MAXIMUM POWER (REV.A)` | 0:48 | 0x92 Maximum Power |
| 09 | `FINAL TAKE OFF REV.A` | `Track 09 - FINAL TAKE OFF (REV.A)` | 4:14 | 0x91 Final Take Off |
| 10 | `RED OUT REV.A` | `Track 10 - RED OUT (REV.A)` | 3:14 | 0x94 Red Out |
| 11 | `SUPER STRIPE REV.A` | `Track 11 - SUPER STRIPE (REV.A)` | 3:28 | 0x93 Super Stripe |
| 12 | `AFTER BURNER REV.A` | `Track 12 - AFTER BURNER (REV.A)` | 5:22 | 0x95 After Burner |
| 13 | `CITY 202 REV.A` | `Track 13 - CITY 202 (REV.A)` | 1:40 | 0x96 City 202 |
| 14 | `MAXIMUM POWER AB1` | `Track 14 - MAXIMUM POWER (AB1)` | 0:52 | 0x92 Maximum Power |
| 15 | `FINAL TAKE OFF AB1` | `Track 15 - FINAL TAKE OFF (AB1)` | 4:14 | 0x91 Final Take Off |
| 16 | `RED OUT AB1` | `Track 16 - RED OUT (AB1)` | 1:42 | 0x94 Red Out |
| 17 | `SUPER STRIPE AB1` | `Track 17 - SUPER STRIPE (AB1)` | 1:48 | 0x93 Super Stripe |
| 18 | `AFTER BURNER AB1` | `Track 18 - AFTER BURNER (AB1)` | 5:01 | 0x95 After Burner |


`MusicRouting` reads descriptor roots from ROM at runtime. It observes the actual
sequencer initialization read at sound PC $0C76, so requests postponed by the
native driver do not start replacements early. Each descriptor initializes eight
FM and eight PCM music records. Primary PCM channels 0–7 are music; channels 8–15
are effects. FM ownership follows actual aligned sequencer records on per-channel
register/key-on writes, including effects stealing a physical music voice.

The adapter adds optional presentation stems and typed start/stop/fade events.
It does not filter commands, write native state, change clocks, or advance the YM
chip twice. Original native audio remains available. All-Original/100% playback
bypasses stem recombination and preserves exact existing PCM; independent YM
channel projections can otherwise differ in final DAC rounding.

A replacement begins at frame zero on the actual start, aligned to the host's
48 kHz sample timeline. The selected file repeats at EOF while that native music
instance remains active. This is a frontend file-loop policy, rather than a claim
that every native song has the same loop structure. Native stop prevents original
music release tails from leaking into replacement playback. Primary fade
attenuation controls replacement gain. Switch/retrigger follows native starts.
Menus pause playback position; restart clears active playback and event history.
Final mixing clamps before signed PCM conversion.

## Decoding and persistence

WAV uses [SDL_LoadWAV](https://wiki.libsdl.org/SDL3/SDL_LoadWAV); FLAC uses the
pinned [dr_flac](https://github.com/mackron/dr_libs) decoder under MIT No Attribution.
Mono/stereo files decode asynchronously on a worker to immutable 48 kHz stereo
float data. FLAC STREAMINFO with unknown total frames is supported. Sample-rate
conversion uses [SDL_ConvertAudioSamples](https://wiki.libsdl.org/SDL3/SDL_ConvertAudioSamples)
once on each complete decoded file, avoiding streaming chunk boundaries.
Unsupported/invalid files produce a menu error. Limits are 256 MiB per input or
decoded file and 512 MiB for cached decoded data; temporary conversion buffers and
active shared files can add to process memory. No network access is needed.

Versioned `AB2AUDIO1` preferences are saved atomically beside native settings with
an `.audio` suffix. `--settings-file` relocates preferences, not the soundtrack
folder. Entries retain global gains, six track gains and quoted relative filenames.
Absolute paths, path separators, traversal and escaping symlinks are rejected.
Malformed entries fall back to defaults. Personal files, captures and generated
test audio remain ignored and are not distributed with the game.

## Validation and remaining coverage

Generated WAV/FLAC tones test lossless agreement, unknown-count FLAC, mono 24-bit
44.1 kHz conversion, folder order, persistence, invalid files, asynchronous choice,
independent mutes/gain multiplication, full-scale headroom, live mapping changes,
inactive/stopped edits, loops, stop/fade, preview, restart and Original fallback. Native menu integration checks selection and volume controls
without advancing the board. Two native sessions compare 781 RGB frames and
627,841 stereo audio frames with presentation capture enabled/disabled, including
exact all-Original frontend output.

Independent passive MAME captures compare 42,148 observations in 781 frames and
119,374 in 1,800 frames, with zero skips. The longer physical-input recording
includes 472 actual FM effects channel writes. Retained negative controls target
wrong song mapping, lost sequencer starts, FM effects classified as music,
lost PCM percussion, bypassed replacement attenuation and deferred live selection. Original speaker/joint-audio and whole-host gates remain
required, alongside the complete regression/mutation profile for shared changes.

Natural recordings exercise Maximum Power and Final Take Off, shooting and
music/effects overlap. The other four song descriptor layouts are ROM-checked,
but their natural stage/ending/game-over/ranking contexts remain unverified.
Replacement fade is unit-tested; these natural captures contain no fade updates.
This feature does not establish full-game audio or progression acceptance.

## Current checkpoint and next work

The fresh complete `required` profile, supplemental `full` targets and optimized
build pass at the [macOS release checkpoint](ENGINE_NOTES.md#macos-preview-packaging-2026-10-15),
including the balance/live-selection corrections and all six music controls.
The packaged launch also activates OpenAL headphone HRTF; physical mix/localization
and long-session acceptance remain separate. Earlier revision-specific routing,
build and smoke evidence remains in
[the music notes](ENGINE_NOTES.md#native-music-presentation-adapter-2026-10-13).

Next: capture the four remaining songs in their natural contexts and natural fade
updates, then extend listening/headroom and device/lifecycle checks. Keep the
existing two-song recordings and every retained control; descriptor-layout and
unit fade checks do not substitute for those missing natural contexts.
