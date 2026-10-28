# OpenAL Soft cockpit audio

**Status: experimental implementation shipped in the macOS preview.**
Updated **2026-10-26** with the planned object-based HRTF work below. The
implemented playback checkpoint is recorded for
[macOS Preview 3](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.3).
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
| Rear-left | `(-0.5, 0.5, 0.866)` with headphones; ear height with speakers | Classified propulsion/crash/explosion/passing voices, native left gain |
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

Reviewed sound policies:

| Sound or identifier | Routing and evidence |
| --- | --- |
| Left/right passing sounds (`0x6E`, `0x6F`) | Rear; `$156F8..$1571C` emits after object depth crosses the player, preserving native stereo side |
| Enemy weapon launch (`0xD8`) | Front; `$14D96` follows enemy weapon/shadow allocation |
| Coin (`0x7E`), timer/flag cue (`0x9A`) | Intentional direct stereo; service label INSERTED COIN and producers `$C79C`/`$DC5E` |
| Carrier cue (`0xEF`, direct sample `0x46`) | Intentional direct stereo; sequence producer `$104EE`, audible identity pending |
| Direct sample `0x06` (`0xA5`) | READY voice, intentional direct stereo; stage-entry `$D804`, service VOICE table |
| Direct samples `0x26`/`0x28` (`0x4C`/`0x4E`) | FLIGHT 1 variants R2/L2, intentional direct stereo; startup `$BF56/$BF60`, service EFECT A table |

Command IDs and PCM sample IDs use different namespaces. Audible names for the
neutral carrier and timer/flag cues remain unverified. Other unreviewed sounds retain
native stereo; music always uses the direct stereo path.

These are **requested virtual positions**, not measured cabinet geometry. The
1987 After Burner Deluxe Type manual (420-5812), printed pp. 63–64 (PDF pages
66–67), shows **two rear and two front speakers**, each 8 Ω / 15 W. Rendering
the scan at higher resolution resolves the earlier ambiguous rear label.

| Connector | Left + | Left − | Right + | Right − |
| --- | --- | --- | --- | --- |
| Amplifier `839-0035-01`, Molex 8P | 1 (`A2`) | 2 (`92`) | 3 (`A5`) | 4 (`95`) |
| Rear `SGB-3824-36`, CP 4P | 1 | 2 | 3 | 4 |
| Front `SGB-3824-17`, CP 4P | 1 | 2 | 3 | 4 |

Both pairs share the same stereo amplifier nets; the front branch passes
mini-jack board `839-0018`. There is no independent electrical front/rear feed.
Roof/lamp-box mounting supports a raised rear image, while exact HRTF angles
and semantic effect placement remain presentation choices. The native reference
stays stereo. This verifies the documented 420-5812 wiring; unchanged wiring
in an After Burner II cabinet revision still needs primary evidence.

## Object-based HRTF (planned)

Added 2026-10-26. Extend the current fixed front/rear sound categories with
moving sources tied to verified game objects. This is an opt-in presentation
enhancement, not implemented or accepted by the existing cockpit audio tests.
Original SDL/native PCM remains the regression reference.

The engine has object activity, handler identity, screen positions, motion and
depth information, plus verified passing and weapon/explosion sound producers.
That supplies a starting point, not a uniform world-coordinate contract:
record fields are reused across object families, and projected screen X/Y
alone cannot establish a source's bearing behind the player. Verify coordinates,
player-relative depth, elevation and lifecycle for each supported family from
its ROM routines and independent MAME observations.

The main missing link is ownership from the producing object through the main
sound queue, Z80 allocation and physical FM/PCM voice. Current routing tracks
sound categories, not a persistent object-to-voice association, and combines
effects into category buses. Dynamic placement needs presentation-only ownership
metadata and separately renderable voices for supported effects. Retain native
trigger times, voice stealing, clocks and original stereo balance; do not assign
an ambiguous voice to whichever object happens to be nearest.

Suggested implementation order:

| Order | Effect | Intended presentation and prerequisite |
| --- | --- | --- |
| 1 | Missile flybys | Move a verified missile sound beside and behind the listener along its path. Start with the existing `$6E/$6F` depth-crossing events; trace their producing object and audible duration before claiming a continuous approach/pass trajectory. |
| 2 | World explosions | Place an explosion at its verified location instead of assigning every explosion to the rear. Distinguish world effects from player damage/crashes; evaluate elevation and restrained distance attenuation as explicit presentation choices. |
| 3 | Enemy weapon launches | Place the existing launch sound at the firing object's verified bearing instead of the fixed front category; trace source/shooter ownership through weapon allocation. |
| 4 | Player missiles | Start an owned sound near its verified launch point and move it forward with the projectile where its voice duration and ownership support that behavior. |
| 5 | Aircraft and rear threats | Track verified aircraft bearings, including behind-left/right for an actual pursuer. Establish the aircraft's relative position and chase state; a passing missile event is not evidence of a persistent chasing plane. |
| 6 | Player propulsion | Retain a stable rear cockpit anchor while external sources move independently; do not mistake a shared propulsion voice for an enemy engine. |

