-- Main road/scenery setup write-set capture: header AB2STG01, records
-- u32 index/frame/entry, MainBus RAM before/after, u32 offset write sets
-- (main then other CPU), and u32 count + (address/data/mask) I/O events.
-- Capture CLR at its first read (68000 CLR.L writes low word first).
-- Return stack is SP in supervisor mode, USP in user mode on MAME's core.
-- *_ALT and *_CONTROL are directed routine coverage, never gameplay proof.
local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local space, subspace = main.spaces.program, sub.spaces.program
local pc = main.state.CURPC
local out = assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")), "wb"))
out:write("AB2STG01")
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
 {0x29c096,0xddfe,0xddfe,false},
 {0x29c0aa,0x13972,0x13972,false}, {0x29c060,0x18918,0x18918,false},
 {0x29c060,0x13cee,0x13cee,false}, {0x3f8c2e,0x13d10,0x13d10,false},
 {0x3f9040,0x183de,0x183de,true}, {0x30d82,0x13c52,0x13c3e,false},
 {0x2edf4,0x13b2c,0x13b1c,false}, {0x29c320,0xe062,0xe062,true},
 {0x29c320,0xe074,0xe074,true}
}
local entry
local entry_counts={}
setup_starts={}
for _,e in ipairs(entries) do
 local function begin(a,data,mask)
  if guard or before or pc.value~=e[2] or frame<skip then return end
  if os.getenv("AB2_ORACLE_SETUP_ALT")=="1" and e[3]==0x13d10 then
   guard=true
   local flags=space:read_u8(0x3f8c2e)
   space:write_u8(0x3f8c2e,flags | 1)
   guard=false
   data=data | 0x100
  end
  before,entry_sp,entry=snapshot(),((main.state.SR.value & 0x2000 ~= 0) and main.state.SP.value or main.state.USP.value) & 0x3fffff,e[3]
  written,other,events={},{},{}
  if e[4] then mark(written,offset(a),mask) end
  return data
 end
 if e[4] then setup_starts[#setup_starts+1]=space:install_write_tap(e[1],e[1]+1,"setup_start",begin)
 else setup_starts[#setup_starts+1]=space:install_read_tap(e[1],e[1]+1,"setup_start",begin) end
end
scene_writes = space:install_write_tap(0,0x3fffff,"scene_writes",function(a,data,mask)
  if guard or before == nil then return end
  a = a & 0x3ffffe
  local o = offset(a)
  if o and o >= 0x7e00 and o < 0x8000 then return end
  local p=pc.value
  if not ((p>=0xddfe and p<0xde4c) or (p>=0x13972 and p<0x139d4) or (p>=0x18918 and p<0x18944) or (p>=0x13cee and p<0x13d9c) or (p>=0x183de and p<0x18444) or (p>=0x13c3e and p<0x13c6c) or (p>=0x13b1c and p<0x13b42) or (p>=0xe062 and p<0xe086)) then
    before=nil; dropped=dropped+1; return
  end
  if o then mark(written,o,mask) else events[#events+1]=u32(a)..u32(data)..u32(mask) end
end)
scene_end = space:install_read_tap(0x3ffe00,0x3fffff,"scene_end",function(a,data,mask)
  if guard or before == nil or a~=entry_sp then return end
  local p=pc.value
  if p~=0xde4a and p~=0x139d2 and p~=0x18942 and p~=0x13d0e and p~=0x13d4e and p~=0x13d9a and p~=0x13c6a and p~=0x13b40 and p~=0xe072 and p~=0xe084 then return end
  out:write(u32(records),u32(frame),u32(entry),before,snapshot(),
            offsets(written),offsets(other),u32(#events),table.concat(events))
  entry_counts[entry]=(entry_counts[entry] or 0)+1
  before=nil; records=records+1
  if limit>0 and records>=limit then
    out:close()
    print(string.format("captured %d stage setup calls through frame %d (%d abnormal windows dropped) -> %s",
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
 if frame>=2400 then
  out:close()
  for e,n in pairs(entry_counts) do print(string.format("setup entry %x: %d",e,n)) end
  print(string.format("captured %d stage setup calls through frame %d (%d abnormal windows dropped) -> %s",records,frame,dropped,os.getenv("AB2_ORACLE_OUT")))
  manager.machine:exit()
 end
end)

-- Directed calls for the two short script commands: MAME's debugger PC
-- import re-decodes the original ROM at the supplied address. Push the
-- interrupted instruction as a return address on the active hardware stack.
-- This run is routine coverage, not a whole-credit gameplay capture.
setup_control_calls=emu.add_machine_frame_notifier(function()
 if os.getenv("AB2_ORACLE_SETUP_CONTROL")~="1" or before or frame<600 or frame>=2400 or frame%30~=0 then return end
 local stack=(main.state.SR.value & 0x2000 ~= 0) and main.state.SP or main.state.USP
 local sp=(stack.value-4) & 0xffffffff
 guard=true
 space:write_u32(sp & 0x3fffff,main.state.CURPC.value)
 guard=false
 stack.value=sp
 main.state.PC.value=(frame%60==0) and 0xe062 or 0xe074
end)
