-- ADC0804/main 315-5250 timeline, including input changes at frame boundaries.
-- State is captured before each bus transaction, not after tap delivery.
-- AB2_ORACLE_IRQ adds kind 6 (entry: 15 registers, 0x8000 work bytes,
-- two I/O direction bytes), kind 7 (return: registers, work bytes, u32
-- count and ordered offset/data/mask u32/u16/u16 writes), and kind 8
-- (interrupted-window discard). Ordinary AB2ADT01 records are unchanged.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local adc=assert(manager.machine.devices[":mainpcb:adc"])
local board=manager.machine.devices[":mainpcb"]
local irq=emu.item(assert(board.items["0/m_timer_irq_state"]))
local mux=emu.item(assert(board.items["0/m_pc_0"]))
local ports=manager.machine.ioport.ports
local integrated=os.getenv("AB2_ORACLE_IRQ")=="1"
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write(integrated and "AB2IRQ01" or "AB2ADT01")
local first_rise=manager.machine.time:as_double()+manager.machine.screens[":mainpcb:screen"]:time_until_pos(1)
out:write(string.pack(">I8",math.floor(first_rise*1e9+0.5)))
local records,frame,calls,dropped=0,0,0,0
local active,guard=false,false
local writes={}
local controls={0xff,0xff}
local function registers()
 local p={}
 for i=0,7 do p[#p+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff) end
 for i=0,6 do p[#p+1]=string.pack(">I4",main.state["A"..i].value&0xffffffff) end
 return table.concat(p)
end
local function snapshot()
 guard=true
 local bytes=space:read_range(0x80000,0x83fff,8)..space:read_range(0xa0000,0xa3fff,8)
 guard=false;return bytes
end
local function record(kind,a,data,mask)
 local now=manager.machine.time:as_double()
 local values={};for i=0,7 do
  local p=ports[":mainpcb:ADC"..i];values[#values+1]=string.char(p and p:read() or 0x10)
 end
 out:write(string.pack(">I8I4I4I2I2BBBB",math.floor(now*1e9+0.5),main.state.CURPC.value,a,data,mask,
  kind,kind==1 and data&255 or 0,0,irq:read(0)),string.char(mux:read(0)),table.concat(values))
 records=records+1
end
adc_timing_r=space:install_read_tap(0x130000,0x13ffff,"adc_timing_r",function(a,d,m) record(1,a,d,m) end)
adc_timing_w=space:install_write_tap(0x130000,0x13ffff,"adc_timing_w",function(a,d,m) record(2,a,d,m) end)
adc_timing_mux=space:install_write_tap(0x140004,0x140005,"adc_timing_mux",function(a,d,m) record(3,a,d,m) end)
adc_timing_timer=space:install_write_tap(0xe8000,0xebfff,"adc_timing_timer",function(a,d,m)
 if integrated and main.state.CURPC.value==0x1461c then
  assert(not active,"nested comparator interrupt")
  record(6,0,0,0);out:write(registers(),snapshot(),string.char(controls[1],controls[2]))
  active=true;writes={}
 end
 record(4,a,d,m)
end)
if integrated then
 adc_irq_writes=space:install_write_tap(0,0x3fffff,"adc_irq_writes",function(a,d,m)
  if guard then return end
  if (a==0x14000c or a==0x14000e) and m&255~=0 then controls[(a-0x14000c)//2+1]=d&255 end
  if not active then return end
  local o
  if a>=0x3f8000 then o=a-0x3f8000
  elseif a>=0x80000 and a<0xc0000 then o=(a<0xa0000 and 0 or 0x4000)+(a&0x3fff) end
  if o and o>=0x7e00 then return end -- IRQ/JSR stack writes
  local p=main.state.CURPC.value
  if not ((p>=0x1461c and p<=0x1466a) or (p>=0xb7f8 and p<=0xb83e)) then
   record(8,0,0,0);active=false;dropped=dropped+1;return
  end
  if o then writes[#writes+1]=string.pack(">I4I2I2",o,d,m) end
 end)
 adc_irq_end=space:install_read_tap(0,0x3fffff,"adc_irq_end",function()
  if guard or not active or main.state.CURPC.value~=0x1466a then return end
  record(7,0,0,0);out:write(registers(),snapshot(),string.pack(">I4",#writes),table.concat(writes))
  active=false;calls=calls+1
 end)
end
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
adc_timing_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1;record(5,0,0,0)
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "900") then
  if active then record(8,0,0,0);dropped=dropped+1;active=false end
  out:close();print(string.format("captured %d ADC/timer timeline events through frame %d (%d IRQ calls, %d interrupted)",records,frame,calls,dropped));manager.machine:exit()
 end
end)

-- Sub-frame probes around conversion completion, including busy writes and
-- changing mux while busy. Only the main CPU is suspended by IRQ masking;
-- these are hardware fixtures and do not claim natural comparator cadence.
if os.getenv("AB2_ORACLE_ADC_DIRECTED")=="1" then
 adc_probes=coroutine.create(function()
  emu.wait(9.1)
  local saved_mux=mux:read(0);local saved_sr=main.state.SR.value;main.state.SR.value=saved_sr|0x700
  for channel=0,5 do
   space:write_u8(0x140005,channel<<2)
   space:write_u8(0x130001,0)
   emu.wait(0.000050);space:read_u8(0x130001)
   space:write_u8(0x130001,0) -- busy write cannot postpone completion
   emu.wait(0.000010);space:read_u8(0x130001)
   space:write_u8(0x130001,0)
   emu.wait(0.000050);space:write_u8(0x140005,((channel+1)%6)<<2)
   emu.wait(0.0000091);space:read_u8(0x130001)
   emu.wait(0.0000002);space:read_u8(0x130001)
  end
  space:write_u8(0x140005,saved_mux);main.state.SR.value=saved_sr
 end)
 assert(coroutine.resume(adc_probes))
end
