-- Explicit synthetic PCM region and directed board-register writes. No arcade
-- data in this file. MAME supplies every raw and mixed/resampled sample.
local region=assert(manager.machine.memory.regions[':mainpcb:pcm'])
for a=0,0x7ffff do region:write_u8(a,((a//32)%2==0) and 255 or 0)end
local space=manager.machine.devices[':mainpcb:soundcpu'].spaces.program
local frames=0
speaker_edges_frames=emu.add_machine_frame_notifier(function()
 frames=frames+1
 if frames>=32 then
  local phase=((frames-32)//8)%3
  for ch=0,15 do
   local a=0xf000+ch*8
   space:write_u8(a+0x86,1)
   space:write_u8(a+2,phase==1 and 0 or 127)
   space:write_u8(a+3,phase==0 and 0 or 127)
   space:write_u8(a+4,0);space:write_u8(a+5,0);space:write_u8(a+6,127)
   space:write_u8(a+7,223);space:write_u8(a+0x84,0);space:write_u8(a+0x85,0)
   space:write_u8(a+0x86,0)
  end
 end
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'64')then
  print(string.format('captured directed speaker edges through frame %d',frames));manager.machine:exit()
 end
end)
