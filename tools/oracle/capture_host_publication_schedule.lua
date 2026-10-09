-- Execution grants with external device timer/CPU completion boundaries.
-- Global basetime and chronological main/sub bus order are assertions only.
local machine=manager.machine
local cpus={machine.devices[':mainpcb:maincpu'],machine.devices[':mainpcb:subcpu'],machine.devices[':mainpcb:soundcpu']}
-- The source probe registers one read-only-to-Lua grant counter directly
-- before global basetime in save_manager's lexicographic item order.
assert(os.getenv('AB2_SCHED_TRACE'),'this capture requires the scheduler probe')
local before=0
for _,index in pairs(machine.devices[':mainpcb:gfxdecode'].items)do before=math.max(before,index)end
local grant,basea,bases=emu.item(before+1),emu.item(before+2),emu.item(before+3)
assert(grant.size==8 and basea.size==8 and bases.size==4,'missing probe scheduler metadata')
local function base()return bases:read(0)*1000000000000000000+basea:read(0)end
local cold=os.getenv('AB2_ORACLE_START_FRAME')=='0'
local mainprogress=os.getenv('AB2_JOINT_MAIN_PROGRESS')=='1'
local out=assert(io.open(os.getenv('AB2_ORACLE_OUT'),'wb'));out:write(cold and 'AB2ARB10' or mainprogress and 'AB2ARB09' or 'AB2ARB07')
local inputs=mainprogress and assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.inputs','wb')) or nil
if inputs then
 inputs:write(cold and 'AB2PIN03' or 'AB2PIN02')
 if cold then for _,port in ipairs({'IO1PORTB','IO1PORTC','IO1PORTD'})do inputs:write(string.char(machine.ioport.ports[':mainpcb:'..port]:read()))end end
