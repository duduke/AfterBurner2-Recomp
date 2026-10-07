-- Bounded sub wait/release phases plus chronological main signal writes.
-- All output stays ignored. Main writer times and external sub/IRQ entries
-- are boundaries; expected ready-read bytes never produce the replay state.
local sub=manager.machine.devices[":mainpcb:subcpu"]
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=sub.spaces.program
local ms=main.spaces.program
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2SUBP2")
local sizes={[0x452]=3,[0x456]=2,[0x458]=5,[0x556]=3,[0x55a]=2,[0x55c]=4,[0x640]=4,[0x646]=2}
local active,guard=nil,false
local stop=false
local order=0
local fixture_index={[0x452]=0,[0x556]=0}
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
  return
 end
 if not sizes[p] then
  if w and (a==0xa3c00 or a==0x9c0d6)then
   out:write(string.char(4),string.pack(">I8I4I4I4I2I2",ns(),order,p,a,d,m))
  end
  return
 end
 if not active then
   local first=(p==0x456 and (a==0x452 or a==p+4))or(p==0x55a and (a==0x556 or a==p+4))or(p==0x646 and (a==0x640 or a==p+4))or(p==0x55c and a==(stack()&0xffffe))or((p==0x452 or p==0x458 or p==0x556 or p==0x640)and a==p+4)
  if w or not first then return end
  local offset=(p==0x456 or p==0x55a or p==0x646) and (a==p+4 and 320 or 160) or 0
  -- Untaken short branch has only one prefetch phase.
  local n=sizes[p];if (p==0x456 or p==0x55a or p==0x646)and a==p+4 then n=1 end
  active={pc=p,n=n,pre=string.pack(">I8",ns()-offset)..snapshot(p),events={}}
 end
 assert(active.pc==p,"sub changed PC within instruction")
 local replacement
 if directed and not w and a==0xa3c00 and frames>=40 and fixture_index[p] and fixture_index[p]<6 then
  local v=({0,1,0x7fff,0x8000,0xffff,0xfffe})[fixture_index[p]+1]
  fixture_index[p]=fixture_index[p]+1;fixtures=fixtures+1
  -- A labelled physical RAM/input fixture, delivered before this read. The
  -- returned tap value and RAM agree; no expected operand enters the C++ port.
  out:write(string.char(2),string.pack(">I8I4I4I4I2I2",ns(),order,0xffffffff,0x2a3c00,v,0xffff))
  guard=true;ms:write_u16(0x2a3c00,v);guard=false
  order=order+1;d=v;replacement=v
 end
 active.events[#active.events+1]=string.pack(">I8I4I4I2I2B",ns(),order,a,d,m,w and 1 or 0)
 if #active.events==active.n then
  local post=state(ns()+320,0)
  local nextpc=w and p+6 or a-2
  out:write(string.char(1),active.pre,string.pack(">I4",#active.events),table.concat(active.events),post:sub(1,8),post:sub(13),string.pack(">I4",nextpc))
  records=records+1;active=nil
 end
 return replacement
end
subpublication_read=space:install_read_tap(0,0xfffff,"subpublication_read",function(a,d,m)return event(false,a,d,m)end)
subpublication_write=space:install_write_tap(0,0xfffff,"subpublication_write",function(a,d,m)event(true,a,d,m)end)
-- Initial shared signals. Other RAM/stack operands remain entry inputs.
guard=true;out:write(string.char(3),string.pack(">I8I2I2",ns(),space:read_u16(0xa3c00),space:read_u16(0x9c0d6)));guard=false
local function mainwrite(a,d,m)
 if guard then return end
 order=order+1
 out:write(string.char(2),string.pack(">I8I4I4I4I2I2",ns(),order,main.state.CURPC.value,a,d,m));writers=writers+1
end
local function mainread(a,d,m)
 if guard then return end
 local p=main.state.CURPC.value
 if p~=0x14674 and p~=0x14798 and p~=0x1467c then return end
 order=order+1;out:write(string.char(6),string.pack(">I8I4I4I4I2I2",ns(),order,p,a,d,m))
end
subpublication_ready_read=ms:install_read_tap(0x2a3c00,0x2a3c01,"subpublication_ready_read",mainread)
subpublication_mutex_read=ms:install_read_tap(0x29c0d6,0x29c0d7,"subpublication_mutex_read",mainread)
subpublication_ready=ms:install_write_tap(0x2a3c00,0x2a3c01,"subpublication_ready",mainwrite)
subpublication_mutex=ms:install_write_tap(0x29c0d6,0x29c0d7,"subpublication_mutex",mainwrite)
if os.getenv("AB2_ORACLE_GAMEPLAY")=="1"then dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")end
subpublication_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1
 if frames>=tonumber(os.getenv("AB2_ORACLE_PASSES")or"120")then
  -- Finish at a subsequent bus boundary, not in a partially captured op.
  stop=true
 end
end)
-- Capture closure is deferred until the next completed instruction is emitted.
subpublication_stop=space:install_read_tap(0,0xfffff,"subpublication_stop",function()
 if stop and not active then
  out:close();print(string.format("captured %d sub publication instructions and %d main signal writes through frame %d (%d partial IRQ cuts, %d endpoint fixtures)",records,writers,frames,cuts,fixtures));manager.machine:exit();stop=false
 end
end)
