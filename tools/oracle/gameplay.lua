-- Deterministic input script for fresh-NVRAM aburner2 runs. set_value on
-- digital fields is logical pressed/released; analog values are raw 8-bit.
-- Used only when AB2_ORACLE_GAMEPLAY=1. No RAM cheats or simulated spawns.
local ports = manager.machine.ioport.ports
local buttons = ports[":mainpcb:IO1PORTA"].fields
local coin, start = assert(buttons["Coin 1"]), assert(buttons["1 Player Start"])
local vulcan, missile = assert(buttons["Vulcan"]), assert(buttons["Missile"])
local x = assert(ports[":mainpcb:ADC0"].fields["AD Stick X"])
local y = assert(ports[":mainpcb:ADC1"].fields["AD Stick Y"])
local throttle = assert(ports[":mainpcb:ADC2"].fields["AD Stick Z"])
local frame = 0
gameplay_inputs = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  coin:set_value(frame >= 360 and frame < 372 and 1 or 0)
  start:set_value(frame >= 420 and frame < 432 and 1 or 0)
  if frame < 480 then
    x:set_value(0x80); y:set_value(0x80); throttle:set_value(0x80)
    vulcan:set_value(0); missile:set_value(0)
  else
    if os.getenv("AB2_ORACLE_INPUT_ENDPOINTS")=="1" then
      -- Physical input fixtures: hold each lane endpoint long enough for
      -- all six ADC samples; include all four weapon-button combinations.
      local phase=((frame-480)//24)%12
      x:set_value(({0x80,0x20,0xe0,0x80,0x80,0x80,0x80,0x20,0xe0,0x80,0x80,0x80})[phase+1])
      y:set_value(({0x80,0x80,0x80,0x40,0xc0,0x80,0x80,0xc0,0x40,0x80,0x80,0x80})[phase+1])
      throttle:set_value(phase==5 and 0 or phase==6 and 0xff or 0x80)
      vulcan:set_value((phase==7 or phase==9 or phase==10) and 1 or 0)
      missile:set_value((phase==8 or phase==9 or phase==10) and 1 or 0)
      return
    end
    -- Optional horizon/roll raster capture: reverse full horizontal input
    -- every 60 frames. Only physical inputs change; no video/RAM patching.
    local rolls = os.getenv("AB2_ORACLE_ROLLS") == "1"
    local phase = ((frame - 480) // (rolls and 60 or 120)) % 4
    x:set_value((rolls and {0x20, 0xe0, 0x20, 0xe0} or {0x30, 0x80, 0xd0, 0x80})[phase + 1])
    y:set_value(({0x60, 0xa0, 0x80, 0x80})[phase + 1])
    throttle:set_value(phase % 2 == 0 and 0xe0 or 0x40)
    vulcan:set_value(1)
    missile:set_value(frame % 90 < 12 and 1 or 0)
  end
  if frame == 432 then print("gameplay inputs: coin and start pulses delivered") end
end)
