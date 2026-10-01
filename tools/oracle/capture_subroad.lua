-- MAME autoboot script: record the sub CPU's per-frame road work
-- ($00046A-$00052C) for tests/oracle_subroad.cpp.
--
-- Window: from the sub CPU's road control write at $000466 (just after the
-- read at $000464 flipped road RAM) to its read of subram0[$54A] at $000530.
-- Each record holds the state at the start of the window, the state at its
-- end, and every shared/road RAM byte the sub CPU itself wrote in between
-- (writes from its VBLANK handler, $00055E-$000593, excluded). Comparing write
-- sets — not whole snapshots — keeps the main CPU's concurrent writes out of
-- the comparison.
--
-- Record: u32 index, u32 frame,
--         32768 shared + 4096 road + 16 chip-register bytes (start),
--         32768 shared + 4096 road (end),
--         u32 n + n x u16 shared offsets written, u32 n + n x u16 road offsets written.
-- Chip registers: multiplier 0,1; divider 0,1,2,4,5,6 (big-endian words).
--
-- Environment: AB2_ORACLE_OUT (output file), AB2_ORACLE_PASSES (stop after N).

local out_path = os.getenv("AB2_ORACLE_OUT") or "subroad.bin"
local max_records = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")

local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local main_space = main.spaces["program"]
local sub_space = sub.spaces["program"]
local sub_pc = sub.state["CURPC"]

local out = assert(io.open(out_path, "wb"))
out:write("AB2ORCL2")

local function u32(v)
  return string.char((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff)
end
local function u16(v) return string.char((v >> 8) & 0xff, v & 0xff) end

-- Read through the main CPU's aliases so the taps below don't see it.
local function shared() return main_space:read_range(0x29C000, 0x2A3FFF, 8) end
local function road() return main_space:read_range(0x2EC000, 0x2ECFFF, 8) end
local function chips()
  local s = ""
  for _, a in ipairs({0xE0000, 0xE0002, 0xE4000, 0xE4002, 0xE4004, 0xE4008, 0xE400A, 0xE400C}) do
    s = s .. u16(sub_space:read_u16(a))
  end
  return s
end

local function offsets(set)
  local list = {}
  for o in pairs(set) do list[#list + 1] = o end
  table.sort(list)
  local parts = {u32(#list)}
  for _, o in ipairs(list) do parts[#parts + 1] = u16(o) end
  return table.concat(parts)
end

local frame, records = 0, 0
local start = nil
local shared_written, road_written = {}, {}

local function in_vblank_handler(pc) return pc >= 0x00055E and pc < 0x000594 end

-- The road work and its callees. A write from anywhere else means the
-- window didn't run normally (e.g. the main CPU reset the sub CPU during
-- boot), so it's dropped rather than recorded.
local function in_road_code(pc)
  return (pc >= 0x00046A and pc < 0x000538) or (pc >= 0x000C0A and pc < 0x0011D4) or (pc >= 0x0016D4 and pc < 0x001900)
end
local dropped = 0

local function check_pc(pc)
  if not in_road_code(pc) then start = nil; dropped = dropped + 1 end
end

local function mark(set, base, offset, mask)
  if mask & 0xff00 ~= 0 then set[base] = true end
  if mask & 0x00ff ~= 0 then set[base + 1] = true end
end

frame_sub = emu.add_machine_frame_notifier(function() frame = frame + 1 end)

window_start = sub_space:install_write_tap(0xEE000, 0xEFFFF, "ab2_road_start", function(offset, data, mask)
  if start == nil and sub_pc.value == 0x000466 then
    start = shared() .. road() .. chips()
    shared_written, road_written = {}, {}
  end
end)

-- Stack pushes (top of subram1) come from wherever the PC happens to be, so
-- they're logged without the PC check; the test excludes the stack area.
local function is_stack(o) return o >= 0x7F00 end

shared_log = sub_space:install_write_tap(0x80000, 0xBFFFF, "ab2_road_shared", function(offset, data, mask)
  local pc = sub_pc.value
  if start ~= nil and not in_vblank_handler(pc) then
    local a = offset & 0xFFFFE
    local o = (a < 0xA0000 and 0 or 0x4000) + (a & 0x3FFF)
    if not is_stack(o) then check_pc(pc) end
    if start ~= nil then mark(shared_written, o, offset, mask) end
  end
end)

road_log = sub_space:install_write_tap(0xEC000, 0xEDFFF, "ab2_road_ram", function(offset, data, mask)
  local pc = sub_pc.value
  if start ~= nil and not in_vblank_handler(pc) then
    check_pc(pc)
    if start ~= nil then mark(road_written, offset & 0xFFE, offset, mask) end
  end
end)

window_end = sub_space:install_read_tap(0x9C54A, 0x9C54B, "ab2_road_end", function(offset, data, mask)
  if start ~= nil and sub_pc.value == 0x000530 then
    out:write(u32(records), u32(frame), start, shared(), road(), offsets(shared_written), offsets(road_written))
    start = nil
    records = records + 1
    if records >= max_records then
      out:close()
      print(string.format("captured %d road frames through frame %d (%d abnormal windows dropped) -> %s",
                          records, frame, dropped, out_path))
      manager.machine:exit()
    end
  end
end)
