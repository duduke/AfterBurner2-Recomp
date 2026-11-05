-- Passive object-to-sound ownership witnesses; physical inputs only.
local machine=manager.machine
local input = assert(io.open(assert(os.getenv('AB2_NATURAL_INPUT')), 'r'))
assert(input:read('*l') == 'AB2PHYS1', 'invalid input version')
local count, dipb, dipc, dipd = input:read('*n', '*n', '*n', '*n')
assert(count and count > 0, 'invalid frame count')
local ports = machine.ioport.ports
for i, name in ipairs({'IO1PORTB', 'IO1PORTC', 'IO1PORTD'}) do
  local port = ports[':mainpcb:'..name]
  local requested = ({dipb,dipc,dipd})[i]
  for _, field in pairs(port.fields) do
    if field.settings and next(field.settings) then field.user_value = requested & field.mask end
  end
end
local buttons = ports[':mainpcb:IO1PORTA'].fields
local fields = {{buttons['Coin 1'],64},{buttons['1 Player Start'],8},
                {buttons['Vulcan'],16},{buttons['Missile'],32}}
local axes = {}
for i, name in ipairs({'AD Stick X','AD Stick Y','AD Stick Z'}) do
  axes[i] = assert(ports[':mainpcb:ADC'..(i-1)].fields[name])
end
local function apply(frame)
  local index, pins = input:read('*n', '*n')
  assert(index == frame and pins and pins >= 0 and pins <= 255, 'missing input frame')
  -- This harness supports only the documented game controls. Refuse silently
  -- dropped service/extra button or ADC values rather than mislabel a replay.
  assert((pins & 0x87) == 0x87, 'unsupported physical button')
  for _, field in ipairs(fields) do assert(field[1]); field[1]:set_value((pins & field[2]) == 0 and 1 or 0) end
  for i=0,7 do
    local value = input:read('*n'); assert(value and value >= 0 and value <= 255, 'invalid ADC')
    if i < 3 then axes[i+1]:set_value(value)
    else assert(value == (i<5 and 128 or 16), 'unsupported physical ADC') end
  end
end

local main=machine.devices[':mainpcb:maincpu'];local mem=main.spaces.program
local z80=machine.devices[':mainpcb:soundcpu'];local sound=z80.spaces.program
local path=assert(os.getenv('AB2_ORACLE_OUT'));local out=assert(io.open(path,'w'));local expanded=os.getenv('AB2_OBJECT_FAMILIES')=='1';out:write(expanded and 'AB2OBJ03\n' or 'AB2OBJ02\n')
local limit=math.min(count,tonumber(os.getenv('AB2_ORACLE_PASSES') or '30000'));local frame=0;local finished=false;local main_count=0
local function ns()return math.floor(machine.time:as_double()*1e9+.5)end
local families={}
local function main_write(a,d,m)
 if finished then return end
 local pc=main.state.CURPC.value
 if a>=0x3fa000 and a<0x3fe000 then if (a&63)~=0 and (a&63)~=16 and (a&63)~=18 then return end end
 local obj=main.state.A5.value&0x3fffff;local sp=(((main.state.SR.value&0x2000)~=0) and main.state.SP.value or main.state.USP.value)&0x3fffff
 local ret=pc==0xb868 and sp>=0x3f8000 and sp<=0x3ffffc and mem:read_u32(sp) or 0
 if expanded then
  if a>=0x3fa000 and a<0x3fe000 and (a&63)==0 and (m&0xff00)~=0 and (d&0x8000)==0 then families[a]=nil end
  if pc==0xb868 and obj>=0x3fa000 and obj<0x3fe000 then
   if ret==0x7bac then families[obj-64]=2 end
   if ret==0xa026 or ret==0x14da0 then families[obj]=2 elseif ret==0x91ac then families[obj]=3 end
  end
 end
 local handler,flags,x,y,z,px,py,pz=0,0,0,0,0,0,0,0
 if pc==0xb868 and (d&255==0x6e or d&255==0x6f or (d>>8)==0x6e or (d>>8)==0x6f) and obj>=0x3fa000 and obj<0x3fe000 then
  handler=mem:read_u32(obj+16);flags=mem:read_u8(obj);x=mem:read_u32(obj+24);y=mem:read_u32(obj+30);z=mem:read_u16(obj+36)
  px=mem:read_u16(0xffff8d14);py=mem:read_u16(0xffff8d16);pz=mem:read_u16(0xffff8d18)
 end
 main_count=main_count+1
 out:write(string.format('M %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n',ns(),pc,a,d,m,obj,sp,main.state.D1.value,ret,mem:read_u16(0x29c09c),handler,flags,x,y,z,px,py,pz))
