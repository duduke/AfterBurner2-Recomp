-- Independent dispatcher-selected handler oracle. No callback-write replay.
-- AB2HND02: index/frame/entry, 15 pre/post registers, IO1 byte,
-- 32 pre/post math words,
-- before/after RAM, own/other sets, ordered writes (RAM and I/O).
local main=manager.machine.devices[":mainpcb:maincpu"]
local sub=manager.machine.devices[":mainpcb:subcpu"]
local space,subspace=main.spaces.program,sub.spaces.program
local pc=main.state.CURPC
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2HND02")
local limit=tonumber(os.getenv("AB2_ORACLE_PASSES") or "120")
local selected=tonumber(os.getenv("AB2_ORACLE_HANDLER") or "b0ac",16)
local wanted={};for target in (os.getenv("AB2_ORACLE_HANDLERS") or os.getenv("AB2_ORACLE_HANDLER") or "b0ac"):gmatch("[^,]+") do wanted[tonumber(target,16)]=true end
local directed=os.getenv("AB2_ORACLE_HANDLER_DIRECTED")=="1"
local frame,records,dropped,guard=0,0,0,false
local before,pre,mathpre,io_input,written,other,events,restore
local leaf=os.getenv("AB2_ORACLE_HANDLER_LEAF")=="1"
local leaf_sp
local isolated=os.getenv("AB2_ORACLE_HANDLER_CALLS")=="1"
local interrupted_registers,interrupted_sr,injected,injected_sp,restore_buttons
if os.getenv("AB2_ORACLE_GAMEPLAY")=="1" then dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua") end
local function u32(v) return string.pack(">I4",v&0xffffffff) end
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

local function port_input()
 guard=true;local value=space:read_u8(0x150001);guard=false;return value
