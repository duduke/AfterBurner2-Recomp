-- PCM device observer runs in the separate probe binary. Natural gameplay
-- uses only physical inputs. Optional directed register programming is a
-- declared device fixture; all resulting stream samples come from MAME.
local frame=0
if os.getenv('AB2_PCM_SYNTHETIC')=='1' then
 local region=assert(manager.machine.memory.regions[':mainpcb:pcm'])
 assert(region.size==0x80000,'unexpected PCM ROM region')
 -- Explicit synthetic ROM fixture, not an arcade-ROM acceptance claim.
 -- Address-sensitive bytes distinguish all eight banks, including erased
 -- banks 6/7 that necessarily look identical in the commercial ROM set.
 for a=0,0x7ffff do region:write_u8(a,((a*37)~(a>>8)~((a>>16)*53))&255)end
end
if os.getenv('AB2_ORACLE_GAMEPLAY')=='1' then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
local space=manager.machine.devices[':mainpcb:soundcpu'].spaces.program
local directed=os.getenv('AB2_PCM_DIRECTED')=='1'
pcm_capture_frames=emu.add_machine_frame_notifier(function()
 frame=frame+1
 if directed and frame>=500 and frame<564 then
  -- Stop channels, then program panning, bank, fraction carry, loop/end
  -- wrap and stop cases. The real CPUs keep running; observer records their
  -- interleaved writes too, so no replacement device snapshots are needed.
  for ch=0,15 do space:write_u8(0xf086+8*ch,1)end
  local phase=(frame-500)%8
  for ch=0,15 do
   local a=0xf000+8*ch
   space:write_u8(a+2,phase==7 and 0xff or (17*ch+phase)&0x7f)
   space:write_u8(a+3,phase==7 and 0xff or (127-7*ch-phase)&0x7f)
   space:write_u8(a+4,phase==2 and 0xfe or 0)
   space:write_u8(a+5,phase==2 and 0xff or 0)
   space:write_u8(a+6,phase<4 and 0xff or 0)
   space:write_u8(a+7,phase==6 and 0 or 0xff)
   space:write_u8(a+0x84,phase<4 and 0xff or 0)
   space:write_u8(a+0x85,phase<4 and 0xff or (phase==4 and 1 or 0))
   space:write_u8(a+0x86,((ch%8)<<4)|((phase==1 or phase==3 or phase==4) and 2 or 0)|((phase==5 or phase==7) and 0x8c or 0))
  end
 end
 if frame>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'600') then
  print(string.format('captured PCM device stream through frame %d (directed=%s)',frame,tostring(directed)))
  manager.machine:exit()
 end
end)
