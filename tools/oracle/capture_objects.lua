-- Phase 3d dispatcher/initializer and compound initialization capture.
-- AB2OBJ01: header index/frame/entry, before/after 15 registers, RAM,
-- own/other write sets, own I/O, callback boundaries (registers and writes).
-- Callback writes are external inputs in AB2OBJ01. The host AB2OBJ02
-- replay executes verified handlers and checks their complete call outputs.
local main=manager.machine.devices[":mainpcb:maincpu"]
local sub=manager.machine.devices[":mainpcb:subcpu"]
local space,subspace=main.spaces.program,sub.spaces.program
local pc=main.state.CURPC
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"))
local host=os.getenv("AB2_ORACLE_HOST")=="1"
out:write(host and "AB2OBJ02" or "AB2OBJ01")
local calls=os.getenv("AB2_ORACLE_OBJECT_CALLS")=="1"
local injected,restore_registers,restore_sr
local leaves=os.getenv("AB2_ORACLE_OBJECT_LEAVES")=="1"
local directed=os.getenv("AB2_ORACLE_OBJECT_DIRECTED")=="1"
assert(not host or (not calls and not leaves and not directed),"host composition must use its own IPL7 fixture")
local limit=tonumber(os.getenv("AB2_ORACLE_PASSES") or "120")
if os.getenv("AB2_ORACLE_GAMEPLAY")=="1" then
 dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
end
local function u32(v) return string.pack(">I4",v&0xffffffff) end
local frame,records,dropped,guard=0,0,0,false
local before,entry,pre,written,other,events,hooks,hook,saved_sr
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

