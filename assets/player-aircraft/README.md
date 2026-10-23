# Registered HD player-aircraft prototype

`level-flight-hd-v1.png` is newly generated transparent RGBA replacement art,
created with the built-in ImageGen tool on 2026-10-23. It is an initial visual
prototype, not a complete aircraft animation set. No extracted ROM image or
capture is distributed here. Local indexed art was used only as a pose reference.

`gentle-bank-hd-v2.png` adds the first clockwise bank of the same aircraft.
The ROM supplies the reflected direction from that one source. It was created
with built-in ImageGen and refined against the local native pose.
`assets/player-aircraft/gentle-bank-prompts.txt` in the repository and included
project source archive records the exact prompt set for both passes.

`neutral-gentle-v1.aircraft` declares fixed bounds, pivots, scale and nozzle
guide points for both poses. The neutral registration freezes the approved
single-image preview's source bounds; pack loading never auto-trims alpha.
Nozzle guides are artist review annotations and do not move the native flames
or change any gameplay attachment. Silhouette and nozzle registration still
need broader in-motion review before production acceptance.
See [usage and scope](../../docs/SPRITE_REPLACEMENT.md).

Neutral generation prompt (built-in tool, transparent background):

```text
Use case: style-transfer
Asset type: HD replacement game sprite, single player-aircraft neutral level-flight rear-view pose for After Burner II.
Input image 1 is the exact pose/silhouette/proportion reference only. Create newly painted high-resolution artwork of this F-14-like twin-tail, twin-engine variable-sweep-wing fighter from directly behind, exactly level symmetrical rear chase camera, matching the reference projection: very wide thin wings, two tall vertical fins, paired round exhaust nozzles, center fuselage ahead of nozzles, visible small upper surfaces. Preserve wing sweep, horizontal wingspan-to-height ratio about 120:47 and attachment/nozzle locations. Clean polished arcade remaster aesthetic: light gray naval metal, restrained panel details, charcoal nozzle rings, subdued warm engine cores, subtle dimensional shading, crisp anti-aliased silhouette. Wings extend nearly edge-to-edge, both fin tips and both wings visible with a narrow transparent margin. Keep the aircraft centered in a wide landscape canvas close to 120:47 aspect ratio (e.g. 1536x640), matching original aircraft orientation and silhouette; do not stretch its proportions to fill a square. True transparent background, no ground, sky, smoke, trails, separate exhaust flames, cast shadow, labels, lettering, watermark, border, other aircraft or scene. Single isolated complete aircraft ONLY. Do not include the low resolution reference in the output.
```
