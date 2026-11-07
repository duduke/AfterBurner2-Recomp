#!/usr/bin/env python3
"""Retained import rejection mutations; compile isolated headers, never edit the tree."""
import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default='c++')
parser.add_argument('--flags', required=True)
parser.add_argument('--libs', required=True)
args = parser.parse_args()
header = (ROOT/'src/main/frontend/content_import.hpp').read_text()
mutations = {
    'archive-traversal': ('!safe_pack_path(name)', 'false'),
    'archive-symlink': ('((stat.m_external_attr>>16)&0170000)==0120000', 'false'),
    'archive-entry-limit': ('stat.m_uncomp_size>32ull*1024*1024', 'false'),
    'audio-signature': ('if(!valid)throw', 'if(false)throw'),
}
for name, (before, after) in mutations.items():
    assert header.count(before) == 1, name
    with tempfile.TemporaryDirectory(prefix='ab2-import-mutation-') as folder:
        root = Path(folder)
        frontend = root/'main/frontend';frontend.mkdir(parents=True)
        (root/'third_party').symlink_to(ROOT/'src/third_party', target_is_directory=True)
        (frontend/'content_import.hpp').write_text(header.replace(before,after))
        for file in ['music_settings.hpp','android_content_import.hpp']:
            (frontend/file).symlink_to(ROOT/'src/main/frontend'/file)
        binary = root/'import-test'
        command = [*shlex.split(args.compiler),'-I'+str(root/'main'),*shlex.split(args.flags),'-pthread',
                   '-I'+str(ROOT/'src/main'),str(ROOT/'tests/content_import_regression.cpp'),
                   str(ROOT/'src/main/romloader.cpp'),'-o',str(binary),*shlex.split(args.libs)]
        compiled = subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
        if compiled.returncode:raise RuntimeError(compiled.stderr)
        checked = subprocess.run(['python3',str(ROOT/'tests/test_content_import.py'),str(binary)],cwd=ROOT,capture_output=True,text=True)
        if checked.returncode == 0:raise RuntimeError('Mutation survived: '+name)
        if 'AssertionError' not in checked.stderr:raise RuntimeError('Unexpected mutation failure: '+checked.stderr)
        print('PASS rejected '+name,flush=True)
