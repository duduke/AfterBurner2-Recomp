-- IRQ4 service envelope and $14856 caller timing. Captures stay ignored.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2IRQ42")
local guard,active,last,hook=false,false,nil,nil
local events,hooks,pre={},{},nil
local frame,calls,dropped=0,0,0
local limit=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local directed=os.getenv("AB2_ORACLE_IRQ4_DIRECTED")=="1"
local targets={
 [0x146c6]={0x14a86,4},[0x146ca]={0x13f92,4},[0x146d8]={0x13bcc,4},
 [0x146e6]={0x13bb6,4},[0x146f4]={0x13bfa,4},[0x14702]={0x1329e,4},
 [0x14706]={0x16c8a,6},[0x1470c]={0x18444,6},[0x14712]={0x1495c,4},
 [0x14720]={0x148be,4},[0x14724]={0x17146,4},[0x14728]={0x13e5c,4},
 [0x1473e]={0xb7d2,6},[0x1475a]={0x148be,4},[0x1475e]={0x1495c,4},
 [0x1476c]={0x14a86,4},[0x14770]={0x13f92,4},[0x14774]={0x18444,6},
 [0x1477a]={0x17146,4},[0x1477e]={0x13e5c,4},[0x1478a]={0xb7c2,6},
 [0x147ac]={0x148b8,4},[0x147b0]={0x14990,4},[0x147b4]={0xc6f8,6},
 [0x147ba]={0xe2da,4},[0x147be]={0x147e8,4}
}
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function regs()
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff)end
 for i=0,6 do r[#r+1]=string.pack(">I4",main.state["A"..i].value&0xffffffff)end
 return table.concat(r)
end
local function snapshot()
 guard=true;local parts={}
 for _,r in ipairs({{0x80000,0x83fff},{0xa0000,0xa3fff},{0x280000,0x283fff},{0x2a0000,0x2a3fff},{0xc0000,0xcffff},{0xd0000,0xd0fff},{0x100000,0x100fff},{0x120000,0x123fff},{0x2ec000,0x2ecfff}})do parts[#parts+1]=space:read_range(r[1],r[2],8)end
 guard=false;return table.concat(parts)
end
local function access(w,a,d,m)
 if guard then return end
 local p=main.state.CURPC.value
 if not active and w and last and blank:read(0)~=0 and main.state.SR.value>>8&7==4 and a==((main.state.SP.value-2)&0x3ffffe) then
  if directed then
   guard=true
   space:write_u16(0x2a3c00,({0,1,0x8000,0xffff})[calls%4+1])
   space:write_u8(0x3f8050,calls==limit-5 and 0xff or 1) -- skip explicit credit/service callbacks
   space:write_u8(0x3f913d,3);space:write_u8(0x3f913e,1);space:write_u8(0x3f9140,1)
   -- Directed operand fixtures, not gameplay/stage-selection claims.
   local road_case=calls//4
   space:write_u32(0x3f9142,0x30cf4+((road_case&7)<<4))
   space:write_u8(0x3f9115,road_case&0xff)
   space:write_u8(0x3f9114,road_case&0xff)
   space:write_u16(0x29c060,road_case%26==25 and 0xffff or road_case%26)
   if os.getenv("AB2_ORACLE_IRQ4_ROAD_ENDPOINTS")=="1" then
    space:write_u8(0x3f9115,({0,1,0xfe,0xff})[road_case%4+1])
    space:write_u8(0x3f9114,0xf8|(road_case&7))
    space:write_u16(0x29c060,({0,24,0xffff,0x8000})[road_case%4+1])
   end
   space:write_u8(0x3f8b6f,calls%8==3 and (space:read_u8(0x3f8b6f)&~2) or (space:read_u8(0x3f8b6f)|2))
   space:write_u16(0x3f8174,59);space:write_u16(0x3f881c,59);space:write_u16(0x3f880a,1)
   guard=false
  end
  pre=string.pack(">I8I4I4I2I4I2",last.t+320,last.pc,main.state.PC.value,(last.sr&0xffe0)|(main.state.SR.value&31),main.state.SP.value,last.count)..regs()..snapshot()
  active=true;events={};hooks={};hook=nil;service_return=main.state.PC.value
 end
 if active then
  local target=targets[p]
  if not w and target and a==target[1]+2 then
   assert(not hook,"nested root callback")
   hook={entry=target[1],ret=p+target[2],start=ns()+320,pre=regs(),events={}}
  end
  -- Data/stack and vector transactions are included. ROM instruction
  -- fetches are represented by clocks. External callees remain transcripts.
  -- Include road-palette ROM operands. These MOVE instructions have no
  -- extensions; their prefetch is at p+4. Data ranges in these fixtures
  -- are disjoint from the instruction stream (also asserted by the test).
  local road_read=(p==0x13bc4 or (p>=0x13bf0 and p<=0x13bf6) or p==0x13c12 or (p>=0x13c28 and p<=0x13c36)) and a~=p+4
  if w or a>=0x80000 or a==0x70 or a==0x72 or road_read then
   local e=string.pack(">I8I4I4I2I2B",ns(),p,a,d,m,w and 1 or 0)
   if hook then hook.events[#hook.events+1]=e else events[#events+1]=e end
  end
  if hook and not w and a==hook.ret+2 and p>=0x14674 and p<=0x147e6 then
   -- CURPC can already identify the caller on return prefetch.
   error("unexpected caller CURPC on callback return")
  end
  if hook and not w and a==hook.ret+2 and not targets[p] and not(p>=0x14674 and p<=0x147e6) then
   hooks[#hooks+1]=string.pack(">I4I8I8I4",hook.entry,hook.start,ns()+320,#hook.events)..hook.pre..regs()..table.concat(hook.events);hook=nil
  end
  if not w and (p==0x147c6 or p==0x147e6) and a==((p==0x147e6 and 0x1a132 or service_return+2)&0x3ffffe) then
   assert(not hook,"unfinished callback")
   out:write(pre,string.pack(">I8I4I4",ns()+320,#events,#hooks),table.concat(events),table.concat(hooks),regs(),string.pack(">I4I2",main.state.SP.value,main.state.SR.value))
   active=false;calls=calls+1
  end
 end
 if not active then
  guard=true;local op=space:read_u16(p);guard=false
  last={t=ns(),pc=p,sr=main.state.SR.value,count=main.state["D"..(op>>9&7)].value&0xffff}
 end
end
irq4_reads=space:install_read_tap(0,0x3fffff,"irq4_reads",function(a,d,m)access(false,a,d,m)end)
irq4_writes=space:install_write_tap(0,0x3fffff,"irq4_writes",function(a,d,m)access(true,a,d,m)end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
irq4_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then
  if active then dropped=dropped+1 end
  out:close();print(string.format("captured %d IRQ4 service envelopes through frame %d (%d unfinished)",calls,frame,dropped));manager.machine:exit()
 end
end)
