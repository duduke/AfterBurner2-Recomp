#!/usr/bin/env python3
"""Vendor a pinned, unmodified SDL community mapping database and license."""
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen
sha = json.load(urlopen('https://api.github.com/repos/mdqinc/SDL_GameControllerDB/commits/master'))['sha']
p = Path(__file__).resolve().parents[2] / 'src/main/frontend/controllerdb'
p.mkdir(exist_ok=True)
for name in ['gamecontrollerdb.txt', 'LICENSE']:
    data = urlopen(f'https://raw.githubusercontent.com/mdqinc/SDL_GameControllerDB/{sha}/{name}').read()
    (p / name).write_bytes(data)
(p / 'README.md').write_text('SDL_GameControllerDB\n====================\n\nUnmodified files from https://github.com/mdqinc/SDL_GameControllerDB\nCommit: ' + sha + '\nLicense: zlib (see LICENSE).\nDatabase SHA256: ' + hashlib.sha256((p / 'gamecontrollerdb.txt').read_bytes()).hexdigest() + '\n\nLoaded through SDL3 before device discovery. No runtime network access.\n`database.inc` is a generated C++ raw-string wrapper of the unmodified database.\nRefresh intentionally with `python3 tools/input/vendor_controllerdb.py`.\n')
print(sha)

(p / 'database.inc').write_text('R"AB2DB(' + (p / 'gamecontrollerdb.txt').read_text() + ')AB2DB"\n')
