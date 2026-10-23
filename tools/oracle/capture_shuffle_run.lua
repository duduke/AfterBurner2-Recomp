-- Execute the declared private ROM-data arrangement on original MAME. Native
-- expected states, CPU registers/PC and gameplay RAM are never imported.
-- The only state policy is the independently checked settled alternate palette
-- at the original ROM's completed initialization boundary.
local machine = manager.machine
local prefix = assert(os.getenv('AB2_SHUFFLE_PREFIX'), 'missing Shuffle prefix')
local complete = os.getenv('AB2_SHUFFLE_COMPLETE') == '1'
local cpu = machine.devices[':mainpcb:maincpu']
local space = cpu.spaces.program
local order = assert(io.open(prefix..'.order', 'r'))
assert(order:read('*l') == 'AB2SHUF1', 'invalid Shuffle order format')
local seed, count = order:read('*n', '*n')
assert(seed and count and count > 0 and count <= 64, 'invalid Shuffle order count')
local stages, seen = {}, {}
for position = 1, count do
  local stage = order:read('*n')
  assert(stage and stage >= 1 and stage <= count and not seen[stage], 'invalid Shuffle permutation')
  stages[position], seen[stage] = stage, true
end
assert(not order:read('*a'):match('%S'), 'trailing Shuffle order')
order:close()
assert(stages[count] == count, 'the native finale must remain last')
assert(space:read_u16(0x32480) == count, 'canonical stage count changed')
-- Derive preparation ownership from the canonical original event programs,
-- before replacing the data view. Only original predecessors containing18
-- prepare an alternate destination; no inferred scenery/stage name table.
local needs_alternate = {}
for previous = 0, count - 2 do
  local selector = space:read_u8(0x32482 + previous)
  local descriptor = space:read_u32(0x3249a + selector * 4)
  local event, alternate, terminal = space:read_u32(descriptor + 16), false, false
  for record = 1, 8192 do
    local command = space:read_u8(event + 2)
    if command == 18 then alternate = true end
    if command == 12 then terminal = true; break end
    if space:read_u16(event) == 0xffff then break end
    event = event + 8
  end
  assert(terminal, 'canonical predecessor lacks terminal event')
  needs_alternate[previous + 2] = alternate
