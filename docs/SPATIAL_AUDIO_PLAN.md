# OpenAL Soft cockpit audio

**Status: experimental implementation shipped in the macOS preview.**
Updated **2026-10-19** against the local source after
[v0.1.0-preview.2](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.2).
Original SDL remains the
default native PCM reference. The enhanced path changes playback, never the
Z80 program, chip clocks, game logic or exported native audio.

## Using it

Open **Options → Audio → Output**. Profile, Device and Interpolation changes
apply live and persist in `.output` preferences. Resampler choices come from the
installed Soft implementation; Default uses its default. Original SDL displays
Native interpolation and SDL Default device. Speaker Test plays front-left,
front-right, rear-left, rear-right over three seconds. Menu exit/focus loss stops
preview/test playback. Music preview uses the selected backend.

| Profile | Playback |
| --- | --- |
| Original SDL | Unchanged 48 kHz native stereo mix, with existing music/volume controls |
| Headphones HRTF | Explicit HRTF request; four head-relative virtual speakers and direct stereo music |
| 2.1 Speakers | Stereo request, HRTF off; speakers/receiver handle bass management |
| 5.1 Speakers | Surround request, HRTF off; spatial sources render into the configured output channels |

The menu reports actual output. HRTF requests are hints: active state, detailed
status and selected dataset are queried and logged through `SDL_Log`. Missing
Soft/device/context support falls back to SDL; denied HRTF and unavailable 5.1
are reported. Device disconnect recovers to default SDL. Re-select an output
profile/device to retry Soft. 2.1 has no separate OpenAL output enum. There is no
synthesized LFE feed; 5.1 needs a configured surround endpoint.

## Virtual cockpit routing

The listener is at `(0,0,0)`, facing `(0,0,-1)` with up `(0,1,0)`. Every source
uses `AL_SOURCE_RELATIVE = AL_TRUE`. Distance attenuation and Doppler are off.

| Speaker | Position | Assigned effects |
| --- | --- | --- |
| Front-left | `(-0.5, 0, -0.866)` | Classified tactical/cannon/missile voices, native left gain |
| Front-right | `(0.5, 0, -0.866)` | Same class, native right gain |
| Rear-left | `(-0.5, 0.5, 0.866)` with headphones; ear height with speakers | Classified propulsion/crash/explosion voices, native left gain |
| Rear-right | `(0.5, 0.5, 0.866)` with headphones; ear height with speakers | Same class, native right gain |

Categories follow completed native sound-sequencer initialization and physical
voice writes, including FM voice stealing and direct PCM sample allocation.
Missile/explosion command tables are read from the user's ROM at runtime.
Unclassified voices stay audible on a direct stereo bus. Further voice clips and
sound contexts need classification; no effect is replayed from a haptic cue or
invented sample. Speaker profiles use equal-power pair normalization. Headphones
retain each native left/right effect channel's full level before HRTF filtering,
avoiding the extra 3 dB reduction that made classified effects quieter than the
direct stereo reference.

The remaining confirmed routing gaps are:

| Sound or identifier | Current status |
| --- | --- |
| Left/right passing sounds (`0x6E`, `0x6F`) | Direct stereo; verified passing-sound producer, cockpit assignment pending |
| Refueling/departure cue (`0xEF`) | Direct stereo; sequence producer verified, audible identity pending |
| Commands `0x7E`, `0x9A`, `0xD8` | Observed in the 1,800-frame sound capture; names and routing pending |
| PCM sample IDs `0x06`, `0x26`, `0x28` | Observed direct allocations without a classified owner; origin and routing pending |

This is the confirmed gap list, not a complete sound catalog. Command IDs and
PCM sample IDs use different namespaces; they are not necessarily distinct
audible effects. Unclassified sounds remain audible. Music's direct stereo path
is intentional and is excluded from this list.

