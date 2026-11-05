-- Passive haptics witnesses from the original ROM, with physical inputs only.
-- Output stays in ignored build/. No gameplay or CPU/device state writes.
local machine=manager.machine
local main=machine.devices[':mainpcb:maincpu']
local space=main.spaces.program
local out=assert(io.open(assert(os.getenv('AB2_ORACLE_OUT')),'wb'))
local details=os.getenv('AB2_HAPTICS_DETAILS')=='1'
out:write(details and 'AB2HAP02' or 'AB2HAP01')
local damage_record=0
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
 if details and space:read_u16(0x29c09c)==16 and main.state.CURPC.value==0x9fd2 and (a&63)==42 and mask==0xffff then
  out:write(string.pack('>BI2I2',3,data,space:read_u16(0xffff8d14)))
 end
 if (a&63)~=18 then return end
 if data==0xe662 and space:read_u16(a-2)==0 then damage_record=a-18 end
 -- Record all handler low-word writes, even inactive/attract/unrelated ones.
 out:write(string.pack('>BI8I4I4I2I2I2BI2',1,ns(),main.state.CURPC.value,a,data,mask,
   space:read_u16(0x29c09c),space:read_u8(a-18),space:read_u16(a-2)))
 if details then out:write(string.pack('>I2',space:read_u16(0x29c0b8))) end
end)
feedback_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 out:write(string.pack('>BI4I8I2I2',2,frame,ns(),space:read_u16(0x29c09c),space:read_u16(0x29c0d2)))
 if details then out:write(string.pack('>I4I2',damage_record~=0 and space:read_u32(damage_record+16) or 0,space:read_u16(0xffff9124))) end
 if frame>=limit then
  feedback_writes:remove();out:close();print(string.format('captured %d haptics witness frames',frame));machine:exit()
 end
end)
