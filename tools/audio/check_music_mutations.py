#!/usr/bin/env python3
"""Retained negative controls for passive ROM routing and PCM stem isolation."""
from pathlib import Path
import shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
routing='src/main/hwaudio/music_routing.hpp'
audio='src/main/hwaudio/xboard_audio_deadlines.hpp'
mutations=[
 ('fm-effects-as-music',routing,'if(r.ix<0xfad0)fm_music|=bit;','if(r.ix<=0xfe82)fm_music|=bit;',False),
 ('wrong-track',routing,'commands{0x92,0x91,0x94,0x93,0x95,0x96}','commands{0x91,0x92,0x94,0x93,0x95,0x96}',False),
 ('missing-sequencer-start',routing,'e.pc==0xc76)for','e.pc==0xc75)for',False),
 ('lost-pcm-percussion',audio,'auto& v=c<8?m:f;','auto& v=c<7?m:f;',True),
]
for name,path,old,new,unit in mutations:
    source=(root/path).read_text();assert source.count(old)==1,name
    with tempfile.TemporaryDirectory(prefix='music-mutant-',dir=root/'build/oracle') as tmp:
        overlay=Path(tmp);target=overlay/path;target.parent.mkdir(parents=True);target.write_text(source.replace(old,new))
        binary=overlay/name
        if unit:
            dry=subprocess.run(['make','-n','-W','tests/native_music_regression.cpp','build/native_music_regression'],cwd=root,capture_output=True,text=True,check=True).stdout
            lines=[shlex.split(line) for line in dry.splitlines() if 'tests/native_music_regression.cpp' in line and ' -o ' in line]
            assert len(lines)==1
            command=lines[0];command[1:1]=['-I'+str(overlay/'src/main')];command[command.index('-o')+1]=str(binary)
            expected='PCM music/effects routing';args=[]
        else:
            dry=subprocess.run(['make','-n','-W','tests/oracle_music_routing.cpp','build/oracle_music_routing'],cwd=root,capture_output=True,text=True,check=True).stdout
            lines=[shlex.split(line) for line in dry.splitlines() if ' -o build/oracle_music_routing' in line]
            assert len(lines)==1
            command=lines[0];objects=[x for x in command if x.endswith('.o') and 'oracle_music_routing.o' not in x]
            flags=subprocess.run(['pkg-config','--cflags','sdl3'],capture_output=True,text=True,check=True).stdout
            command=[command[0],'-std=c++20','-O3','-flto','-pthread','-I'+str(overlay/'src/main'),'-Isrc/main',*shlex.split(flags),'tests/oracle_music_routing.cpp',*objects,'-o',str(binary)]
            expected='native music routing differs from MAME';args=['build/oracle/music-routing-1800.bin','--require-fm-effects']
        subprocess.run(command,cwd=root,check=True,capture_output=True)
        result=subprocess.run([str(binary),*args],cwd=root,capture_output=True,text=True,timeout=120)
        (root/'build/oracle'/('music_mutation_'+name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode!=1 or expected not in result.stderr:raise SystemExit(f'{name}: not rejected by required comparison: {result.stderr[-2000:]}')
        print('Music mutation '+name+': rejected',flush=True)
