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
local start_frame=tonumber(os.getenv('AB2_ORACLE_START_FRAME')or'32')
assert(start_frame and start_frame>=0,'invalid arbiter start frame')
local active=nil;local ready=false;local finish=false
local reset_items,reset_serials={},{}
if start_frame==0 then
 for i=1,2 do
  local index=assert(cpus[i].items['0/ab2_reset_snapshot'],'cold capture requires CPU probe')
  reset_items[i]=emu.item(index)
  reset_serials[i]=reset_items[i]:read(0)
 end
end
-- Before installing taps, retain power-on RAM once. Subsequent reads are
-- assertions, including boot clears, mirrored accesses and reset handshakes.
if start_frame==0 then
 assert(base()==0 and grant:read(0)==0,'cold capture started after CPU execution')
 out:write(cpus[2].spaces.program:read_range(0x9c000,0xa3fff,8));ready=true
end
local function close()
 if not active then return end
 out:write(string.pack('>I8I8I4',active.grant,active.base,#active.events),table.concat(active.events));groups=groups+1
 active=nil
end
local function event(cpu,w,a,d,m,probe)
 if not ready then return end
 local b=base();local g=grant:read(0)
 if active and g~=active.grant then close()end
 if finish then out:close();print(string.format('captured %d CPU grants and %d main/sub phases through frame %d',groups,phases,frames));machine:exit();ready=false;return end
 if not active then active={grant=g,base=b,events={}}end
 local ns=math.floor(machine.time:as_double()*1e9+0.5)
 local reset_entry=0
 if reset_items[cpu] and not probe then
  local serial=reset_items[cpu]:read(0)
  if serial~=reset_serials[cpu] then
   assert(not w and a==0 and m==0xffff,'reset did not start with vector zero')
   reset_entry=1;reset_serials[cpu]=serial
  end
 end
 active.events[#active.events+1]=string.pack('>BI8BI4I2I2B',cpu,ns,w and 1 or 0,a,d,m,reset_entry|(probe and 2 or 0));phases=phases+1
end
local guard=false
local mirror_counts={{0,0},{0,0}}
arbiter_taps={}
for i=1,2 do local cpu=i;local space=cpus[i].spaces.program
 arbiter_taps[#arbiter_taps+1]=space:install_read_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_read'..i,function(a,d,m)
  if guard then return end
  event(cpu,false,a,d,m)
  local shared=cpu==1 and a>=0x280000 and a<0x2c0000 or cpu==2 and a>=0x80000 and a<0xc0000
  if start_frame==0 and ready and shared and d~=0 then
   local bank=(a&0xfffff)<0xa0000 and 1 or 2
   local count=mirror_counts[cpu][bank]
   if count<16 then
    -- Explicit read-only debug probes, not CPU phases: inspect all eight
    -- aliases twice after real nonzero reads. No memory/CPU/timer setters.
    local alias=(a&~0x1c000)|((count%8)<<14)
    guard=true;local value=space:read_u16(alias);guard=false
    event(cpu,false,alias,value,0xffff,true)
    mirror_counts[cpu][bank]=count+1
   end
  end
 end)
 arbiter_taps[#arbiter_taps+1]=space:install_write_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_write'..i,function(a,d,m)event(cpu,true,a,d,m)end)
end
if os.getenv('AB2_ORACLE_GAMEPLAY')=='1'then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
arbiter_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1;if start_frame>0 and frames==start_frame then out:write(cpus[2].spaces.program:read_range(0x9c000,0xa3fff,8));ready=true end
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'120')then finish=true end
end)
