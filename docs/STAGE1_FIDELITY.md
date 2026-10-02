# Stage 1 reference experience

PR #1 targets the Stage 1 appearance and playable experience selected by the
user from `Afterburner2.mov`. The video is reference material, not instructions.
Phase 3c runs native object logic at 30 Hz and display at 60 Hz. The temporary
native stage still clears after 3,600 logic ticks (7,200 display ticks). Enter resets
score, hits, ammunition, scenery, objects, palettes, and sound playback.

## Original data used at runtime

Observations were checked against the user's CRC-verified `aburner2` ROMs and
local MAME Stage 1 captures. Only table addresses/interpretation are recorded
here; copyrighted ROM bytes and captures are not included in the repository.

- Sub CPU sprite records are 16 bytes: width at +0, last source row at +2
  (height is that value plus one), bank bits at +4, offset at +6, stride at +8.
- Player attitude tables start at `0x10000`, tilt stride `0x190`, pitch stride
  `0x50`. Normal rear view selects `0x100f0`, bank 0, offset `0x0ab2`, 47 rows.
- Bank-3 Stage 1 canopy LOD records: `0x165f0`, `0x16600`, `0x16610`.
  Shore rocks: `0x16af0`, `0x16b00`. Sea patch: `0x15f60` (offset `0x201d`).
- Fighter LODs: `0x11a30` through `0x11a60`; fighter palette `0xcb`.
  Canopy palettes 0/`0x41`, rocks `0x10`, sea `0x23`.
- Player colors use palette `0xf2`, animated from main ROM `0x2bf5e` in
  32-byte frames. Reticle palette `0xf1` comes from `0x2ea14`.
- HUD palette sources `0x30b14`–`0x30bc4` and original text tile codes drive
  labels/numbers/icons; the text layer's visible origin is column 24.
- The Z80 runs the original sound ROM at 4 MHz. ROM `0000–efff`, PCM registers
  `f000–f0ff` mirrored through `f7ff`, RAM `f800–ffff`; mirrored YM ports
  `00/01`, command latch `40`, NMI until latch read, YM timer IRQ.
  Observed commands include music `91`, cannon `90`, missile `40`, explosion
  `4c/4e`, lock `e0/c1`, and engine `f1` followed by its speed parameter.

## Reconstructed behavior and limits

Phase 3b replaced the native terrain projection, sea-sprite sampling,
canopy/shore patches and speed-gauge fill with verified road/scenery ports.
Road scanlines, tile uploads and ground palettes now reach the hardware
compositor through `MainBus`. The host selects ordinary flight (`$10`);
the full credit/stage dispatcher is still pending 3e. Until the exact player
port in 3d supplies shared attitude targets, the native stick does not bank
or pitch this road. The integration test sets arcade targets directly.

Phase 3c replaced native depth sorting, hardware descriptor encoding and
palette timing. `OMainFrame` preserves both interrupt branches, pointer swaps,
mutex, lamps and callee order. Sprite requests pass through `$00B628`, the
verified sub transform and `$00B7C2/$00B7D2`; palette animation/fade follow
runtime ROM pointers. The sub CPU transforms after the logic phase and
latches/generates road on the next display phase, once per two display frames.

`OSprites` remains a temporary native producer adapter: it resolves exact
bank/offset/stride/row tuples to real ROM records, inverts the ROM zoom formula
for its linear scale branch, converts native top/left coordinates, truncates
native depth to the request byte, and uses emission-slot position history.
It omits unsupported tuples/scales. Per-native-sprite priority is discarded in
favor of the arcade global priority. This is not verified object behavior.
Host boot still chooses ordinary flight and a terminal airframe palette phase;
credits, compound player setup and service/attract dispatch remain 3d/3e.

Phase 3d now has independent checks for input mapping/buttons, live flight,
recovery and compound parts, player projectiles, reload/HUD, world projection,
ground effects, scaled player collision and a paired enemy missile/shadow/trail
graph, alongside the earlier dispatcher/RNG/sound ring. Observable ADC result
and timer/mux timelines also match MAME. Damage controllers through state 6,
fuselage animation and debris match 1,792/1,792 directed and 120/120 ordinary gameplay
MAME calls, with no skips. Damage companion, carrier/horizon, enemy tracking/burst/debris and remaining
world handler graphs now have separate routine checks. Refuel stage-flow and
a natural death sequence remain unverified. These ports remain unwired until
the actual host ADC/IRQ caller and paired player/weapon acceptance pass.
`OLevelObjs`, its request adapter and native sound cues therefore remain.
This foundation does not change the native flight or combat experience.