end
local irqboot=mainprogress and assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.irqboot','wb')) or nil
if irqboot then irqboot:write('AB2IRQ02')end
local banks=mainprogress and assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.video','wb')) or nil
if banks then banks:write('AB2VBN03')end
local hidden=banks and emu.item(assert(machine.devices[':mainpcb:sprites'].items['0/m_buffer'])) or nil
local hidden_road=banks and emu.item(assert(machine.devices[':mainpcb:segaic16road'].items['0/ab2_road_buffer'])) or nil
local function hidden_sprites()
 local parts={};for i=0,2047 do parts[#parts+1]=string.pack('>I2',hidden:read(i))end;return table.concat(parts)
end
local function road_bank()
 local parts={};for i=0,2047 do parts[#parts+1]=string.pack('>I2',hidden_road:read(i))end;return table.concat(parts)
end
local sprite_display,road_display=string.rep('\0',4096),string.rep('\0',4096)
local road_control=0
local sound=mainprogress and assert(io.open(os.getenv('AB2_ORACLE_OUT')..'.sound','wb')) or nil
if sound then sound:write('AB2SND01')end
if os.getenv('AB2_JOINT_SOUND_EXECUTION')=='1' then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/capture_sound_execution.lua')end
local guard=false
local frames,groups,phases=0,0,0
local active=nil;local ready=false;local finish=false;local finish_user_serial=nil;local stopping=false
local function close()
 if not active then return end
 out:write(string.pack('>I8I8I4',active.grant,active.base,#active.events),table.concat(active.events));groups=groups+1
 active=nil
end
-- The isolated MAME probe saves executing-device state before instruction
-- effects. No debugger symbols, focus changes, or CPU state setters are used.
local meta=dofile(assert(os.getenv('AB2_ORACLE_SCRIPT_DIR'))..'/sub_schedule.lua')
local mainpcs={0x1458a,0x14590,0x14594,0x1459c,0x145a0,0x145a4,
 0x145a8,0x145ac,0x145b0,0x145b2,0x145b6,0x145b8,
 0x14856,0x1485c,0x1485e,0x14866,0x1486a,0x1486e,0x14876,0x14878}
local subpcs={0x420,0x428,0x432,0x436,0x43a,0x43e,0x442,0x446,
 0x44a,0x44e,0x452,0x456,0x458,0x556,0x55a,0x55c,0x640,0x646,
 0x55e,0x562,0x568,0x56e,0x570,0x574,0x578,0x57c,0x580,0x586,0x58a,0x58e,0x592}
local bounded={{},{}}
for _,p in ipairs(mainpcs)do bounded[1][p]=true end
if mainprogress then
 for _,p in ipairs(dofile(assert(os.getenv('AB2_ORACLE_SCRIPT_DIR'))..'/joint_main_schedule.lua'))do bounded[1][p]=true end
end
for _,p in ipairs(subpcs)do bounded[2][p]=true end
local startup={};for _,p in ipairs({0x402,0x408,0x40c,0x40e,0x414,0x418,0x41a,0x41c,0x42a,0x42e,0x62e,0x632,0x634,0x636,0x63a,0x63c,0x63e})do startup[p]=true;bounded[2][p]=true end
for p in pairs(meta)do bounded[2][p]=true end
local irqpcs={};for _,p in ipairs({0x55e,0x562,0x568,0x56e,0x570,0x574,0x578,0x57c,0x580,0x586,0x58a,0x58e,0x592})do irqpcs[p]=true end
local probes={};local resetprobe=emu.item(assert(cpus[2].items['0/ab2_reset_snapshot']))
local resetserial=resetprobe:read(0)
local mainirq=mainprogress and emu.item(assert(cpus[1].items['0/ab2_irq_snapshot'])) or nil
local mainirqserial=mainirq and mainirq:read(0) or 0
local mainreset=cold and emu.item(assert(cpus[1].items['0/ab2_reset_snapshot'])) or nil
local mainresetserial=mainreset and mainreset:read(0) or 0
local mainentry=nil
for i=1,2 do local index=cpus[i].items['0/ab2_cpu_snapshot'];assert(index,'missing CPU instruction probe');probes[i]=emu.item(index)end
local last_serial={0,0};local in_entry=false;local initial_irq=false
local function packed(kind,t,p,sr,sp,d,a,usp,ssp,bias)
 local r={};for i=1,8 do r[#r+1]=string.pack('>I4',d[i]&0xffffffff)end
 for i=1,7 do r[#r+1]=string.pack('>I4',a[i]&0xffffffff)end
 return string.char(kind)..string.pack('>I8I4I2I4',t,p,sr,sp)..table.concat(r)..string.pack('>I4I4I8',usp,ssp,bias)
end
local function instruction(cpu,p,w,a,t)
 local state=cpus[cpu].state
 if cpu==1 and mainreset and mainreset:read(0)~=mainresetserial then
  mainresetserial=mainreset:read(0)
  assert(not w and a==0,'missing main reset vector')
  local v={};for i=0,21 do v[i]=mainreset:read(i)end
  local d,r={},{};for i=0,7 do d[i+1]=v[5+i]end;for i=0,6 do r[i+1]=v[13+i]end
  return packed(7,v[2],v[3],v[4],v[21],d,r,v[20],v[21],v[2]//80-v[1])
 end
 if cpu==1 and mainirq then
  if mainirq:read(0)~=mainirqserial then
   mainirqserial=mainirq:read(0)
   local v={};for i=0,23 do v[i]=mainirq:read(i)end
   assert(w and a==((v[21]-2)&0x3ffffe) and p==v[23],'missing main IRQ first stack phase')
   local level=v[22];assert(level==2 or level==4 or level==6,'unsupported main IRQ level')
   mainentry=level==2 and 0x1461e or level==4 and 0x14676 or 0x1466e
   local d,r={},{};for i=0,7 do d[i+1]=v[5+i]end;for i=0,6 do r[i+1]=v[13+i]end
   local sp=v[4]&0x2000~=0 and v[21] or v[20]
   return packed(3+level//2,v[2],v[3],v[4],sp,d,r,v[20],v[21],v[2]//80-v[1])
  end
  if mainentry then
   if not w and a==mainentry then mainentry=nil end
   return string.char(0)
  end
 end
 if cpu==2 and not w and a==0 and resetprobe:read(0)~=resetserial then
  resetserial=resetprobe:read(0);in_entry=false;initial_irq=false
  local v={};for i=0,21 do v[i]=resetprobe:read(i)end
  local d,r={},{};for i=0,7 do d[i+1]=v[5+i]end;for i=0,6 do r[i+1]=v[13+i]end
  local usp,ssp=v[20],v[21];local sp=v[4]&0x2000~=0 and ssp or usp
  return packed(3,v[2],v[3],v[4],sp,d,r,usp,ssp,v[2]//80-v[1])
 end
 -- A capture boundary may split the ISR already running at frame 32. Keep
 -- all its bus events, but do not claim an entry that was not captured.
 if cpu==2 and initial_irq then
  if state.SR.value&0x2000~=0 or irqpcs[p]then return string.char(0)end
  initial_irq=false
 end
 if cpu==2 and in_entry then
  if not w and a==0x560 then in_entry=false end
  return string.char(0)
 end
 -- After RESET, startup PEA/TRAP stack writes also run in supervisor mode.
 -- A VBLANK entry must have an eligible pre-interrupt mask; startup keeps 7.
 local entry=cpu==2 and w and state.SR.value&0x2000~=0 and ((probes[cpu]:read(4)>>8)&7)<4 and not irqpcs[p] and a==((state.SP.value-2)&0xffffe)
 if not cold and not entry and (not bounded[cpu][p] or (cpu==2 and state.SR.value&0x2000~=0 and not irqpcs[p] and not startup[p]))then return string.char(0)end
 local serial=probes[cpu]:read(0)
 if not entry and last_serial[cpu]==serial then return string.char(0)end
 local v={};for i=0,21 do v[i]=probes[cpu]:read(i)end
 if entry then
  in_entry=true
  local d,r={},{};for i=0,7 do d[i+1]=state['D'..i].value end
  for i=0,6 do r[i+1]=state['A'..i].value end
  local sr=(v[4]&0xffe0)|(state.SR.value&0x1f)
  return packed(2,t-480,state.PC.value,sr,state.USP.value,d,r,state.USP.value,state.SP.value,v[2]//80-v[1])
 end
 if last_serial[cpu]==v[0]then return string.char(0)end
 assert(v[3]==p,'probe PC does not match executing instruction')
 last_serial[cpu]=v[0]
 local d,r={},{};for i=0,7 do d[i+1]=v[5+i]end
 for i=0,6 do r[i+1]=v[13+i]end
 local usp,ssp=v[20],v[21];local sp=v[4]&0x2000~=0 and ssp or usp
 return packed(1,v[2],p,v[4],sp,d,r,usp,ssp,v[2]//80-v[1])
end
local function event(cpu,w,a,d,m,space)
 if guard then return end
 local p=cpus[cpu].state.CURPC.value
 local bankkind=0
 if banks then
  if cpu==1 and w and a>=0x110000 and a<0x120000 then
   guard=true;sprite_display=cpus[1].spaces.program:read_range(0x100000,0x100fff,8);guard=false;bankkind=1
  elseif cpu==2 and not w and a>=0xee000 and a<0xf0000 then road_display=road_bank();bankkind=2
  elseif cpu==2 and w and a>=0xee000 and a<0xf0000 and m&255~=0 then road_control=d&7;bankkind=3 end
 end
 if not ready then
  if irqboot and cpu==1 and ((a>=0xe8000 and a<0xe8020 and (w or ((a//2)&15)==9 or ((a//2)&15)==13)) or (w and ((a>=0x140000 and a<0x140010) or a==0x130000)))then
   irqboot:write(string.pack('>I8I8BI4I2I2',base()//1000000000,math.floor(machine.time:as_double()*1e9+0.5),w and 1 or 0,a,d,m))
  end
  return
 end
 local b=base();local g=grant:read(0)
 if active and g~=active.grant then close()end
 -- Stop only between complete grant groups, after a resumed user
 -- instruction has completed. This includes the final RTE's private debt.
 if finish and not active and cpus[2].state.SR.value&0x2000==0 and not irqpcs[cpus[2].state.CURPC.value]then
  local serial=probes[2]:read(0)
  if not finish_user_serial then finish_user_serial=serial end
  if serial>finish_user_serial then
   if cold then
    stopping=true
   else
    out:close();if inputs then inputs:close()end;if banks then banks:close();banks=nil end;if sound then sound:close();sound=nil end;print(string.format('captured %d CPU grants and %d main/sub phases through frame %d',groups,phases,frames));machine:exit();ready=false;return
   end
  end
 end
 if not active then active={grant=g,base=b,events={}}end
 local ns=math.floor(machine.time:as_double()*1e9+0.5)
 if banks and bankkind~=0 then
  banks:write(string.pack('>BI8I4B',bankkind,ns,p,road_control))
  if bankkind==1 then banks:write(sprite_display)elseif bankkind==2 then banks:write(road_display)end
 end
 active.events[#active.events+1]=string.pack('>BI8BI4I2I2I4B',cpu,ns,w and 1 or 0,a,d,m,p,space or 0)..(space==1 and string.char(0)or instruction(cpu,p,w,a,ns));phases=phases+1
end
arbiter_taps={}
for i=1,2 do local cpu=i;local space=cpus[i].spaces.program
 arbiter_taps[#arbiter_taps+1]=space:install_read_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_read'..i,function(a,d,m)event(cpu,false,a,d,m)end)
 arbiter_taps[#arbiter_taps+1]=space:install_write_tap(0,i==1 and 0x3fffff or 0xfffff,'arbiter_write'..i,function(a,d,m)event(cpu,true,a,d,m)end)
end
if sound then
 local cpu=machine.devices[':mainpcb:soundcpu']
 local function transaction(kind,a,d)
  if not ready or not sound then return end
  sound:write(string.pack('>I8BI8I2I2B',grant:read(0),kind,math.floor(machine.time:as_double()*1e9+0.5),cpu.state.CURPC.value,a,d&255))
 end
 sound_taps={}
 for _,w in ipairs({false,true})do
  local method=w and 'install_write_tap' or 'install_read_tap'
  sound_taps[#sound_taps+1]=cpu.spaces.io[method](cpu.spaces.io,0,0xff,'joint_sound_io'..tostring(w),function(a,d,m)
   local port=a&255;if port<0x40 then transaction(w and 3 or 2,a,d)elseif not w and port<0x80 then transaction(1,a,d)end
  end)
  sound_taps[#sound_taps+1]=cpu.spaces.program[method](cpu.spaces.program,0xf000,0xf7ff,'joint_sound_pcm'..tostring(w),function(a,d,m)transaction(w and 5 or 4,a,d)end)
 end
end
-- Actual autovector bus reads distinguish the VPA wait from public overrun.
local acknowledge=assert(cpus[2].spaces.cpu_space,'missing sub CPU-space bus')
arbiter_taps[#arbiter_taps+1]=acknowledge:install_read_tap(0,0xffffff,'sub_acknowledge',function(a,d,m)event(2,false,a,d,m,1)end)
if mainprogress then
 local mainack=assert(cpus[1].spaces.cpu_space,'missing main CPU-space bus')
 arbiter_taps[#arbiter_taps+1]=mainack:install_read_tap(0,0xffffff,'main_acknowledge',function(a,d,m)event(1,false,a,d,m,1)end)
end
if os.getenv('AB2_ORACLE_GAMEPLAY')=='1'then dofile(os.getenv('AB2_ORACLE_SCRIPT_DIR')..'/gameplay.lua')end
local function initialize()
 guard=true
  local s=cpus[1].spaces.program
  out:write(s:read_range(0x3f8000,0x3fffff,8),cpus[2].spaces.program:read_range(0x9c000,0xa3fff,8))
  if mainprogress then
   for _,range in ipairs({{0xc0000,0xcffff},{0xd0000,0xd0fff},{0x100000,0x100fff},{0x120000,0x123fff},{0x2ec000,0x2ecfff}})do out:write(s:read_range(range[1],range[2],8))end
   for _,a in ipairs({0xe0000,0xe4000,0x2e0000,0x2e4000})do for i=0,7 do out:write(string.pack('>I2',s:read_u16(a+i*2)))end end
   out:write(string.char(s:read_u8(0x150001)))
  end
  if irqboot then
   irqboot:write(string.pack('>I8I8',0xffffffffffffffff,math.floor(machine.time:as_double()*1e9+0.5)));irqboot:close();irqboot=nil
  end
  guard=false
  if banks then banks:write(hidden_sprites(),road_bank(),string.char(road_control))end
  for i=1,2 do last_serial[i]=probes[i]:read(0)end
  resetserial=resetprobe:read(0)
  if mainirq then mainirqserial=mainirq:read(0)end
end
if cold then
 assert(base()==0 and grant:read(0)==0,'cold capture started after execution')
 initialize();ready=true
 inputs:write(string.pack('>I8B',0,machine.ioport.ports[':mainpcb:IO1PORTA']:read()))
 for i=0,7 do local p=machine.ioport.ports[':mainpcb:ADC'..i];inputs:write(string.char(p and p:read() or 0x10))end
end
arbiter_frame=emu.add_machine_frame_notifier(function()
 if banks and ready then banks:write(string.pack('>BI8I4B',4,math.floor(machine.time:as_double()*1e9+0.5),0,road_control),hidden_sprites())end
 if inputs then
  -- Independent physical port observations, not expected CPU read values.
  inputs:write(string.pack('>I8B',math.floor(machine.time:as_double()*1e9+0.5),machine.ioport.ports[':mainpcb:IO1PORTA']:read()))
  for i=0,7 do local p=machine.ioport.ports[':mainpcb:ADC'..i];inputs:write(string.char(p and p:read() or 0x10))end
 end
 frames=frames+1;if frames==tonumber(os.getenv('AB2_ORACLE_START_FRAME')or'32')then
  initialize()
  initial_irq=cpus[2].state.SR.value&0x2000~=0;ready=true
 end
 if frames>=tonumber(os.getenv('AB2_ORACLE_PASSES')or'120')then finish=true end
 -- Exit only from this device callback, with no executing CPU. Calling
 -- schedule_exit inside a program tap eats the current microcode clocks.
 if cold and stopping then machine:exit()end
end)

-- machine:exit is deferred until MAME finishes its current scheduler slice.
-- Cold captures keep observing through that stop, including both old final
-- continuations; the last group is closed only after all CPUs have yielded.
if cold then arbiter_stop=emu.add_machine_stop_notifier(function()
 close();out:close();if inputs then inputs:close()end
 if banks then banks:close();banks=nil end;if sound then sound:close();sound=nil end
 print(string.format('captured %d CPU grants and %d main/sub phases through frame %d, through machine stop',groups,phases,frames))
end)end
