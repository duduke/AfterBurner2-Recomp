-- Address-bounded caller/IRQ-entry oracle, optionally linked through IRQ2/6
-- and immediate post-RTE exception selection (AB2_ORACLE_CALLER_RETURN).
-- Physical input/shared-RAM producer frontiers and IRQ4 remain boundaries.
-- Includes caller prefetches; captures remain ignored.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local continuation=os.getenv("AB2_ORACLE_CALLER_SERVICE")=="1"
local returned_steps=os.getenv("AB2_ORACLE_CALLER_STEPS")=="1"
local returned_words=os.getenv("AB2_ORACLE_CALLER_WORDS")=="1"
local snapshots=os.getenv("AB2_ORACLE_CALLER_SNAPSHOTS")=="1"
local closure=os.getenv("AB2_ORACLE_CALLER_CLOSURE")=="1"
local successors=os.getenv("AB2_ORACLE_CALLER_SUCCESSORS")=="1"
local return_selection=os.getenv("AB2_ORACLE_CALLER_RETURN")=="1"
assert(not closure or successors,"closure requires successors")
assert(not snapshots or closure,"snapshots require closure")
assert(not successors or returned_words,"successors require returned words")
assert(not returned_words or returned_steps,"returned words require returned steps")
assert(not returned_steps or return_selection,"returned steps require linked RTE selection")
assert(not return_selection or continuation,"RTE selection requires linked services")
local handler,services,resume_check
services=0
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write(snapshots and "AB2CALL9" or closure and "AB2CALL8" or successors and "AB2CALL7" or returned_words and "AB2CALL6" or returned_steps and "AB2CALL5" or return_selection and "AB2CALL4" or continuation and "AB2CALL3" or "AB2CALL2")
-- Sparse board/CPU visibility transcript. Previous completed main bus phase is
-- an explicit boundary across omitted/unported instruction graphs. Retain every
-- bounded caller phase, hardware access and scanline-adjacent observation.
local visibility=assert(io.open(os.getenv("AB2_ORACLE_OUT")..".visibility","wb"))
visibility:write("AB2VIS02")
local previous_end=0
local previous_pc,previous_opcode,previous_source,previous_fetch=0,0,0,false
local opcodes={}
local visibility_records=0
local mux=emu.item(assert(board.items["0/m_pc_0"]))
local ports=manager.machine.ioport.ports
local guard=false
local active,pending,entry
local frames,records,irqs,spins,pubspins,cuts=0,0,0,0,0,0
local sizes={ [0x1458a]=3,[0x14590]=4,[0x14594]=6,[0x1459c]=2,[0x145a0]=2,[0x145a4]=3,
 [0x145a8]=2,[0x145ac]=4,[0x145b0]=4,[0x145b2]=4,[0x145b6]=2,[0x145b8]=2,
 [0x14856]=4,[0x1485c]=2,[0x1485e]=5,[0x14866]=2,[0x1486a]=4,[0x1486e]=5,[0x14876]=1,[0x14878]=4,
 [0xb676]=4,[0xb67c]=2,[0xb67e]=5,[0xb684]=5,[0xb68a]=5,[0xb690]=5,[0xb696]=4 }
if returned_steps then
 sizes[0x88c2]=6;sizes[0x8af2]=7;sizes[0x7bb6]=7;sizes[0x14ee2]=7;sizes[0xb628]=4;sizes[0x15bfe]=4
