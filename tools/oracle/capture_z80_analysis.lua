-- Read-only, bounded debugger trace for the offline sound-ROM tooling pilot.
-- No register setters, synthetic commands or device/state replacements.
local cpu=manager.machine.devices[':mainpcb:soundcpu']
local debugger=manager.machine.debugger
local path=assert(os.getenv('AB2_ORACLE_OUT'))
assert(not path:find('"',1,true), 'trace path contains a quote')
os.remove(path..'.json') -- Never accept completion metadata from an older run.
debugger.visible_cpu=cpu
debugger:command('trace "'..path..'",:mainpcb:soundcpu,noloop')
debugger.execution_state='run'
local frames=0
emu.register_frame_done(function()
 frames=frames+1
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES') or '30') then
  debugger:command('trace off,:mainpcb:soundcpu')
  local meta=assert(io.open(path..'.json','w'))
  meta:write(string.format('{"frames":%d,"complete":true,"sound":"none"}\n',frames))
  meta:close()
  print(string.format('captured %d frames of read-only sound instruction trace',frames))
  manager.machine:exit()
 end
end)
