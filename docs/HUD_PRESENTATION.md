# High-resolution HUD presentation

The local source offers **Options → Video → HUD → Original / Sharp 3x /
Custom**. Original is the default. Sharp 3x applies Scale3x to the ROM's
individual text tiles and draws them separately with linear sampling at the
window's output resolution. Custom uses larger user-supplied tiles. This
improves text-plane artwork without changing the arcade's score, timing,
instrument values or layout. Sharp 3x is filtered original art; it does not
add authored detail.

The SDL window requests high pixel density. Fullscreen uses the desktop's
resolution, including 3840×2160 on a 4K desktop. Letterboxing and Integer
scaling still apply; no fixed 4K intermediate buffer is allocated. The HUD
stays in the centered native 320×224 area in Original, 16:10 and 16:9 viewports.
Menus retain their ROM font and the existing logical mouse coordinates.

## Custom artwork

Provide a PNG RGBA atlas arranged as **16 columns × 32 rows**, with square
tiles between **16 and 128 pixels** per side. Tile code `n` is at column
`n % 16`, row `n / 16`. All 512 text codes are addressed directly; there is
no inferred Unicode, score or instrument mapping.

The red byte of each nontransparent pixel must be a native **pen index 1–7**;
green/blue bytes are ignored. Alpha supplies coverage, including antialiasing.
Transparent pixels may have any RGB value. These are categorical palette
indices, not ordinary full-colour artwork: the live native palette supplies
each pen's RGB colour, blink/fade and shadow-or-highlight variant. An entirely
transparent tile falls back to the original ROM tile with nearest sampling.
An authored tile replaces its complete 8×8 logical cell, preserving its pivot.

Launch with:

```sh
./build/afterburner2-release --hud-atlas /absolute/path/hud.png
```

This selects Custom for that launch. When a Video setting is saved, `hud=custom`
and `hud-atlas=/absolute/path/hud.png` persist in the normal settings file.
Alternatively place the atlas next to that file as `settings.cfg.hud.png` and
select Custom. With `--settings-file build/my.cfg`, that default is
`build/my.cfg.hud.png`. A missing or invalid startup atlas falls back to Sharp
3x and prints its error. The menu cycles between Original and Sharp 3x until a
valid custom atlas is loaded. Restart the application to
load changed artwork. Atlas validation is transactional; malformed dimensions
or pen indices cannot replace an accepted atlas.

No replacement art is distributed. ROM-derived glyph caches and test images
remain runtime/local outputs; ROM data and captures must never be committed.

## Ownership and rendering

The optional path consumes the existing immutable partial-render scene clips.
`WideScene` reconstructs the underlay without text, and exposes visible text
entries, palette pens and a native-coordinate sprite ownership mask through
`HudScene`. It never changes the hardware renderer, CPU routines, bus or native
frame. Native-width HUD presentation uses a 320-wide detached scene with
extended visibility disabled; wider viewports retain their existing scenery.

Each partial clip retains its own text and palette, even when a frontier cuts
through a glyph. Category-1 text remains above sprites. For category-0 text,
the native top priority-3 sprite either covers the glyph or selects the palette
effects pens. Display-disabled rows omit HUD artwork. The frontend clips custom
art to these same pixel boundaries and batches unobstructed cells. Palette-keyed
glyph textures are bounded to 1,024 entries / 32 MiB. Enabling while paused waits
for the next completed board frame; changing modes on a captured frame rebuilds
the detached composition.

This is text-plane ownership, including the HUD artwork actually published
there. Sprite-based gauges, target markers and other instruments keep their
native assets. Semantic instrument replacements, new layouts/edge anchoring,
production art, exhaustive scene coverage, other-platform high-DPI acceptance
and sustained 4K performance remain open.

## Validation

```sh
python3 tools/workflow.py run test-hud-mutations test-native-hud
python3 tools/workflow.py run verify-hud-presentation verify-native-menu
python3 tools/workflow.py run verify-gpu-hud verify-gpu-native-menu native-release
```

These supplement the normal `test` / `verify-stage1` cadence; they do not replace
the complete regression gate for a merge or milestone. The synthetic scene
checks cover text palette/priority, effects, frontier splits, display disable,
reset and same-frame mode changes, with retained rejected mutations. The native
integration compares original versus captured RGB/PCM/time and independently
reconstructs native text from planar ROM pixels at all three widths. Software
and real GPU checks exercise custom atlas validation/fallback, palette changes,
occlusion and output layouts. Detailed local results live in
[ENGINE_NOTES](ENGINE_NOTES.md).