A continuous pursuing-aircraft engine requires an appropriate verified native
voice. If none exists, an added loop/sample is a separately specified optional
enhancement with explicit asset provenance and lifecycle rules; do not invent
native sound events or retrigger one-shots to imply a continuous engine.
Music and existing neutral UI/radio cues retain their current stereo policy.
Distance mapping, gain limits and any later Doppler treatment need explicit
presentation policy; the current implementation has attenuation and Doppler off.

- [ ] Verify per-family coordinate and lifecycle contracts, including offscreen
  sources, player depth crossings, attachments and object-slot reuse.
- [ ] Carry passive, timestamped object ownership through queue delivery,
  sequencer allocation, overlapping commands and voice stealing. Invalidate
  stale ownership on despawn/reuse, reset, restart and profile changes; retain
  the existing category/stereo fallback when ownership is unknown.
- [ ] Render supported voices separately with bounded source/queue costs and
  smooth movement aligned to audio playback time. Define the listener frame
  and camera/roll relationship explicitly; never feed presentation state back
  into game execution or native sound-device state.
- [ ] Add independent MAME ownership/trajectory fixtures and rejected mutations
  for wrong source, side/depth, stale identity and timing. Preserve all retained
  fixtures, mutations and native RGB/PCM/time comparisons; apply ROADMAP's
  focused/full validation cadence to the actual changes and acceptance gate.
- [ ] Exercise overlapping sources, pause/resume, clear/restart, voice stealing,
  fallback and starvation; measure clipping, latency and sustained peak-load
  cost. Obtain headphone listening checks for moving front/side/rear and
  elevation cues, with separate physical speaker/platform acceptance.

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
output leaves the game running. Global starvation re-primes streams together;
isolated source restarts remain immediate. Windows OpenAL now collects 80 ms
at startup and synchronized recovery, matching the SDL reserve and protecting
against the measured short frame stalls. This adds 55 ms of startup buffering
and 30 ms after starvation compared with the former 25/50 ms policy. Other
platforms retain 25 ms startup and 50 ms synchronized recovery. Native PCM and
emulated timing are unchanged; the reserve cannot fix sustained underproduction.

The default SDL path now primes 80 ms of native stereo PCM before starting and
re-primes after its usable output is exhausted. It counts software starvation
in `audio_underruns`/`audio_reprimes`; `audio_sdl_priming` reports the refill
state. These counters do not measure the output device's own buffer or prove
that hardware playback was glitch-free. Earlier zero SDL counters lacked this
starvation instrumentation. An atomic latch records SDL's actual unfulfilled
playback data requests; the playback owner handles recovery outside the audio
callback. Queue emptiness alone does not trigger recovery: the device may
still be playing prefetched audio. Resampling is covered by the SDL tests.
A finite short tail is flushed after 100 ms without producer data. No native
PCM is discarded, repeated or clocked at a different rate by this policy.

Host pacing retains a bounded 16-frame catch-up budget. The previous four-frame
cap could discard ordinary driver delays and permanently reduce PCM supply
relative to device playback. Every simulated frame still polls input and is
presented individually. Suspension catch-up stays bounded; native CPU/device
clocks and the native sample stream remain unchanged.

Focused checks are available through:

```sh
python3 tools/workflow.py run test-frame-pacing test-frame-pacing-mutations test-spatial-sdl-only test-sdl-recovery-mutations
```

## Preview 1 release checkpoint (2026-10-15)

