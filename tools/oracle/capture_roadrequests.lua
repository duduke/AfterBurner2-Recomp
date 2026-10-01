-- Road request oracle, original sub ROM $436/$44A and $5C4/$5EE.
-- AB2ORCL4 extends road-init records with a third u16-offset set: main CPU
-- writes during the window. Start at the $300 read, end before the frame
-- wait at $452, ahead of the road swap. Ignore sprite/IRQ writes; discard
-- reset-interrupted windows. A main write to a port-read byte is a race.
local out_path = os.getenv("AB2_ORACLE_OUT") or "subroad.bin"
local max_records = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local skip = tonumber(os.getenv("AB2_ORACLE_SKIP") or "0")
if os.getenv("AB2_ORACLE_GAMEPLAY") == "1" then
  dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")) .. "/gameplay.lua")
end

local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local main_space = main.spaces["program"]
local sub_space = sub.spaces["program"]
local sub_pc = sub.state["CURPC"]

local out = assert(io.open(out_path, "wb"))
out:write("AB2ORCL4")

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
  return (pc >= 0x0005C4 and pc < 0x000604) or (pc >= 0x00046A and pc < 0x000538) or (pc >= 0x000C0A and pc < 0x0011D4) or (pc >= 0x0016D4 and pc < 0x0019E8)
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

local entry, entry_sp
local main_written={}
window_start = sub_space:install_read_tap(0x9C300,0x9C301,"ab2_road_requests_start",function(offset,data,mask)
 if start==nil and sub_pc.value==0x436 then
  start=shared()..road()..chips();entry=0x436
  shared_written,road_written={},{};main_written={}
 end
end)

-- Stack pushes (top of subram1) come from wherever the PC happens to be, so
-- they're logged without the PC check; the test excludes the stack area.
local function is_stack(o) return o >= 0x7F00 end

shared_log = sub_space:install_write_tap(0x80000, 0xBFFFF, "ab2_road_shared", function(offset, data, mask)
  local pc = sub_pc.value
  if start and pc<0x436 then start=nil;dropped=dropped+1;return end
  if start ~= nil and not in_vblank_handler(pc) then
    local a = offset & 0xFFFFE
    local o = (a < 0xA0000 and 0 or 0x4000) + (a & 0x3FFF)
    if not (pc>=0x436 and pc<0x44e) and not (pc>=0x5c4 and pc<0x604) then return end
    if start ~= nil then mark(shared_written, o, offset, mask) end
  end
end)

road_log = sub_space:install_write_tap(0xEC000, 0xEDFFF, "ab2_road_ram", function(offset, data, mask)
  local pc = sub_pc.value
  if start ~= nil and not in_vblank_handler(pc) then
    if not (pc>=0x5c4 and pc<0x604) then return end
    if start ~= nil then mark(road_written, offset & 0xFFE, offset, mask) end
  end
end)

window_end = sub_space:install_read_tap(0xA3C00,0xA3C01,"ab2_road_requests_end",function(offset,data,mask)
 if start and sub_pc.value==0x452 then
  out:write(u32(records),u32(frame),u32(entry),start,shared(),road(),offsets(shared_written),offsets(road_written),offsets(main_written))
  start=nil;records=records+1
 end
end)
finish_capture=emu.add_machine_frame_notifier(function()
 if frame>=1200 then
  out:close();print(string.format("captured %d road request windows through frame %d (%d reset windows dropped) -> %s",records,frame,dropped,out_path))
  manager.machine:exit()
 end
end)

main_request_writes=main_space:install_write_tap(0x280000,0x2bffff,"ab2_request_other_cpu",function(a,data,mask)
 if start then
  local o=(a<0x2a0000 and 0 or 0x4000)+(a & 0x3ffe)
  mark(main_written,o,a,mask)
 end
end)
