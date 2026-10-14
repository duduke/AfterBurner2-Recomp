#!/usr/bin/env python3
"""Retain real failing controls for the optional Soft playback path."""
from pathlib import Path
import os, subprocess, shutil
root=Path(__file__).resolve().parents[2]
os.chdir(root)
folder=root/'build/oracle/spatial-audio/mutations';folder.mkdir(parents=True,exist_ok=True)
header=Path('src/main/frontend/audio_output.hpp')
original=header.read_text()
pkg=os.environ.copy();pkg['PKG_CONFIG_PATH']=str(root/'build/deps/openal/lib/pkgconfig')+':'+pkg.get('PKG_CONFIG_PATH','')
def flags(option,name):
    return subprocess.check_output(['pkg-config',option,name],env=pkg,text=True).split()
try:
    alflags=flags('--cflags','openal');allibs=flags('--libs','openal')
except subprocess.CalledProcessError:
    print('OpenAL Soft unavailable; explicit optional mutation skip');raise SystemExit(0)
mutations=[
 ('swapped-speaker-sides','{-.5f,0,-.866f},{.5f,0,-.866f}','{.5f,0,-.866f},{-.5f,0,-.866f}'),
 ('hrtf-disabled','context=alcCreateContext(device,attrs.data());','for(auto& a:attrs)if(a==ALC_STEREO_HRTF_SOFT)a=ALC_STEREO_BASIC_SOFT;for(size_t i=0;i+1<attrs.size();++i)if(attrs[i]==ALC_HRTF_SOFT)attrs[i+1]=ALC_FALSE;context=alcCreateContext(device,attrs.data());'),
 ('stereo-effects','stream.channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16','AL_FORMAT_STEREO16'),
 ('bgm-spatialized','if(direct)alSourcei(sources[i],AL_DIRECT_CHANNELS_SOFT,direct);','/* deliberate mutation: no direct music routing */'),
]
for name,before,after in mutations:
    case=folder/name;(case/'frontend').mkdir(parents=True,exist_ok=True)
    if original.count(before)!=1:raise RuntimeError(f'{name}: ambiguous mutation')
    (case/'frontend/audio_output.hpp').write_text(original.replace(before,after))
    exe=case/'check';compile_log=case/'build.log'
    command=[os.environ.get('CXX','c++'),'-std=c++20','-O2','-DAB2_OPENAL=1','-I'+str(case),'-Isrc/main',*flags('--cflags','sdl3'),*alflags,'tests/spatial_audio_regression.cpp','-o',str(exe),*flags('--libs','sdl3'),*allibs,'-Wl,-rpath,'+str(root/'build/deps/openal/lib'),'-pthread']
    with compile_log.open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    with (case/'run.log').open('w') as log:
        result=subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,timeout=20)
    if result.returncode==0:raise RuntimeError(f'{name}: mutation accepted')
    print(f'Spatial mutation rejected: {name}')

# Missing direct-channel support must fall back to SDL, preserving dry stereo.
# Disabling source positioning alone still virtualizes stereo speakers in HRTF.
case=folder/'no-direct-capability';(case/'frontend').mkdir(parents=True,exist_ok=True)
needle='direct=alIsExtensionPresent("AL_SOFT_direct_channels_remix")?AL_REMIX_UNMATCHED_SOFT:alIsExtensionPresent("AL_SOFT_direct_channels")?AL_TRUE:0;'
if original.count(needle)!=1:raise RuntimeError('ambiguous direct capability override')
(case/'frontend/audio_output.hpp').write_text(original.replace(needle,'direct=0;'))
exe=case/'check'
(case/'fallback.cpp').write_text(r'''#include "frontend/audio_output.hpp"
#include <iostream>
int main(){SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");Frontend::OutputSettings s;s.profile=Frontend::AudioProfile::headphones;const std::string path="build/oracle/spatial-audio/fallback.output";Frontend::save_output_settings(path,s);Frontend::AudioOutput output(path,true);for(int i=0;i<400&&!output.status().ready;++i)SDL_Delay(5);const auto state=output.status();if(!state.ready||state.openal||state.hrtf||!state.text.starts_with("SDL FALLBACK:"))return 1;std::cout<<"SDL fallback status is truthful; no HRTF claimed\n";}
''')
command=[os.environ.get('CXX','c++'),'-std=c++20','-O2','-DAB2_OPENAL=1','-I'+str(case),'-Isrc/main',*flags('--cflags','sdl3'),*alflags,str(case/'fallback.cpp'),'-o',str(exe),*flags('--libs','sdl3'),*allibs,'-Wl,-rpath,'+str(root/'build/deps/openal/lib'),'-pthread']
with (case/'build.log').open('w') as log:subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
with (case/'run.log').open('w') as log:subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=20)
print('Spatial capability fallback passed: SDL without direct-channel extension')
