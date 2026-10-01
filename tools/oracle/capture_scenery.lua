-- Main scenery event engine $018444, including its tail calls and helpers.
-- Same RAM/write-set/event layout as AB2MAIN1; four header bytes reserved.
-- Capture starts on the first $9044 read and ends on the outer RTS stack
-- read, not opcode prefetch (which can read past a DBF before it finishes).
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local space, subspace = main.spaces.program, sub.spaces.program
local pc = main.state.CURPC
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")), "wb"))
out:write("AB2SCN01")
local limit = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local playing = os.getenv("AB2_ORACLE_GAMEPLAY") == "1"
if playing then dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")) .. "/gameplay.lua") end
local function u32(v) return string.pack(">I4", v) end
local frame, records, dropped, guard = 0, 0, 0, false
local before, entry_sp, written, other, events
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
local skip = tonumber(os.getenv("AB2_ORACLE_SKIP") or "0")
local stride = tonumber(os.getenv("AB2_ORACLE_STRIDE") or "1")
scene_start = space:install_read_tap(0x3f9044,0x3f9045,"scene_start",function(a,data,mask)
  if guard or pc.value ~= 0x18444 or frame < skip or frame % stride ~= 0 then return end
  before, entry_sp = snapshot(), ((main.state.SR.value & 0x2000 ~= 0) and main.state.SP.value or main.state.USP.value) & 0x3fffff
  written, other, events = {}, {}, {}
end)
scene_writes = space:install_write_tap(0,0x3fffff,"scene_writes",function(a,data,mask)
  if guard or before == nil then return end
  a = a & 0x3ffffe
  local o = offset(a)
  if o and o >= 0x7e00 and o < 0x8000 then return end
  local p=pc.value
  if not ((p>=0x18444 and p<0x188a8) or (p>=0x18902 and p<0x18918) or (p>=0x13a74 and p<0x13a9a)) then
    before=nil; dropped=dropped+1; return
  end
  if o then mark(written,o,mask) else events[#events+1]=u32(a)..u32(data)..u32(mask) end
end)
scene_end = space:install_read_tap(0x3ffe00,0x3fffff,"scene_end",function(a,data,mask)
  if guard or before == nil or a~=entry_sp then return end
  local p=pc.value
  if p~=0x1848c and p~=0x1854a and p~=0x188a6 and p~=0x13a98 then return end
  out:write(u32(records),u32(frame),string.char(0,0,0,0),before,snapshot(),
            offsets(written),offsets(other),u32(#events),table.concat(events))
  before=nil; records=records+1
  if records>=limit then
    out:close()
    print(string.format("captured %d scenery calls through frame %d (%d abnormal windows dropped) -> %s",
                        records,frame,dropped,os.getenv("AB2_ORACLE_OUT")))
    manager.machine:exit()
  end
end)
scene_other = subspace:install_write_tap(0x80000,0xeffff,"scene_other",function(a,data,mask)
  if guard or before == nil then return end
  local o=offset((a & 0xffffe)+0x200000)
  if o then mark(other,o,mask) end
end)
