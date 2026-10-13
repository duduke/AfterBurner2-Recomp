#!/usr/bin/env python3
"""Intentionally refresh the pinned offline FLAC decoder and its license."""
from urllib.request import urlopen
from pathlib import Path
import json,hashlib
sha=json.load(urlopen('https://api.github.com/repos/mackron/dr_libs/commits/master'))['sha']
p=Path(__file__).resolve().parents[2]/'src/third_party/dr_flac'
for name in ['dr_flac.h','LICENSE']:
    (p/name).write_bytes(urlopen(f'https://raw.githubusercontent.com/mackron/dr_libs/{sha}/{name}').read())
(p/'README.md').write_text('# dr_flac\n\nUnmodified from https://github.com/mackron/dr_libs\nCommit: '+sha+'\nLicense: choose MIT No Attribution (see LICENSE and header).\nSHA256: '+hashlib.sha256((p/'dr_flac.h').read_bytes()).hexdigest()+'\n\nUsed solely to decode user-provided replacement music. No soundtrack files bundled.\n')
print(sha)
