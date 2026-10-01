-- Main sprite write-set capture: AB2SPR01.
-- u32 index/frame/entry/A5, MainBus RAM before/after, u32 offset sets
-- (main then other CPU), and ordered write I/O (address/data/mask).
-- SP/USP follows SR; all tap handles remain rooted. Captures stay ignored.
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local space, subspace = main.spaces.program, sub.spaces.program
local pc = main.state.CURPC
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")), "wb"))
out:write("AB2SPR01")
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
local entries = {
 {0x2a3cf0,0xb628,0xb628,false}, {0x2a3c00,0xb676,0xb676,false},
 {0x2a3c08,0x13832,0x13832,true}, {0x2a3c14,0xb7c2,0xb7c2,false}, {0x2a3c14,0xb7d2,0xb7d2,false}
}
local entry, object
local entry_counts={}
setup_starts={}
for _,e in ipairs(entries) do
 local function begin(a,data,mask)
  if guard or before or pc.value~=e[2] or frame<skip then return end
  if os.getenv("AB2_ORACLE_SPRITE_INIT")=="1" and e[3]~=0x13832 then return end
  if os.getenv("AB2_ORACLE_SPRITE_DIRECTED")=="1" then
   guard=true
   local n=records%4
   if e[3]==0xb628 then
    local counts={0,0xbf,0xc0,0xc1};space:write_u16(0x2a3cf0,counts[n+1])
   elseif e[3]==0xb7c2 or e[3]==0xb7d2 then
    local counts={0,1,0xbf,0xc0};space:write_u16(space:read_u32(0x2a3c1c),counts[n+1])
   end
   data=space:read_u16(a);guard=false
  end
  before,entry_sp,entry=snapshot(),((main.state.SR.value & 0x2000 ~= 0) and main.state.SP.value or main.state.USP.value) & 0x3fffff,e[3]
  object=main.state.A5.value
  written,other,events={},{},{}
  if e[4] then mark(written,offset(a),mask) end
  return data
 end
 if e[4] then setup_starts[#setup_starts+1]=space:install_write_tap(e[1]&0xfffffe,(e[1]&0xfffffe)+1,"setup_start",begin)
 else setup_starts[#setup_starts+1]=space:install_read_tap(e[1]&0xfffffe,(e[1]&0xfffffe)+1,"setup_start",begin) end
end
scene_writes = space:install_write_tap(0,0x3fffff,"scene_writes",function(a,data,mask)
  if guard or before == nil then return end
  a = a & 0x3ffffe
  local o = offset(a)
  if o and o >= 0x7e00 and o < 0x8000 then return end
  local p=pc.value
  if not ((p>=0xb628 and p<=0xb654) or (p>=0xb676 and p<=0xb696) or (p>=0xb7c2 and p<=0xb7f6) or (p>=0x13832 and p<=0x13884)) then
    before=nil; dropped=dropped+1; return
  end
  if o then mark(written,o,mask) else events[#events+1]=u32(a)..u32(data)..u32(mask) end
end)
-- The reset pointer slice ends before TRAP #10's vector fetch, after all
-- eight longword stores. This is a slice, not a claim to port reset dispatch.
init_end=space:install_read_tap(0xa8,0xab,"init_end",function(a,data,mask)
 if guard or not before or entry~=0x13832 or pc.value~=0x13884 then return end
 out:write(u32(records),u32(frame),u32(entry),u32(object),before,snapshot(),offsets(written),offsets(other),u32(#events),table.concat(events))
 before=nil;records=records+1
 if os.getenv("AB2_ORACLE_SPRITE_INIT")=="1" then
  out:close();print("captured 1 sprite reset-pointer slice -> "..os.getenv("AB2_ORACLE_OUT"));manager.machine:exit()
 end
end)
scene_end = space:install_read_tap(0x3ffe00,0x3fffff,"scene_end",function(a,data,mask)
  if guard or before == nil or a~=entry_sp then return end
  local p=pc.value
  if p~=0xb654 and p~=0xb696 and p~=0xb7f6 then return end
  out:write(u32(records),u32(frame),u32(entry),u32(object),before,snapshot(),
            offsets(written),offsets(other),u32(#events),table.concat(events))
  entry_counts[entry]=(entry_counts[entry] or 0)+1
  before=nil; records=records+1
  if limit>0 and records>=limit then
    out:close()
    print(string.format("captured %d main sprite calls through frame %d (%d abnormal windows dropped) -> %s",
                        records,frame,dropped,os.getenv("AB2_ORACLE_OUT")))
    manager.machine:exit()
  end
end)
scene_other = subspace:install_write_tap(0x80000,0xeffff,"scene_other",function(a,data,mask)
  if guard or before == nil then return end
  local o=offset((a & 0xffffe)+0x200000)
  if o then mark(other,o,mask) end
end)

setup_finish=emu.add_machine_frame_notifier(function()
 if frame>=1800 then
  out:close()
  for e,n in pairs(entry_counts) do print(string.format("setup entry %x: %d",e,n)) end
  print(string.format("captured %d main sprite calls through frame %d (%d abnormal windows dropped) -> %s",records,frame,dropped,os.getenv("AB2_ORACLE_OUT")))
  manager.machine:exit()
 end
end)


init_call=emu.add_machine_frame_notifier(function()
 if os.getenv("AB2_ORACLE_SPRITE_INIT")=="1" and frame==500 then
  main.state.PC.value=0x13832 -- run only the original eight-store slice, then exit
 end
end)
