#!/usr/bin/env python3
"""Reject visibility lifetime/horizon/ownership faults with detached fixtures."""
from pathlib import Path
import os,shlex,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
MUTATIONS=[
 ('water-depth-advance','wide_visibility.hpp','w(d[1],bus.read16(scratch+36))','w(d[1],bus.read16(scratch+36)-256)','takeoff water projection changed native depth or coordinates'),
 ('takeoff-water-field','wide_visibility.hpp','extend_water(h,out,submitted[readlong(h,0x2a3c04)&0xfffff]);','/* omit adjacent water cells */','takeoff water field extension missing or leaked into another mode'),
 ('nonflight-scenery','wide_visibility.hpp','(mode!=1&&mode!=15&&mode!=16)','mode!=16','non-flight scenery retention missing'),
 ('frozen-shared-pair','wide_visibility.hpp','// The pointed-to pair is shared camera/roll state, not object-owned','bus.write32(bus.read32(g.object+54)+4,0); // freeze the rotation pair','retained scenery froze the shared rotation pair'),
 ('scenery-native-cull','wide_visibility.hpp','SceneryProjection::update(bus,regs,margin)','SceneryProjection::update(bus,regs,0)','retained scenery lost after native slot reuse'),
 ('horizon-length','wide_visibility.hpp','const unsigned extra=(margin+23)/24+1;','const unsigned extra=0;','horizon extension missing at wide edges'),
 ('native-memory-alias','wide_visibility.hpp','MainBus::Memory copy=h.memory;','MainBus::Memory& copy=const_cast<MainBus::Memory&>(h.memory);','wide observer changed native memory/time'),
 ('capacity-discard','wide_visibility.hpp','if(s.rejected||!present)','if(!s.rejected&&!present)','capacity overflow requests missing'),
 ('bucket-discard','wide_visibility.hpp','if(s.rejected||!present)','if(s.rejected)','missing native bucket request not recovered'),
 ('bucket-duplicate','wide_visibility.hpp','if(represented[index])continue;','if(false)continue;','depth bucket overflow lost requests or duplicated stale native order'),
 ('midpass-default','wide_visibility.hpp','&&projected[i].recovered','','mid-pass enable replaced uncaptured native descriptor'),
 ('unpublished-horizon','wide_visibility.hpp','if(horizon_published)for(auto& q:horizon)','if(true)for(auto& q:horizon)','unpublished horizon extended'),
 ('flight-only-horizon','wide_visibility.hpp','if(horizon_published)for(auto& q:horizon)','if(scene_mode==0x10&&horizon_published)for(auto& q:horizon)','non-flight published horizon missing'),
 ('stale-horizon','wide_visibility.hpp','        horizon.clear();','        /* retain stale horizon */','old horizon reused without a controller update'),
 ('nonflight-history','wide_visibility.hpp','horizon_history=true;','horizon_history=read(h,0x29c09c)==0x10;','non-flight horizon interpolation differs'),
 ('cross-scene-history','wide_visibility.hpp','if(scene!=horizon_scene)horizon_history=false;','if(false)horizon_history=false;','horizon interpolation crossed a scene change'),
 ('descriptor-cap','sprite_scene.hpp','bank.empty() || bank.size()%16 ||','bank.empty() || bank.size()%16 || bank.size()>0x10000 ||','Invalid sprite scene source'),
]
logs=ROOT/'build/oracle/widescreen/visibility-mutations';logs.mkdir(parents=True,exist_ok=True)
subprocess.run(['make','build/wide_scene_integration'],cwd=ROOT,check=True,capture_output=True)
baseline=subprocess.run(['./build/wide_scene_integration','--visibility-fixtures'],cwd=ROOT,capture_output=True,text=True)
assert baseline.returncode==0,baseline.stdout+baseline.stderr
# Keep the production object graph and selected compiler/flags. Replace only
# the test translation unit with source so the private header overlay applies.
dry=subprocess.check_output(['make','-n','-W','tests/wide_scene_integration.cpp','build/wide_scene_integration'],cwd=ROOT,text=True)
cmd=next(shlex.split(line) for line in dry.splitlines() if ' -o build/wide_scene_integration ' in line)
for name,file,old,new,witness in MUTATIONS:
 with tempfile.TemporaryDirectory(prefix='wide-visibility-',dir=ROOT/'build/oracle') as tmp:
  overlay=Path(tmp);(overlay/'hwvideo').mkdir();(overlay/'engine').mkdir()
  source=(ROOT/'src/main/hwvideo'/file).read_text();assert source.count(old)>0,name
  (overlay/'hwvideo'/file).write_text(source.replace(old,new))
  (overlay/'engine/game_session.hpp').write_text((ROOT/'src/main/engine/game_session.hpp').read_text())
  build=list(cmd);build[1:1]=['-I'+str(overlay),'-Isrc/main']
  build[build.index('build/native_obj/tests/wide_scene_integration.o')]='tests/wide_scene_integration.cpp'
  binary=overlay/name;build[build.index('-o')+1]=str(binary)
  with (logs/(name+'-build.log')).open('w') as f:subprocess.run(build,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,check=True)
  result=subprocess.run([str(binary),'--visibility-fixtures'],cwd=ROOT,capture_output=True,text=True)
  output=result.stdout+result.stderr;(logs/(name+'-run.log')).write_text(output)
  assert result.returncode==1 and witness in output,(name,output[-2000:])
  print('Wide visibility mutation '+name+': rejected',flush=True)

