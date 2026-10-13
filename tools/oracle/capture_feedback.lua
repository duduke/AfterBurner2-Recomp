-- Passive haptics witnesses from the original ROM, with physical inputs only.
-- Output stays in ignored build/. No gameplay or CPU/device state writes.
local machine=manager.machine
local main=machine.devices[':mainpcb:maincpu']
local space=main.spaces.program
local out=assert(io.open(assert(os.getenv('AB2_ORACLE_OUT')),'wb'))
out:write('AB2HAP01')
local frame=0
local limit=tonumber(os.getenv('AB2_ORACLE_PASSES') or '4200')
dofile(assert(os.getenv('AB2_ORACLE_SCRIPT_DIR'))..'/gameplay.lua')
local ports=machine.ioport.ports
feedback_inputs=emu.add_machine_frame_notifier(function()
 if frame>=480 then ports[':mainpcb:ADC2'].fields['AD Stick Z']:set_value(255) end
 if frame>=480 then
  ports[':mainpcb:ADC0'].fields['AD Stick X']:set_value(128)
  ports[':mainpcb:ADC1'].fields['AD Stick Y']:set_value(128)
 end
end)
local function ns()return math.floor(machine.time:as_double()*1e9+0.5)end
feedback_writes=space:install_write_tap(0x3fa000,0x3fdfff,'feedback_writes',function(a,data,mask)
 if (a&63)~=18 then return end
 -- Record all handler low-word writes, even inactive/attract/unrelated ones.
 out:write(string.pack('>BI8I4I4I2I2I2BI2',1,ns(),main.state.CURPC.value,a,data,mask,
   space:read_u16(0x29c09c),space:read_u8(a-18),space:read_u16(a-2)))
end)
feedback_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 out:write(string.pack('>BI4I8I2I2',2,frame,ns(),space:read_u16(0x29c09c),space:read_u16(0x29c0d2)))
 if frame>=limit then
  feedback_writes:remove();out:close();print(string.format('captured %d haptics witness frames',frame));machine:exit()
 end
end)
