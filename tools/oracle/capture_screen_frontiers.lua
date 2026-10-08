-- Read-only screen-update trace comes from the isolated scheduler probe.
-- Fresh boot/gameplay inputs match the joint capture; no CPU or RAM setters.
assert(os.getenv('AB2_SCREEN_TRACE'),'screen observer required')
if os.getenv('AB2_ORACLE_GAMEPLAY')=='1' then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
local frames=0
screen_frontiers=emu.add_machine_frame_notifier(function()
 frames=frames+1
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'600')then
  print(string.format('captured screen frontiers through frame %d',frames));manager.machine:exit()
 end
end)