The Preview 1 macOS ZIP bundled OpenAL Soft 1.25.2 with embedded HRTF data,
SDL3 3.4.16, complete sources and notices. Its extracted-package 240-frame smoke
reported Metal/VSync and active headphone HRTF through CoreAudio. This proves
initialization/playback, not measured front/rear localization or physical 5.1.
The complete `required` and supplemental `full` gates pass; see
[release evidence](ENGINE_NOTES.md#macos-preview-packaging-2026-10-15).
No new classification or cabinet-wiring acceptance is implied by that release.
The later integrated source and package checks are recorded in
[the Preview 3 release record](ENGINE_NOTES.md#preview-3-release-2026-10-23).

## Validation and remaining acceptance

Passed checks include mono/stereo WAV conversion, actual headphone HRTF,
left/right/front/rear loopback output, direct stereo music, chip-rate resampling,
live 2.1/5.1 switching, pause/clear and persisted/menu controls. Nine playback
mutations cover spatial placement, dry music, queue bounds, pause/recovery and
simultaneous/staggered starvation. Six ownership/reference controls cover
corrupted MAME metadata, observer detachment, routing policy and native PCM
identity. SDL-only playback is compiled and exercised separately.

The independent MAME oracle matches **117,509/117,509 transactions/IX/DE/BC,
zero skips**, across 1,800 frames, with 15 observed owners and **943,466 exact PCM
voice sums**. Export-on/off integration preserves **1,800 native RGB/PCM frames**,
including pause/restart and live capture toggles. Native-to-Soft loopback also
renders 1,448,641 stereo output frames without clipping while preserving unity
native PCM. Full-suite, Make/CMake and finite launch results are
recorded in [ENGINE_NOTES](ENGINE_NOTES.md). ROMs/captures remain ignored.

The 2026-10-22 playback checks add queued pause/resume, bounded producer
backpressure, SDL failure recovery/retry, injected disconnect, and synchronized
re-priming after a producer stall. `--profile` reports software queue times,
stopped-source counts and recoveries; device mix buffers and physical output
latency are excluded. The extended `verify-spatial-lifecycle` target passed all
four retained natural recordings: **104,452 native RGB/PCM/time frames identical**,
with no clipping or steady underruns. It observes all six native song contexts
and 633 fade updates. The reference is an untapped native board; this is not an
independent continuous whole-credit MAME comparison or replacement-file
listening check. A 600 sample-second, 13-source loopback also passes without
new underruns or clipping. A muted five-minute CoreAudio run recovered once
from a load stall without repeated restarts; software buffering peaked at
120 ms under concurrent tests.
Detailed results and source-specific logs belong in
[Engine Notes](ENGINE_NOTES.md#spatial-audio-routing-and-playback-acceptance-2026-10-22).
The complete local `required` gate and supplemental `full` profile pass on the
final 2026-10-22 shared source. Local software validation is complete within
this recorded scope; the external checks below remain open.

The user confirmed corrected headphone loudness and front/rear placement with
External Headphones on 2026-10-19, plus earlier Speaker Test directions on
headphones and the Mac's built-in stereo speakers. Those checks establish
headphone listening and stereo left/right. The user could not retest the new
passing/recovery changes on 2026-10-22.

Remaining external acceptance:

- Listening to the revised passing/enemy cues; physical 2.1/5.1 channel tests;
  Windows/Linux output and real unplug/reconnect checks. Avoid stacking OS
  spatial processing during calibration.
- Physical end-to-end latency and sustained peak-load checks across named
  devices. Offline sample-time tests do not establish hardware timing.
- Primary evidence for unchanged wiring in an After Burner II cabinet revision;
  audible identities of the intentional neutral cues and additional unobserved
  sound contexts.

Optional extensions: discrete LFE crossover, replacement-effect selection and
preserving external tracks' original rates through OpenAL. They are separate
features rather than gates for the current cockpit output.

## API references

- SDL: [LoadWAV](https://wiki.libsdl.org/SDL3/SDL_LoadWAV),
  [ConvertAudioSamples](https://wiki.libsdl.org/SDL3/SDL_ConvertAudioSamples).
- OpenAL Soft: [HRTF](https://openal-soft.org/openal-extensions/SOFT_HRTF.txt),
  [output modes](https://openal-soft.org/openal-extensions/SOFT_output_mode.txt),
  [direct channels](https://openal-soft.org/openal-extensions/SOFT_direct_channels_remix.txt),
  [resamplers](https://openal-soft.org/openal-extensions/SOFT_source_resampler.txt).
- [Sega Deluxe manual 420-5812, first printing](https://arcarc.xmission.com/PDF_Arcade_Manuals_and_Schematics/After%20Burner%20Deluxe%20Type%20%28420-5812%201st%20Printing%29.pdf):
  retained audit source; After Burner II revision applicability is unverified.
- [Sega service manual 420-5842-01](https://arcarc.xmission.com/PDF_Arcade_Manuals_and_Schematics/After%20Burner%20Service%20Manual%20%28420-5842-01%201st%20Printing%29.pdf),
  pp. 1–4: diagnostic version 2.00 and sound-test categories, corroborated by
  the ABII ROM's version text and command/display tables.