end
local image_file = assert(io.open(prefix..'.rom', 'rb'))
local image = image_file:read('*a'); image_file:close()
assert(#image == 0x80000, 'invalid arranged ROM image size')
for address = 0, #image - 1 do space:write_direct_u8(address, image:byte(address + 1)) end
for address = 0, #image - 1 do
  assert(space:read_u8(address) == image:byte(address + 1), 'arranged ROM readback differs at '..address)
end
assert(space:read_u16(0x32480) == count, 'arranged stage count changed')

local input = assert(io.open(prefix..'.inputs', 'r'))
assert(input:read('*l') == 'AB2PHYS1', 'invalid input format')
local frames, dipb, dipc, dipd = input:read('*n', '*n', '*n', '*n')
assert(frames and frames > 0, 'invalid input frame count')
local ports = machine.ioport.ports
for i, name in ipairs({'IO1PORTB', 'IO1PORTC', 'IO1PORTD'}) do
  local requested = ({dipb,dipc,dipd})[i]
  assert(requested and requested >= 0 and requested <= 255, 'invalid DIP byte')
  for _, field in pairs(ports[':mainpcb:'..name].fields) do
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
  assert(index == frame and pins and pins >= 0 and pins <= 255, 'missing physical input frame')
  assert((pins & 0x87) == 0x87, 'unsupported physical buttons')
  for _, field in ipairs(fields) do
    assert(field[1]);field[1]:set_value((pins & field[2]) == 0 and 1 or 0)
  end
  for i = 0, 7 do
    local value = input:read('*n')
    assert(value and value >= 0 and value <= 255, 'invalid ADC input')
    if i < 3 then axes[i+1]:set_value(value)
    else assert(value == (i < 5 and 128 or 16), 'unsupported physical ADC') end
  end
end
local initialized, last_position, guard = false, nil, false
local boundaries, preparations = 0, 0
local function prepare(position)
  if not needs_alternate[stages[position + 1]] then return end
  local selector = space:read_u8(0x32482 + position)
  local descriptor = space:read_u32(0x3249a + selector * 4)
  local alternate = space:read_u32(descriptor + 4)
  local gradient = space:read_u32(0x140da + position * 4)
  space:write_u16(0x122f00, space:read_u16(alternate))
  -- Original $14424 uploads only the first long; preserve the sky tail.
  space:write_u32(0x122e60, space:read_u32(alternate + 4))
  for offset = 8, 20, 4 do space:write_u32(0x122e10 + offset - 8, space:read_u32(alternate + offset)) end
  for word = 0, 13 do space:write_u16(0x122f02 + word * 2, space:read_u16(gradient + 34 + word * 2)) end
  preparations = preparations + 1
end
-- RTS stack reads retain CURPC for the completed initializer. Restrict to the
-- current stack pointer so the second word of its return cannot repeat policy.
shuffle_run_boundary = space:install_read_tap(0x3f8000, 0x3fffff, 'shuffle_initialized', function(address)
  if guard then return end
  local pc = cpu.state.CURPC.value
  if pc ~= 0x13ce8 and pc ~= 0x138fe then return end
  local stack = (cpu.state.SR.value & 0x2000) ~= 0 and cpu.state.SP.value or cpu.state.USP.value
  if address ~= (stack & 0x3fffff) then return end
  guard = true
  if pc == 0x138fe then last_position = nil end
  local position = space:read_u16(0x29c060)
  if position < count and position ~= last_position then
    prepare(position)
    last_position = position
    initialized = true
    boundaries = boundaries + 1
  end
  guard = false
end)
local output_path = assert(os.getenv('AB2_ORACLE_OUT'))
local out = assert(io.open(output_path, 'w'))
out:write('frame,time_ns,mode,position,lives,score\n')
local physical = assert(io.open(output_path..'.inputs', 'w'))
physical:write(string.format('AB2PHYS1\n%d %d %d %d\n', frames, dipb, dipc, dipd))
local function record_pins(index)
  physical:write(string.format('%d %d', index, ports[':mainpcb:IO1PORTA']:read()))
  for i = 0, 7 do
    local port = ports[':mainpcb:ADC'..i]
    physical:write(' '..(port and port:read() or 16))
  end
  physical:write('\n')
end
local frame, finished = 0, false
local flight_positions, lifecycle = {}, {}
local flight_seen, returned_to_attract = false, false
-- Follow the established natural-credit probe: the immediately observed
-- cached digital port is the upcoming CPU-frame value, whereas ADC overrides
-- are immediately readable. Preserve both requested and actual pin streams.
apply(0);record_pins(0)
shuffle_run_frames = emu.add_machine_frame_notifier(function()
  if finished then return end
  frame = frame + 1
  if frame == 1 then
    for i, name in ipairs({'IO1PORTB','IO1PORTC','IO1PORTD'}) do
      assert(ports[':mainpcb:'..name]:read() == ({dipb,dipc,dipd})[i], 'physical DIP mismatch')
    end
  end
  local ordinal = space:read_u16(0x29c060)
  local position = initialized and ordinal < count and ordinal + 1 or 0
  local mode = space:read_u16(0x29c09c)
  if mode == 16 and position > 0 then flight_positions[position] = true end
  if mode == 16 then flight_seen = true end
  if flight_seen and not returned_to_attract then
    if lifecycle[#lifecycle] ~= mode then lifecycle[#lifecycle + 1] = mode end
    if mode == 0 or mode == 1 then returned_to_attract = true end
  end
  out:write(string.format('%d,%d,%d,%d,%d,%d\n', frame,
    math.floor(machine.time:as_double()*1e9+0.5), mode, position,
    space:read_u16(0xffff88e0), space:read_u32(0xffff8884)))
  if frame == frames then
    finished = true
    assert(not input:read('*a'):match('%S'), 'trailing physical input records')
    input:close();out:close();physical:close()
    assert(boundaries > 0 and preparations > 0, 'initialization/preparation was not observed')
    if complete then
      for position = 1, count do assert(flight_positions[position], 'complete Shuffle missed a flight position') end
      local expected = {16,17,18,19,20,21,22,25,26}
      assert(returned_to_attract and #lifecycle == #expected + 1, 'complete Shuffle lifecycle incomplete')
      for index, mode in ipairs(expected) do assert(lifecycle[index] == mode, 'complete Shuffle ending order differs') end
      assert(lifecycle[#lifecycle] == 0 or lifecycle[#lifecycle] == 1, 'complete Shuffle did not return to attract')
    end
    print(string.format('captured %d continuous arranged-Shuffle physical frames; %d initialization boundaries, %d palette preparations; observations only', frame, boundaries, preparations))
    machine:exit()
  else apply(frame);record_pins(frame) end
end)
