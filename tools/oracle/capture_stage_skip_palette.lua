-- Directed debug-startup evidence, not natural stage progression.
-- Original MAME executes the previous stage's command 18 and palette IRQs.
-- Capture only the fourteen alternate road gradient words after they settle.
local out_path = assert(os.getenv("AB2_ORACLE_OUT"))
local stage = tonumber(os.getenv("AB2_ORACLE_STAGE") or "8")
local space = manager.machine.devices[":mainpcb:maincpu"].spaces.program
local buttons = manager.machine.ioport.ports[":mainpcb:IO1PORTA"].fields
local frame, phase, blending = 0, 0, false
assert(stage >= 2 and stage <= space:read_u16(0x32480))
stage_palette_frames = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  buttons["Coin 1"]:set_value(frame >= 360 and frame < 372 and 1 or 0)
  buttons["1 Player Start"]:set_value(frame >= 420 and frame < 432 and 1 or 0)
  if phase == 0 then
    if space:read_u16(0x29c09c) ~= 16 then return end
    local previous = stage - 2
    local descriptor = space:read_u32(0x3249a + space:read_u8(0x29c010 + previous) * 4)
    local event, alternate = space:read_u32(descriptor + 16), nil
    for n = 1, 8192 do
      if space:read_u8(event + 2) == 18 then alternate = event end
      if space:read_u8(event + 2) == 12 or space:read_u16(event) == 0xffff then break end
      event = event + 8
    end
    if not alternate then
      phase = 3
      error("Selected stage has no preceding alternate palette event")
    end
    space:write_u16(0x29c060, previous)
    space:write_u16(0xffff8d30, previous * 4)
    space:write_u32(0xffff8a08, alternate)
    space:write_u32(0x29c006, space:read_u16(alternate) << 4)
    phase = 1
  elseif phase == 1 then
    if space:read_u8(0x29c54c) & 2 ~= 0 then blending = true end
    if blending and space:read_u8(0x29c54c) & 2 == 0 then
      local out = assert(io.open(out_path, "wb"))
      out:write("AB2NSP01", string.pack(">I2", stage), space:read_range(0x122f02, 0x122f1d, 8))
      out:close()
      phase = 2
      print(string.format("captured stage %d alternate road palette at frame %d -> %s", stage, frame, out_path))
      manager.machine:exit()
    end
  end
end)