These are **requested virtual positions**, not measured cabinet geometry. The
1987 After Burner Deluxe Type manual (420-5812), wiring diagram pp. 63–64,
labels **REAR SP** and **FRONT SP ×2**. Its parts list names a 10 cm, 8 Ω,
15 W speaker in the cabinet roof and another in the lamp box, plus a phone-jack
board in the front cabinet. This supports a raised rear image for headphone
presentation, but does not establish exact listener angles, independent board
channels or the wiring of an After Burner II revision. The HRTF profile retains
left/right rear separation as an optional presentation choice; it does not claim
that the Deluxe cabinet had two rear speakers. The native board reference remains
stereo. The scanned diagram's conductor labels are too unclear to verify its
exact pin-to-speaker routing, so that electrical mapping remains open.

## Interpolation and music

Passive taps retain **62,500 Hz FM** and **31,250 Hz PCM** before the MAME reference
resampler. OpenAL queues mono S16 effects at their chip rates, using separate
FM/PCM sources at each of the four positions. Thus its selected resampler performs
real chip-rate conversion, rather than processing an already-resampled mix.
Related sources start together; stream history survives successive buffers.

Native FM/PCM music has separate chip-rate stereo sources. Replacement WAV/FLAC
music retains the established 48 kHz decoder, looping, fades, live mappings,
independent gains and -12 dB baseline. Music uses `AL_FORMAT_STEREO16` and
`AL_DIRECT_CHANNELS_SOFT`: remix unmatched channels when the remix extension
exists, otherwise the base extension's direct routing. Without either, playback falls back to SDL
to preserve dry stereo music. Direct music bypasses positional HRTF;
it does not create rear surround music.

Native effects do not require WAV files. `load_openal_wav` provides the future
asset path: `SDL_LoadWAV`, then complete-file `SDL_ConvertAudioSamples` to mono
`SDL_AUDIO_S16` for positional effects, or stereo S16 for music. It validates
format/rate/frame sizes, bounds decoded allocations and uses RAII `SDL_free`
cleanup. Continuous chip output does not use one-shot SDL conversion per block.
The project retains SDL3 3.4+; these loading/conversion APIs exist since 3.2.

## Build and playback ownership

```sh
tools/audio/build_openal_soft.sh
python3 tools/workflow.py run native-release -j4
```

The builder pins Soft 1.25.2, checks its archive SHA-256 and builds under ignored
`build/deps/` with embedded HRTF data. Make auto-detects that installation or
`pkg-config`; `OPENAL=0` retains SDL-only builds. CMake finds the Soft package or
`pkg-config`; `-DAB2_ENABLE_OPENAL=OFF` disables it. Windows CMake builds copy the
Soft DLL beside the executable. macOS uses Soft/CoreAudio, not Apple's legacy
OpenAL framework. Retain [dependency notices](THIRD-PARTY-NOTICES.md) when packaging.

One playback thread owns SDL streams and the OpenAL context/sources/buffers.
SDL subsystem initialization/teardown stays on the main thread. Bounded queues
apply backpressure outside board execution. Pause, restart, device/profile
changes and shutdown stop/clear the appropriate playback state. Unsupported
output leaves the game running. Source underruns are logged and restarted;
sustained latency/performance acceptance remains open.

## Current release checkpoint

