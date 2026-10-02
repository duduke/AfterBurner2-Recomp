-- CPU service windows, from the final interrupted-instruction bus phase to
-- RTE's final return prefetch. Bus timestamps are outputs for comparison.
-- No ROM code or captures belong in git. Complete work RAM is an input.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer_irq=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local mux=emu.item(assert(board.items["0/m_pc_0"]))
local ports=manager.machine.ioport.ports
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2SVC02")
local guard,active,finished=false,false,false
local events,pre,last={},{},nil
local calls,dropped,frame=0,0,0
local controls={0xff,0xff}
local unmask_probe
local directed=os.getenv("AB2_ORACLE_SERVICE_DIRECTED")=="1"
local function ns() return math.floor(manager.machine.time:as_double()*1e9+0.5) end
local function regs()
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff) end
 for i=0,6 do r[#r+1]=string.pack(">I4",main.state["A"..i].value&0xffffffff) end
 return table.concat(r)
end
local function ram()
 guard=true;local r=space:read_range(0x80000,0x83fff,8)..space:read_range(0xa0000,0xa3fff,8);guard=false;return r
end
local function inputs()
 local values={};for i=0,7 do local port=ports[":mainpcb:ADC"..i];values[#values+1]=string.char(port and port:read() or 0x10) end
 return table.concat(values)
end
local function hardware(kind,a,d,m)
 if guard or finished or active then return end
 out:write(string.pack(">BI8I4I2I2BB",kind,ns(),a,d,m,timer_irq:read(0),blank:read(0)),inputs())
end
local function finish()
 out:write(string.char(6),pre,string.pack(">I8I4",ns()+320,#events),table.concat(events),regs(),ram(),string.pack(">I4I2",main.state.SP.value,main.state.SR.value))
 active=false;calls=calls+1
end
local function access(w,a,d,m)
 if guard or finished then return end
 if w and (a==0x14000c or a==0x14000e) and m&255~=0 then controls[(a-0x14000c)//2+1]=d&255 end
 local p=main.state.CURPC.value
 if unmask_probe and not w and p==0x3ffd20 and (main.state.SR.value&0x700)==0x100 then
  unmask_probe();unmask_probe=nil
 end
 if not active and w and last and timer_irq:read(0)~=0 and
    (main.state.SR.value>>8&7==2 or main.state.SR.value>>8&7==6) and
    a==((main.state.SP.value-2)&0x3ffffe) and not (p>=0x1461c and p<=0x14670) then
  if directed then
   guard=true
   local case=calls%4
   space:write_u16(0x3f8890,calls%6)
   space:write_u8(0x3f8902,case==0 and 0 or 0xff)
   space:write_u16(0x3f8900,case==1 and 1 or 0)
   space:write_u8(0x3f8906,case==2 and 0 or 2)
   space:write_u16(0x3f890a,63)
   space:write_u8(0x3f894b,0x40)
   guard=false
  end
  local sr=(last.sr&0xffe0)|(main.state.SR.value&0x1f)
  local returnpc=main.state.PC.value
  pre=string.pack(">I8I4I4I2BBBB",last.t+320,last.pc,returnpc, sr,
    main.state.SR.value>>8&7,timer_irq:read(0),blank:read(0),mux:read(0))..
    string.pack(">I4",main.state.SP.value)..regs()..ram()..inputs()..string.char(controls[1],controls[2])..string.pack(">I2",last.count)
  active=true;events={};service_return=returnpc
 end
 if active then
  local in_handler=(p>=0x1461c and p<=0x14670) or (p>=0xb7f8 and p<=0xb83e)
  -- Vector reads and complete data/stack transactions are compared. ROM
  -- instruction prefetch is accounted for by cycles, not captured inputs.
  if w or (a>=0x80000 and not (a>=0x3ffd00 and a<0x3ffd80)) or a==0x68 or a==0x6a or a==0x78 or a==0x7a then
   events[#events+1]=string.pack(">I8I4I4I2I2B",ns(),p,a,d,m,w and 1 or 0)
  end
  if not w and (p==0x1466a or p==0x14672) and a==((service_return+2)&0x3ffffe) then finish()
  elseif w and not in_handler and a~=((main.state.SP.value-2)&0x3ffffe) and a<0x3ffe00 then
   active=false;dropped=dropped+1
  end
 end
 if not active then
  local op
  if last and last.pc==p then op=last.op
  else guard=true;op=space:read_u16(p);guard=false end
  last={t=ns(),pc=p,op=op,sr=main.state.SR.value,count=main.state["D"..(op>>9&7)].value&0xffff}
 end
end
service_r=space:install_read_tap(0,0x3fffff,"service_r",function(a,d,m) access(false,a,d,m) end)
service_w=space:install_write_tap(0,0x3fffff,"service_w",function(a,d,m) access(true,a,d,m) end)
service_adc_r=space:install_read_tap(0x130000,0x13ffff,"service_adc_r",function(a,d,m) hardware(1,a,d,m) end)
service_adc_w=space:install_write_tap(0x130000,0x13ffff,"service_adc_w",function(a,d,m) hardware(2,a,d,m) end)
service_mux=space:install_write_tap(0x140004,0x140005,"service_mux",function(a,d,m) hardware(3,a,d,m) end)
service_timer=space:install_write_tap(0xe8000,0xebfff,"service_timer",function(a,d,m) hardware(4,a,d,m) end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
service_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if active then
  -- A frame/input boundary is an external input, not a CPU timing output.
  events[#events+1]=string.pack(">I8I4I4I2I2B",ns(),0,0,0,0,2)..inputs()
 else hardware(5,0,0,0) end
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "900") then
  finished=true;if active then dropped=dropped+1 end
  out:close();print(string.format("captured %d CPU service calls through frame %d (%d interrupted)",calls,frame,dropped));manager.machine:exit()
 end
end)


-- Execute real MOVE.W #imm,SR instructions in a small RAM trampoline.
-- MAME's debugger SR setter does not call update_interrupt; it cannot stand
-- in for the CPU unmask instruction. These are synthesized fixture opcodes,
-- not ROM data. NOP/BRA loops expose pending/masked and unmask boundaries.
if directed then
 service_mask=coroutine.create(function()
  emu.wait(9.2)
  while active or (main.state.SR.value&0x700)~=0x100 do emu.wait(0.000002) end
  emu.wait(0.000002)
  local saved_sr=main.state.SR.value
  local saved_pc=main.state.CURPC.value
  local ssp,usp=main.state.SP.value,main.state.USP.value
  guard=true
  for _,base in ipairs({0x3ffd00,0x3ffd20}) do
   space:write_u16(base,0x46fc) -- MOVE.W #imm,SR
   space:write_u16(base+2,base==0x3ffd00 and 0x2700 or 0x2100)
   space:write_u16(base+4,0x4e71) -- NOP
   space:write_u16(base+6,0x60fc) -- BRA.S back to NOP
  end
  space:write_u16(0x3ffd40,0x46fc);space:write_u16(0x3ffd42,saved_sr)
  space:write_u16(0x3ffd44,0x4ef9);space:write_u32(0x3ffd46,saved_pc) -- JMP abs.L
  main.state.SR.value=saved_sr|0x2000
  main.state.PC.value=0x3ffd00
  guard=false
  emu.wait(0.000002)
  assert(main.state.SR.value&0x700==0x700,"mask instruction did not execute")
  local baseline=calls
  local function probe(kind,stage)
   local level=(timer_irq:read(0)~=0 and 2 or 0)|(blank:read(0)~=0 and 4 or 0)
   out:write(string.pack(">BBI8I2BI4",kind,stage,ns(),main.state.SR.value,level,calls-baseline))
   return level
  end
  probe(7,0)
  local pending=false
  for i=1,96 do
   emu.wait(0.000064);if probe(7,1)~=0 then pending=true end
   assert(calls==baseline,"CPU serviced an IRQ while masked")
  end
  assert(pending,"mask fixture did not observe a pending IRQ")
  unmask_probe=function() assert(probe(8,0)>=2,"unmask requires pending timer IRQ") end
  guard=true;main.state.PC.value=0x3ffd20;guard=false
  emu.wait(0.000128)
  assert(not active and not unmask_probe and calls>baseline,"pending IRQ was not serviced after unmask")
  probe(9,0)
  guard=true
  main.state.SP.value=ssp;main.state.USP.value=usp;main.state.PC.value=0x3ffd40
  guard=false
 end)
 assert(coroutine.resume(service_mask))
end
