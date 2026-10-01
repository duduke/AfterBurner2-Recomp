-- Main palette/small scheduler write-set capture: AB2PAL01.
-- u32 index/frame/entry/input, MainBus RAM before/after, u32 offset sets
-- (main then other CPU), and ordered write I/O (address/data/mask).
-- SP/USP follows SR; all tap handles remain rooted. Captures stay ignored.
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local space, subspace = main.spaces.program, sub.spaces.program
local pc = main.state.CURPC
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")), "wb"))
out:write("AB2PAL01")
local limit = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local playing = os.getenv("AB2_ORACLE_GAMEPLAY") == "1"
if playing then dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")) .. "/gameplay.lua") end
local function u32(v) return string.pack(">I4", v) end
local frame, records, dropped, guard = 0, 0, 0, false
local before, entry_sp, written, other, events, input
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
 {0xcdd07,0x14a86,0x14a86,false}, {0xcdd05,0x13f92,0x13f92,false},
 {0xcdd08,0x142dc,0x142dc,false}, {0xcc000,0x1442a,0x14424,false},
 {0x29c54c,0x13e5c,0x13e5c,false},
 {0x3f9142,0x13bbc,0x13bb6,false}, {0x3f9114,0x13be4,0x13bcc,false},
 {0x29c060,0x13bfa,0x13bfa,false}, {0x29c09c,0x1329e,0x1329e,false},
 {0x3f8174,0x148c6,0x148be,false}, {0x29c074,0x1495c,0x1495c,false},
 {0x3f8050,0x147e8,0x147e8,false}, {0x2a3c00,0x14856,0x14856,false}
}
local entry
local entry_counts={}
setup_starts={}
for _,e in ipairs(entries) do
 local function begin(a,data,mask)
  if guard or before or pc.value~=e[2] or frame<skip then return end
  if e[3]==0x14856 and data&0x8000==0 then return end
  if os.getenv("AB2_ORACLE_PALETTE_DIRECTED")=="1" then
   guard=true
   local n=records%64
   if e[3]==0x14a86 then
    space:write_u8(0xcdd07,n<4 and 1 or 0)
    space:write_u8(0xcdd04,n&31)
    space:write_u16(0xcdd02,n<8 and (n&3) or 0)
    space:write_u16(0xcdd00,n&7)
    space:write_u8(0xcdd05,n==63 and 0 or 1)
    space:write_u8(0xcdd06,(n//8)&7)
    -- Exercise the handler's independently captured palette request callees.
    space:write_u8(0x3f913d,3);space:write_u8(0x3f913e,1);space:write_u8(0x3f9140,1)
   elseif e[3]==0x13f92 then
    space:write_u8(0xcdd05,n==63 and 1 or 0)
    space:write_u16(0x3f8c2a,n==62 and 0x1e or (n&31))
    space:write_u8(0xcdd08,n%8==0 and 1 or (n%8==1 and 2 or 0))
    space:write_u8(0xcdd09,n%8==1 and 1 or 0x40)
    space:write_u16(0x3f8d30,0)
    space:write_u16(0x29c0d2,n==61 and 0xffff or ((n&31)<<4))
    space:write_u16(0x3f8b5a,n)
   elseif e[3]==0x13e5c then
    space:write_u8(0x29c54c,n%4==0 and 0 or 2)
    space:write_u8(0x3f8c2e,n&1)
    space:write_u8(0x3f8c2c,n&7)
    space:write_u16(0x3f8c2a,n%8==3 and 31 or 4)
   elseif e[3]==0x1329e then
    space:write_u16(0x29c09c,n%3==0 and 0x11 or n%3==1 and 0x12 or 0x10)
    space:write_u8(0xcdd05,n&1)
   elseif e[3]==0x14856 then
    space:write_u16(0x2a3c00,n&1==0 and 0xffff or 0xfffe)
   elseif e[3]==0x148be then
    space:write_u16(0x3f8174,n%3==0 and 59 or n%3==1 and 0xffff or 0)
    space:write_u16(0x3f880a,n&1);space:write_u16(0x3f881c,59)
   end
   data=space:read_u16(a)
   guard=false
  end
  before,entry_sp,entry=snapshot(),((main.state.SR.value & 0x2000 ~= 0) and main.state.SP.value or main.state.USP.value) & 0x3fffff,e[3]
  written,other,events={},{},{}
  input=space:read_u8(0x150001)
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
  if not ((p>=0x14a86 and p<=0x14c34) or (p>=0x13f92 and p<=0x140d8) or (p>=0x142dc and p<=0x1449c) or (p>=0x13e5c and p<0x13eb0) or (p>=0x13bb6 and p<=0x13c3c) or (p>=0x1329e and p<=0x1331e) or (p>=0x148be and p<=0x148fa) or (p>=0x1495c and p<=0x14974) or (p>=0x147e8 and p<=0x14816) or (p>=0x14856 and p<=0x14878)) then
    before=nil; dropped=dropped+1; return
  end
  if o then mark(written,o,mask) else events[#events+1]=u32(a)..u32(data)..u32(mask) end
end)
scene_end = space:install_read_tap(0x3ffe00,0x3fffff,"scene_end",function(a,data,mask)
  if guard or before == nil or a~=entry_sp then return end
  local p=pc.value
  if p~=0x14b28 and p~=0x14c34 and p~=0x140d8 and p~=0x143a6 and p~=0x14422 and p~=0x14486 and p~=0x1449c and p~=0x13e8c and p~=0x13eac and p~=0x13bca and p~=0x13bf8 and p~=0x13c3c and p~=0x132e8 and p~=0x1331e and p~=0x148fa and p~=0x14974 and p~=0x14816 and p~=0x14878 then return end
  out:write(u32(records),u32(frame),u32(entry),u32(input),before,snapshot(),
            offsets(written),offsets(other),u32(#events),table.concat(events))
  entry_counts[entry]=(entry_counts[entry] or 0)+1
  before=nil; records=records+1
  if limit>0 and records>=limit then
    out:close()
    print(string.format("captured %d palette calls through frame %d (%d abnormal windows dropped) -> %s",
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
  print(string.format("captured %d palette calls through frame %d (%d abnormal windows dropped) -> %s",records,frame,dropped,os.getenv("AB2_ORACLE_OUT")))
  manager.machine:exit()
 end
end)