# The geometry/lifetime fixtures above validate the enhanced policy. These
# additional faults must fail against independent original-ROM MAME captures.
for name,file,old,new,target,test,capture,witness in [
 ('mame-projection-pitch','wide_projection.hpp','d4 & 0x7f), 7','d4 & 0x7f), 6','oracle-wide-projection','tests/oracle_subsprites.cpp','build/oracle/subsprites_directed.bin','detached native projection differs from MAME'),
 ('mame-scenery-height','wide_scenery.hpp','w(d[0],d[0]+0x70);','w(d[0],d[0]+0x71);','oracle-handlers','tests/oracle_handlers.cpp','build/oracle/world_directed.bin','detached scenery evolution differs from MAME'),
 ('mame-request-metadata','wide_request.hpp','q[12]=object[20]','q[12]=object[19]','oracle-wide-requests','tests/oracle_mainsprites.cpp','build/oracle/mainsprites_directed.bin','detached request copy differs from MAME'),
 ('mame-request-limit','wide_request.hpp','int16_t(count)>=0xc0','int16_t(count)>0xc0','oracle-wide-requests','tests/oracle_mainsprites.cpp','build/oracle/mainsprites_directed.bin','detached request rejection differs from MAME'),
]:
 with tempfile.TemporaryDirectory(prefix='wide-mame-',dir=ROOT/'build/oracle') as tmp:
  overlay=Path(tmp);(overlay/'hwvideo').mkdir()
  source=(ROOT/'src/main/hwvideo'/file).read_text();assert old in source,name
  (overlay/'hwvideo'/file).write_text(source.replace(old,new))
  dry=subprocess.check_output(['make','-n',target],cwd=ROOT,text=True)
  command=next(shlex.split(line) for line in dry.splitlines() if test in line and ' -o ' in line)
  command[1:1]=['-I'+str(overlay)];binary=overlay/name;command[command.index('-o')+1]=str(binary)
  with (logs/(name+'-build.log')).open('w') as f:subprocess.run(command,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,check=True)
  result=subprocess.run([str(binary),capture],cwd=ROOT,capture_output=True,text=True);output=result.stdout+result.stderr
  (logs/(name+'-run.log')).write_text(output)
  assert result.returncode==1 and witness in output,(name,output[-2000:])
  print('Wide MAME mutation '+name+': rejected',flush=True)
