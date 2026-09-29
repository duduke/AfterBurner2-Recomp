# ROM setup

This directory is where the program looks for the original After Burner II
arcade ROM data at runtime. Nothing here is redistributed — you must supply
your own legally-obtained dump.

Extract the contents of a MAME `aburner2.zip` set directly into this
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
