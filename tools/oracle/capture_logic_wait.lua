-- Uninterrupted $14856 spin/re-arm steps. IRQ-overlapped steps are counted
-- separately, not claimed as independently scheduled caller execution.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2WAIT1")
local guard,active=false,false
local pre,events,ret
local frame,calls,interrupted,spins=0,0,0,0
local directed=os.getenv("AB2_ORACLE_WAIT_DIRECTED")=="1"
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function regs()
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff)end
 for i=0,6 do r[#r+1]=string.pack(">I4",main.state["A"..i].value&0xffffffff)end
 return table.concat(r)
end
local function stack()return (main.state.SR.value&0x2000~=0) and main.state.SP.value or main.state.USP.value end
local function access(w,a,d,m)
 if guard then return end
 local p=main.state.CURPC.value
 if not w and p==0x14856 and a==0x1485a then
  assert(not active,"unfinished wait step")
  guard=true
  spins=spins+1
  if not directed and space:read_u16(0x2a3c00)<0x8000 and spins%128~=0 then guard=false;return end
  if directed then
   space:write_u16(0x2a3c00,({0,1,0x7fff,0xffff,0xfffe,0x8000})[calls%6+1])
   space:write_u16(0x3f8ba4,calls%2==0 and 0xffff or 9)
  end
  ret=space:read_u32(stack())
  pre=string.pack(">I8I4I2I2I2I4",ns(),stack(),main.state.SR.value,space:read_u16(0x2a3c00),space:read_u16(0x3f8ba4),ret)..regs()
  guard=false;active=true;events={}
 end
 if not active then return end
 if w and a==((main.state.SP.value-2)&0x3ffffe) then active=false;interrupted=interrupted+1;return end
 if w or a>=0x80000 then events[#events+1]=string.pack(">I8I4I4I2I2B",ns(),p,a,d,m,w and 1 or 0)end
 if not w and ((p==0x1485c and a==0x14858) or (p==0x14878 and a==((ret+2)&0x3ffffe))) then
  guard=true;local values=string.pack(">I2I2",space:read_u16(0x2a3c00),space:read_u16(0x3f8ba4));guard=false
  out:write(pre,string.pack(">I8I4",ns()+320,#events),table.concat(events),regs(),string.pack(">I4I2I4",stack(),main.state.SR.value,p==0x1485c and 0x14856 or ret),values)
  calls=calls+1;active=false
 end
end
wait_r=space:install_read_tap(0,0x3fffff,"wait_r",function(a,d,m)access(false,a,d,m)end)
wait_w=space:install_write_tap(0,0x3fffff,"wait_w",function(a,d,m)access(true,a,d,m)end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
wait_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then
  out:close();print(string.format("captured %d uninterrupted logic-wait steps (%d IRQ-overlapped, %d unfinished excluded)",calls,interrupted,active and 1 or 0));manager.machine:exit()
 end
end)
