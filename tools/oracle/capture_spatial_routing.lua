-- Passive spatial-audio ownership audit. All changes are physical inputs.
local machine=manager.machine
local cpu=machine.devices[':mainpcb:soundcpu']
local space=cpu.spaces.program
local path=assert(os.getenv('AB2_ORACLE_OUT'))
local out=assert(io.open(path,'wb'));out:write('AB2SPA01')
local inputs=assert(io.open(path..'.inputs','wb'));inputs:write('AB2MIP01')
for _,name in ipairs({'IO1PORTB','IO1PORTC','IO1PORTD'})do inputs:write(string.char(machine.ioport.ports[':mainpcb:'..name]:read()))end
local function ns()return math.floor(machine.time:as_double()*1e9+.5)end
local count,frames,finished=0,0,false
local function observe(k,a,d)
 if finished then return end
 local pc=cpu.state.CURPC.value
 if (k==0 and (pc==0xc76 or (pc==0x17a2 and a==pc))) or (k==1 and a>=0xf000 and a<0xf100) or (k==3 and a<=1) then
  out:write(string.pack('>BI8I2I2BI2I2I2',k,ns(),pc,a,d&255,cpu.state.IX.value,cpu.state.DE.value,cpu.state.BC.value));count=count+1
 end
end
spatial_taps={space:install_read_tap(0,0xffff,'spatial_reads',function(a,d,m)observe(0,a,d)end),space:install_write_tap(0,0xffff,'spatial_writes',function(a,d,m)observe(1,a,d)end),cpu.spaces.io:install_write_tap(0,0xff,'spatial_ym',function(a,d,m)observe(3,a,d)end)}
local function input()
 if finished then return end
 inputs:write(string.pack('>I8B',ns(),machine.ioport.ports[':mainpcb:IO1PORTA']:read()))
 for i=0,7 do local p=machine.ioport.ports[':mainpcb:ADC'..i];inputs:write(string.char(p and p:read() or 0x10))end
end
input()
if machine.debugger then machine.debugger.execution_state='run' end
dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')
spatial_inputs=emu.add_machine_frame_notifier(function()frames=frames+1;input()end)
emu.register_frame_done(function()
 if not finished and frames>=tonumber(os.getenv('AB2_ORACLE_PASSES') or '1800') then finished=true;out:write(string.pack('>BI8',255,ns()));out:close();inputs:close();print(string.format('captured %d spatial observations through %d frames',count,frames));machine:exit()end
end)
