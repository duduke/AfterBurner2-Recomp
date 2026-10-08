-- Bounded timed sub sprite/road phases plus chronological external writes.
-- All output stays ignored. Main writer times and external sub/IRQ entries
-- are boundaries; expected ready-read bytes never produce the replay state.
local sub=manager.machine.devices[":mainpcb:subcpu"]
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=sub.spaces.program
local ms=main.spaces.program
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2SUBT1")
local meta=dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/sub_schedule.lua")
local sizes={[0x432]=4,[0x436]=4,[0x43a]=2,[0x43e]=4,[0x442]=4,[0x446]=2,[0x44a]=4,[0x44e]=4,[0x452]=3,[0x456]=2,[0x458]=5,[0x556]=3,[0x55a]=2,[0x55c]=4,[0x640]=4,[0x646]=2}
for p,m in pairs(meta)do sizes[p]=m.phases end
local active,guard=nil,false
local stop=false
local order=0
local fixture_index={[0x452]=0,[0x556]=0,[0x436]=0,[0x442]=0}
local fixtures=0
local frames,records,writers,cuts=0,0,0,0
local directed=os.getenv("AB2_ORACLE_PUBLICATION_DIRECTED")=="1"
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function regs()
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",sub.state["D"..i].value&0xffffffff)end
 for i=0,6 do r[#r+1]=string.pack(">I4",sub.state["A"..i].value&0xffffffff)end
 return table.concat(r)
end
local function stack()return sub.state.SR.value&0x2000~=0 and sub.state.SP.value or sub.state.USP.value end
local function state(t,p)return string.pack(">I8I4I4I2",t,p,stack(),sub.state.SR.value)..regs()end
local cache,sequence
local function snapshot(p)
 local log=manager.machine.debugger.consolelog
 if #log~=sequence then
  sequence=#log;cache=nil
  local line=log[sequence]
  if line and line:match("^AB2 ")then
   local v={};for h in line:gmatch("%x+")do v[#v+1]=tonumber(h,16)end
   assert(#v==19,"invalid sub snapshot")
   local r={};for i=5,19 do r[#r+1]=string.pack(">I4",v[i]&0xffffffff)end
   cache={pc=v[2],data=string.pack(">I4I4I2",v[2],v[4],v[3])..table.concat(r)}
  end
 end
 assert(cache and cache.pc==p,"missing sub instruction snapshot")
 return cache.data
end
manager.machine.debugger.visible_cpu=sub
for p in pairs(sizes)do
 local fmt=string.format("AB2 %06X %%04X %%08X",p)
 local args="sr,(sp*((sr & 2000)!=0)+usp*((sr & 2000)==0))"
 for i=0,7 do fmt=fmt.." %08X";args=args..",d"..i end
 for i=0,6 do fmt=fmt.." %08X";args=args..",a"..i end
 sub.debug:bpset(p,"1",'printf "'..fmt..'",'..args..';g')
end
manager.machine.debugger.execution_state="run"
local function event(w,a,d,m)
 if guard then return end
 order=order+1
 local p=sub.state.CURPC.value
 -- Sub normal work runs in user mode; supervisor IRQ/trap entry is an
 -- explicit boundary, even while CURPC still names the interrupted wait.
 if sub.state.SR.value&0x2000~=0 then
  if active then out:write(string.char(5));cuts=cuts+1;active=nil end
  if w and (a>=0x80000 and a<0xc0000 or a>=0xe0000 and a<0xf0000) then out:write(string.char(4),string.pack(">I8I4I4I4I2I2",ns(),order,p,a,d,m))end
  return
 end
 if not sizes[p] then
  if w and (a>=0x80000 and a<0xc0000 or a>=0xe0000 and a<0xf0000) then
   out:write(string.char(4),string.pack(">I8I4I4I4I2I2",ns(),order,p,a,d,m))
  end
  return
 end
 if not active then
   local call=p==0x432 or p==0x43e or p==0x44a or p==0x44e
   local wordbranch=p==0x43a or p==0x446
   local first=meta[p]~=nil or (call and a==(stack()&0xffffe))or(wordbranch and (a==p+8 or a==p+4))or((p==0x436 or p==0x442)and a==p+4)or(p==0x456 and (a==0x452 or a==p+4))or(p==0x55a and (a==0x556 or a==p+4))or(p==0x646 and (a==0x640 or a==p+4))or(p==0x55c and a==(stack()&0xffffe))or((p==0x452 or p==0x458 or p==0x556 or p==0x640)and a==p+4)
  if (w and not call and not meta[p])or not first then return end
  local m=meta[p]
  local offset=call and 160 or wordbranch and (a==p+4 and 320 or 160)or (p==0x456 or p==0x55a or p==0x646) and (a==p+4 and 320 or 160) or 0
  -- Untaken short branch has only one prefetch phase.
  local n=sizes[p];local tail=0
  if m then
   offset=m.offset;tail=m.tail
   if m.branch>0 then
    local sr=string.unpack(">I2",snapshot(p),9);local z=sr&4~=0;local negative=sr&8~=0;local v=sr&2~=0;local carry=sr&1~=0
    local take=({BT=true,BEQ=z,BNE=not z,BLE=z or negative~=v,BGE=negative==v,BGT=not z and negative==v,BLT=negative~=v,BPL=not negative,BMI=negative,BCC=not carry,BCS=carry})[m.condition]
    assert(take~=nil,"unknown branch condition");offset=take and 160 or 320;n=take and 2 or (m.branch==2 and 1 or 2)
   elseif m.branch==-2 then
    local data=snapshot(p);local count=string.unpack(">I4",data,11+m.source*4)&0xffff
    offset=160;n=count==0 and 3 or 2
   else offset=offset*80 end
   if m.variable>0 then local data=snapshot(p);local count=string.unpack(">I4",data,11+(m.variable-1)*4)&63;tail=tail+count*2 end
  end
  if (p==0x456 or p==0x55a or p==0x646)and a==p+4 then n=1 end
  active={pc=p,n=n,pre=string.pack(">I8",ns()-offset)..snapshot(p),events={},tail=tail}
 end
 assert(active.pc==p,"sub changed PC within instruction")
 local replacement
 if directed and not w and (a==0xa3c00 or a==0x9c300 or a==0x9c302)and frames>=40 and fixture_index[p] and fixture_index[p]<6 then
  local v=({0,1,0x7fff,0x8000,0xffff,0xfffe})[fixture_index[p]+1]
  fixture_index[p]=fixture_index[p]+1;fixtures=fixtures+1
  -- A labelled physical RAM/input fixture, delivered before this read. The
  -- returned tap value and RAM agree; no expected operand enters the C++ port.
  out:write(string.char(2),string.pack(">I8I4I4I4I2I2",ns(),order,0xffffffff,a|0x200000,v,0xffff))
  guard=true;ms:write_u16(a|0x200000,v);guard=false
  order=order+1;d=v;replacement=v
 end
 active.events[#active.events+1]=string.pack(">I8I4I4I2I2B",ns(),order,a,d,m,w and 1 or 0)
 if #active.events==active.n then
  local post=state(ns()+320+active.tail*80,0)
  local nextpc=meta[p]and (w and meta[p].next or a-2)or w and (p==0x436 or p==0x442)and p+4 or w and p+6 or a-2
  out:write(string.char(1),active.pre,string.pack(">I4",#active.events),table.concat(active.events),post:sub(1,8),post:sub(13),string.pack(">I4",nextpc))
  records=records+1;active=nil
 end
 return replacement
end
subprogress_read=space:install_read_tap(0,0xfffff,"subprogress_read",function(a,d,m)return event(false,a,d,m)end)
subprogress_write=space:install_write_tap(0,0xfffff,"subprogress_write",function(a,d,m)event(true,a,d,m)end)
-- Initial shared signals. Other RAM/stack operands remain entry inputs.
guard=true;out:write(string.char(3),string.pack(">I8I2I2",ns(),space:read_u16(0xa3c00),space:read_u16(0x9c0d6)));guard=false
local fixture_ready
local function mainwrite(a,d,m)
 if guard then return end
 order=order+1
 local p=main.state.CURPC.value
 if fixture_ready and (a&0xfffff)==0xa3c00 then d=fixture_ready;p=0xffffffff end
 out:write(string.char(2),string.pack(">I8I4I4I4I2I2",ns(),order,p,a,d,m));writers=writers+1
 return d
end
local function mainread(a,d,m)
 if guard then return end
 local p=main.state.CURPC.value
 if p~=0x14674 and p~=0x14798 and p~=0x1467c then return end
 order=order+1;out:write(string.char(6),string.pack(">I8I4I4I4I2I2",ns(),order,p,a,d,m))
end
subprogress_ready_read=ms:install_read_tap(0x2a3c00,0x2a3c01,"subprogress_ready_read",mainread)
subprogress_mutex_read=ms:install_read_tap(0x29c0d6,0x29c0d7,"subprogress_mutex_read",mainread)
subprogress_shift=ms:install_write_tap(0x280000,0x2bffff,"subprogress_shift",mainwrite)


if os.getenv("AB2_ORACLE_GAMEPLAY")=="1"then dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")end
-- Directed bounded graph calls use a real SR-writing trampoline. Injection
-- is allowed only while a user-mode read/branch wait has no stack side effect.
local fixture_calls=0
local function graph_fixture()
 if frames<32 or fixture_ready~=nil or sub.state.SR.value&0x2000~=0 then return end
 local pc=sub.state.CURPC.value
 if pc~=0x452 and pc~=0x456 and pc~=0x556 and pc~=0x55a and pc~=0x534 and pc~=0x640 and pc~=0x646 then return end
 if active then out:write(string.char(5));cuts=cuts+1;active=nil end
 local k=fixture_calls;fixture_calls=k+1
 local saved={pc=pc,sr=sub.state.SR.value,sp=sub.state.SP.value,usp=sub.state.USP.value,d={},a={}}
 for i=0,7 do saved.d[i]=sub.state["D"..i].value end
 for i=0,6 do saved.a[i]=sub.state["A"..i].value end
 guard=true;local ram=space:read_range(0x9c000,0xa3fff,8);guard=false
 local function put(a,v)
  order=order+1;out:write(string.char(2),string.pack(">I8I4I4I4I2I2",ns(),order,0xffffffff,a|0x200000,v&0xffff,0xffff))
  guard=true;space:write_u16(a,v&0xffff);guard=false
 end
 local function long(a,v)put(a,v>>16);put(a+2,v)end
 local entry=({0x648,0xc0a,0xd0e,0xd64,0xf02,0x16d4,0x45e,0x5b6,0x5ee})[k%9+1]
 if k==126 then entry=0x11d2 end -- isolated trailing RTS, no caller claimed
 local n=k//9
 fixture_ready=entry==0x45e and 1 or 0xffff
 -- Explicit initial operand fixtures; every graph instruction executes in ROM.
 put(0xa3e00,0x46fc);put(0xa3e02,0x0700);put(0xa3e04,0x4eb9);long(0xa3e06,entry)
 put(0xa3e0a,0x4e71);put(0xa3e0c,0x60fc)
 put(0xa3e40,0x46fc);put(0xa3e42,saved.sr);put(0xa3e44,0x4ef9);long(0xa3e46,saved.pc)
 put(0xa3c00,entry==0x45e and 1 or 0xffff);put(0x9c0d6,0)
 -- Runtime pointers and flags, not native descriptors.
 long(0xa3c08,0xa0000);long(0xa3c10,0xa1000);long(0xa3c18,0x9c800);long(0xa3c20,0xa1002)
 local count=n==0 and 0 or n==1 and 1 or n==12 and 255 or 40
 put(0xa1000,count);put(0x9c03a,n%8==1 and n~=1 and 0x0100 or 0)
 for a=0xa1800,0xa37fe,2 do put(a,0)end
 for i=0,count do
  for j=0,12,2 do put(0x9c800+i*16+j,0)end
  put(0x9c800+i*16+14,i*16)
 end
 for i=0,count-1 do
  local a=0xa0000+i*16
  local zoom=({0,1,0x3f,0x40,0x7f,0xff,0x1ff,0x3ff})[n%8+1]
  put(a,(i%8==7 and 0x3ff or zoom)|((i%8)<<12)|((n%4)<<10));put(a+2,((n%2==0 and 0xff or 0x80)<<8)|0xf2)
  put(a+4,({0xfe00,0xff00,0,0x80,0x140,0x200,0x400,0xffff})[(n+i)%8+1])
  put(a+6,({0xfe00,0xffce,0,112,256,384,512,1024})[(n+i//8)%8+1])
  put(a+8,10);put(a+10,120);long(a+12,(i<<24)|0x100f0)
 end
 put(0x9c09a,(n//6)%2);put(0x9c09c,({1,3,7,0x10,0x18,2})[n%6+1])
 put(0x9c0ce,({0,0xb1,0xff4f,0x7fff,0x8000,0xffff})[n%6+1])
 put(0x9c200,n%2==0 and 0x0001 or 0);put(0x9c0ae,({0,1,5,7,0xffff,0xfff9})[n%6+1]);put(0x9c0b2,({0,0xc1,0xff3f,0x8000,0x7fff,1})[(n//6)%6+1])
 put(0x9c0b0,0)
 put(0x9c0b4,({0,0x300,0xfd00,0x8000,0x7fff})[n%5+1]);put(0x9c0b6,({1,0xfd00,0x300,0x7fff,0x8000})[n%5+1])
 put(0x9c0d0,({0,0x7fff,0x8000,0x8001,0xffff,0x70,0xff90})[n%7+1]);put(0x9c0ea,0xff90);put(0x9c0ec,0x70);put(0x9c0ee,2)
 put(0x9c09e,({0,0x40,0x80,0xc0,0x7f,0xff})[n%6+1]);put(0x9c094,0);put(0x9c096,({0,0x70,0xff90,0x400,0xfc00,0x7fff,0x8000})[n%7+1]);put(0x9c0c2,0x1fe0)
 put(0x9c082,({0,111,0xff91})[n%3+1]);put(0x9c060,0)
 for a=0x9c620,0x9c62e,2 do put(a,({0,0x7f,0x80,0xff,0x100,0x17f,0x180,0x1ff})[n%8+1])end
 put(0x9c320,({0x0300,0x0301,0x0b01,0x4301,0x8301,0xff01,0x0302,0x0304,0x03ff,0x0001,0x4701,0xcb01})[n%12+1])
 long(0x9c322,n==10 and 0xfffb0140 or ({0xffffffff,0,0x1fe0,0x1fdf,0xfffb013f,0x4fec0})[n%6+1]);put(0x9c326,({0,0x1000,0xf000,0x1001,0xefff,0xffff})[n%6+1]);put(0x9c338,n*0x3e0);put(0x9c33a,n%2==0 and 0x1001 or 0xefff)
 long(0x9c328,0xff);long(0x9c32c,0x010001ff);put(0x9c54a,0)
 sub.state.A6.value=0x9c000;sub.state.A4.value=0xe4000
 sub.state.USP.value=0xa3ef0;sub.state.SR.value=saved.sr|0x2000;sub.state.PC.value=0xa3e00
 local co=coroutine.create(function()
  emu.wait(0.008)
  assert(sub.state.CURPC.value==0xa3e0a or sub.state.CURPC.value==0xa3e0c or entry==0x45e and (sub.state.CURPC.value==0x556 or sub.state.CURPC.value==0x55a),string.format("directed sub graph failed to return: fixture=%d entry=%06x pc=%06x sr=%04x frame=%d",k,entry,sub.state.CURPC.value,sub.state.SR.value,frames))
  if active then out:write(string.char(5));cuts=cuts+1;active=nil end
  fixture_ready=nil
  for a=0x9c000,0xa3ffe,2 do local o=a-0x9c000+1;put(a,(ram:byte(o)<<8)|ram:byte(o+1))end
  -- Restored SR is executed by an instruction, then JMP re-enters the safe wait.
  put(0xa3e40,0x46fc);put(0xa3e42,saved.sr);put(0xa3e44,0x4ef9);long(0xa3e46,saved.pc)
  for i=0,7 do sub.state["D"..i].value=saved.d[i]end
  for i=0,6 do sub.state["A"..i].value=saved.a[i]end
  sub.state.SP.value=saved.sp;sub.state.USP.value=saved.usp;sub.state.SR.value=saved.sr|0x2000;sub.state.PC.value=0xa3e40
 end)
 assert(coroutine.resume(co))
end

subprogress_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1
 if os.getenv("AB2_ORACLE_SUB_GRAPH_DIRECTED")=="1"and frames+2<tonumber(os.getenv("AB2_ORACLE_PASSES")or"120")then
  local co=coroutine.create(function()emu.wait(0.006);graph_fixture()end);assert(coroutine.resume(co))
 end
 if frames>=tonumber(os.getenv("AB2_ORACLE_PASSES")or"120")then
  -- Finish at a subsequent bus boundary, not in a partially captured op.
  stop=true
 end
end)
-- Capture closure is deferred until the next completed instruction is emitted.
subprogress_stop=space:install_read_tap(0,0xfffff,"subprogress_stop",function()
 if stop and not active then
  out:close();print(string.format("captured %d sub progress instructions and %d main signal writes through frame %d (%d partial cuts, %d endpoint fixtures, %d graph fixtures)",records,writers,frames,cuts,fixtures,fixture_calls));manager.machine:exit();stop=false
 end
end)