end
object_taps={mem:install_write_tap(0x3fa000,0x3fdfff,'object_slots',main_write),mem:install_write_tap(0x3f8900,0x3f894b,'object_queue',main_write),mem:install_write_tap(0xe8016,0xe8017,'object_latch',main_write),mem:install_write_tap(0x140004,0x140005,'object_reset',main_write)}
-- Read-only witness of the actual flyby comparison, including the unsigned
-- wrap in depth-$1FE0. Absence is reported, never synthesized as coverage.
if expanded then object_flyby_reads=mem:install_read_tap(0x3fa000,0x3fdfff,'flyby_predicate',function(a,d,m)
 if not finished and main.state.CURPC.value==0x156ec and (a&63)==36 then
  out:write(string.format('B %d %d %d %d\n',ns(),a-36,d,mem:read_u16(0xffff8d18)))
 end
end) end
local function observe(k,a,d)
 if finished then return end
 local pc=z80.state.CURPC.value
 if (k==0 and ((pc==0x6eb or pc==0x702) and d~=0x80 or (pc==0x6f7 or pc==0x70c) and a>=0xf800 and a<0xf818 or pc==0xc76 or pc==0x17a2)) or
    (k==1 and (a>=0xf800 and a<0xf818 or a>=0xf820 and a<=0xfe82 and (a-0xf820)%43==0 or a>=0xf000 and a<0xf100 and (a&0x87)==0x86)) or k==2 and a>=0x40 and a<0x80 or k==3 then
  out:write(string.format('Z %d %d %d %d %d %d %d %d %d\n',ns(),k,pc,a,d&255,z80.state.IX.value,z80.state.DE.value,z80.state.BC.value,z80.state.AF.value))
 end
end
object_sound_taps={sound:install_read_tap(0,0xffff,'object_sound_reads',function(a,d,m)observe(0,a,d)end),sound:install_write_tap(0xf000,0xffff,'object_sound_writes',function(a,d,m)observe(1,a,d)end),z80.spaces.io:install_read_tap(0,0xff,'object_sound_io_reads',function(a,d,m)observe(2,a,d)end),z80.spaces.io:install_write_tap(0,0xff,'object_sound_io_writes',function(a,d,m)observe(3,a,d)end)}
local physical=assert(io.open(path..'.inputs','w'));physical:write(string.format('AB2PHYS1\n%d %d %d %d\n',limit,dipb,dipc,dipd))
local function pins(i)
 physical:write(string.format('%d %d',i,ports[':mainpcb:IO1PORTA']:read()))
 for a=0,7 do local p=ports[':mainpcb:ADC'..a];physical:write(' '..(p and p:read() or 16))end
 physical:write('\n')
end
if machine.debugger then
 machine.debugger:command('dasm "'..path..'.z80.txt",c74,90,0,:mainpcb:soundcpu')
 machine.debugger:command('dasm "'..path..'.direct.txt",17a2,80,0,:mainpcb:soundcpu')
 machine.debugger.execution_state='run'
end
apply(0);pins(0)
object_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 for p=0x3fa000,0x3fdfc0,64 do
  if (mem:read_u8(p)&0x80)~=0 and mem:read_u32(p+16)==0x156ce then
   out:write(string.format('P %d %d %d %d %d %d %d %d\n',p,mem:read_u32(p+24),mem:read_u32(p+30),mem:read_u16(p+36),mem:read_u16(0xffff8d14),mem:read_u16(0xffff8d16),mem:read_u16(0xffff8d18),ns()))
  end
 end
 if expanded then for p,kind in pairs(families) do
  if (mem:read_u8(p)&0x80)~=0 then
   local base=mem:read_u32(p+54)&0x3fffff
   local bx,by=0,0
   if kind==3 and base>=0x3f8000 and base<=0x3ffff8 then bx=mem:read_u32(base);by=mem:read_u32(base+4) end
   out:write(string.format('Q %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n',p,kind,mem:read_u16(p+42),mem:read_u16(p+44),mem:read_u16(p+36),mem:read_u16(0xffff8d14),mem:read_u16(0xffff8d16),mem:read_u16(0xffff8d18),base,bx,by,mem:read_u16(0x29c092),mem:read_u16(0x29c090),mem:read_u16(0x29c09e),ns()))
  end
 end end
 out:write(string.format('F %d %d\n',frame,ns()))
 if frame>=limit then assert(main_count>0,'main ownership tap did not record');finished=true;out:close();physical:close();input:close();print(string.format('captured %d object audio frames',frame));machine:exit()
 else apply(frame);pins(frame) end
end)