end
local function mathstate()
 guard=true;local p={}
 for _,base in ipairs({0xe0000,0xe4000,0x2e0000,0x2e4000}) do
  for i=0,7 do p[#p+1]=u32(space:read_u16(base+2*i)) end
 end
 guard=false;return table.concat(p)
end
local function fixtures()
 restore={};guard=true
 local function set(a,v,size)
  restore[#restore+1]={a,size==1 and space:read_u8(a) or size==4 and space:read_u32(a) or space:read_u16(a),size}
  if size==1 then space:write_u8(a,v) elseif size==4 then space:write_u32(a,v) else space:write_u16(a,v) end
 end
 local n=records%64
 if selected==0x98c0 then
  local a=main.state.A5.value&0x3fffff
  set(a+16,selected,4)
  set(0x3f90fe,n%16==0 and 1 or 0,1)
  set(0x29c09a,n%16==1 and 1 or 0,2)
  set(0x3f8d76,n%16==2 and 2 or 0,2)
  set(0x3f8d60,(n%16==2 or n%16==3 or n%16==4) and 1 or 0,1)
  set(0x3f89e3,n%16==4 and 1 or 0,1)
  set(0x29c09c,n%16==5 and 15 or 16,2)
  set(0x29c200,n%16==6 and 0 or 16,1)
  set(0x3f91d0,n%16==7 and 16 or 0,1)
  set(0x3f8d77,n%16==8 and 4 or 0,1)
  set(0x2a3cf0,n%16==9 and 192 or 0,2)
  set(0x29c0ce,({0,0x7f,0xff81,0xfffe})[n%4+1],2)
  set(0x29c0d0,({0,0xff81,0x7f,2})[n%4+1],2)
  set(0x29c0b4,({0,0xffc0,0x40,0x7f})[(n//4)%4+1],2)
  set(0x29c096,({0,0x80,0xff80,0x20})[(n//4)%4+1],2);set(0x29c098,0,2)
  set(0x29c09e,n*4,2)
  set(0x29c0aa,({0x100,0x120,0x220,0x320})[(n//4)%4+1],2)
  set(0x3f8d5a,n%4==0 and 0 or 64,2)
  set(0x3f91fe,n%4==0 and 252 or 0,2)
  set(a+4,({0,0x20,0xffe0,2})[n%4+1],2)
  set(a+50,({0,0x20,0xffe0,2})[(n//4)%4+1],2)
  set(a+30,({0,1,0xcc,0xffff})[(n//16)%4+1],2)
  set(a+48,({0,0x40,0xffc0,3})[n%4+1],2)
  guard=false;return
 end
 local axes={0,0x25,0x40,0x7e,0x80,0x82,0xc0,0xff}
 set(0x3f8892,axes[n%8+1],1);set(0x3f8893,axes[(n//8)%8+1],1)
 set(0x3f8894,axes[(n//4)%8+1],1)
 set(0x3f8070,n%3==0 and 4 or 0,1)
 set(0x3f91c4,n//8==1 and 3 or n//8==2 and 1 or 0,1)
 set(0x3f91c5,axes[n%8+1],1)
 set(0x3f90fe,n//8==3 and 4 or n//8==4 and 1 or 0,1)
 set(0x3f8d36,n//8==5 and 1 or 0,1);set(0x3f8d38,0xffc1,2)
 set(0x29c09a,n//8==6 and 1 or 0,2)
 set(0x29c0cc,n//8==7 and 0xffb9 or 0,2)
 set(0x29c09c,n%2==0 and 0x10 or 1,2)
 set(0x3fe000,({0,0x2ee,0x2ef,0x348,0x349})[n%5+1],2)
 local a=main.state.A5.value&0x3fffff
 set(a+22,axes[n%8+1],2);set(a+26,axes[(n//8)%8+1],2)
 set(a+24,({0,1,2,3,0xffff,0xfffe,0xfffd,0x8000})[n%8+1],2)
 set(a+28,({0,1,2,3,0xffff,0xfffe,0xfffd,0x8000})[(n//8)%8+1],2)
 guard=false
end
local function restore_fixture()
 if not restore then return end
 guard=true
 for i=#restore,1,-1 do local v=restore[i];if v[3]==1 then space:write_u8(v[1],v[2]) elseif v[3]==4 then space:write_u32(v[1],v[2]) else space:write_u16(v[1],v[2]) end end
 guard=false;restore=nil
 if restore_buttons then
  for _,v in ipairs(restore_buttons) do v[1]:set_value(v[2]) end;restore_buttons=nil
 end
end
local function finish()
 local after=snapshot();local mathpost=mathstate()
 out:write(u32(records),u32(frame),u32(selected),packed(pre),packed(registers()),u32(io_input),mathpre,mathpost,
  before,after,offsets(written),offsets(other),u32(#events),table.concat(events))
 before=nil;records=records+1;restore_fixture()
 if injected then
  for i=0,7 do main.state["D"..i].value=interrupted_registers[i+1] end
  for i=0,6 do main.state["A"..i].value=interrupted_registers[i+9] end
  main.state.SR.value=interrupted_sr;injected=nil
 end
 if records>=limit then
  out:close();print(string.format("captured %d handler boundaries (%d interrupted windows dropped) -> %s",records,dropped,os.getenv("AB2_ORACLE_OUT")));manager.machine:exit()
 end
end
handler_frames=emu.add_machine_frame_notifier(function() frame=frame+1 end)
handler_reads=space:install_read_tap(0,0x3fffff,"handler_reads",function(a,data,mask)
 if guard then return end
 if leaf and not before and pc.value==selected and frame>=tonumber(os.getenv("AB2_ORACLE_SKIP") or "0") then
  local active=(main.state.SR.value&0x2000~=0) and main.state.SP or main.state.USP
  leaf_sp=active.value
  before=snapshot();pre=registers();mathpre=mathstate();io_input=port_input();written,other,events={},{},{}
 elseif leaf and before then
  local active=(main.state.SR.value&0x2000~=0) and main.state.SP or main.state.USP
  if active.value==leaf_sp and a==(active.value&0x3ffffe) then
   guard=true;local opcode=space:read_u16(pc.value);guard=false
   if opcode==0x4e75 then finish() end
  end
 elseif injected then
  local active=(main.state.SR.value&0x2000~=0) and main.state.SP or main.state.USP
  if before and active.value==injected_sp and a==(active.value&0x3ffffe) then
   guard=true;local opcode=space:read_u16(pc.value);guard=false
   if opcode==0x4e75 then finish() end
  end
 elseif pc.value==0x145ac and not before and not isolated and not leaf and frame>=tonumber(os.getenv("AB2_ORACLE_SKIP") or "0") then
  guard=true;local target=space:read_u32((main.state.A5.value+16)&0x3fffff);guard=false
  if wanted[target] then
   selected=target
   if directed then fixtures() end
   before=snapshot();pre=registers();pre[9]=target;mathpre=mathstate();io_input=port_input();written,other,events={},{},{}
  end
 elseif pc.value==0x145b2 and before then finish() end
end)
handler_writes=space:install_write_tap(0,0x3fffff,"handler_writes",function(a,data,mask)
 if guard or not before then return end
 local active=(main.state.SR.value&0x2000~=0) and main.state.SP or main.state.USP
 local top=injected and injected_sp or leaf and leaf_sp
 -- Stack writes can use a mapped mirror or an unmapped stack address.
 -- Limit the exclusion to the active call's stack, never an entire RAM bank.
 if top and a>=(active.value&0x3ffffe) and a<((top&0x3fffff)+4) then return end
 local o=offset(a)
 if o and o<0x8000 then
  -- Delayed fixtures may interrupt below the usual $FE00 stack window.
  -- Exclude the active stack by the CPU's actual SP, including nested pushes.
  local active=(main.state.SR.value&0x2000~=0) and main.state.SP or main.state.USP
  local stack=offset(active.value&0x3fffff)
  if o>=0x7e00 or (not top and stack and stack<0x8000 and o>=stack) then return end
 end
 if pc.value>=0x1461c and pc.value<=0x14816 then
  before=nil;dropped=dropped+1;restore_fixture();return
 end
 if o then mark(written,o,mask) end
 events[#events+1]=u32(a)..u32(data)..u32(mask)
end)
handler_other=subspace:install_write_tap(0x80000,0xeffff,"handler_other",function(a,data,mask)
 if guard or not before then return end
 local o=offset((a&0xffffe)+0x200000);if o then mark(other,o,mask) end
end)

-- Directed isolated ROM calls are routine fixtures, not a credited caller.
local function inject_call()
 if not isolated or before or frame<540 then return end
 local targets={};for target in pairs(wanted) do targets[#targets+1]=target end;table.sort(targets)
 selected=targets[records%#targets+1]
 interrupted_registers=registers();interrupted_sr=main.state.SR.value
 local stack=(interrupted_sr&0x2000~=0) and main.state.SP or main.state.USP
 -- A task may be using an object-local stack. Do not inject damage
 -- fixtures on top of their scratch compound or allocated debris records.
 if (selected>=0xe744 and selected<=0xecac) or (selected>=0xf58e and selected<=0xfaa0) then
  local stack_offset=offset(stack.value&0x3fffff)
  if stack_offset and ((stack_offset>=0x4400 and stack_offset<=0x4520)
    or (stack_offset>=0x5200 and stack_offset<=0x5420)) then return end
 end
 local sp=(stack.value-4)&0xffffffff
 guard=true;space:write_u32(sp&0x3fffff,main.state.CURPC.value);guard=false;stack.value=sp;injected_sp=sp
 main.state.A5.value=0xffffc400
 main.state.A2.value=0xffffc440;main.state.A3.value=0xffffc480;main.state.A4.value=0xffffc4c0
 main.state.A6.value=0xe4000;main.state.A0.value=selected
 main.state.D0.value=0x12349876;main.state.D1.value=0xabcd7654
 -- Preserve interrupt masking for this artificial call; captures of natural
 -- callers still reject interrupted windows rather than changing cadence.
 main.state.SR.value=interrupted_sr|0x700
 restore={};guard=true
 local function set(a,v,size)
  restore[#restore+1]={a,size==1 and space:read_u8(a) or size==4 and space:read_u32(a) or space:read_u16(a),size}
  if size==1 then space:write_u8(a,v) elseif size==4 then space:write_u32(a,v) else space:write_u16(a,v) end
 end
 local n=records//#targets
 for i=0,3 do
  set(0x3fc400+64*i,({0x80,0xb0,0xff,0x81})[n%4+1],1)
  set(0x3fc42c+64*i,0x70,2);set(0x3fc42a+64*i,0x10,2)
 end
 set(0x2a3cf0,n%4==1 and 192 or 0,2)
 set(0x3f90e4,({0,1,0x4000,0xffff})[n%4+1],2)
 set(0x3fc428,({0,0xfed8,0xffd0,0x7fff})[n%4+1],2)
 set(0x3f90f0,({0,9,10,18,19})[n%5+1],2)
 set(0x3f90f2,({0,1,2,0x8000})[n%4+1],2)
 set(0x3f90e0,n%2==0 and 3 or 4,2)
 set(0x29c09a,n%4==1 and 1 or 0,2);set(0x29c09c,n%2==0 and 1 or 16,2)
 set(0x29c200,n%4==2 and 0 or 16,1);set(0x3f91d0,n%4==3 and 16 or 0,1)
 set(0x3f8d77,n%4==3 and 4 or 0,1);set(0x3f8d78,n%4==2 and 0 or 1,2)
 set(0x3fc42e,n%4,2);set(0x3fc430,({0,5,0xfffb,1})[n%4+1],2)
 if selected==0x88c2 or selected==0x8af2 or selected==0x91c0 or selected==0x9556 or selected==0x8d44 or selected==0x8d58 or selected==0x8e18 or selected==0x8ece or selected==0x8cc8 then
  set(0x3fc436,0x29c190,4)
  set(0x3fc43c,0x28c94,4)
  set(0x3fc43b,0,1);set(0x29c0a2,0,2)
  set(0x29c0c2,({0,0x1000,0x2000,0x7fff})[n%4+1],2)
  set(0x3fc424,({0,0x1000,0x2000,0x7fff})[n%4+1],2)
  set(0x3fc414,n%4,1);set(0x3fc415,n%4,1)
  set(0x29c33c,n%4==1 and 1 or 0,2);set(0x29c33e,0x10,2)
  set(0x3fc418,n%4==0 and 0 or 0x80,1)
  set(0x3fc432,n%4*32,2);set(0x3fc430,n%4,2)
  set(0x3fc42e,n%4,1);set(0x3fc42f,n%4,1)
  set(0x3f91d0,n%4==0 and 1 or 0,1)
  set(0x29c0aa,({0,0x100,0x200,0x7fff})[(n//4)%4+1],2)
  set(0x29c092,0x7f,2);set(0x29c090,0,2)
  set(0x29c0c6,({0,0x40,0xffc0,0x7fff})[n%4+1],2)
  set(0x3fc42a,({0,0x7fff,0xffff,0x1000})[(n//4)%4+1],2)
  set(0x2a3800+n%4*4,({0,0x20,0x100,0x7fff})[n%4+1],2)
 end
 if selected==0x9f7c or selected==0xa030 or selected==0xac4c or selected==0xae1c or selected==0xae2e or selected==0xae50 then
  set(0x29c09a,0,2);set(0x29c09c,16,2)
  set(0x29c200,n%8==0 and 0 or 24,1);set(0x3f91d0,n%8==1 and 16 or 0,1)
  set(0x3fc424,({0,0x20,0x1000,0x3000,0x3fff,0x7fff,0x8000,0xffff})[n%8+1],2)
  set(0x3fc42e,space:read_u16(0x29c0aa)+0x200,2)
  set(0x3fc430,({0,0x10,0xfff0,0x7fff})[n%4+1],2)
  set(0x3fc432,({0,0x100,0xff00,0x7fff})[n%4+1],2)
  set(0x3fc42c,({0x70,0x168,0x169,0xffff})[(n//4)%4+1],2)
  set(0x3fc42a,({0,0x7fff,0x8000,1})[(n//4)%4+1],2)
  set(0x29c09e,n*4,2);set(0x29c09f,n*4,1)
  set(0x3f8d80,n%4==0 and 0 or 1,1);set(0x3f8d81,200,1)
  set(0x29c0a1,n%4==1 and 1 or 0,1);set(0x29c202,n%4==2 and 1 or 0,1)
  set(0x3fc430,n%16*4,2)
  set(0x3f8d46,0x123,2);set(0x3f8d48,0x167,2)
 end
 if selected==0xa234 or selected==0xa382 or selected==0xa53a or selected==0xa9f2 or selected==0xaa32 or selected==0xaaee or selected==0xae32 then
  set(0x29c09c,16,2);set(0x29c200,n%16==0 and 0 or 24,1);set(0x3f91d0,n%16==1 and 16 or 0,1)
  set(0x3fc424,({0,0x1000,0x3000,0x7fff})[(n//16)%4+1],2)
  set(0x3fc42e,space:read_u16(0x29c0aa)+0x200,2)
  set(0x3fc436,n%4*256,2);set(0x3fc438,0xffffc4c0,4)
  set(0x3fc430,n%16*4,2);set(0x3fc43e,({0,6,7,126})[(n//4)%4+1],2)
  set(0x3fc4c0,n%16==2 and 0 or 0x80,1);set(0x3fc4d6,n%16==3 and 0 or 1,1)
  set(0x3fc4e4,n%16==4 and 0 or 0x5000,2)
  set(0x3fc4ea,n%4*256,2);set(0x3fc4ec,0x100,2);set(0x3fc4e8,n*4,2)
  set(0x3fc42a,({0,0x7fff,0xffff,0x1000})[(n//4)%4+1],2)
  set(0x3fc42c,({0x70,0x168,0x169,0xffff})[(n//4)%4+1],2)
  set(0x3f8e7f,n%4==0 and 0 or 1,1);set(0x3f8e80,200,1)
  set(0x29c09e,n*4,2);set(0x29c09f,n*4,1)
 end
 if selected==0x8cc8 then
  local player=space:read_u32(0x1c5c0)&0x3fffff
  set(0x3fc418,n%16==0 and 0 or 0x80,1);set(0x29c09a,n%16==1 and 1 or 0,2)
  set(0x3fc414,0,1);set(0x2a3800,0x40,2);set(0x2a3802,0x40,2)
  set(0x3fc424,0x100,2);set(0x3f8d18,({0x100,0x101,0x2ff,0x300})[(n//4)%4+1],2)
  set(0x3fc404,0,2);set(0x3fc406,0x70,2)
  set(player+4,({0,31,32,0xffe0})[n%4+1],2);set(player+6,({0x70,0x31,0x30,0x71})[(n//16)%4+1],2)
  set(0x3f8d60,0,1)
 end
 if selected>=0x14dc4 and selected<=0x1586a then
  set(0x29c09a,n%16==0 and 1 or 0,2);set(0x29c200,n%16==1 and 0 or 2,1)
  set(0x3f91d0,n%16==2 and 2 or 0,1);set(0x3f90fe,n%16==3 and 1 or 0,1)
  set(0x3fc424,({0,0x1000,0x1ff7,0x1ff8,0x2a9f,0x2aa0,0x4000,0x7fff})[n%8+1],2)
  set(0x3fc43e,0xc440,2);set(0x3fc426,n%6,1);set(0x3fc427,n%6*10,1)
  set(0x3fc42d,({0,0x40,0x10,0x20,0x80,8,0xff,0})[n%8+1],1)
  set(0x3fc418,({0,0x100,0xff00,0x7fff})[(n//8)%4+1],2)
  set(0x3fc41a,0,2);set(0x3fc41c,({0,0x100,0xff00,0x7fff})[n%4+1],2)
  set(0x3fc41e,({0x70,0x168,0,0x7fff})[(n//8)%4+1],2)
  set(0x3fc420,0,2);set(0x3fc422,n*16,2);set(0x3fc428,n*32,2)
  set(0x3fc42c,({0,0xb,0xc,0x1f,0x20,0x21,0x7f,0xff})[(n//4)%8+1],1)
  set(0x3fc42e,0x100,2);set(0x3fc430,0,2);set(0x3fc432,0x70,2);set(0x3fc434,0,2);set(0x3fc43c,0x10,2)
  set(0x3f8e7f,n%16==4 and 0 or 8,1)
  for i=1,8 do set(0x3f8e7f+i,200+i,1) end
  set(0x3f9160,0x3f,1)
  for i=0,5 do
   set(0x3f9162+i*10,n%4==0 and 8 or n%4,1);set(0x3f9163+i*10,n%8,1)
   for j=0,7 do set(0x3f9164+i*10+j,200+j,1) end
  end
  set(0x3f89d6,0x3000,2);set(0x3f8d18,0x1000,2)
  set(0x3f8d20,0xff00,2);set(0x3f8d22,0x100,2);set(0x3f8d24,0,2);set(0x3f8d26,0x100,2)
  set(0x29c09f,n*4,1);set(0x29c0c4,n*4,2)
 end
 if selected~=0xa234 and selected~=0xa382 and selected~=0xa53a then set(0x3fc43a,0x80,1) end
 set(0x3f91c6,({0,29,30,127})[n%4+1],1)
 if selected==0x9626 or selected==0x964c then set(0x3fc424,({0,0x100,0x200,0x3e8})[n%4+1],2) end
 set(0x3fc434,n%2,1)
 if selected>=0x9d2c and selected<=0x9f5e then
  set(0x29c200,({0,8,16,24})[(n//4)%4+1],1)
  set(0x3f91d0,n%7==0 and 8 or 0,1)
  set(0x29c202,n%7==1 and 1 or 0,1);set(0x3f90fe,n%7==2 and 1 or 0,1)
  set(0x29c076,({0,16,32,48})[n%4+1],1)
  set(0x29c077,({0,16,32,48})[(n//4)%4+1],1)
  set(0x3f8b5b,n%2==0 and 1 or 5,1);set(0x3f8d4a,n%4,2)
  for _,t in ipairs({{0x1c5d0,0x3f8e7f},{0x1c5d8,0x3f8ee0}}) do
   local first=space:read_u16(t[1]+4);local count=space:read_u16(t[1]+6)
   set(t[2],n%3==0 and 0 or n%3==1 and 24 or count,1)
   for i=1,count do set(t[2]+i,first+i-1,1) end
  end
  set(0x3fc43e,n*4,2);set(0x29c09a,n%16==0 and 1 or 0,2)
 end
 set(0x3f9102,({0,1,2,3})[n%4+1],1)
 set(0x3f90ff,n%4==0 and 0 or 255,1)
 set(0x3f9103,({0,1,0xe0,0x7f})[(n//4)%4+1],1)
 set(0x3f9100,({0,1,0xffff,15})[(n//4)%4+1],2)
 set(0x3f8d66,({0,1,9,10,11,19,20,21,99,100,101,139,140,0x7fff})[n%14+1],2)
 set(0x3f9106,({0,3,0xffff,0x7fff})[(n//4)%4+1],2)
 set(0x3f8902,n%4==3 and 0 or 1,1);set(0x3f8908,n%4==2 and 63 or 0,2)
 if selected==0xcfc4 then set(0x29c09c,({1,14,15,16,18,19})[n%6+1],2) end
 if selected==0x16c8a then
  local k=n%12
  set(0x3f9102,k==7 and 1 or k==8 and 2 or k==9 and 3 or 0,1)
  set(0x3f90ff,k==0 and 0 or 255,1)
  set(0x3f9103,k==4 and 0xe0 or k==5 and 0 or k==6 and 0x7f or 1,1)
  set(0x3f9100,k==2 and 0 or k==3 and 0xffff or 2,2)
  set(0x29c09c,k==10 and 1 or 16,2);set(0x3f8902,k==11 and 0 or 1,1)
 end
 if selected==0x14976 then
  local port=manager.machine.ioport.ports[":mainpcb:IO1PORTA"]
  local f=port.fields;local previous=port:read();restore_buttons={}
  for i,name in ipairs({"Vulcan","Missile","1 Player Start","Coin 1"}) do
   restore_buttons[#restore_buttons+1]={f[name],(previous&f[name].mask)==0 and 1 or 0}
   f[name]:set_value((n>>(i-1))&1)
  end
 end
 if (selected>=0xe744 and selected<=0xecac) or (selected>=0xf58e and selected<=0xfaa0) then
  -- Damage callees are isolated routine coverage, awaiting the $E662 caller.
  set(0x3f8802,({0,1,0x80000000,0xffffffff})[(n//4)%4+1],4)
  set(0x3f8e7f,({0,3,4,8})[n%4+1],1)
  for i=1,8 do set(0x3f8e7f+i,199+i,1) end
  set(0x3fc414,200,1);set(0x3fc410,selected,4)
  set(0x3fc404,({0,0x20,0xffe0,0x7fff})[n%4+1],2)
  set(0x3fc406,({0x70,0,0x168,0x8000})[(n//4)%4+1],2)
  set(0x3fc42a,({0,0x20,0xffe0,0x7fff})[(n//4)%4+1],2)
  set(0x3fc42c,({0x70,0,0x168,0x8000})[n%4+1],2)
  set(0x3fc41e,n*4,2);set(0x3fc420,({0,1,11,12,0xffff,0xfff5,0xfff4,0x8000})[n%8+1],2)
  set(0x3fc430,({0,0x40,0xffc0,0x8000})[(n//4)%4+1],2)
  set(0x3fc434,({1,2,4,0x8000})[(n//4)%4+1],2)
  set(0x3fc436,({1,2,4,0x8000})[(n//4)%4+1],2)
  set(0x3f91b4,({0,3,4,0x8000})[n%4+1],2)
  set(0x3f91b6,({0,0x400,0x7fff,0x8000})[(n//4)%4+1],2)
  set(0x3f91b8,({0,0x10,0xfff0,0x7fff})[n%4+1],2)
  set(0x3f91ba,({0x70,0,0x168,0x8000})[(n//4)%4+1],2)
  set(0x3f91bc,({0,1,0xffff,0x8000})[n%4+1],2)
  set(0x3f9130,({0,0x40,0xffc0,0x7fff})[n%4+1],2)
  set(0x3f9132,({0,0x20,0x80,0xd0,0xff,0x7fff,0x8000,0xffff})[n%8+1],2)
  set(0x3f912e,({0,1,0xffff,0x7fff})[(n//4)%4+1],2)
  set(0x29c0aa,({0,0x180,0x220,0x7fff})[n%4+1],2)
  set(0x29c0ae,({0,1,7,8,9,0xffff,0xfff8,0xfff7})[n%8+1],2)
  set(0x29c0b4,({0,0x40,0xffc0,0x8000})[(n//4)%4+1],2)
  set(0x29c0b6,({0,0x40,0xffc0,0x8000})[n%4+1],2)
  set(0x29c096,({0,0x59,0xffa8,0x8000})[(n//4)%4+1],2)
  set(0x29c092,0x7f,2);set(0x29c090,0,2);set(0x29c09e,n*4,2)
  set(0x29c080,0x10,2);set(0x29c088,0x20,2);set(0x29c08a,0x30,2)
  set(0x29c630,({0,12,14,0xfffe})[n%4+1],2)
  set(0x29c200,n%16==0 and 0 or 4,1);set(0x3f91d0,n%16==1 and 4 or 0,1)
  -- Resolve all animation data from ROM, including delay and end markers.
  local table=space:read_u32(0xe89e+(n%2)*4)
  local start=space:read_u32(table+4+(n%4)*4)
  local a=start
  local delays,ends,animations={},{},{}
  for i=1,256 do
   local marker=space:read_u16(a)
   if marker==0xffff then ends[#ends+1]=a;break
   elseif marker==0xeeee then delays[#delays+1]=a;a=a+2
   else animations[#animations+1]=a;a=a+16 end
  end
  local trajectory=start
  if n%8==2 and #delays>0 then trajectory=delays[1]
  elseif n%8==3 and #ends>0 then trajectory=ends[1]
  elseif n%8>=4 and #animations>0 then trajectory=animations[n%#animations+1] end
  set(0x3fc41a,trajectory,4)
  main.state.D7.value=({0,0x20,0x7fff,0xffe0})[n%4+1]
 end
 guard=false
 injected=selected;before=snapshot();pre=registers();mathpre=mathstate();io_input=port_input();written,other,events={},{},{}
 main.state.PC.value=selected
end
handler_calls=emu.add_machine_frame_notifier(function()
 if not isolated or before or frame<540 then return end
 local delay=tonumber(os.getenv("AB2_ORACLE_HANDLER_DELAY") or "0")
 if delay==0 then inject_call() else
  handler_delayed_call=coroutine.create(function() emu.wait(delay);inject_call() end)
  assert(coroutine.resume(handler_delayed_call))
 end
end)