local function registers()
 local r={}
 for i=0,7 do r[#r+1]=main.state["D"..i].value end
 for i=0,6 do r[#r+1]=main.state["A"..i].value end
 return r
end
local function packed(r)
 local p={};for _,v in ipairs(r) do p[#p+1]=u32(v) end;return table.concat(p)
end
-- Full chip and digital-input contract for executing handlers through the
-- host router. Sub inputs remain captured; concurrent sub writes are races
-- only when the composed port actually reads those bytes.
local function math_state()
 guard=true
 local parts={}
 for _,a in ipairs({0xe0000,0xe4000,0x2e0000,0x2e4000}) do
  for i=0,7 do parts[#parts+1]=u32(space:read_u16(a+i*2)) end
 end
 guard=false
 return table.concat(parts)
end
local function io_input()
 guard=true;local v=space:read_u8(0x150001);guard=false;return u32(v)
end
local math_pre
local function finish_hook()
 if hook then hook.post=registers();if host then hook.math_post=math_state() end;hooks[#hooks+1]=hook;hook=nil end
end
local function begin(e)
 if host then assert(main.state.SR.value&0x700==0x700,"host fixture must execute the IPL7 instruction") end
 if e==0x1488e or e==0x16892 or (e==0x14594 and directed) then
  saved_sr=main.state.SR.value; main.state.SR.value=saved_sr|0x700
 end
 if directed then
  guard=true
  local n=records%8
  if e==0x14594 then
   local count=space:read_u16(0x1c176)
   space:write_u8(0x3f9fc0,0x80);space:write_u32(0x3f9fd0,0x148b6)
   for i=0,count-1 do
    local active=(n%4==1 and i==count-1) or (n%4==2) or (n%4==3 and i==0)
    space:write_u8(0x3fa000+i*64,active and 0x80 or 0)
    space:write_u32(0x3fa010+i*64,0x148b6)
   end
  elseif e==0xc5a8 then
   local seeds={0,1,0x80000000,0xffffffff,0x2a6d365a,0xffff,0x10000,0x12345678}
   space:write_u32(0x3f8802,seeds[n+1])
  elseif e==0xb7f8 or e==0xb840 or e==0xb84c then
   space:write_u8(0x3f8902,n==0 and 0 or 1)
   space:write_u16(0x3f8900,n%2==0 and 0 or 0xffff)
   space:write_u8(0x3f8906,n==1 and 0 or n==2 and 0xff or 1)
   space:write_u16(0x3f8908,n%2==0 and 0x3f or 0)
   space:write_u16(0x3f890a,n%4==2 and 0x1f or (n%2==0 and 0x3f or 0))
   space:write_u16(0x29c09c,n==3 and 1 or 0x10)
  elseif e==0x16892 then
   local a=main.state.A5.value&0x3fffff
   for i=0,3 do space:write_u8(a+i*64,({0,0x7f,0x80,0xff})[n%4+1]) end
  end
  guard=false
 end
 before=snapshot();entry=e;pre=registers();if host then math_pre=math_state() end;written,other,events,hooks={},{},{},{}
end
local function finish(a,data,mask)
 finish_hook()
 local after=snapshot()
 if a then
  local o=offset(a)
  if mask&0xff00~=0 then after=after:sub(1,o)..string.char((data>>8)&255)..after:sub(o+2) end
  if mask&0xff~=0 then after=after:sub(1,o+1)..string.char(data&255)..after:sub(o+3) end
 end
 local p={u32(#hooks)}
 for _,h in ipairs(hooks) do
  p[#p+1]=u32(h.entry)..packed(h.pre)..packed(h.post)
  if host then p[#p+1]=h.io..h.math_pre..h.math_post end
  p[#p+1]=u32(#h.writes)..table.concat(h.writes)
 end
 out:write(u32(records),u32(frame),u32(entry),packed(pre),packed(registers()),
  (host and math_pre or ""),before,after,offsets(written),offsets(other),u32(#events),table.concat(events),table.concat(p))
 before=nil;records=records+1
 if saved_sr then main.state.SR.value=(main.state.SR.value&~0x700)|(saved_sr&0x700);saved_sr=nil end
 if injected and not host then
  for i=0,7 do main.state["D"..i].value=restore_registers[i+1] end
  for i=0,6 do main.state["A"..i].value=restore_registers[i+9] end
  main.state.SR.value=restore_sr;injected=nil
 end
 if records>=limit then
  out:close();print(string.format("captured %d object boundaries (%d interrupted windows dropped) -> %s",records,dropped,os.getenv("AB2_ORACLE_OUT")));manager.machine:exit()
 end
end
object_frames=emu.add_machine_frame_notifier(function() frame=frame+1 end)
object_start=space:install_read_tap(0x1c176,0x1c177,"object_start",function(a,data,mask)
 if guard or before then return end
 if leaves then return end
 if pc.value==0x14898 and not host then begin(0x1488e)
 elseif pc.value==0x14594 and (not host or injected) and frame>=tonumber(os.getenv("AB2_ORACLE_SKIP") or "0") then begin(0x14594) end
end)
object_reads=space:install_read_tap(0,0x3fffff,"object_reads",function(a,data,mask)
 if guard then return end
 local p=pc.value
 -- The dispatcher is a main-loop range, not an RTS routine. Its final
 -- branch back to $1458A is outside the captured range; substitute RTS
 -- only for this directed fixture so it returns to the synthesized caller.
 if host and injected and a==0x145b8 then data=0x4e75 end
 if host and injected and p==0x3ffd0a then
  -- RTS has returned from the dispatcher. The following real MOVE to SR
  -- restores the mask/mode; restore only the interrupted data registers.
  for i=0,7 do main.state["D"..i].value=restore_registers[i+1] end
  for i=0,6 do main.state["A"..i].value=restore_registers[i+9] end
  injected=nil
 end
 if not before and leaves and (not calls or injected) and frame>=tonumber(os.getenv("AB2_ORACLE_SKIP") or "0") and (p==0x16976 or p==0x169b8 or p==0xc5a8 or p==0xb7f8 or p==0xb840 or p==0xb84c) then
  begin(p==0x169b8 and 0x169b2 or p)
  guard=true;data=space:read_u16(a);guard=false
 end
 if not before then return data end
 if entry==0x14594 then
  if p==0x145ac and not hook then
   guard=true
   local r=registers();local target=space:read_u32((r[14]+16)&0x3fffff)
   r[9]=target -- A0 after MOVEA.L, before JSR.
   hook={entry=target,pre=r,writes={}};guard=false
   if host then hook.io=io_input();hook.math_pre=math_state() end
  elseif p==0x145b2 then finish_hook() end
 elseif p==0x148b4 or p==0x16974 or p==0x169b0 or p==0x169d6 or p==0xc5ce or p==0xb834 or p==0xb83e or p==0xb87a or p==0xb884 then finish() end
 return data
end)
object_writes=space:install_write_tap(0,0x3fffff,"object_writes",function(a,data,mask)
 if guard then return end
 local p=pc.value
 if not before then
  if p==0x16892 and not host and (not calls or injected) then begin(0x16892)
  elseif not host and not leaves and p==0x148a4 and offset(a)==0x2000 then begin(0x1488e)
  elseif not leaves and p==0x14594 and (not host or injected) and frame>=tonumber(os.getenv("AB2_ORACLE_SKIP") or "0") then begin(0x14594) end
 end
 if not before then return end
 local o=offset(a)
 if o and o>=0x7e00 and o<0x8000 then return end
 if p>=0x1461c and p<=0x14816 and (host or not hook) then
  before=nil;hook=nil;dropped=dropped+1;if saved_sr then main.state.SR.value=(main.state.SR.value&~0x700)|(saved_sr&0x700);saved_sr=nil end;return
 end
 local own=(entry==0x1488e and p>=0x1488e and p<=0x148b4)
  or (entry==0x16892 and p>=0x16892 and p<=0x16974)
  or (entry==0x14594 and p>=0x14594 and p<=0x145b8)
  or ((entry==0x16976 or entry==0x169b2) and p>=0x16976 and p<=0x169d6)
  or (entry==0xc5a8 and p>=0xc5a8 and p<=0xc5ce)
  or ((entry==0xb7f8 or entry==0xb840 or entry==0xb84c) and p>=0xb7f8 and p<=0xb884)
 if own then
  if o then mark(written,o,mask) else events[#events+1]=u32(a)..u32(data)..u32(mask) end
  if entry==0x14594 and p==0x145b2 and data==0 then finish(a,data,mask) end
 elseif entry==0x14594 and hook then
  hook.writes[#hook.writes+1]=u32(a)..u32(data)..u32(mask)
 else before=nil;hook=nil;dropped=dropped+1;if saved_sr then main.state.SR.value=(main.state.SR.value&~0x700)|(saved_sr&0x700);saved_sr=nil end end
end)
object_other=subspace:install_write_tap(0x80000,0xeffff,"object_other",function(a,data,mask)
 if guard or not before then return end
 local o=offset((a&0xffffe)+0x200000);if o then mark(other,o,mask) end
end)

-- Isolated calls exercise real ROM routines without claiming their callers
-- or a complete credit. Save the interrupted registers and use an RTS return
-- on the active hardware stack, as in capture_stagesetup.lua.
object_directed_calls=emu.add_machine_frame_notifier(function()
 if not calls or before or injected or frame<540 then return end
 local routines={0x16892,0x16976,0x169b2,0xb840,0xb84c,0xc5a8,0xb7f8}
 local target=routines[records%#routines+1]
 restore_registers=registers();restore_sr=main.state.SR.value
 local stack=(restore_sr&0x2000~=0) and main.state.SP or main.state.USP
 local sp=(stack.value-4)&0xffffffff
 guard=true;space:write_u32(sp&0x3fffff,main.state.CURPC.value);guard=false
 stack.value=sp
 main.state.A5.value=0xffffa300
 main.state.D0.value=0x1234007b;main.state.D1.value=0xabcd9876
 injected=target;main.state.PC.value=target
end)

-- Directed host composition: call the ROM dispatcher on live gameplay
-- records with a real IPL7 instruction, then restore SR and resume the
-- interrupted program. This isolates composition, not IRQ interleaving or
-- independent host cadence. Never use the debugger SR setter as a mask.
object_host_calls=emu.add_machine_frame_notifier(function()
 if not host or before or injected or frame<540 or frame%2~=0 then return end
 restore_registers=registers();restore_sr=main.state.SR.value
 local saved_pc=main.state.PC.value
 guard=true
 space:write_u16(0x3ffd00,0x46fc);space:write_u16(0x3ffd02,0x2700) -- MOVE #$2700,SR
 space:write_u16(0x3ffd04,0x4eb9);space:write_u32(0x3ffd06,0x14594) -- JSR dispatcher
 space:write_u16(0x3ffd0a,0x46fc);space:write_u16(0x3ffd0c,restore_sr)
 space:write_u16(0x3ffd0e,0x4ef9);space:write_u32(0x3ffd10,saved_pc) -- JMP resume
 guard=false
 -- Select supervisor privilege so MOVE to SR is legal; IPL changes only
 -- when MAME executes the synthesized instruction above.
 main.state.SR.value=restore_sr|0x2000
 injected=0x14594;main.state.PC.value=0x3ffd00
end)