The macOS ZIP bundles OpenAL Soft 1.25.2 with embedded HRTF data, SDL3 3.4.16,
complete sources and notices. The final extracted-package 240-frame smoke
reported Metal/VSync and active headphone HRTF through CoreAudio. This proves
initialization/playback, not measured front/rear localization or physical 5.1.
The complete `required` and supplemental `full` gates pass; see
[release evidence](ENGINE_NOTES.md#macos-preview-packaging-2026-10-15).
No new classification or cabinet-wiring acceptance is implied by that release.

## Validation and remaining acceptance

Passed checks include mono/stereo WAV conversion, actual headphone HRTF,
left/right/front/rear loopback output, direct stereo music, chip-rate resampling,
live 2.1/5.1 switching, pause/clear and persisted/menu controls. Four retained
playback mutations reject swapped sides, disabled HRTF, stereo positional
buffers and spatialized music. Two ownership controls reject corrupted MAME
metadata and a detached native observer.

The independent MAME oracle matches **117,509/117,509 transactions/IX/DE/BC,
zero skips**, across 1,800 frames, with 15 observed owners and **943,466 exact PCM
voice sums**. Export-on/off integration preserves **1,800 native RGB/PCM frames**,
including pause/restart and live capture toggles. Native-to-Soft loopback also
renders 1,448,641 stereo output frames without clipping while preserving unity
native PCM. Full-suite, Make/CMake and finite launch results are
recorded in [ENGINE_NOTES](ENGINE_NOTES.md). ROMs/captures remain ignored.

Remaining work:

- Physical headphone localization/balance and 2.1/5.1 channel tests on Windows,
  Linux and macOS; avoid stacking OS spatial processing during calibration.
- Complete native voice classification, all six music contexts and exact Deluxe
  cabinet pin-to-speaker and After Burner II revision audit.
- Sustained/peak-load latency, underrun recovery, device-loss and long-session
  performance coverage.
- Optional discrete LFE crossover/routing, replacement-effect selection and
  preservation of external tracks' original rates through OpenAL.

On 2026-10-18, the macOS software recheck passed Soft loopback, four retained
playback mutations, 1,800-frame game playback, independent MAME routing
(117,509/117,509 observations, zero skips), and ownership mutations. The user
reported that the Speaker Test sequence sounded correctly directed through
headphones and the Mac's built-in stereo speakers. In a stage with Headphones
HRTF, the user also reported that cannon/missile sounds felt in front and
engine/crash sounds behind, without an obvious imbalance or delay. This is
headphone listening and stereo left/right evidence; no physical 2.1/5.1 rear-channel or
Windows/Linux speaker test was performed. Logs:
`build/workflow/20261018T165843.993427Z/` and
`build/workflow/20261018T165921.689316Z/`.
A 3,600-frame Stage 5 Metal/16:10/HRTF window holds 59.6374 simulation and
display FPS across 25 reported intervals, with no logged stream underrun;
`build/oracle/spatial-audio/stress-stage5.log` retains the run. This bounded
window does not close long-session or device-loss acceptance.

On 2026-10-19, the headphone HRTF rear image was raised from below ear level to
above and behind, following the manual's rear/roof speaker evidence. 2.1 and
5.1 rear sources use ear height. The four-cue Speaker Test and the earlier user
listening result preceded this position change; physical listening of the
revised build is still required. The spatial loopback, four retained playback
mutations, 1,800-frame game playback, 117,509/117,509 MAME ownership oracle,
and native release build pass at
`build/workflow/20261018T220405.403751Z/`. The log directory uses UTC and
therefore carries a 2026-10-18 timestamp for this 2026-10-19 local update.

After the user reported quiet gunfire, missiles and explosions with weak
placement, the headphone-only positional pair normalization was removed on
2026-10-19. In the same 1,800-frame loopback scenario, rendered squared-sample
energy rose from 3,428.69 to 3,895.41 (native reference 4,933.92), with zero
clipped samples. This is a signal-level check, not a perceptual loudness measure.
The general test suite, game playback, independent MAME routing oracle and
native release build pass at `build/workflow/20261019T035751.967100Z/`.
The user selected Headphones HRTF with External Headphones and confirmed that
gunfire/missiles were louder and more clearly in front, and explosions/crashes
were more clearly behind in the revised build. This closes the reported Mac
headphone level/localization issue for that device, not the electrical cabinet
pinout, independent 2.1/5.1 hardware or other-platform listening checks.

## API references

- SDL: [LoadWAV](https://wiki.libsdl.org/SDL3/SDL_LoadWAV),
  [ConvertAudioSamples](https://wiki.libsdl.org/SDL3/SDL_ConvertAudioSamples).
- OpenAL Soft: [HRTF](https://openal-soft.org/openal-extensions/SOFT_HRTF.txt),
  [output modes](https://openal-soft.org/openal-extensions/SOFT_output_mode.txt),
  [direct channels](https://openal-soft.org/openal-extensions/SOFT_direct_channels_remix.txt),
  [resamplers](https://openal-soft.org/openal-extensions/SOFT_source_resampler.txt).
- [Sega Deluxe manual 420-5812, first printing](https://arcarc.xmission.com/PDF_Arcade_Manuals_and_Schematics/After%20Burner%20Deluxe%20Type%20%28420-5812%201st%20Printing%29.pdf):
  retained audit source; After Burner II revision applicability is unverified.
