-- Interrupt scheduler capture: AB2FRM01.
-- u32 index/frame/entry, RAM before/after, own/other offset sets, own I/O,
-- then callees with their external RAM write transcripts. Callee writes
-- are input boundaries, never counted as scheduler correctness.
-- SP/USP follows SR; all tap handles remain rooted. Captures stay ignored.
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local space, subspace = main.spaces.program, sub.spaces.program
local pc = main.state.CURPC
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")), "wb"))
out:write("AB2FRM01")
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
-- Only the interrupt's own instructions are compared here. Callees are
-- explicit interaction transcripts, independently oracle-tested where ported.
local calls={
 [0x146c6]=0x14a86,[0x146ca]=0x13f92,[0x146d8]=0x13bcc,
 [0x146e6]=0x13bb6,[0x146f4]=0x13bfa,[0x14702]=0x1329e,
 [0x14706]=0x16c8a,[0x1470c]=0x18444,[0x14712]=0x1495c,
 [0x14720]=0x148be,[0x14724]=0x17146,[0x14728]=0x13e5c,
 [0x1473e]=0xb7d2,[0x1475a]=0x148be,[0x1475e]=0x1495c,
 [0x1476c]=0x14a86,[0x14770]=0x13f92,[0x14774]=0x18444,
 [0x1477a]=0x17146,[0x1477e]=0x13e5c,[0x1478a]=0xb7c2,
 [0x147ac]=0x148b8,[0x147b0]=0x14990,[0x147b4]=0xc6f8,
 [0x147ba]=0xe2da,[0x147be]=0x147e8
}
local hooks,hook
local function in_handler(p) return p>=0x14674 and p<=0x147e6 end
local function end_hook()
 if hook then hooks[#hooks+1]=hook;hook=nil end
end
frame_start=space:install_read_tap(0x29c0d6,0x29c0d7,"frame_start",function(a,data,mask)
 if guard or before or pc.value~=0x14674 or frame<skip then return end
 if os.getenv("AB2_ORACLE_FRAME_DIRECTED")=="1" then
  guard=true
  space:write_u8(0x29c000,space:read_u8(0x29c000)|1)
  if records==limit-2 then space:write_u16(0x2a3c00,0x8000) end
  space:write_u8(0x3f913d,3);space:write_u8(0x3f913e,1);space:write_u8(0x3f9140,1)
  space:write_u8(0x3f8b6f,space:read_u8(0x3f8b6f)|2)
  space:write_u8(0x3f8050,records==limit-1 and 0xff or 1)
  guard=false
 end
 before=snapshot();written,other,events,hooks={},{},{},{}
end)
frame_writes=space:install_write_tap(0,0x3fffff,"frame_writes",function(a,data,mask)
 if guard or not before then return end
 a=a&0x3ffffe
 local p=pc.value
 local o=offset(a)
 if in_handler(p) then
  if not(hook and calls[p]==hook.entry) then
   end_hook()
   if calls[p] then hook={entry=calls[p],writes={}} end
  end
  if o and o>=0x7e00 and o<0x8000 then return end
  if o then mark(written,o,mask) else events[#events+1]=u32(a)..u32(data)..u32(mask) end
 elseif hook and o and not(o>=0x7e00 and o<0x8000) then
  hook.writes[#hook.writes+1]=u32(a)..u32(data)..u32(mask)
 end
end)
frame_reads=space:install_read_tap(0,0x3fffff,"frame_reads",function(a,data,mask)
 if guard or not before or not in_handler(pc.value) then return end
 if not(hook and calls[pc.value]==hook.entry) then end_hook() end
 -- RTE first exception-stack access; no CPU register/stack comparison.
 if pc.value~=0x147c6 and pc.value~=0x147e6 then return end
 local parts={u32(#hooks)}
 for _,h in ipairs(hooks) do parts[#parts+1]=u32(h.entry)..u32(#h.writes)..table.concat(h.writes) end
 out:write(u32(records),u32(frame),u32(0x14674),before,snapshot(),offsets(written),offsets(other),u32(#events),table.concat(events),table.concat(parts))
 before=nil;records=records+1
 if records>=limit then
  out:close();print(string.format("captured %d frame boundaries through frame %d -> %s",records,frame,os.getenv("AB2_ORACLE_OUT")));manager.machine:exit()
 end
end)
frame_other=subspace:install_write_tap(0x80000,0xeffff,"frame_other",function(a,data,mask)
 if guard or not before then return end
 local o=offset((a&0xffffe)+0x200000);if o then mark(other,o,mask) end
end)