Formation cadence/trajectories, aiming, projectile motion, hitboxes, scoring
and the 3,600-logic-tick duration remain native rules. Original fighter art is
reused, but exact attitude selection/animation timing is still future work.
This is not yet a frame-identical whole-game arcade port.

The stage has fighter waves, target tracking, cannon/homing missiles, enemy
missiles, hits and temporary invulnerability. Surviving reaches a clear screen;
there is no lives/game-over loop or later-stage progression yet. Carrier
introduction, refueling, attract mode and all remaining stages are excluded.
Tilemap row/column fine-scroll is implemented and video-oracle verified. Sound output is exercised
with real ROMs, but exact original command timing, balance and waveform have
not been verified against arcade audio.

## Reproduce checks

From the repository root, supply the original ROMs described in `roms/README.md`:

```sh
make -j4
make test
make verify-stage1
make render-scene
./build/render_scene build/level.ppm level 480
./build/render_scene build/right.ppm right 480
./build/render_scene build/left.ppm left 480
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

The right/left visual fixtures set an arcade bank target directly, separately
from native controls, to exercise the hardware road path.

For memory checks, compile the integration suite with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and run from the root.
All generated captures and supplied ROMs remain ignored.

The Phase 3d damage companion continuation adds independent routine ports for
`$FB08/$FB16`, both allocated emission/animation graphs, removal and the
`$FBC4/$10310` emission-pool reset. These remain unwired, alongside the checked
pre-lives damage controller and debris routines. Their MAME checks do not
establish the host death/recovery sequence, sound cadence or player/weapon
30/60 Hz equality. Native `OLevelObjs` and its request adapter remain required.

The remaining handler graph continuation adds `OMainSequence`, tracking and
burst/debris enemy families, world variants and explosion setup. Directed
checks match MAME for 4,539 of 4,544 calls; five sub-input races are skipped. Projectile-hit checks stop
before scoring `$E542`; the refuel controller's first call `$E224` remains
Phase 3e. Carrier and refuel routine coverage does not establish stage flow.
Native flight, objects, sound cues and their host adapter remain in use.

[PR #11](https://github.com/duduke/AfterBurner2-Recomp/pull/11) is the authorized
handler-graph checkpoint. Its independent routine checks leave the native
Stage 1 experience unchanged. Phase 3d remains incomplete until the host
input/IRQ, sound cadence and paired player/weapon acceptance checks pass.

The comparator IRQ continuation verifies the full acknowledge/sample/start/
sound-consume chain against a continuous MAME hardware timeline: 7,058/7,058
calls and 45,176/45,176 events identical, including 946 sound-latch writes,
0 skipped. Separate physical axis endpoint and button mapping checks match
720/720 calls. This test supplies MAME bus timestamps and entry work RAM;
it does not independently schedule host IRQs or produce the sound ring from
ported gameplay. `OEngine` still runs native Stage 1 objects and sound cues.

The board-timeline continuation independently generates physical EXCK,
VBLANK and ADC deadlines from reset; 669,845/669,845 MAME probe events match,
0 skipped, and three mutations fail. Its frame period is 16.768 ms, derived
from the hardware clock. CPU transaction timestamps remain capture inputs;
The board oracle alone does not establish CPU IRQ service latency; the
bounded service check follows below. `OEngine` retains its native display-step
scheduling until host wiring and paired acceptance pass.
This checkpoint does not replace native controls, objects or sound cues.

The CPU service extension checks IRQ2/IRQ6 entry, ADC/sound accesses and
stack/return timings: 9,439/9,439 calls and 474,024/474,024 bus events match,
0 skipped, with seven mutations rejected. The unported main instruction's
last-bus boundary and entry context remain external inputs. Running
`./afterburner2` shows the existing Stage 1 experience; this timing port is
still separate from `OEngine`. Visible Phase 3d player/weapon changes require
host wiring and paired acceptance before the native producers are retired.
