#!/usr/bin/env python3
"""Real provider-copy path and ZIP reader: hostile archives never publish content."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    music, shaders = root/'music', root/'shaders'
    source = root/'provider-object'  # URI basename is deliberately not the filename.
    def run(mode, source, destination, name='', valid=True):
        before = sorted(str(p.relative_to(destination)) for p in destination.rglob('*')) if destination.exists() else []
        result = subprocess.run([binary, mode, str(source), str(destination), name], capture_output=True, text=True)
        assert (result.returncode == 0) == valid, result.stdout + result.stderr
        if not valid:
            after = sorted(str(p.relative_to(destination)) for p in destination.rglob('*')) if destination.exists() else []
            assert before == after, (before, after)
        return [Path(p) for p in result.stdout.splitlines()]
    # Tiny valid PCM WAV; decoder remains responsible for full audio validation.
    wave = b'RIFF'+struct.pack('<I',38)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,8000,16000,2,16)+b'data'+struct.pack('<Ih',2,0)
    source.write_bytes(wave)
    first = run('music',source,music,'音楽.WAV')[0]
    second = run('music',source,music,'音楽.WAV')[0]
    assert first != second and first.read_bytes() == second.read_bytes() == wave
    for name in ['../escape.wav','bad.mp3','bad\\path.wav','', 'bad\n.wav']:
        run('music',source,music,name,False)
    source.write_bytes(b'not audio');run('music',source,music,'bad.wav',False)
    source.write_bytes(b'');run('music',source,music,'empty.wav',False)
    run('music',root/'missing',music,'missing.wav',False)
    source.write_bytes(b'fLaC'+bytes(20));run('music',source,music,'track.flac')
    archive = root/'provider-zip'
    def pack(entries, compression=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(archive,'w',compression=compression) as output:
            for name,data in entries:output.writestr(name,data)
    entries = [('bundle/crt.slangp',b'shaders = 1\nshader0 = "shaders/crt.slang"\n'),
               ('bundle/second.slangp',b'#reference "crt.slangp"\n'),
               ('bundle/shaders/crt.slang',b'#include "common.inc"\n'),
               ('bundle/shaders/common.inc',b'// include\n'),('bundle/texture.png',b'texture')]
    pack(entries);presets = run('zip',archive,shaders)
    assert len(presets)==2
    assert (presets[0].parent/'shaders'/'common.inc').read_bytes()==b'// include\n'
    again = run('zip',archive,shaders);assert presets[0] != again[0] and presets[0].exists()
    for path in ['../escape','/absolute','nested/../../escape','C:/drive','back\\slash','./dot','bad\nname','bundle//crt.slangp']:
        pack(entries+[(path,b'bad')]);run('zip',archive,shaders,valid=False)
    pack(entries+[('BUNDLE/CRT.SLANGP',b'duplicate')]);run('zip',archive,shaders,valid=False)
    link=zipfile.ZipInfo('bundle/link');link.create_system=3;link.external_attr=0o120777<<16
    pack(entries+[(link,b'../../../outside')]);run('zip',archive,shaders,valid=False)
    pack([('readme.txt',b'no preset')]);run('zip',archive,shaders,valid=False)
    pack(entries+[('big',bytes(32*1024*1024+1))]);run('zip',archive,shaders,valid=False)
    pack(entries+[(f'large{i}',bytes(32*1024*1024)) for i in range(9)]);run('zip',archive,shaders,valid=False)
    pack(entries+[(f'file{i}',b'') for i in range(4097)]);run('zip',archive,shaders,valid=False)
    pack([('bad.slangp',b'CRC-test')],zipfile.ZIP_STORED)
    raw=archive.read_bytes().replace(b'CRC-test',b'BAD-test');archive.write_bytes(raw)
    run('zip',archive,shaders,valid=False)
    archive.write_bytes(b'not a zip');run('zip',archive,shaders,valid=False)
    # Copy size bound and cleanup, without reading a large fixture into memory.
    with source.open('wb') as out:
        out.write(wave);out.truncate(256*1024*1024+1)
    run('music',source,music,'large.wav',False)
    subprocess.run([binary,'lifecycle',str(root/'private')],check=True)
print('Content imports: audio copies, Unicode names, collisions, multi-preset dependencies, traversal/symlink/duplicate/size/count/CRC rejection and rollback passed')
