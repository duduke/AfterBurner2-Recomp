-- MAME autoboot script: record the sub CPU's sprite pass ($000640) as
-- before/after snapshots of the 32KB block of RAM shared with the main CPU.
--
-- A pass starts when the sub CPU, spinning at $000640 on subram0[$D6] bit 0,
-- reads it clear (main CPU VBLANK has released it). It ends when the sub CPU
-- sets subram0[$D6] bit 1 at $000458. Each record is
--   u32 pass, u32 frame, 32768 bytes before, 32768 bytes after
-- where the 32KB block is the sub CPU's A6-relative view ($09C000-$0A3FFF:
-- subram0 then subram1), read through the main CPU's alias ($29C000-$2A3FFF)
-- so the snapshot itself doesn't trip the taps below.
--
-- Environment: AB2_ORACLE_OUT (output file), AB2_ORACLE_PASSES (stop after N).

local out_path = os.getenv("AB2_ORACLE_OUT") or "subsprites.bin"
local max_passes = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")

local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local main_space = main.spaces["program"]
local sub_space = sub.spaces["program"]
local sub_pc = sub.state["CURPC"]

local out = assert(io.open(out_path, "wb"))
out:write("AB2ORCL1")

local function u32(v)
  return string.char((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff)
end

local function snapshot()
  return main_space:read_range(0x29C000, 0x2A3FFF, 8)
end

local frame, passes = 0, 0
local before = nil

-- Keep handler references alive: a collected tap is uninstalled.
frame_sub = emu.add_machine_frame_notifier(function() frame = frame + 1 end)

pass_start = sub_space:install_read_tap(0x9C0D6, 0x9C0D7, "ab2_pass_start", function(offset, data, mask)
  if before == nil and sub_pc.value == 0x000640 and ((data >> 8) & 1) == 0 then
    before = snapshot()
  end
end)

pass_end = sub_space:install_write_tap(0x9C0D6, 0x9C0D7, "ab2_pass_end", function(offset, data, mask)
  if before ~= nil and sub_pc.value == 0x000458 then
    out:write(u32(passes), u32(frame), before, snapshot())
    before = nil
    passes = passes + 1
    if passes >= max_passes then
      out:close()
      print(string.format("captured %d sprite passes through frame %d -> %s", passes, frame, out_path))
      manager.machine:exit()
    end
  end
end)
