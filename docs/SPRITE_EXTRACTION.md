# Sprite extraction

Export the sprite frame records from your own extracted After Burner II ROMs:

```sh
python3 tools/workflow.py run extract-sprites
# Or choose a new output folder (must be under this repository's build/):
python3 tools/assets/extract_sprites.py --rom-dir roms --output build/sprites-review
```

The default output is `build/sprites/mapping.json` and one indexed PNG per
frame under `build/sprites/sprites/`. Existing output directories are refused
to protect edits. Choose another folder for subsequent exports. No third-party
Python packages or game build are required. The extractor needs the two Sub CPU
chips and sixteen sprite chips listed in [HARDWARE](HARDWARE.md).
ROM-derived images and mappings stay under ignored `build/`; never commit them.

The current ROM produces **2,931 structural frame-record candidates**. This is
a catalogue of candidate records, including smaller LOD art and aliases; it is
not a count of distinct gameplay objects. Discovery scans aligned 16-byte
records in Sub ROM `$10000..$1FFFF`, checks dimensions, bank bits, pitch and
reserved words, and rejects fill/nonrecords. It does not prove every record is
reachable or find art referenced outside this region. The decoded fields come
from Sub `$7B8..$9C8` and the existing sprite renderer, rather than visual guesses.

PNG indices preserve pens 1–14; transparent pens 0 and 15 become index 0. The
PNG palette is a **grayscale preview**, not the game's colors. Real colors are
selected dynamically by each sprite request and palette RAM. Pen 14 is retained
as an opaque index for inspection; its runtime shadow/highlight effect requires
the compositor. Images use the record width and last-row plus one, without
trimming or scaling. Rows follow the hardware format in its forward,
positive-pitch orientation, including end-word markers and 16-bit address wrap.
Runtime flips, zoom, shadows, palette animation and compositing are not baked in.

Each mapping entry contains:

| Field | Meaning |
| --- | --- |
| `id`, `frame_address` | Stable Sub CPU record identity, e.g. `sub_016500` |
| `image` | Relative path from the mapping file to the PNG |
| `bank`, `offset_words`, `pitch_words` | Original hardware asset location; offsets/pitches count 32-bit words |
| `width`, `height` | Export extent before runtime scaling/flips |
| `vertical_flip_offset_words` | Record's original vertical-flip adjustment |
| `pens_sha256` | Hash of uncompressed exported pen indices, row-major |
| `label`, `replacement` | Empty string/null for artist annotations and future replacement paths |
| `lod_candidates` | Candidate IDs at the signed offsets from the ROM LOD table |

The root records the schema, source filenames/sizes/SHA-256 hashes, discovery
limits, transparency conventions and all 64 LOD offsets read from Sub `$9E4`.
LOD links are arithmetic address candidates: only the game's original request
pointer establishes a true frame family. Names, animation sequences and palette
assignments remain unverified. Replacement fields are preparation for asset
authoring; the application does not consume this mapping as a replacement pack.
A separate opt-in [HD player-aircraft prototype](SPRITE_REPLACEMENT.md) now
maps the verified neutral pose and its LODs directly from the runtime ROM.

Validation:

```sh
python3 tools/workflow.py run test-sprite-extraction verify-sprite-extraction -j 4
python3 tools/workflow.py run oracle-video-rolls -j 4
```

The first target is ROM-free and part of `make test`. The second compares every
candidate's unscaled pens to the unchanged `HWSprites` renderer and rejects four
retained mutations (nibble order, end marker, row pitch, bank). Its reference
inputs/outputs remain under `build/oracle/sprite-extraction/`. The video oracle
independently checks the renderer against the retained MAME roll capture. This
validates pixel decoding, not candidate reachability, semantic labels or palette
assignment; it does not close whole-game fidelity acceptance.
