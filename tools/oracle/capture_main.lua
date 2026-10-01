-- Main ROM $014626-$01465A ADC sampler, before the unported $00B7F8 call.
-- MainBus memory order: work/shared/tile/text/sprite/palette/road (0x27000).
-- Each record: index/frame u32, ADC byte/expected port-C byte/I/O0 controls,
-- start and end memory, then main-written and other-CPU-written byte sets
-- (u32 count + u32 offsets), and I/O writes (u32 count + addr/data/mask u32).
-- Stack $FE00-$FFFF excluded. Snapshots bypass this script's logging guard.
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local space, subspace = main.spaces.program, sub.spaces.program
local pc = main.state.CURPC
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")), "wb"))
out:write("AB2MAIN1")
local limit = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local playing = os.getenv("AB2_ORACLE_GAMEPLAY") == "1"
if playing then dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")) .. "/gameplay.lua") end
local function u32(v) return string.pack(">I4", v) end
local frame, records, dropped, guard = 0, 0, 0, false
local before, adc, written, other, events
local controls = {0xff,0xff}
local ranges = {
  {0x080000,0x083fff}, {0x0a0000,0x0a3fff},
  {0x280000,0x283fff}, {0x2a0000,0x2a3fff},
  {0x0c0000,0x0cffff}, {0x0d0000,0x0d0fff},
  {0x100000,0x100fff}, {0x120000,0x123fff}, {0x2ec000,0x2ecfff}
}
local function snapshot()
  guard = true
  local parts = {}
  for _, r in ipairs(ranges) do parts[#parts+1] = space:read_range(r[1],r[2],8) end
  guard = false
  return table.concat(parts)
end
local function offset(a)
  a = a & 0x3fffff
  if a >= 0x80000 and a < 0xc0000 then return (a < 0xa0000 and 0 or 0x4000) + (a & 0x3fff) end
  if a >= 0x3f8000 then return a - 0x3f8000 end
  if a >= 0x280000 and a < 0x2c0000 then return 0x8000 + (a < 0x2a0000 and 0 or 0x4000) + (a & 0x3fff) end
  if a >= 0xc0000 and a < 0xd0000 then return 0x10000 + (a & 0xffff) end
  if a >= 0xd0000 and a < 0xe0000 then return 0x20000 + (a & 0xfff) end
  if a >= 0x100000 and a < 0x110000 then return 0x21000 + (a & 0xfff) end
  if a >= 0x120000 and a < 0x130000 then return 0x22000 + (a & 0x3fff) end
  if a >= 0x2ec000 and a < 0x2ee000 then return 0x26000 + (a & 0xfff) end
end
local function mark(set, o, mask)
  if mask & 0xff00 ~= 0 then set[o] = true end
  if mask & 0xff ~= 0 then set[o + 1] = true end
end
local function offsets(set)
  local list, parts = {}, {}
  for o in pairs(set) do list[#list + 1] = o end
  table.sort(list); parts[1] = u32(#list)
  for _, o in ipairs(list) do parts[#parts+1] = u32(o) end
  return table.concat(parts)
end
main_frames = emu.add_machine_frame_notifier(function() frame = frame + 1 end)
main_start = space:install_read_tap(0x130000,0x13ffff,"ab2_main_start",function(a,data,mask)
  if guard or pc.value ~= 0x14626 or (playing and frame < 540) then return end
  before, adc = snapshot(), data & 0xff
  written, other, events = {}, {}, {}
end)
main_writes = space:install_write_tap(0,0x3fffff,"ab2_main_writes",function(a,data,mask)
  if guard then return end
  if a >= 0x140000 and a < 0x150000 and mask & 0xff ~= 0 then
    local r = (a >> 1) & 7
    if r == 6 or r == 7 then controls[r-5] = data & 0xff end
  end
  if before == nil then return end
  a = a & 0x3ffffe
  local o = offset(a)
  if o and o >= 0x7e00 and o < 0x8000 then return end -- exception/return stack
  if pc.value < 0x14626 or pc.value >= 0x14660 then
    before = nil; dropped = dropped + 1; return
  end
  if o then mark(written,o,mask)
  else events[#events+1] = u32(a)..u32(data)..u32(mask) end
  if pc.value == 0x1465a and a >= 0x130000 and a < 0x140000 then
    local board = manager.machine.devices[":mainpcb"]
    local portc = emu.item(assert(board.items["0/m_pc_0"])):read(0)
    out:write(u32(records),u32(frame),string.char(adc,portc,controls[1],controls[2]),before,snapshot(),
              offsets(written),offsets(other),u32(#events),table.concat(events))
    before = nil; records = records + 1
    if records >= limit then
      out:close()
      print(string.format("captured %d main ADC samples through frame %d (%d abnormal windows dropped) -> %s",
                          records,frame,dropped,os.getenv("AB2_ORACLE_OUT")))
      manager.machine:exit()
    end
  end
end)
main_other_writes = subspace:install_write_tap(0x80000,0xeffff,"ab2_other_writes",function(a,data,mask)
  if guard or before == nil then return end
  local o = offset((a & 0xffffe) + 0x200000)
  if o then mark(other,o,mask) end
end)
