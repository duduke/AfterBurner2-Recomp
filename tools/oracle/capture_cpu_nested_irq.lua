-- Explicit pin stimulus in the isolated probe: SR write admits IRQ2;
-- IRQ6 is asserted during its entry. Handler code remains original ROM.
local machine=manager.machine;local cpu=machine.devices[':mainpcb:maincpu'];local space=cpu.spaces.program
local irq=emu.item(assert(cpu.items['0/ab2_irq_snapshot']));local probe=emu.item(assert(cpu.items['0/ab2_cpu_snapshot']))
local out=assert(io.open(os.getenv('AB2_ORACLE_OUT'),'wb'));out:write('AB2NEST1')
local guard,active,serial,ready,done=false,nil,irq:read(0),false,false
local completed=0
local function ns()return math.floor(machine.time:as_double()*1e9+0.5)end
local function snapshot(item,n)local v={};for i=0,n do v[i]=item:read(i)end;return v end
local function packed(v)
 local r={string.pack('>I8I4I2',v[2],v[3],v[4])};for i=5,21 do r[#r+1]=string.pack('>I4',v[i]&0xffffffff)end;return table.concat(r)
end
local function finish(post)
 out:write(string.pack('>BI4I8',active.pre[22],active.pre[23],active.pre[2]//80-active.pre[1]),packed(active.pre),active.ram,string.pack('>I4',#active.events),table.concat(active.events),packed(post))
 completed=completed+1;active=nil
end
local function event(w,a,d,m,cpu_space)
 if guard or not ready or done then return end
 if irq:read(0)~=serial then
  local pre=snapshot(irq,23);serial=pre[0]
  if active then finish(pre)end
  guard=true;local ram=space:read_range(0x3f8000,0x3fffff,8);guard=false
  active={pre=pre,ram=ram,events={},serial=probe:read(0)}
 elseif active and probe:read(0)~=active.serial then
  finish(snapshot(probe,21));assert(completed==2,'nested fixture must capture both entries');done=true;out:close();print(string.format('captured %d retained nested main IRQ entries',completed));machine:exit();return
 end
 if active then active.events[#active.events+1]=string.pack('>I8BBI4I2I2',ns(),w and 1 or 0,cpu_space and 1 or 0,a,d,m)end
end
nested_irq_taps={space:install_read_tap(0,0x3fffff,'nested_read',function(a,d,m)event(false,a,d,m,false)end),space:install_write_tap(0,0x3fffff,'nested_write',function(a,d,m)event(true,a,d,m,false)end),cpu.spaces.cpu_space:install_read_tap(0xfffff0,0xffffff,'nested_ack',function(a,d,m)event(false,a,d,m,true)end)}
local frames=0
nested_irq_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1;if frames==40 then
  guard=true;cpu.state.SR.value=0x2700;cpu.state.SP.value=0x3ffe00;cpu.state.USP.value=0x3ffc00
  space:write_u16(0x3ffd00,0x46fc);space:write_u16(0x3ffd02,0);space:write_u16(0x3ffd04,0x60fe)
  serial=irq:read(0);cpu.state.PC.value=0x3ffd00;ready=true;guard=false
 end
end)
