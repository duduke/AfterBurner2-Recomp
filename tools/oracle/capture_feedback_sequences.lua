-- Passive landing/departure/refueling witnesses. Physical inputs only.
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

local space=machine.devices[':mainpcb:maincpu'].spaces.program
local main=machine.devices[':mainpcb:maincpu']
local out=assert(io.open(assert(os.getenv('AB2_ORACLE_OUT')),'w'))
out:write('AB2SEQH1\n')
local limit=math.min(count,tonumber(os.getenv('AB2_ORACLE_PASSES') or '30000'))
local frame,base=0,0
local function ns()return math.floor(machine.time:as_double()*1e9+0.5)end
local function record(a,data,mask)
 local pc=main.state.CURPC.value
 local old=(a&~1)==0xe8016 and 0 or space:read_u16(a&~1)
 if (a&63)==18 and data==0x11be and mask==0xffff and space:read_u16(a-2)==1 then base=a-18 end
 out:write(string.format('W %d %d %d %d %d %d %d %d %d\n',ns(),pc,a,data,mask,space:read_u16(0x29c09c),space:read_u8(0xffff90fe)&1,base,old))
end
sequence_pool=space:install_write_tap(0x3fa000,0x3fdfff,'sequence_objects',function(a,d,m)
 local pc=main.state.CURPC.value
 if pc==0x111a6 or pc==0x11406 or pc==0x114e8 or ((a&63)==18 and d==0x11be) then record(a,d,m) end
end)
sequence_carrier=space:install_write_tap(0x3f90b0,0x3f90b1,'sequence_carrier',record)
sequence_nozzle=space:install_write_tap(0x3f9102,0x3f9103,'sequence_nozzle',record)
sequence_latch=space:install_write_tap(0xe8016,0xe8017,'sequence_latch',record)
local physical=assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.inputs','w'))
physical:write(string.format('AB2PHYS1\n%d %d %d %d\n',limit,dipb,dipc,dipd))
local function record_pins(index)
 physical:write(string.format('%d %d',index,ports[':mainpcb:IO1PORTA']:read()))
 for i=0,7 do local port=ports[':mainpcb:ADC'..i];physical:write(' '..(port and port:read() or 16)) end
 physical:write('\n')
end
apply(0);record_pins(0)
sequence_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 local handler=base~=0 and space:read_u32(base+16) or 0
 if base~=0 and (space:read_u8(base)&0x80)==0 then handler=0 end
 out:write(string.format('F %d %d %d %d %d %d %d %d %d %d %d\n',frame,ns(),space:read_u16(0x29c09c),space:read_u16(0xffff90b0),space:read_u16(0x29c0aa),space:read_u16(0xffff90ae),space:read_u8(0xffff90fe)&1,handler,base~=0 and space:read_u16(base+40) or 0,base~=0 and (space:read_u8(base+60)&1) or 0,base~=0 and space:read_u16(base+54) or 0))
 if frame>=limit then
  input:close();physical:close();out:close();print(string.format('captured %d sequence haptics frames',frame));machine:exit()
 else apply(frame);record_pins(frame) end
end)
