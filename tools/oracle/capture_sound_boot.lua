-- Cold-reset sound ROM execution through the initial zero-latch polling.
-- Stop before main command/reset fixtures or an unimplemented device read. Device
-- writes are assertions only; YM/PCM stepping and IRQ/NMI are outside this
-- bounded boot check. Never return captured read values to native execution.
local cpu=manager.machine.devices[':mainpcb:soundcpu']
local space=cpu.spaces.program
local out=assert(io.open(assert(os.getenv('AB2_ORACLE_OUT')),'wb'))
out:write('AB2ZBT03')
local started,finished,guard=false,false,false
local count=0
local function close(reason)
 finished=true;out:close()
 print(string.format('captured %d sound boot transactions: %s',count,reason))
 manager.machine:exit()
end
local function event(kind,a,d)
 if guard or finished then return end
 local pc=cpu.state.CURPC.value
 if not started then
  if kind~=0 or a~=0 or pc~=0 then return end
  started=true;guard=true
  out:write(space:read_range(0xf800,0xffff,8))
  guard=false
 end
 local ns=math.floor(manager.machine.time:as_double()*1e9+0.5)
 out:write(string.pack('>BI8I2I2B',kind,ns,pc,a,d&255));count=count+1
 if (kind==2 and a<0x40) or
    (kind==1 and a>=0xf000 and a<0xf800 and (a&0x87)==0x86 and (d&1)==0) then
  close('stopped before YM read or active PCM channel')
 elseif count>=4096 then
  out:write(string.char(6));close('bounded zero-latch polling window')
 end
end
sound_boot_taps={
 space:install_read_tap(0,0xffff,'sound_boot_read',function(a,d,m)event(0,a,d)end),
 space:install_write_tap(0,0xffff,'sound_boot_write',function(a,d,m)event(1,a,d)end),
 cpu.spaces.io:install_read_tap(0,0xff,'sound_boot_io_read',function(a,d,m)event(2,a,d)end),
 cpu.spaces.io:install_write_tap(0,0xff,'sound_boot_io_write',function(a,d,m)event(3,a,d)end)
}
local main=manager.machine.devices[':mainpcb:maincpu'].spaces.program
sound_boot_main=main:install_write_tap(0,0x3fffff,'sound_boot_main_boundary',function(a,d,m)
 if not started or finished then return end
 if (a>=0xe8000 and a<0xec000 and ((a//2)&15)==11) or
    (a>=0xe8000 and a<0xec000 and ((a//2)&15)==15) or
    (a>=0x140000 and a<0x140010) then
  out:write(string.char(6));close('stopped before main sound command or I/O change')
 end
end)
