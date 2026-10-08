#!/usr/bin/env python3
"""Prove real MAME captures reject SegaPCM algorithm mutations."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    root=Path(__file__).resolve().parents[2]
    captures=[root/'build/oracle/pcm.bin',root/'build/oracle/pcm_directed.bin',root/'build/oracle/pcm_synthetic.bin']
    for capture in captures:
        result=subprocess.run([str(root/'build/oracle_pcm'),str(capture)],cwd=root,capture_output=True,text=True)
        if result.returncode:raise SystemExit(f'PCM baseline must pass: {result.stderr}')
    source=(root/'src/main/hwaudio/xboard_pcm.hpp').read_text()
    mutations=[
        ('bank-shift','(r[0x86]&0x70)<<12','(r[0x86]&0x70)<<11'),
        ('erased-bank-alias','rom[bank+(addr>>8)]','rom[(bank==0x70000?0x60000:bank)+(addr>>8)]'),
        ('end-address','static_cast<uint8_t>(r[6]+1)','static_cast<uint8_t>(r[6])'),
        ('stop-bit','if(r[0x86]&2)','if(r[0x86]&4)'),
        ('loop-address','addr=loop;','addr=loop^0x100;'),
        ('volume-mask','out.left+=v*(r[2]&0x7f);','out.left+=v*r[2];'),
        ('fraction-retention','s.low[ch]=(r[0x86]&1)?0:static_cast<uint8_t>(addr);','s.low[ch]=0;'),
        ('output-clamp','return out;','out.left=std::clamp(out.left,-32768,32767);out.right=std::clamp(out.right,-32768,32767);return out;'),
    ]
    with tempfile.TemporaryDirectory(prefix='pcm-mutations-',dir=root/'build/oracle')as tmp:
        overlay=Path(tmp);(overlay/'hwaudio').mkdir()
        for name,old,new in mutations:
            if source.count(old)!=1:raise SystemExit(f'Mutation anchor missing/ambiguous: {name}')
            mutated=source.replace(old,new).replace('#include <array>','#include <array>\n#include <algorithm>')
            (overlay/'hwaudio/xboard_pcm.hpp').write_text(mutated)
            binary=overlay/name
            command=shlex.split(os.environ.get('CXX','c++'))
            command+=shlex.split(os.environ.get('CXXFLAGS','-std=c++20 -Wall -Wextra -O2'))
            command+=[f'-I{overlay}','-Isrc/main','tests/oracle_pcm.cpp','src/main/romloader.cpp','-o',str(binary)]
            subprocess.run(command,cwd=root,check=True)
            rejected=False
            for capture in captures:
                result=subprocess.run([str(binary),str(capture)],cwd=root,capture_output=True,text=True)
                mismatch=any(message in result.stderr for message in
                             ('PCM sample differs','PCM register read differs','PCM register/fraction write-back differs'))
                if result.returncode==1 and mismatch:
                    rejected=True;print(f'PCM mutation {name}: rejected by {capture.name}');break
                if result.returncode:raise SystemExit(f'Non-comparison failure for {name}: {result.stderr}')
            if not rejected:raise SystemExit(f'PCM mutation accepted: {name}')
    return 0


if __name__=='__main__':raise SystemExit(main())
