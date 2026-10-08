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
local out=assert(io.open(os.getenv('AB2_ORACLE_OUT'),'wb'));out:write('AB2ARB03')
local frames,groups,phases=0,0,0
local active=nil;local ready=false;local finish=false
local function close()
 if not active then return end
 out:write(string.pack('>I8I8I4',active.grant,active.base,#active.events),table.concat(active.events));groups=groups+1
 active=nil
end
local function event(cpu,w,a,d,m)
 if not ready then return end
 local b=base();local g=grant:read(0)
 if active and g~=active.grant then close()end
 if finish then out:close();print(string.format('captured %d CPU grants and %d main/sub phases through frame %d',groups,phases,frames));machine:exit();ready=false;return end
 if not active then active={grant=g,base=b,events={}}end
 local ns=math.floor(machine.time:as_double()*1e9+0.5)
 active.events[#active.events+1]=string.pack('>BI8BI4I2I2',cpu,ns,w and 1 or 0,a,d,m);phases=phases+1
end
arbiter_taps={}
for i=1,2 do local cpu=i;local space=cpus[i].spaces.program
 arbiter_taps[#arbiter_taps+1]=space:install_read_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_read'..i,function(a,d,m)event(cpu,false,a,d,m)end)
 arbiter_taps[#arbiter_taps+1]=space:install_write_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_write'..i,function(a,d,m)event(cpu,true,a,d,m)end)
end
if os.getenv('AB2_ORACLE_GAMEPLAY')=='1'then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
arbiter_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1;if frames==32 then out:write(cpus[2].spaces.program:read_range(0x9c000,0xa3fff,8));ready=true end
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'120')then finish=true end
end)
