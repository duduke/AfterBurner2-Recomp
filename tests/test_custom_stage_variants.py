"""Native tanker backdrops stay paired, reproducible and physically dockable."""
from pathlib import Path
import json
import os
import re
import struct
import subprocess
import sys
import hashlib
root=Path(__file__).resolve().parents[1]
binary=(root/sys.argv[1]).resolve()
out=root/'build/oracle/tanker-variants';out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
source=json.loads((root/'assets/custom_stages/refuel.json').read_text())
def run(args, good=True):
 p=subprocess.run([str(binary),'--roms','roms',*args],cwd=root,env=env,text=True,capture_output=True,timeout=120)
 assert (p.returncode==0)==good,p.stdout+p.stderr
 return p.stdout

def section(path,x1,y1,x2,y2):
 b=path.read_bytes();offset=struct.unpack_from('<I',b,10)[0];w,h=struct.unpack_from('<ii',b,18);step=struct.unpack_from('<H',b,28)[0]//8;stride=(w*step+3)//4*4
 assert (w,abs(h))==(1152,720)
 return b''.join(b[offset+(719-y if h>0 else y)*stride+x1*step:offset+(719-y if h>0 else y)*stride+x2*step] for y in range(y1,y2))

worlds=set();skies=set();fields=set();manifest=[]
for stage in (3,9,11,16,19,21):
 data=json.loads(json.dumps(source));data['presentation']['tanker_variant']=stage
 config=out/f'variant-{stage}.json';config.write_text(json.dumps(data))
 image=out/f'variant-{stage}.bmp'
 result=run(['--seed','123','--stage-file',str(config),'--demo','--frames','1250','--screenshot',str(image)])
 assert f'tanker_variant={stage},' in result,result
 assert 'sky_aircraft=0' in result and 'clouds_drawn=4' in result,result
 assert 'phase=2' in result and 'coupled=1' in result and 'fuel=1' in result,result
 assert float(re.search(r'probe_error_px=([0-9.e+-]+)',result)[1])<.01,result
 assert int(re.search(r'trees_drawn=(\d+)',result)[1])>256,result
 world=hashlib.sha256(section(image,50,100,1100,560)).hexdigest()
 sky=section(image,1080,200,1081,201).hex()
 field=hashlib.sha256(section(image,50,530,1100,560)).hexdigest()
 worlds.add(world);skies.add(sky);fields.add(field);manifest.append({'stage':stage,'sky':sky,'world':world,'scenery':field})
(out/'variants.json').write_text(json.dumps(manifest,indent=2)+'\n')
assert len(worlds)==len(skies)==len(fields)==6,(len(worlds),len(skies),len(fields))
a,b=out/'seed-a.bmp',out/'seed-b.bmp'
ra=run(['--seed','456','--stage','refuel','--demo','--frames','1250','--screenshot',str(a)])
rb=run(['--seed','456','--stage','refuel','--demo','--frames','1250','--screenshot',str(b)])
assert re.search(r'tanker_variant=(\d+)',ra)[1]==re.search(r'tanker_variant=(\d+)',rb)[1]
assert a.read_bytes()==b.read_bytes(),'Seeded native variant pixels changed'
data['presentation']['tanker_variant']=5
invalid=out/'not-a-tanker.json';invalid.write_text(json.dumps(data))
run(['--stage-file',str(invalid),'--frames','1'],False)
print('Six native tanker variants: distinct sky/terrain/scenery sets, connected rearming, seeded pixel replay and non-tanker rejection passed')
