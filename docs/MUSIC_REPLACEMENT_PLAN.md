# Per-track music replacement

Status: implemented in the native SDL3 frontend; validation coverage and remaining
music contexts are listed below. Original is the default for every track.

## Using the menu

Place your WAV/FLAC files directly in `soundtrack/` beneath the startup working
directory. Open Options → Audio → Music Tracks. Left/Right cycles backward/forward
through Original and a stable alphabetical file list; Enter advances. No file
picker opens. The folder refreshes when entering this submenu. Subfolders are
excluded; extensions are case-insensitive. A missing folder leaves Original
available and displays a notice. Filenames, rather than list indices, are saved.

The six rows are Maximum Power, Final Take Off, Red Out, Super Stripe, After
Burner and City 202. Long filenames scroll in the detail footer. Spaces and Unicode
filenames are supported in storage; the existing ROM menu font has a limited
character repertoire. Space, or gamepad North, previews the selected file while
board execution is paused. Space stops preview; leaving the menu also stops it.
Original preview is heard during gameplay.

A mapping change applies at the next genuine native music start. Invalid files
retain the prior choice and show an error; cycling can proceed past them. If a
saved file is missing or not decoded when the cue arrives, that entire music
instance uses Original. The saved filename remains available for recovery.
All Original resets file mappings without resetting volumes.

Audio also offers Music Volume, Sound Effects Volume and Track Volumes. Each
volume ranges from 0–100% in 5% steps, defaults to 100%, and persists. Zero mutes
without stopping playback. Reset Volumes restores both global gains and all six
track gains. Music gain equals global music gain times the active track gain;
50% global and 80% track yields 40%. Effects gain is independent. Preview follows
the selected track and global music gains. Live changes use a 10 ms frontend gain
ramp and preserve music position.

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
independent mutes/gain multiplication, loops, stop/fade, preview, restart and
Original fallback. Native menu integration checks selection and volume controls
without advancing the board. Two native sessions compare 781 RGB frames and
627,841 stereo audio frames with presentation capture enabled/disabled, including
exact all-Original frontend output.

Independent passive MAME captures compare 42,148 observations in 781 frames and
119,374 in 1,800 frames, with zero skips. The longer physical-input recording
includes 472 actual FM effects channel writes. Retained negative controls target
wrong song mapping, lost sequencer starts, FM effects classified as music, and
lost PCM percussion. Original speaker/joint-audio and whole-host gates remain
required, alongside the complete regression/mutation profile for shared changes.

Natural recordings exercise Maximum Power and Final Take Off, shooting and
music/effects overlap. The other four song descriptor layouts are ROM-checked,
but their natural stage/ending/game-over/ranking contexts remain unverified.
Replacement fade is unit-tested; these natural captures contain no fade updates.
This feature does not establish full-game audio or progression acceptance.

Final validation on 2026-10-13: the complete `required` profile passed, along
with release/CMake builds, four targeted CTests, menu integration and finite
software/Metal smokes using generated replacements with muted gains. Existing
historical oracle skips retain their documented policies; the new routing and
presentation checks have zero skips. Details and local log location are recorded
in [ENGINE_NOTES](ENGINE_NOTES.md#native-music-presentation-adapter-2026-10-13).
