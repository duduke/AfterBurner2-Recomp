-- Address-bounded caller/IRQ-entry oracle, optionally linked through IRQ2/6
-- and immediate post-RTE exception selection (AB2_ORACLE_CALLER_RETURN).
-- Shared RAM/ADC producers and IRQ4 remain external boundaries.
-- Includes caller prefetches; captures remain ignored.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local continuation=os.getenv("AB2_ORACLE_CALLER_SERVICE")=="1"
local return_selection=os.getenv("AB2_ORACLE_CALLER_RETURN")=="1"
assert(not return_selection or continuation,"RTE selection requires linked services")
local handler,services,resume_check
services=0
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write(return_selection and "AB2CALL4" or continuation and "AB2CALL3" or "AB2CALL2")
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
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function level()return (timer:read(0)~=0 and 2 or 0)|(blank:read(0)~=0 and 4 or 0)end
local function stack()return main.state.SR.value&0x2000~=0 and main.state.SP.value or main.state.USP.value end
local function regs(reverse)
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff)end
 for i=0,6 do local v=main.state["A"..i].value;if reverse and i==5 then v=v-64 end;r[#r+1]=string.pack(">I4",v&0xffffffff)end
 return table.concat(r)
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
  local irq=w and a==((main.state.SP.value-2)&0x3ffffe) and (main.state.SR.value&0x700)>0x100
  if irq then
   emit(q,main.state.PC.value,main.state.SR.value>>8&7)
   entry={pc=q.pc,endns=q.endns,ssp=main.state.SP.value,returnpc=main.state.PC.value,sr=q.sr,regs=q.regs,
    level=main.state.SR.value>>8&7,events={event(w,p,a,d,m)}}
   entry.target=entry.level==2 and 0x1461c or entry.level==4 and 0x14674 or 0x1466c
   return
  elseif q.selected then emit(q,p,0)end
 end
 if not active and sizes[p] then
  local n=sizes[p];local offset=0
  if p==0x14590 then offset=160
  elseif p==0x145a8 or p==0x14866 then offset=a==p+4 and 320 or 160
  elseif p==0x145b6 or p==0x1485c or p==0xb67c then
   if a==p+4 then offset=320;n=1 else offset=160 end
  elseif p==0x145b8 then offset=160 end
  -- First BSR stack write already exposes decremented SP; LEA d16(A5)
  -- already exposes its new A5. Undo only these entry-phase effects.
  local sp=stack()+(p==0x14590 and 4 or 0)
  local selected=true
  if p==0x14856 then spins=spins+1 end
  if (p==0x14856 or p==0x1485c) and spins%128~=0 then
   guard=true;selected=space:read_u16(0x2a3c00)>=0x8000;guard=false
  end
  if p==0xb676 then pubspins=pubspins+1 end
  if (p==0xb676 or p==0xb67c) and pubspins%128~=0 then
   guard=true;local v=space:read_u16(0x2a3c00);selected=v==0 or v>=0x8000;guard=false
  end
  active={pc=p,n=n,selected=selected,events={},pre=string.pack(">I4I8I4I2",p,ns()-offset,sp,main.state.SR.value)..regs(p==0x145a0)}
 end
 if active then
  assert(active.pc==p,"caller changed PC within instruction")
  active.events[#active.events+1]=event(w,p,a,d,m)
  if #active.events==active.n then
   active.endns=ns()+320;active.sr=main.state.SR.value;active.regs=regs(false)
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
local function fixture(k)
 local co=coroutine.create(function()
  local dispatcher=k%4==3
  local lead=dispatcher and (20+(k//4)%100*2) or (1+(k%64)*0.24)
  emu.wait((261*64-lead)/1e6)
  if entry or handler or (main.state.SR.value&0x700)~=0x100 then return end
  guard=true
  local saved={pc=main.state.CURPC.value,sr=main.state.SR.value,ssp=main.state.SP.value,usp=main.state.USP.value,d={},a={}}
  for i=0,7 do saved.d[i]=main.state["D"..i].value end
  for i=0,6 do saved.a[i]=main.state["A"..i].value end
  out:write(string.char(3)) -- explicit scheduler context cut
  if active or pending then cuts=cuts+1 end;active=nil;pending=nil;resume_check=false
  space:write_u16(0x3ffd00,0x46fc);space:write_u16(0x3ffd02,0x2300)
  space:write_u16(0x3ffd04,0x4eb9);space:write_u32(0x3ffd06,dispatcher and 0x14594 or 0x14856)
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
