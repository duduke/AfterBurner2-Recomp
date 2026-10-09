-- Continuous, read-only sound CPU bus oracle. External main writes are
-- labelled host inputs; no sound registers, RAM or device state are seeded.
local cpu=manager.machine.devices[':mainpcb:soundcpu']
local space=cpu.spaces.program
local joint=os.getenv('AB2_JOINT_SOUND_EXECUTION')=='1'
local path=assert(os.getenv('AB2_ORACLE_OUT'))..(joint and '.soundcpu' or '')
local out=assert(io.open(path,'wb'))
local timer_b=os.getenv('AB2_SOUND_TIMER_B_FIXTURE')=='1'
local directed=os.getenv('AB2_SOUND_IRQ_FIXTURE')=='1' or timer_b
out:write(directed and 'AB2ZEX03' or 'AB2ZEX02')
if directed then
 out:write(string.char(timer_b and 2 or 1))
 local rom=manager.machine.memory.regions[':mainpcb:soundcpu']
 for a=0,0xefff do rom:write_u8(a,0)end
 local source=assert(io.open(os.getenv('AB2_ORACLE_SCRIPT_DIR')..(timer_b and '/sound_timer_b_program.txt' or '/sound_irq_program.txt'),'r'))
 for line in source:lines()do
  if line:sub(1,1)~='#' then
   local a,bytes=line:match('^(%x+)%s+(.*)$')
   if a then a=tonumber(a,16);for byte in bytes:gmatch('%x+')do rom:write_u8(a,tonumber(byte,16));a=a+1 end end
  end
 end
 source:close()
end
assert(os.getenv('AB2_SCHED_TRACE'),'sound execution capture requires scheduler probe')
local before=0
for _,index in pairs(manager.machine.devices[':mainpcb:gfxdecode'].items)do before=math.max(before,index)end
local grant=emu.item(before+1)
assert(grant.size==8,'missing scheduler grant observer')
local started,guard,finished=false,false,false
local count,frames=0,0
local early_host={}
local function ns() return math.floor(manager.machine.time:as_double()*1e9+0.5) end
local function event(kind,a,d)
 if guard or finished then return end
 local pc=cpu.state.CURPC.value
 if not started and kind>=4 then
  early_host[#early_host+1]=string.pack('>BI8I8I2I2B',kind,grant:read(0),ns(),pc,a,d&255)
  return
 end
 if not started then
  if kind~=0 or a~=0 or pc~=0 then return end
  started=true;guard=true
  out:write(space:read_range(0xf800,0xffff,8))
  for _,record in ipairs(early_host)do out:write(record);count=count+1 end
  early_host={}
  guard=false
 end
 out:write(string.pack('>BI8I8I2I2B',kind,grant:read(0),ns(),pc,a,d&255));count=count+1
end
sound_execution_taps={
 space:install_read_tap(0,0xffff,'sound_exec_read',function(a,d,m)event(0,a,d)end),
 space:install_write_tap(0,0xffff,'sound_exec_write',function(a,d,m)event(1,a,d)end),
 cpu.spaces.io:install_read_tap(0,0xff,'sound_exec_io_read',function(a,d,m)event(2,a,d)end),
 cpu.spaces.io:install_write_tap(0,0xff,'sound_exec_io_write',function(a,d,m)event(3,a,d)end)
}
local main=manager.machine.devices[':mainpcb:maincpu'].spaces.program
sound_execution_main=main:install_write_tap(0,0x3fffff,'sound_exec_host_inputs',function(a,d,m)
 if finished or guard then return end
 if a>=0x140000 and a<0x150000 and (m&255)~=0 then event(5,(a//2)&7,d)
 elseif a>=0xe8000 and a<0xec000 and (((a//2)&15)==11 or ((a//2)&15)==15) then event(4,0,d)end
end)
ab2_sound_execution_close=function()
 assert(started,'no sound reset entry');finished=true
 out:write(string.pack('>BI8',255,ns()));out:close()
 print(string.format('captured %d continuous sound transactions through frame %d',count,frames))
end
if not joint and os.getenv('AB2_ORACLE_GAMEPLAY')=='1' then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
sound_execution_frames=emu.add_machine_frame_notifier(function()
 frames=frames+1
 if not joint and frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'16') then
  ab2_sound_execution_close()
  manager.machine:exit()
 end
end)

if joint then sound_execution_stop=emu.add_machine_stop_notifier(ab2_sound_execution_close)end
