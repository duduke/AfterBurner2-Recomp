#!/usr/bin/env python3
"""Retained negative controls for optional ground horizon geometry/protection."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/main/hwvideo/ground_horizon.hpp').read_text()
mutations={
 'lost clipped side normal':('profile.sky(x,y,nx,ny,extend_plane)','profile.sky(x,y,nx,ny,false)'),
 'missing banked corner plane':('if(extend_plane&&profile.measured)','if(false&&extend_plane&&profile.measured)'),
 'native-edge seam':('else if(extend_plane&&!priority[i]','else if((original<0||original>=320)&&!priority[i]'),
 'missing side ground':('else if(extend_plane&&!priority[i]','else if(false&&extend_plane&&!priority[i]'),
 'side ground overwrites center':('profile.sky(x,y,nx,ny,extend_plane)','profile.sky(x,y,nx,ny,true)'),
 'invented ground color':('if(ground>=0){indices[i]=ground;','if(true){indices[i]=ground;'),
 'wrong native ground color':('indices[i]=ground;','indices[i]=0x1707;'),
 'side ground priority overwrite':('extend_plane&&!priority[i]&&indices[i]>=0x1720','extend_plane&&indices[i]>=0x1720'),
 'local gradient reversal':('return sky_max<ground_min?1:ground_max<sky_min?-1:0;','return sky_max<ground_min?-1:ground_max<sky_min?1:0;'),
 'fitted-plane shortcut':('yedge=ny>0?upper[x]:lower[x]','yedge=ny>0?upper[0]:lower[0]'),
 'reversed ROM orientation':('return {int8_t(main[0x33e98+angle]),-int(int8_t(main[0x33e58+angle]))};','return {-int(int8_t(main[0x33e98+angle])),int8_t(main[0x33e58+angle])};'),
 'opposite sky side':('profile.sky(x,y,nx,ny,extend_plane)','!profile.sky(x,y,nx,ny,extend_plane)'),
 'foreground overwrite':('!priority[i] && indices[i]>=0x1700','indices[i]>=0x1700'),
 'solid fill overwrite':('if(data&0xa00)continue;','if(false && (data&0xa00))continue;'),
 'lost clip frontier':('for(unsigned y=top;y<=bottom;++y)','for(unsigned y=top;y<224;++y)'),
 'lost sky palette':('0x1720^((word(0x600+y)>>8)&15)','0x1720'),
}
for name,(before,after) in mutations.items():
 assert source.count(before)==1,name
 with tempfile.TemporaryDirectory(prefix='ab2-ground-') as temporary:
  path=Path(temporary);(path/'hwvideo').mkdir();(path/'hwvideo/ground_horizon.hpp').write_text(source.replace(before,after))
  binary=path/'check'
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(path),'-I'+str(root/'src/main'),str(root/'tests/ground_horizon_regression.cpp'),'-o',str(binary)],check=True)
  result=subprocess.run([str(binary)],capture_output=True,text=True)
  assert result.returncode==1 and 'ground' in result.stderr.lower(),(name,result.stderr)
  print('Ground mutation rejected: '+name,flush=True)

# The recorded visual gate must reject failures on the actual MAME roll corpus.
fixture_mutations={
 'recorded sea overwrite':('profile.sky(x,y,nx,ny,extend_plane)','true'),
 'recorded sky side reversal':('profile.sky(x,y,nx,ny,extend_plane)','!profile.sky(x,y,nx,ny,extend_plane)'),
 'recorded fitted-plane shortcut':('yedge=ny>0?upper[x]:lower[x]','yedge=ny>0?upper[0]:lower[0]'),
}
for name,(before,after) in fixture_mutations.items():
 assert source.count(before)==1,name
 with tempfile.TemporaryDirectory(prefix='ab2-ground-fixture-') as temporary:
  path=Path(temporary);(path/'hwvideo').mkdir();(path/'hwvideo/ground_horizon.hpp').write_text(source.replace(before,after))
  binary=path/'check'
  sources=['tests/ground_horizon_fixture.cpp','src/main/roms.cpp','src/main/romloader.cpp','src/main/hwvideo/hwroad.cpp','src/main/hwvideo/hwtiles.cpp','src/main/hwvideo/hwpalette.cpp','src/main/hwvideo/hwsprites.cpp','src/main/hwvideo/screen.cpp']
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(path),'-I'+str(root/'src/main'),*[str(root/s) for s in sources],'-o',str(binary)],check=True)
  result=subprocess.run([str(binary),"--no-images"],cwd=root,capture_output=True,text=True)
  assert result.returncode==1 and any(message in result.stderr for message in ['erased sea below published horizon','blue wedge remains above published horizon']),(name,result.stderr)
  print('Ground fixture mutation rejected: '+name,flush=True)
