-- ADC0804/main 315-5250 timeline, including input changes at frame boundaries.
-- State is captured before each bus transaction, not after tap delivery.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local adc=assert(manager.machine.devices[":mainpcb:adc"])
local board=manager.machine.devices[":mainpcb"]
local irq=emu.item(assert(board.items["0/m_timer_irq_state"]))
local mux=emu.item(assert(board.items["0/m_pc_0"]))
local ports=manager.machine.ioport.ports
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write("AB2ADT01")
local first_rise=manager.machine.time:as_double()+manager.machine.screens[":mainpcb:screen"]:time_until_pos(1)
out:write(string.pack(">I8",math.floor(first_rise*1e9+0.5)))
local records,frame=0,0
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
adc_timing_timer=space:install_write_tap(0xe8000,0xebfff,"adc_timing_timer",function(a,d,m) record(4,a,d,m) end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
adc_timing_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1;record(5,0,0,0)
 if frame>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "900") then
  out:close();print(string.format("captured %d ADC/timer timeline events through frame %d",records,frame));manager.machine:exit()
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
