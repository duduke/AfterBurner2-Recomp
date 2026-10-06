-- IRQ4 service envelope and $14856 caller timing. Captures stay ignored.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2IRQ46")
local guard,active,last,hook=false,false,nil,nil
local events,hooks,subevents,pre={},{},{},nil
local frame,calls,dropped,operand_frontier=0,0,0,0
local limit=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local directed=os.getenv("AB2_ORACLE_IRQ4_DIRECTED")=="1"
-- Data/RMW instruction lengths; extensions and prefetches are clock phases.
local operand_lengths={
 [0xb84c]=4,[0xb854]=8,[0xb860]=4,[0xb876]=4,[0xb87c]=4,[0xb880]=4,[0xcfc4]=8,[0xcfd0]=8,
 [0xcfdc]=4,[0xcfe0]=6,[0xcfea]=6,[0x13e5c]=8,[0x13e6c]=4,[0x13e70]=4,[0x13e80]=4,[0x13e84]=6,
 [0x13e8e]=8,[0x13e96]=6,[0x13e9c]=4,[0x13ea0]=8,[0x13f92]=6,[0x13f9c]=4,[0x13fa8]=4,[0x13fb6]=4,
 [0x13fba]=4,[0x13fca]=2,[0x13fcc]=2,[0x13fce]=2,[0x13fd0]=2,[0x13fd2]=2,[0x13fd4]=2,[0x13fd6]=2,
 [0x13fd8]=4,[0x13fe6]=4,[0x13fea]=4,[0x13ffa]=2,[0x13ffc]=2,[0x13ffe]=2,[0x14000]=2,[0x14002]=2,
 [0x14004]=2,[0x14006]=2,[0x14008]=4,[0x14016]=4,[0x1401a]=4,[0x14028]=2,[0x1402a]=2,[0x1402c]=2,
 [0x1402e]=2,[0x14030]=2,[0x14032]=2,[0x14034]=2,[0x14036]=6,[0x14046]=4,[0x14054]=2,[0x14056]=2,
 [0x14058]=2,[0x1405a]=2,[0x1405c]=2,[0x1405e]=2,[0x14060]=2,[0x14062]=6,[0x1406e]=4,[0x14092]=4,
 [0x14096]=4,[0x140a4]=2,[0x140a8]=2,[0x140aa]=4,[0x140c2]=2,[0x140c4]=2,[0x140d4]=2,[0x140d6]=2,
 [0x142dc]=6,[0x142f2]=8,[0x14316]=4,[0x1431a]=2,[0x14326]=2,[0x14338]=2,[0x143c6]=2,[0x143c8]=2,
 [0x143ca]=4,[0x143de]=2,[0x143e0]=2,[0x143e2]=4,[0x143f6]=2,[0x143f8]=2,[0x14412]=6,[0x1441a]=8,
 [0x1442a]=2,[0x1443a]=6,[0x14460]=2,[0x14478]=2,[0x1447a]=2,[0x1447c]=2,[0x1447e]=2,[0x14480]=2,
 [0x14482]=2,[0x14484]=2,[0x14488]=6,[0x1448e]=2,[0x14490]=2,[0x14492]=2,[0x14494]=2,[0x14496]=2,
 [0x14498]=2,[0x1449a]=2,[0x14a86]=8,[0x14a90]=6,[0x14aa4]=6,[0x14aaa]=6,[0x14ab6]=6,[0x14ac4]=2,
 [0x14aca]=6,[0x14ad0]=6,[0x14ada]=6,[0x14b14]=2,[0x14b16]=2,[0x14b18]=2,[0x14b1a]=2,[0x14b1c]=2,
 [0x14b1e]=2,[0x14b20]=2,[0x14b2a]=6,[0x14b34]=6,[0x14b58]=2,[0x14b5a]=2,[0x14b5c]=2,[0x14b5e]=2,
 [0x14b60]=2,[0x14b62]=2,[0x14b64]=2,[0x14b88]=2,[0x14b8a]=2,[0x14b8c]=2,[0x14b8e]=2,[0x14b90]=2,
 [0x14b92]=2,[0x14b94]=2,[0x14b96]=2,[0x14ba0]=2,[0x14ba6]=6,[0x14bac]=6,[0x14bc0]=6,[0x14bd6]=2,
 [0x14c26]=6,[0x14c2e]=6,[0x16c8a]=6,[0x16c94]=6,[0x16c9e]=6,[0x16ca4]=4,[0x16ca8]=6,[0x16cae]=4,
 [0x16cb2]=4,[0x16cbe]=4,[0x16cd8]=4,[0x16ce0]=4,[0x16cec]=4,[0x16cf4]=4,[0x16cf8]=4,[0x16d1c]=4,
 [0x16d22]=6,[0x16d28]=4,[0x16d2c]=6,[0x16d3a]=4,[0x18444]=4,[0x1844c]=6,[0x1848e]=4,[0x184a0]=4,
 [0x184a8]=6,[0x184ca]=4,[0x184e6]=2,[0x184e8]=2,[0x184ea]=2,[0x184ec]=2,[0x184ee]=2,[0x184f0]=2,
 [0x184f2]=2,[0x184f4]=2,[0x184f6]=2,[0x184f8]=2,[0x184fa]=2,[0x18502]=4,[0x18506]=2,[0x18510]=2,
 [0x1851a]=4,[0x1852a]=2,[0x1853e]=4,[0x1854c]=4,[0x1857a]=4,[0x18582]=4,[0x18588]=4,[0x1858e]=4,
 [0x18594]=4,[0x1859a]=4,[0x1859e]=4,[0x185a8]=6,[0x185ae]=4,[0x185b2]=4,[0x185b6]=6,[0x185c2]=2,
 [0x185c6]=2,[0x185c8]=2,[0x185d2]=2,[0x185d4]=2,[0x185d6]=2,[0x185d8]=2,[0x185e0]=4,[0x185e6]=4,
 [0x185ec]=4,[0x185f0]=4,[0x185fa]=4,[0x185fe]=4,[0x18608]=4,[0x18616]=4,[0x1861a]=4,[0x18624]=4,
 [0x18632]=4,[0x18636]=4,[0x18640]=4,[0x1864e]=4,[0x18652]=4,[0x1865c]=4,[0x1866c]=2,[0x18672]=4,
 [0x18678]=4,[0x18680]=2,[0x18688]=2,[0x1868a]=2,[0x18690]=2,[0x186aa]=2,[0x186ac]=2,[0x186b2]=2,
 [0x1871a]=4,[0x1871e]=4,[0x1872a]=6,[0x18730]=6,[0x1874a]=2,[0x1874c]=2,[0x1874e]=2,[0x1876a]=6,
 [0x1878c]=2,[0x1878e]=2,[0x18790]=2,[0x18798]=8,[0x187a4]=6,[0x187b6]=2,[0x187c2]=4,[0x187d0]=4,
 [0x187d4]=2,[0x187d6]=2,[0x187d8]=2,[0x187e4]=6,[0x18800]=6,[0x1881e]=6,[0x18832]=2,[0x18834]=2,
 [0x18836]=2,[0x18848]=2,[0x1884a]=2,[0x1884c]=2,[0x18852]=8,[0x1885a]=6,[0x1887a]=2,[0x1887c]=2,
 [0x1887e]=4,[0x1888a]=2,[0x18898]=2,[0x188a0]=2,[0x18906]=2
}
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
   if os.getenv("AB2_ORACLE_IRQ4_CYCLE_ENDPOINTS")=="1" then
    -- Cross unsigned selector boundaries, phase masking/word wrap and
    -- both hardware/fade destinations. Earlier fade executes normally.
    space:write_u16(0x29c09c,({0x11,0x12,0x16,0x17,0x10,0xffff,0,0x8000})[road_case%8+1])
    space:write_u16(0x3f9146,({0,1,0x0f,0x10,0x1f,0x20,0x7fff,0x8000,0xffff})[(road_case//8)%9+1])
    space:write_u8(0xcdd07,0);space:write_u16(0xcdd02,0)
    space:write_u16(0xcdd00,0);space:write_u8(0xcdd05,(road_case//72)%2==0 and 0 or 2)
   end
   if os.getenv("AB2_ORACLE_IRQ4_CALLEE_ENDPOINTS")=="1" then
    local k=calls//4
    local stages={15,16,5,13,20,1,7,9}
    space:write_u16(0x29c09c,stages[k%8+1])
    -- Fade trigger/capture, all upload banks, all three channel masks,
    -- inactive animation, blend setup/step and transition completion.
    local fade=k%4==0
    space:write_u8(0xcdd07,fade and k%12==0 and 1 or 0)
    space:write_u8(0xcdd04,k&31);space:write_u8(0xcdd06,(k//4)&7)
    space:write_u16(0xcdd02,fade and (k//4)%5==0 and (1+(k//20)%3) or 0)
    space:write_u16(0xcdd00,(k//4)&7)
    space:write_u8(0xcdd05,fade and 2 or 0)
    space:write_u8(0xcdd08,({0,1,2,2})[k%4+1]);space:write_u8(0xcdd09,k%8<4 and 1 or 0x40)
    space:write_u16(0x3f8c2a,({0,4,30,31})[k%4+1])
    space:write_u16(0x3f8d30,0);space:write_u16(0x3f8b5a,k&7)
    space:write_u16(0x29c0d2,k%9==0 and 0xffff or (k%32)<<4)
    space:write_u8(0x29c54c,k%6==0 and 0 or 2)
    space:write_u8(0x3f8c2e,k&1);space:write_u8(0x3f8c2c,k%8)
    -- Reload request priorities, signed delay, exhaustion, sound disable
    -- and wrap, decimal tens/hundreds and HUD clamp.
    space:write_u8(0x3f9102,({0,1,2,3})[k%4+1])
    space:write_u8(0x3f90ff,k%7==0 and 0 or 255)
    space:write_u8(0x3f9103,({1,0,0xe0,0x7f})[(k//4)%4+1])
    space:write_u16(0x3f9100,({2,0,0xffff,15})[(k//16)%4+1])
    space:write_u16(0x3f8d66,({0,1,9,10,19,99,100,139,140,0x7fff})[k%10+1])
    space:write_u16(0x3f9106,({0,3,0xffff,0x7fff})[(k//4)%4+1])
    space:write_u8(0x3f8902,k%5==0 and 0 or 1);space:write_u16(0x3f8908,k%2==0 and 63 or 0)
    -- Scenery visibility entry/exit, segmented display and tile clear.
    -- Refuel-mode stages retain their ROM-initialized event records.
    space:write_u16(0x3f9044,k%13==0 and 7 or 0)
    space:write_u16(0x3f9046,k%(8*(space:read_u16(0x188ca)+1)))
    space:write_u16(0x3f9048,k%3==0 and 0x4000 or 0)
    local flags=space:read_u16(0x29c200)
    space:write_u16(0x29c200,k%3==1 and (flags|0x4000) or (flags&~0x4000))
   end
   if os.getenv("AB2_ORACLE_IRQ4_SCENERY_ENDPOINTS")=="1" then
    local k=calls//4
    space:write_u16(0x29c09c,5);space:write_u8(0xcdd05,0)
    space:write_u16(0xcdd02,0);space:write_u8(0xcdd07,0)
    space:write_u16(0x3f9044,k%23==0 and 7 or 0)
    space:write_u16(0x3f9040,(k//2)%16)
    space:write_u8(0x3f9035,0xff)
    if k%2==1 then
     -- Already initialized event: timed scroll modes/underflow and ROM
     -- palette-script loops. Source pointers remain ROM-selected.
     space:write_u16(0x3f9040,0x100)
     space:write_u8(0x3f9000,({0,4,8,12,16,20,24,28})[(k//2)%8+1])
     space:write_u16(0x3f9002,k%3==0 and 0 or 1)
     space:write_u16(0x3f9004,k%3==0 and 0 or 1)
     space:write_u16(0x3f9030,0);space:write_u8(0x3f9034,k%3==0 and 0 or 1)
     space:write_u8(0x3f9001,k%3==0 and 0 or 1)
     local record=({0,1,3,4})[(k//16)%4+1]
     space:write_u32(0x3f9020,space:read_u32(0x220004+record*16))
     space:write_u16(0x3f9032,0)
    end
   end
   space:write_u8(0x3f8b6f,calls%8==3 and (space:read_u8(0x3f8b6f)&~2) or (space:read_u8(0x3f8b6f)|2))
   space:write_u16(0x3f8174,59);space:write_u16(0x3f881c,59);space:write_u16(0x3f880a,1)
   guard=false
  end
  pre=string.pack(">I8I4I4I2I4I2",last.t+320,last.pc,main.state.PC.value,(last.sr&0xffe0)|(main.state.SR.value&31),main.state.SP.value,last.count)..regs()..snapshot()
  active=true;events={};hooks={};subevents={};operand_frontier=0;hook=nil;service_return=main.state.PC.value
 end
 if active then
  local target=targets[p]
  if not w and target and a==target[1]+2 then
   assert(not hook,"nested root callback")
   hook={entry=target[1],ret=p+target[2],start=ns()+320,pre=regs(),events={}}
  end
  -- Data/stack and vector transactions are included. ROM instruction
  -- fetches are represented by clocks. External callees remain transcripts.
  -- Include road/cycle palette ROM operands. These MOVE instructions have no
  -- extensions; their prefetch is at p+4. Data ranges in these fixtures
  -- are disjoint from the instruction stream (also asserted by the test).
  local palette_read=(p==0x13bc4 or (p>=0x13bf0 and p<=0x13bf6) or p==0x13c12 or (p>=0x13c28 and p<=0x13c36) or (p>=0x132da and p<=0x132e6) or (p>=0x13310 and p<=0x1331c)) and a~=p+4
  local length=operand_lengths[p]
  local callee_read=hook and length and not(a>=p+2 and a<=p+length+2)
  if w or a>=0x80000 or a==0x70 or a==0x72 or palette_read or callee_read then
   local e=string.pack(">I8I4I4I2I2B",ns(),p,a,d,m,w and 1 or 0)
   if hook then hook.events[#hook.events+1]=e else events[#events+1]=e end
   operand_frontier=operand_frontier+1
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
   out:write(pre,string.pack(">I8I4I4I4",ns()+320,#events,#hooks,#subevents),table.concat(subevents),table.concat(events),table.concat(hooks),regs(),string.pack(">I4I2",main.state.SP.value,main.state.SR.value))
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
-- Sub progress remains supplied at captured main operand frontiers for sub-owned
-- shared/road write lanes; no main read values or callee clocks are supplied.
local sub=manager.machine.devices[":mainpcb:subcpu"]
irq4_subwrites=sub.spaces.program:install_write_tap(0x80000,0xeffff,"irq4_subwrites",function(a,d,m)
 if guard or not active then return end
 if (a>=0x80000 and a<0xc0000) or (a>=0xec000 and a<0xed000) then
  subevents[#subevents+1]=string.pack(">I4I8I4I4I2I2B",operand_frontier,ns(),sub.state.CURPC.value,a+0x200000,d,m,1)
 end
end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
irq4_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then
  if active then dropped=dropped+1 end
  out:close();print(string.format("captured %d IRQ4 service envelopes through frame %d (%d unfinished)",calls,frame,dropped));manager.machine:exit()
 end
end)
