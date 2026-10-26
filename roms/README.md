# ROM setup

This directory is where the program looks for the original After Burner II
arcade ROM data at runtime. Nothing here is redistributed — you must supply
your own legally-obtained dump.

Source builds can read `roms/aburner2.zip` directly, without extracting it.
If the default ROMs cannot be loaded, the game opens a native file picker for
an `aburner2` ROM ZIP. A successful selection is remembered beside the frontend
settings as `settings.cfg.roms`; the archive stays in its original location.
Cancel the picker to exit. An incomplete or invalid archive shows an error and
lets you choose again. ZIPs may contain a folder prefix; duplicate chip names,
encrypted entries and compression other than stored/Deflate are rejected.
Each chip must still match its expected filename, size and CRC32.

For command-line use, `--roms /path/to/aburner2.zip` selects an archive and
`--roms /path/to/extracted-directory` selects extracted files. Bounded
`--frames` runs report missing ROMs and exit without opening a picker.
The default `roms/` directory takes precedence over a remembered ZIP; loose
chips take precedence over matching chips in `roms/aburner2.zip`.

Alternatively, extract the contents of a MAME `aburner2.zip` set directly into this
directory (flat, no subfolders):

```bash
unzip /path/to/aburner2.zip -d .
```

Individual ROM files are matched by filename and verified by CRC32 — see
[roms.txt](roms.txt) for the authoritative list, sourced from MAME's
`segaxbd.cpp` driver (`ROM_START( aburner2 )`).

Everything in this directory except `README.md` and `roms.txt` is
gitignored, since arcade ROM dumps are copyrighted and must never be
committed.
