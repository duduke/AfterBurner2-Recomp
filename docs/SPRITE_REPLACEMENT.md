# Registered HD player-aircraft prototype

Build and launch the opt-in neutral/gentle-bank pack:

```sh
python3 tools/workflow.py run native-release -j 4
./build/afterburner2-release --player-aircraft assets/player-aircraft/neutral-gentle-v1.aircraft
```

Omit `--player-aircraft` to use Original. The prototype is deliberately a command
line option while the asset format and animation coverage are being developed;
it does not change saved preferences. An unavailable or invalid image falls
back to Original. Normal coin/start, input and gameplay remain ROM-owned.

The pack supplies neutral flight and the first gentle bank at the same pitch.
The ROM reuses one bank image for both turn directions, with hardware reflection.
Both authored roots cover their five ROM LOD records, including reuse of those
poses in transitions. Other pitch/bank frames, carrier/crash art and unmatched
descriptors use the original artwork. Native afterburner flames remain separate.
This is not a complete player-aircraft remaster; switching between HD and native
art is visible outside these poses. The original single-image option remains
supported, with its neutral-only mapping and automatic alpha bounds:

```sh
./build/afterburner2-release --player-aircraft assets/player-aircraft/level-flight-hd-v1.png
```

## Registered pack format

A `.aircraft` file is UTF-8 text starting with `aircraft-pack 1`. Blank lines and
lines starting with `#` are ignored. Image paths are quoted and relative to the
manifest directory. The supplied [pack](../assets/player-aircraft/neutral-gentle-v1.aircraft)
is an editable example; it contains artist registration and ROM identifiers,
without ROM pixels or copied gameplay tables.

```text
pose ROOT "IMAGE" SX SY SW SH BASE_W BASE_H ART_PIVOT_X ART_PIVOT_Y NATIVE_PIVOT_X NATIVE_PIVOT_Y SCALE_X SCALE_Y
nozzle ROOT "NAME" ART_X ART_Y NATIVE_X NATIVE_Y
lod ROOT FRAME NATIVE_PIVOT_X NATIVE_PIVOT_Y SCALE_X SCALE_Y
```

`pose` defines the fixed source rectangle in image pixels, reference-frame
dimensions, a corresponding point in the image and reference frame, and image
pixels per reference-frame unit along each axis. These points determine stable
placement; alpha bounds are never recomputed for a pack. The supplied neutral
entry freezes the accepted PNG preview's registration. The bank source keeps
its authored canvas and explicitly positions its reference point.

`nozzle` annotates corresponding review points in image/reference coordinates.
It does not alter native flame positions. `lod` is optional and uses the matched
LOD's native source-pixel units. Missing overrides use the base pose's normalized
registration; an override whose pivot does not fit the actual ROM LOD also uses
that default. `nozzle` and `lod` rows follow their `pose`; duplicate roots or
annotation identities are rejected.

Loads are transactional: a failed image or manifest reload retains all previous
textures, registrations and admitted roots. Bounds are 64 KiB per manifest,
64 poses, eight nozzle annotations and 64 LOD overrides per pose, images through
4096×4096, and 32 million cached texture pixels per pack. Scale is finite and
within 1/4096..4096 pixels per reference unit. Paths cannot escape the pack
directory, including through symlinks. Software rendering counts all four cached
image orientations against the budget; those copies preserve HD linear sampling
when SDL's software reflection path would downsample a logical ownership row.
No image copying or orientation allocation occurs during gameplay rendering.
Software row endpoints also align to physical pixel ownership so fractional
window sizes do not leave cracks or blend adjacent strips twice.

## Identity and presentation contract

Main `$99BE..$9A48` selects signed bank `-5..5` and one of five pitch tables from
`$28C94`. The neutral pitch-2/bank-0 entry points to Sub `$100A0`; banks -1/+1
both point to `$10230`. The normal
aircraft palette is `$F2`; exhaust uses `$80`. The presentation adapter reads
the identity and LOD offsets from the user's ROM at runtime. It does not infer
identity from screen position, palette alone, or a sprite-list slot.

`WideScene` supplies a separate underlay, per-clip aircraft rectangles and native
ownership masks. Accepted aircraft descriptors are omitted from the underlay;
later sprites and tile/text priority clip the HD draw. Descriptor placement,
zoom and flips come from the original hardware list. Ambiguous/unsupported
descriptors fall back to native art. Fullcolor replacement art cannot reproduce
palette effect pens; overlapping later shadow effects, or later effects whose
translucency option is enabled, conservatively retain the native aircraft for
that clip. This avoids blending effects against an aircraft-free underlay.
With detached Sharp/Custom HUD text, priority-3 aircraft descriptors also retain
Original because authored glyph/aircraft alpha can differ from native coverage.
The observed active-flight aircraft uses priority 1 and remains supported.
The original board RGB, audio, timing and
sprite feedback remain authoritative and unchanged.

The frontend draws RGBA art at output resolution before CRT presentation.
Pack source rectangles preserve fixed authored padding; only the legacy image
option auto-trims alpha. RGB is premultiplied before filtering to retain
antialiased edges. The authored PNG is preserved.
This RGBA prototype uses its authored colors; it does not reproduce native
aircraft palette animation or recoloring.
The default registration scales to each native LOD extent. Production work
still needs nozzle/pivot review and appropriate LOD art, rather than assuming
one image is final at every size. Aircraft whose complete rectangle crosses
the native 320-column safe area retain Original: the present HD ownership mask
cannot represent a widescreen overhang without losing underlying margin pixels.

## Remaining work

- Review silhouette, shading and exhaust/nozzle registration in motion.
- Author the remaining pitch/bank frames and transitions as a consistent set.
- Complete the current pitch's bank row (three more source poses), then the
  remaining pitch rows; the 55 ROM selections reuse 22 distinct base frames.
- Extend effect-palette support, sustained-load and Windows/Linux acceptance.
- Broaden natural scene/overlap coverage; this prototype does not close the
  roadmap's whole-credit CPU/device/RGB/audio acceptance.

Art provenance and the exact generation prompt are in the
[asset directory](../assets/player-aircraft/README.md). Extracted ROM references
and captures stay under ignored `build/`, as required by the project policy.

## Validation and preview

```sh
python3 tools/workflow.py run test-native-player-aircraft-mutations test-player-aircraft-mutations verify-player-aircraft -j 4
python3 tools/workflow.py run verify-gpu-player-aircraft -j 4
python3 tools/workflow.py run preview-player-aircraft -j 4
```

The preview command replays physical inputs and saves the first observed HD
neutral aircraft to ignored `build/oracle/player-aircraft-active-flight.bmp` and
both banks to sibling `-bank-unflipped.bmp` / `-bank-mirrored.bmp` files. It does
not seed the board or add synthetic aircraft. The recorded zero-based input
frames are 1098, 1928 and 2552. With the stick centered at other times, native
pitch selection can select another frame and therefore Original.

The local replay compares 3,000 input frames plus four restart frames against a
separate native session, with exact native RGB/PCM/time and zero skips. Original
sprite reconstruction at 320/358/398 uses the unchanged hardware renderer and
each partial frontier's palette. It observes both base frames in attract and
active flight; smaller LODs remain covered by generated direction/zoom fixtures
rather than claiming natural in-flight observations. Software and 4K Metal
tests cover alpha, flips, registered bounds/pivots, LOD overrides, masks and
invalid input. Retained mutations prove the gates reject faults.
See [engine evidence](ENGINE_NOTES.md#registered-aircraft-pack-and-gentle-bank-2026-10-23)
for logs and remaining acceptance scope.
