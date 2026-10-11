-- Physical-input replay plus read-only frame observations. No CPU/state seeds.
local machine = manager.machine
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
local out = assert(io.open(os.getenv('AB2_ORACLE_OUT'),'w'))
out:write('frame,time_ns,mode,stage_index,lives,death_state,score,credits_bcd,carrier_state,refuel_state\n')
local space = machine.devices[':mainpcb:maincpu'].spaces.program
local frame = 0
local finished=false
apply(0)
local physical = assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.inputs', 'w'))
physical:write(string.format('AB2PHYS1\n%d %d %d %d\n',count,dipb,dipc,dipd))
local function record_pins(index)
  physical:write(string.format('%d %d',index,ports[':mainpcb:IO1PORTA']:read()))
  for i=0,7 do
    local port=ports[':mainpcb:ADC'..i]
    physical:write(' '..(port and port:read() or 16))
  end
  physical:write('\n')
end
record_pins(0)
natural_frame = emu.add_machine_frame_notifier(function()
  if finished then return end
  frame = frame + 1
  if frame == 1 then
    for i, name in ipairs({'IO1PORTB','IO1PORTC','IO1PORTD'}) do
      assert(ports[':mainpcb:'..name]:read() == ({dipb,dipc,dipd})[i], 'DIP mismatch after input update')
    end
  end
  out:write(string.format('%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n', frame,
    math.floor(machine.time:as_double()*1e9+0.5), space:read_u16(0x29c09c),
    space:read_u16(0x29c060), space:read_u16(0xffff88e0), space:read_u16(0xffff9124),
    space:read_u32(0xffff8884),space:read_u8(0xffff8052),space:read_u16(0xffff90b0),space:read_u16(0xffff90e0)))
  if frame == count then
    finished=true
    assert(not input:read('*a'):match('%S'), 'trailing physical input records')
    input:close();out:close();physical:close()
    if os.getenv('AB2_PC_CENSUS') then
      for _,cpu in ipairs({'main','sub'}) do
        local device=machine.devices[':mainpcb:'..cpu..'cpu']
        local item_index=assert(device.items['0/ab2_pc_census'],'missing passive PC census')
        local bitmap=emu.item(item_index)
        local pcs=assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.'..cpu..'.pcs','w'))
        local visited=0
        for word=0,0xffff do
          local bits=bitmap:read(word)
          for bit=0,31 do
            if (bits & (1 << bit))~=0 then
              pcs:write(string.format('%06x\n',word*64+bit*2));visited=visited+1
            end
          end
        end
        pcs:close();print(string.format('passive %s PC census: %d distinct physical instruction sites',cpu,visited))
      end
    end
    print(string.format('captured %d natural physical-input frames; observations only',frame))
    machine:exit()
  else apply(frame);record_pins(frame) end
end)
