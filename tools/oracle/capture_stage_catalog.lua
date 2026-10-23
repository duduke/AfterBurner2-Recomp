-- Passive stage metadata observations after original MAME cold boot/start.
-- This verifies ordered descriptors, not later-stage initialization or play.
local out_path = assert(os.getenv("AB2_ORACLE_OUT"))
local space = manager.machine.devices[":mainpcb:maincpu"].spaces.program
local buttons = manager.machine.ioport.ports[":mainpcb:IO1PORTA"].fields
local frame, complete = 0, false
stage_catalog_frames = emu.add_machine_frame_notifier(function()
  if complete then return end
  frame = frame + 1
  buttons["Coin 1"]:set_value(frame >= 360 and frame < 372 and 1 or 0)
  buttons["1 Player Start"]:set_value(frame >= 420 and frame < 432 and 1 or 0)
  if frame < 432 or space:read_u16(0x29c09c) ~= 16 then return end
  local count = space:read_u16(0x29c00e)
  assert(count > 0 and count <= 64 and count == space:read_u16(0x32480))
  local out = assert(io.open(out_path, "wb"))
  out:write("AB2SC001", string.pack(">I2", count))
  for number = 1, count do
    -- Observe the selector copied by the executing ROM, separately from the
    -- native catalog's direct reads of the ordered ROM selector source.
    local selector = space:read_u8(0x29c010 + number - 1)
    assert(selector == space:read_u8(0x32482 + number - 1))
    local descriptor = space:read_u32(0x3249a + selector * 4)
    local events = space:read_u32(descriptor + 16)
    if number == 1 then assert(descriptor == space:read_u32(0xffff8a0c)) end
    out:write(string.pack(">I2I1I4I4", number, selector, descriptor, events))
  end
  out:close()
  complete = true
  print(string.format("captured %d original-MAME stage catalog entries at frame %d -> %s", count, frame, out_path))
  manager.machine:exit()
end)
