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
local path=assert(os.getenv('AB2_ORACLE_OUT'));local out=assert(io.open(path,'w'));out:write('AB2FINH1\n')
local limit=math.min(count,tonumber(os.getenv('AB2_ORACLE_PASSES') or '74452'));local frame=0
local function ns()return math.floor(machine.time:as_double()*1e9+0.5)end
finale_pool=space:install_write_tap(0x3fa000,0x3fdfff,'finale_contact',function(a,d,m)
 local pc=main.state.CURPC.value
 if pc==0x11b18 or pc==0x11f4c then
  local p=a-(pc==0x11b18 and 18 or 24)
  out:write(string.format('W %d %d %d %d %d %d %d %d\n',ns(),pc,a,d,m,space:read_u16(0x29c09c),space:read_u16(a),space:read_u32(p+16)))
 end
end)
local physical=assert(io.open(path..'.inputs','w'));physical:write(string.format('AB2PHYS1\n%d %d %d %d\n',limit,dipb,dipc,dipd))
local function pins(i)
 physical:write(string.format('%d %d',i,ports[':mainpcb:IO1PORTA']:read()))
 for a=0,7 do local p=ports[':mainpcb:ADC'..a];physical:write(' '..(p and p:read() or 16))end
 physical:write('\n')
end
apply(0);pins(0)
finale_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 local p,handler,flags=0,0,0
 for a=0x3fa000,0x3fdfc0,64 do
  local h=space:read_u32(a+16)
  if (space:read_u8(a)&0x80)~=0 and (h==0x11e3e or h==0x11f16 or h==0x120aa) then
   assert(p==0,'ambiguous finale aircraft');p=a;handler=h;flags=space:read_u16(a+24)
  end
 end
 out:write(string.format('F %d %d %d %d %d %d %d %d %d %d %d\n',frame,ns(),space:read_u16(0x29c09c),space:read_u16(0x29c0aa),p,handler,flags,space:read_u16(0x29c060),p~=0 and space:read_u16(p+36) or 0,p~=0 and space:read_u16(p+44) or 0,p~=0 and space:read_u16(p-28) or 0))
 if frame>=limit then out:close();physical:close();input:close();print(string.format('captured %d finale haptics frames',frame));machine:exit()
 else apply(frame);pins(frame) end
end)