end
if returned_words then sizes[0x8af8]=4;sizes[0xb62e]=2 end
if successors then sizes[0x8afe]=4;sizes[0xb632]=2 end
if closure then
 sizes[0x8b64]=6;sizes[0x8b6c]=5;sizes[0x8b72]=4;sizes[0x8b76]=1;sizes[0x8b78]=1
 sizes[0x8b7a]=3;sizes[0x8b7e]=3;sizes[0x8b82]=4;sizes[0x8b86]=1;sizes[0x8b88]=1;sizes[0x8b8a]=4
 sizes[0x8b90]=3;sizes[0x8b94]=3;sizes[0x8b9a]=1;sizes[0x8b9c]=3;sizes[0x8ba0]=1;sizes[0x8ba2]=1
 sizes[0x8ba4]=3;sizes[0x8ba8]=1;sizes[0x8baa]=2;sizes[0x8bae]=1;sizes[0x8bb0]=2;sizes[0x8bb4]=1;sizes[0x8bb6]=1;sizes[0x8bb8]=2
 sizes[0x8bbc]=6;sizes[0x8bc4]=5;sizes[0x8bca]=4;sizes[0x8bce]=1;sizes[0x8bd0]=1;sizes[0x8bd2]=3;sizes[0x8bd6]=3
 sizes[0x8bda]=4;sizes[0x8bde]=1;sizes[0x8be0]=1;sizes[0x8be2]=2;sizes[0x8be6]=4;sizes[0x8bec]=3
 sizes[0x8bf0]=3;sizes[0x8bf6]=1;sizes[0x8bf8]=3;sizes[0x8bfc]=3;sizes[0x8c00]=4;sizes[0x8c04]=7
 sizes[0x8c0a]=4;sizes[0x8c0e]=4;sizes[0x8c14]=3;sizes[0x8c16]=4
 sizes[0x9556]=4;sizes[0x955a]=4;sizes[0x955e]=4;sizes[0x9562]=2;sizes[0x9566]=3;sizes[0x9568]=1;sizes[0x956a]=2;sizes[0x956c]=5;sizes[0x9572]=4
 sizes[0x8b30]=4;sizes[0x8b34]=3;sizes[0x8b36]=4;sizes[0x8b3a]=3;sizes[0x8b3c]=3
 sizes[0x8b3e]=1;sizes[0x8b40]=1;sizes[0x8b42]=3;sizes[0x8b48]=3;sizes[0x8b4c]=1
 sizes[0x8b4e]=3;sizes[0x8b52]=6;sizes[0x8b5a]=4;sizes[0x8b5e]=1;sizes[0x8b60]=4
 sizes[0x8c1a]=4
 sizes[0x8b0c]=2;sizes[0x8b10]=6;sizes[0x8b18]=3;sizes[0x8b1c]=3
 sizes[0x8b20]=1;sizes[0x8b22]=1;sizes[0x8b24]=1;sizes[0x8b26]=3;sizes[0x8b2a]=3;sizes[0x8b2e]=1
 sizes[0x8b02]=3;sizes[0x8b06]=1;sizes[0x8b08]=2
 sizes[0xb636]=1;sizes[0xb638]=5;sizes[0xb63e]=1;sizes[0xb640]=1
 sizes[0xb642]=5;sizes[0xb644]=5;sizes[0xb646]=5;sizes[0xb648]=5
 sizes[0xb64a]=4;sizes[0xb64e]=5;sizes[0xb654]=4
