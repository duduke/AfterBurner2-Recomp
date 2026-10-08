-- Natural main RESET pulses, observed before sub reset microcode effects.
-- Read-only snapshots and bus taps; no CPU setters or forced reset.
local machine=manager.machine
local sub=machine.devices[':mainpcb:subcpu']
local reset=emu.item(assert(sub.items['0/ab2_reset_snapshot']))
local instruction=emu.item(assert(sub.items['0/ab2_cpu_snapshot']))
local out=assert(io.open(os.getenv('AB2_ORACLE_OUT'),'wb'));out:write('AB2RST01')
local active=nil;local serial=reset:read(0);local frames,count=0,0
local function snapshot(item)
 local v={};for i=0,21 do v[i]=item:read(i)end
 local r={string.pack('>I8I4I2',v[2],v[3],v[4])}
 for i=5,21 do r[#r+1]=string.pack('>I4',v[i]&0xffffffff)end
 return table.concat(r)
end
subreset_tap=sub.spaces.program:install_read_tap(0,0xfffff,'subreset',function(a,d,m)
 local nextserial=reset:read(0)
 if nextserial~=serial then
  assert(not active,'RESET interrupted unfinished reset fixture')
  serial=nextserial;assert(a==0,'first RESET phase is not vector zero')
  active={pre=snapshot(reset),events={}}
 end
 if not active then return end
 if #active.events==6 then
  assert(instruction:read(3)==0x402,'reset did not enter sub startup')
  out:write(active.pre,snapshot(instruction),string.pack('>I4',#active.events),table.concat(active.events))
  active=nil;count=count+1;return
 end
 active.events[#active.events+1]=string.pack('>I8I4I2I2I4',math.floor(machine.time:as_double()*1e9+0.5),a,d,m,sub.state.CURPC.value)
end)
dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')
subreset_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'600')then
  assert(not active and count>=2,'natural RESET assertion/release not captured')
  out:close();print(string.format('captured %d natural sub RESET sequences through frame %d',count,frames));machine:exit()
 end
end)
