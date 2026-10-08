-- Execution grants with external device timer/CPU completion boundaries.
-- Global basetime and chronological main/sub bus order are assertions only.
local machine=manager.machine
local cpus={machine.devices[':mainpcb:maincpu'],machine.devices[':mainpcb:subcpu'],machine.devices[':mainpcb:soundcpu']}
-- The source probe registers one read-only-to-Lua grant counter directly
-- before global basetime in save_manager's lexicographic item order.
assert(os.getenv('AB2_SCHED_TRACE'),'this capture requires the scheduler probe')
local before=0
for _,index in pairs(machine.devices[':mainpcb:gfxdecode'].items)do before=math.max(before,index)end
local grant,basea,bases=emu.item(before+1),emu.item(before+2),emu.item(before+3)
assert(grant.size==8 and basea.size==8 and bases.size==4,'missing probe scheduler metadata')
local function base()return bases:read(0)*1000000000000000000+basea:read(0)end
local out=assert(io.open(os.getenv('AB2_ORACLE_OUT'),'wb'));out:write('AB2ARB04')
local frames,groups,phases=0,0,0
local active=nil;local ready=false;local finish=false
local function close()
 if not active then return end
 out:write(string.pack('>I8I8I4',active.grant,active.base,#active.events),table.concat(active.events));groups=groups+1
 active=nil
end
-- Selected instructions have no register/CCR effects before the first bus,
-- except BSR's four-byte stack decrement, which is reversed explicitly.
-- Capture from the executing CPU's state, avoiding debugger visible-CPU symbols.
local bounded={
 [1]={[0x14856]=true,[0x1485c]=true,[0x1485e]=true,[0x14866]=true,
      [0x1486a]=true,[0x1486e]=true,[0x14876]=true,[0x14878]=true},
 [2]={[0x432]=true,[0x436]=true,[0x43a]=true,[0x43e]=true,[0x442]=true,
      [0x446]=true,[0x44a]=true,[0x44e]=true,[0x452]=true,[0x456]=true,
      [0x458]=true,[0x556]=true,[0x55a]=true,[0x55c]=true,[0x640]=true,[0x646]=true}}
local function instruction(cpu,p,w,a)
 if not bounded[cpu][p] or (cpu==2 and cpus[cpu].state.SR.value&0x2000~=0)then return string.char(0)end
 local first=false
 if cpu==1 then
  if p==0x1485c then first=not w and (a==0x14856 or a==p+4)
  elseif p==0x14866 then first=not w and (a==0x1486e or a==p+4)
  elseif p==0x14878 then
   local sr=cpus[cpu].state.SR.value;local sp=sr&0x2000~=0 and cpus[cpu].state.SP.value or cpus[cpu].state.USP.value
   first=not w and a==(sp&0x3ffffe)
  else first=not w and a==p+4 end
 else
  if p==0x432 or p==0x43e or p==0x44a or p==0x44e or p==0x55c then
   local sr=cpus[cpu].state.SR.value;local sp=sr&0x2000~=0 and cpus[cpu].state.SP.value or cpus[cpu].state.USP.value
   first=a==(sp&0xffffe) and (w==(p~=0x55c))
  elseif p==0x43a or p==0x446 then first=not w and (a==p+8 or a==p+4)
  elseif p==0x456 then first=not w and (a==0x452 or a==p+4)
  elseif p==0x55a then first=not w and (a==0x556 or a==p+4)
  elseif p==0x646 then first=not w and (a==0x640 or a==p+4)
  else first=not w and a==p+4 end
 end
 if not first then return string.char(0)end
 local state=cpus[cpu].state;local sr=state.SR.value
 local sp=sr&0x2000~=0 and state.SP.value or state.USP.value
 if cpu==2 and (p==0x432 or p==0x43e or p==0x44a or p==0x44e)then sp=sp+4 end
 local packed={};for i=0,7 do packed[#packed+1]=string.pack('>I4',state['D'..i].value&0xffffffff)end
 for i=0,6 do packed[#packed+1]=string.pack('>I4',state['A'..i].value&0xffffffff)end
 return string.char(1)..string.pack('>I4I2I4',p,sr,sp)..table.concat(packed)
end
local function event(cpu,w,a,d,m)
 if not ready then return end
 local b=base();local g=grant:read(0)
 if active and g~=active.grant then close()end
 if finish then out:close();print(string.format('captured %d CPU grants and %d main/sub phases through frame %d',groups,phases,frames));machine:exit();ready=false;return end
 if not active then active={grant=g,base=b,events={}}end
 local ns=math.floor(machine.time:as_double()*1e9+0.5)
 local p=cpus[cpu].state.CURPC.value
 active.events[#active.events+1]=string.pack('>BI8BI4I2I2I4',cpu,ns,w and 1 or 0,a,d,m,p)..instruction(cpu,p,w,a);phases=phases+1
end
arbiter_taps={}
for i=1,2 do local cpu=i;local space=cpus[i].spaces.program
 arbiter_taps[#arbiter_taps+1]=space:install_read_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_read'..i,function(a,d,m)event(cpu,false,a,d,m)end)
 arbiter_taps[#arbiter_taps+1]=space:install_write_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_write'..i,function(a,d,m)event(cpu,true,a,d,m)end)
end
if os.getenv('AB2_ORACLE_GAMEPLAY')=='1'then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
arbiter_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1;if frames==32 then out:write(cpus[1].spaces.program:read_range(0x3f8000,0x3fffff,8),cpus[2].spaces.program:read_range(0x9c000,0xa3fff,8));ready=true end
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'120')then finish=true end
end)