end
local completion={
 [0xb636]={reg=0,tail=10},[0x8b06]={reg=1,tail=12},[0x8b24]={reg=0,tail=8},[0x8b2e]={reg=1,variable=true},
 [0x8b40]={reg=6,tail=10},[0x8b5e]={reg=6,tail=16},[0x8b76]={reg=0,tail=4},[0x8bce]={reg=0,tail=4},
 [0x8b78]={reg=0,tail=18},[0x8b86]={reg=0,tail=20},[0x8b88]={reg=0,tail=8},[0x8ba8]={reg=1,tail=4},
 [0x8bd0]={reg=0,tail=18},[0x8bde]={reg=0,tail=20},[0x8be0]={reg=0,tail=8}
}
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function level()return (timer:read(0)~=0 and 2 or 0)|(blank:read(0)~=0 and 4 or 0)end
local function stack()return main.state.SR.value&0x2000~=0 and main.state.SP.value or main.state.USP.value end
local function regs(reverse,initialpc)
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff)end
 for i=0,6 do local v=main.state["A"..i].value;if reverse and i==5 then v=v-64 end
  if initialpc==0xb63e and i==0 then v=(v&0xffff0000)|((v-main.state.D0.value)&0xffff)end;r[#r+1]=string.pack(">I4",v&0xffffffff)end
 return table.concat(r)
end
-- Read-only debugger breakpoints run printf/g at the instruction hook.
-- They preserve the actual boundary state before any first-bus side effects
-- and after internal ALU tails; no expected arithmetic is synthesized here.
local snapshot_cache,snapshot_sequence
local snapshot_addresses={}
local function snapshot(p)
 if not snapshots then return nil end
 local log=manager.machine.debugger.consolelog;local sequence=#log
 if sequence~=snapshot_sequence then
  snapshot_sequence=sequence;snapshot_cache=nil
  local line=log[sequence]
  if line and line:match("^AB2 ") then
   local values={};for hex in line:gmatch("%x+") do values[#values+1]=tonumber(hex,16)end
   -- The prefix AB2 is also hexadecimal.
   assert(#values==19,"invalid instruction snapshot")
   local packed={};for i=5,19 do packed[#packed+1]=string.pack(">I4",values[i]&0xffffffff)end
   snapshot_cache={pc=values[2],sr=values[3],sp=values[4],regs=table.concat(packed)}
  end
 end
 return snapshot_cache and snapshot_cache.pc==p and snapshot_cache or nil
end
if snapshots then
 assert(closure,"instruction snapshots require closure capture")
 local addresses=snapshot_addresses
 for p=0x8b02,0x8c1c,2 do addresses[p]=true end
 for p=0xb636,0xb654,2 do addresses[p]=true end
 for p=0x9556,0x9574,2 do addresses[p]=true end
 for p in pairs(addresses) do
  local fmt=string.format("AB2 %06X %%04X %%08X",p)
  local args="sr,(sp*((sr & 2000)!=0)+usp*((sr & 2000)==0))"
  for i=0,7 do fmt=fmt.." %08X";args=args..",d"..i end
  for i=0,6 do fmt=fmt.." %08X";args=args..",a"..i end
  main.debug:bpset(p,"1",'printf "'..fmt..'",'..args..';g')
 end
 manager.machine.debugger.execution_state="run"
end
local function ram()
 guard=true
 local value=space:read_range(0x80000,0x83fff,8)..space:read_range(0xa0000,0xa3fff,8)
 guard=false;return value
end
local function emit(q,nextpc,irq)
 records=records+1
 out:write(string.char(1),q.pre,string.pack(">I4",#q.events),table.concat(q.events),q.post,string.pack(">I4B",nextpc,irq))
end
local function event(w,p,a,d,m)
 return string.pack(">I8I4I4I2I2BB",ns(),p,a,d,m,w and 1 or 0,level())
end
local visibility_inputs
local function visibility_record(kind,p,a,d,m,t)
 if not visibility_inputs or kind==2 then
  local values={};for i=0,7 do local port=ports[":mainpcb:ADC"..i];values[#values+1]=string.char(port and port:read() or 0x10)end
  visibility_inputs=table.concat(values)
 end
 visibility:write(string.pack(">I8I8I4I4I2I2BBB",t,previous_end,p,a,d,m,kind,level(),mux:read(0)),visibility_inputs,string.pack(">I2I2B",p~=previous_pc and previous_fetch and previous_opcode or 0,previous_source,previous_fetch and 1 or 0))
 visibility_records=visibility_records+1
end
local function access(w,a,d,m)
 if guard then return end
 local p=main.state.CURPC.value
 local t=ns()
 local phase=t%64000
 local hardware=(a>=0xe8000 and a<0xec000) or (a>=0x130000 and a<0x140000) or a==0x140004
 if sizes[p] or (return_selection and (p==0x1466a or p==0x14672)) or hardware or phase<1000 or phase>=63000 then
  visibility_record(w and 1 or 0,p,a,d,m,t)
 end
 previous_end=t+320
 previous_pc=p
 if not opcodes[p] then guard=true;opcodes[p]=space:read_u16(p);guard=false end
 previous_opcode=opcodes[p]
 previous_source=(previous_opcode>>12==14 and previous_opcode&0x20~=0) and (main.state["D"..(previous_opcode>>9&7)].value&0xffff) or 0
 previous_fetch=not w and a>=p+4 and a<=p+10
 if handler then
  assert((p>=0x1461c and p<=0x14672) or (p>=0xb7f8 and p<=0xb83e),"nested/unported linked IRQ handler")
  if w or a>=0x80000 then handler.events[#handler.events+1]=event(w,p,a,d,m)end
  if not w and (p==0x1466a or p==0x14672) and a==((handler.returnpc+2)&0x3ffffe) then
   out:write(string.char(4),handler.before,string.pack(">I4",#handler.events),table.concat(handler.events),
    string.pack(">I8I4I2I4",ns()+320,stack(),main.state.SR.value,handler.returnpc),regs(),ram())
   handler=nil;services=services+1;resume_check={endns=ns()+320,sr=main.state.SR.value,regs=regs()}
  end
  return
 end
 if resume_check then
  local q=resume_check;resume_check=false
  local irq=w and a==((main.state.SP.value-2)&0x3ffffe) and (main.state.SR.value&0x700)>0x100
  if return_selection then
   out:write(string.char(6,irq and (main.state.SR.value>>8&7) or 0))
   if irq then
    entry={pc=p,endns=q.endns,ssp=main.state.SP.value,returnpc=main.state.PC.value,
     sr=q.sr,regs=q.regs,level=main.state.SR.value>>8&7,events={event(w,p,a,d,m)}}
    entry.target=entry.level==2 and 0x1461c or entry.level==4 and 0x14674 or 0x1466c
    return
   end
  end
  -- Unported returned instructions remain explicit execution boundaries.
  if not sizes[p] then out:write(string.char(5))end
 end
 if entry then
  if w or a<0x80 then entry.events[#entry.events+1]=event(w,p,a,d,m)end
  if not w and a==entry.target+2 then
   out:write(string.char(2),string.pack(">I4I8I4I4I2B",entry.pc,entry.endns,entry.ssp,entry.returnpc,entry.sr,entry.level),entry.regs,
    string.pack(">I8I4",ns()+320,#entry.events),table.concat(entry.events),string.pack(">I4I2",main.state.SP.value,main.state.SR.value))
   if continuation and entry.level~=4 then
    handler={before=ram(),events={},returnpc=entry.returnpc}
   end
   entry=nil;irqs=irqs+1
  end
  return
 end
 if pending then
  local q=pending;pending=nil
  local boundary=snapshot(p)
  if snapshots and (completion[q.pc] or q.pc==0xb63e) and p~=q.pc then
   assert(boundary,"missing post-tail instruction-hook snapshot")
  end
  if snapshots and completion[q.pc] and boundary and p~=q.pc then
   q.sr=boundary.sr;q.regs=boundary.regs;q.sp=boundary.sp
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  elseif completion[q.pc] then
   -- Exception entry preserves the completed ALU registers/CCR until
   -- its first stack write. Legacy CALL8 successors also preserve that
   -- late state; CALL9 uses the mandatory instruction-hook snapshot.
   q.sr=(q.sr&0xff00)|(main.state.SR.value&0xff)
   local register=completion[q.pc].reg;local offset=register*4
   q.regs=q.regs:sub(1,offset)..string.pack(">I4",main.state["D"..register].value&0xffffffff)..q.regs:sub(offset+5)
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  end
  if snapshots and q.pc==0xb63e and boundary and p~=q.pc then
   q.sr=boundary.sr;q.regs=boundary.regs;q.sp=boundary.sp
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  elseif q.pc==0xb63e then
   q.regs=q.regs:sub(1,32)..string.pack(">I4",main.state.A0.value&0xffffffff)..q.regs:sub(37)
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  end
  local irq=w and a==((main.state.SP.value-2)&0x3ffffe) and (main.state.SR.value&0x700)>0x100
  if irq then
   emit(q,main.state.PC.value,main.state.SR.value>>8&7)
   entry={pc=q.pc,endns=q.endns,ssp=main.state.SP.value,returnpc=main.state.PC.value,sr=q.sr,regs=q.regs,
    level=main.state.SR.value>>8&7,events={event(w,p,a,d,m)}}
   entry.target=entry.level==2 and 0x1461c or entry.level==4 and 0x14674 or 0x1466c
   return
  elseif q.selected then
   emit(q,p,0)
   if returned_steps and not sizes[p] then out:write(string.char(7))end
  end
 end
 if not active and sizes[p] then
  local n=sizes[p];local offset=0
  if p==0x8b48 or p==0x8ba4 or p==0x8bfc or p==0x8c04 or p==0x8c16 then offset=160
  elseif p==0x14590 then offset=160
  elseif p==0x145a8 or p==0x14866 or p==0xb632 or p==0x8b08 or p==0x8bb0 or p==0x8bb8 then offset=a==p+4 and 320 or 160
  elseif p==0x145b6 or p==0x1485c or p==0xb67c then
   if a==p+4 then offset=320;n=1 else offset=160 end
  elseif p==0x145b8 or p==0x15bfe then offset=160 end
  -- First BSR stack write already exposes decremented SP; LEA d16(A5)
  -- already exposes its new A5. Undo only these entry-phase effects.
  local sp=stack()+((p==0x14590 or p==0x15bfe or p==0x8c16) and 4 or 0)
  local selected=true
  if p==0x14856 then spins=spins+1 end
  if (p==0x14856 or p==0x1485c) and spins%128~=0 then
   guard=true;selected=space:read_u16(0x2a3c00)>=0x8000;guard=false
  end
  if p==0xb676 then pubspins=pubspins+1 end
  if (p==0xb676 or p==0xb67c) and pubspins%128~=0 then
   guard=true;local v=space:read_u16(0x2a3c00);selected=v==0 or v>=0x8000;guard=false
  end
  active={pc=p,n=n,selected=selected,events={},pre=string.pack(">I4I8I4I2",p,ns()-offset,sp,main.state.SR.value)..regs(p==0x145a0,p)}
  local boundary=snapshot(p)
  assert(not (snapshots and snapshot_addresses[p]) or boundary,"missing instruction-hook snapshot")
  if boundary then active.pre=string.pack(">I4I8I4I2",p,ns()-offset,boundary.sp,boundary.sr)..boundary.regs end
 end
 if active then
  assert(active.pc==p,"caller changed PC within instruction")
  active.events[#active.events+1]=event(w,p,a,d,m)
  if #active.events==active.n then
   local tail=completion[p]
   local extra=tail and (tail.variable and (2+2*(main.state.D0.value&63)) or tail.tail)*80 or p==0xb63e and 320 or 0
   active.endns=ns()+320+extra;active.sp=stack();active.sr=main.state.SR.value;active.regs=regs(false)
   active.post=string.pack(">I8I4I2",active.endns,stack(),active.sr)..active.regs
   pending=active;active=nil
  end
 end
end
caller_r=space:install_read_tap(0,0x3fffff,"caller_r",function(a,d,m)access(false,a,d,m)end)
caller_w=space:install_write_tap(0,0x3fffff,"caller_w",function(a,d,m)access(true,a,d,m)end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
-- Place a real IPL3 instruction and ROM caller before the physical line-223
-- edge. Vary lead time; no debugger SR setter is used to unmask pending IRQs.
local directed=os.getenv("AB2_ORACLE_CALLER_DIRECTED")=="1"
local successor_directed=os.getenv("AB2_ORACLE_SUCCESSOR_DIRECTED")=="1"
assert(not successor_directed or (directed and successors),"successor fixture requires directed CALL7")
local function fixture(k)
 local co=coroutine.create(function()
  local dispatcher=not successor_directed and k%4==3
  local lead=dispatcher and (20+(k//4)%100*2) or (1+(k%64)*0.24)
  emu.wait((261*64-lead)/1e6)
  if entry or handler or (main.state.SR.value&0x700)~=0x100 then return end
  guard=true
  local saved={pc=main.state.CURPC.value,sr=main.state.SR.value,ssp=main.state.SP.value,usp=main.state.USP.value,d={},a={}}
  for i=0,7 do saved.d[i]=main.state["D"..i].value end
  for i=0,6 do saved.a[i]=main.state["A"..i].value end
  out:write(string.char(3)) -- explicit scheduler context cut
  if active or pending then cuts=cuts+1 end;active=nil;pending=nil;resume_check=false
  space:write_u16(0x3ffd00,0x46fc);space:write_u16(0x3ffd02,successor_directed and (k%2==0 and 0x2300 or 0x231a) or 0x2300)
  space:write_u16(0x3ffd04,0x4eb9);space:write_u32(0x3ffd06,successor_directed and 0xb632 or dispatcher and 0x14594 or 0x14856)
  space:write_u16(0x3ffd0a,0x46fc);space:write_u16(0x3ffd0c,0x2700)
  space:write_u16(0x3ffd0e,0x4e71);space:write_u16(0x3ffd10,0x60fc)
  space:write_u16(0x3ffd40,0x46fc);space:write_u16(0x3ffd42,saved.sr)
  space:write_u16(0x3ffd44,0x4ef9);space:write_u32(0x3ffd46,saved.pc)
  space:write_u16(0x2a3c00,({0xffff,0xfffe,0x8000})[k%3+1])
  space:write_u16(0x3f8ba4,k%2==0 and 0xffff or 9)
  space:write_u8(0x3f8050,1)
  if dispatcher then
   for i=0,253 do space:write_u8(0x3fa000+64*i,0)end
  end
  main.state.SR.value=saved.sr|0x2000;main.state.PC.value=0x3ffd00
  guard=false
  emu.wait(0.002)
  assert(not entry and not handler and (main.state.SR.value&0x700)~=0x400 and (main.state.SR.value&0x700)~=0x600,"fixture ended during IRQ service")
  guard=true;out:write(string.char(3)) -- fixture restore cuts the CPU schedule
  if active or pending then cuts=cuts+1 end;active=nil;pending=nil;resume_check=false
  for i=0,7 do main.state["D"..i].value=saved.d[i]end
  for i=0,6 do main.state["A"..i].value=saved.a[i]end
  main.state.SP.value=saved.ssp;main.state.USP.value=saved.usp;main.state.PC.value=0x3ffd40
  guard=false
 end)
 assert(coroutine.resume(co))
end
caller_frames=emu.add_machine_frame_notifier(function()
 frames=frames+1
 visibility_record(2,0,0,0,0,ns())
 if directed and frames>=32 and frames%4==0 then fixture(frames//4)end
 if frames>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then
  assert(not handler,"unfinished caller service")
  print(string.format("captured %d linked caller IRQ services",services))
  visibility:close();print(string.format("captured %d CPU visibility observations",visibility_records))
  out:close();print(string.format("captured %d caller instructions, %d IRQ entries (%d fixture cuts, %d unfinished instructions, %d unfinished entries)",records,irqs,cuts,active and 1 or 0,entry and 1 or 0));manager.machine:exit()
 end
end)
