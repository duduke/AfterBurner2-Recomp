-- Hardware oracle: boot-time main 315-5250 writes/reads, raster EXCK ticks,
-- IRQ acknowledgement, Z80 sound-latch reads, and directed sub comparator
-- probes (signed bounds, history, no-history alias, byte writes, mirrors).
-- Record: u32 rising-edge count, u32 PC, u8 kind/chip, u16 data/mask,
--         u8 main IRQ state/reserved. kinds: write=1/read=2/Z80=3/check=4.
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local sound = manager.machine.devices[":mainpcb:soundcpu"]
local space = main.spaces.program
local irq = emu.item(assert(manager.machine.devices[":mainpcb"].items["0/m_timer_irq_state"]))
local first_rise = manager.machine.time:as_double() + manager.machine.screens[":mainpcb:screen"]:time_until_pos(1)
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"))
out:write("AB2CMP01")
local frame, records = 0, 0
local function record(kind,chip,pc,data,mask)
  -- Ask the screen for the initial phase: a fresh MAME screen starts at
  -- line 224, not line 0. EXCK rises on line 1, then every 128 us (262
  -- lines/frame is even). Main/sub CPU taps see scheduler time.
  local ticks = math.max(0,1+math.floor((manager.machine.time:as_double() - first_rise) / 0.000128 + 1e-7))
  out:write(string.pack(">I4I4BBI2I2BB",ticks,pc,kind,chip,data,mask,irq:read(0),0))
  records = records + 1
end
compare_main_w = space:install_write_tap(0xe8000,0xebfff,"cmp_main_w",function(a,d,m)
  record(1,0,(a >> 1) & 15,d,m)
end)
compare_main_r = space:install_read_tap(0xe8000,0xebfff,"cmp_main_r",function(a,d,m)
  record(2,0,(a >> 1) & 15,d,m)
end)
compare_submain_w = space:install_write_tap(0x2e8000,0x2ebfff,"cmp_submain_w",function(a,d,m)
  record(1,1,(a >> 1) & 7,d,m)
end)
compare_submain_r = space:install_read_tap(0x2e8000,0x2ebfff,"cmp_submain_r",function(a,d,m)
  record(2,1,(a >> 1) & 7,d,m)
end)
compare_sub_w = sub.spaces.program:install_write_tap(0xe8000,0xebfff,"cmp_sub_w",function(a,d,m)
  record(1,1,(a >> 1) & 7,d,m)
end)
compare_sub_r = sub.spaces.program:install_read_tap(0xe8000,0xebfff,"cmp_sub_r",function(a,d,m)
  record(2,1,(a >> 1) & 7,d,m)
end)
compare_sound_r = sound.spaces.io:install_read_tap(0x40,0x7f,"cmp_sound_r",function(a,d,m)
  record(3,0,0,d,m)
end)
compare_frames = emu.add_machine_frame_notifier(function()
  frame = frame + 1
  record(4,0,0,0,0)
  if frame == 40 then
    local base = 0x2ebff0 -- exercise the last mirrored sub chip window
    for _, bounds in ipairs({{0xff80,0x007f},{0x100,0xff00},{0x8000,0x7fff},{0,0}}) do
      space:write_u16(base,bounds[1]); space:write_u16(base+2,bounds[2])
      space:write_u16(base+8,0) -- clear history
      for _, value in ipairs({0x8000,0xff00,0xff80,0xffff,0,1,0x7f,0x100,0x7fff}) do
        space:write_u16(base+4,value)
        for r=0,7 do space:read_u16(base+r*2) end
      end
      space:write_u8(base+12,0x12); space:write_u8(base+13,0x34) -- no-history alias
      for r=0,7 do space:read_u16(base+r*2) end
      space:write_u8(base,0x7f); space:write_u8(base+3,0x80)
      for r=0,7 do space:read_u16(base+r*2) end
    end
    space:write_u16(base,0xff80); space:write_u16(base+2,0x7f); space:write_u16(base+8,0)
    for i=1,18 do space:write_u16(base+4,0); space:read_u16(base+8) end
  end
  if frame >= tonumber(os.getenv("AB2_ORACLE_PASSES") or "300") then
    out:close(); print(string.format("captured %d compare/timer events through frame %d",records,frame))
    manager.machine:exit()
  end
end)
