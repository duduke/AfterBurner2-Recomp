-- Passive music metadata oracle. Only physical input fields change.
local machine=manager.machine
local cpu=machine.devices[':mainpcb:soundcpu']
local space=cpu.spaces.program
local path=assert(os.getenv('AB2_ORACLE_OUT'))
local out=assert(io.open(path,'wb'));out:write('AB2MUS01')
local inputs=assert(io.open(path..'.inputs','wb'));inputs:write('AB2MIP01')
for _,name in ipairs({'IO1PORTB','IO1PORTC','IO1PORTD'})do inputs:write(string.char(machine.ioport.ports[':mainpcb:'..name]:read()))end
local roots={}
for i,cmd in ipairs({0x92,0x91,0x94,0x93,0x95,0x96})do roots[space:read_u16(0x735+2*(cmd-0x40))]=i end
local function ns()return math.floor(machine.time:as_double()*1e9+0.5)end
local finished=false
local function input(time)
 if finished then return end
 inputs:write(string.pack('>I8B',time,machine.ioport.ports[':mainpcb:IO1PORTA']:read()))
 for i=0,7 do local p=machine.ioport.ports[':mainpcb:ADC'..i];inputs:write(string.char(p and p:read() or 0x10))end
end
input(0)
local track,playing,mask,ym,attenuation=0,0,0,0,0
local count,frames,guard=0,0,false
local function observe(kind,a,d)
 if guard or finished then return end
 local pc=cpu.state.CURPC.value
 local ix=cpu.state.IX.value
 local interesting=false
 if kind==0 and pc==0xc76 and roots[a] then track=roots[a];playing=1;attenuation=0;interesting=true end
 if kind==1 and a==0xf810 and d==0 and (pc==0x658 or pc==0x68f)then playing=0;interesting=true end
 if kind==1 and a==0xfeb8 and playing==1 then attenuation=d;interesting=true end
 if kind==1 and a>=0xf820 and a<0xfad0 and (a-0xf820)%43==0 then
  interesting=true
  if playing==1 then
   guard=true;local active=false
   for i=0,15 do
    local address=0xf820+i*43
    local value=address==a and d or space:read_u8(address)
    if value&0x80~=0 then active=true end
   end
   guard=false;if not active then playing=0 end
  end
 end
 if kind==1 and a>=0xf000 and a<0xf100 then interesting=true end
 if kind==3 and (a&255)==0 then ym=d;interesting=true end
 if kind==3 and (a&255)==1 then
  interesting=true
  if ix>=0xf820 and ix<=0xfe82 and (ix-0xf820)%43==0 then
   local ch=nil
   if ym>=0x20 then ch=ym&7 end
   if ym==8 and d&0x78~=0 then ch=d&7 end
   if ch then if ix<0xfad0 then mask=mask|(1<<ch)else mask=mask&~(1<<ch)end end
  end
 end
 if interesting then out:write(string.pack('>BI8I2I2BI2BBBB',kind,ns(),pc,a,d&255,ix,track,playing,mask,attenuation));count=count+1 end
end
music_routing_taps={
 space:install_read_tap(0,0xffff,'music_read',function(a,d,m)observe(0,a,d)end),
 space:install_write_tap(0,0xffff,'music_write',function(a,d,m)observe(1,a,d)end),
 cpu.spaces.io:install_write_tap(0,0xff,'music_io',function(a,d,m)observe(3,a,d)end)
}
if machine.debugger then machine.debugger.execution_state='run' end
dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')
local limit=tonumber(os.getenv('AB2_ORACLE_PASSES') or '781')
music_input_observer=emu.add_machine_frame_notifier(function()
 frames=frames+1;input(ns())
end)
emu.register_frame_done(function()
 if finished then return end
 if frames>=limit then
  finished=true
  out:write(string.pack('>BI8',255,ns()));out:close();inputs:close()
  print(string.format('captured %d music ownership observations through %d frames',count,frames))
  machine:exit()
 end
end)
