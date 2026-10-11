-- Independent original-ROM instruction fixtures, not replacement joint state.
-- Real MOVE-to-SR establishes IPL7 before each JMP; all captures stay local.
local machine=manager.machine
local cpus={machine.devices[':mainpcb:maincpu'],machine.devices[':mainpcb:subcpu']}
local out=assert(io.open(os.getenv('AB2_ORACLE_OUT'),'wb'));out:write('AB2COV02')
local probes={};for i=1,2 do probes[i]=emu.item(assert(cpus[i].items['0/ab2_cpu_snapshot']))end
local cases={}
for _,pc in ipairs({0xb836,0xb83a,0xb83e,0x146d8,0x14702})do
 for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17}end
end
-- Original ROM DIVU sites: both helpers and all three HUD divisions.
for _,pc in ipairs({0xd090,0x1767e,0x17712,0x17fce,0x17fde,0x17fee})do
 for _,operand in ipairs({{0,0},{0x10000,1},{0xffffffff,0xffff},{0x1234,17}})do
  cases[#cases+1]={cpu=1,pc=pc,sr=0x271f,d0=operand[1],d1=operand[2],d2=operand[1]}
 end
end
for _,pc in ipairs({0x40c,0x63e})do
 for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=2,pc=pc,sr=0x0700|ccr,d0=0x12345678,d1=17}end
end
for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=0x14842,sr=0x0700|ccr,d0=0,d1=1}end
for _,pc in ipairs({0xd090,0x1767e})do
 for _,d in ipairs({0x7fff0000,0x80000000,0xffff0000})do
  for _,sr in ipairs({0x0710,0x2700})do cases[#cases+1]={cpu=1,pc=pc,sr=sr,d0=d,d1=0}end
 end
end
if os.getenv('AB2_CPU_COVERAGE_PLAN')then cases=dofile(os.getenv('AB2_CPU_COVERAGE_PLAN'))end
out:write(string.pack('>I4',#cases))
for _,c in ipairs(cases)do out:write(string.pack('>BI4',c.cpu,c.pc))end
local current,guard,index,complete=nil,false,0,0
local finished=false
local function ns()return math.floor(machine.time:as_double()*1e9+0.5)end
local function snapshot(i)
 local v={};for n=0,21 do v[n]=probes[i]:read(n)end;return v
end
local function packed(v)
 local data={string.pack('>I8I4I2',v[2],v[3],v[4])}
 for n=5,21 do data[#data+1]=string.pack('>I4',v[n]&0xffffffff)end
 return table.concat(data)
end
local function memory()
 local space=cpus[1].spaces.program
 local data={space:read_range(0x3f8000,0x3fffff,8),cpus[2].spaces.program:read_range(0x9c000,0xa3fff,8)}
 for _,range in ipairs({{0xc0000,0xcffff},{0xd0000,0xd0fff},{0x100000,0x100fff},{0x120000,0x123fff},{0x2ec000,0x2ecfff}})do data[#data+1]=space:read_range(range[1],range[2],8)end
 return table.concat(data)
end
local function event(i,w,a,d,m)
 if guard or not current or current.case.cpu~=i or current.done then return end
 local v=snapshot(i)
 if not current.pre then
  if v[3]~=current.case.pc or cpus[i].state.CURPC.value~=current.case.pc then return end
  guard=true;current.ram=memory();guard=false
  current.pre=v;current.events={};current.serial=v[0]
 elseif v[0]~=current.serial then
  current.post=v;current.done=true;complete=complete+1
  out:write(string.pack('>BI4',i,current.case.pc),packed(current.pre),current.ram,string.pack('>I4',#current.events),table.concat(current.events),packed(current.post))
  return
 end
 current.events[#current.events+1]=string.pack('>I8BI4I2I2',ns(),w and 1 or 0,a,d,m)
end
cpu_coverage_taps={}
for i=1,2 do
 local space=cpus[i].spaces.program
 cpu_coverage_taps[#cpu_coverage_taps+1]=space:install_read_tap(0,i==1 and 0x3fffff or 0xfffff,'coverage_read',function(a,d,m)event(i,false,a,d,m)end)
 cpu_coverage_taps[#cpu_coverage_taps+1]=space:install_write_tap(0,i==1 and 0x3fffff or 0xfffff,'coverage_write',function(a,d,m)event(i,true,a,d,m)end)
end
local frames=0
local function launch()
 if finished then return false end
 if current then assert(current.done,string.format('fixture %d PC %x did not complete',index,current.case.pc))end
 if index==#cases then out:close();print(string.format('captured %d original-ROM CPU instruction fixtures',complete));finished=true;machine:exit();return false end
 index=index+1;local case=cases[index];local cpu=cpus[case.cpu];local space=cpu.spaces.program
 local base=case.cpu==1 and 0x3ffd00 or 0xa3d00;local ssp=base-0x100;local usp=base-0x200
 guard=true
 cpu.state.SR.value=0x2700;cpu.state.SP.value=ssp;cpu.state.USP.value=usp
 for n=0,7 do cpu.state['D'..n].value=n==case.counter_reg and case.counter_value or 0 end
 if case.counter_reg==nil then cpu.state.D0.value=case.d0;cpu.state.D1.value=case.d1;cpu.state.D2.value=case.d2 or 0 end
 for offset=0,0xfe,2 do space:write_u16(case.cpu==1 and 0x3ffc00+offset or 0x9c000+0x300+offset,0)end
 for n=0,6 do cpu.state['A'..n].value=case.cpu==1 and 0x3ffc00 or 0x9c000 end
 for _,sp in ipairs({ssp,usp})do space:write_u32(sp,base+0x80)end
 -- A supervisor RTE consumes an explicit valid exception frame at IPL7.
 -- User RTE cases still fault before reading it.
 if space:read_u16(case.pc)==0x4e73 then space:write_u16(ssp,0x2700);space:write_u32(ssp+2,base+0x80)end
 -- Operand fixtures must win over stack initialization when an instruction
 -- addresses the stack's backing bytes (for example TAS/ANDI at (A5)).
 if case.memory_word_address then space:write_u16(case.memory_word_address,case.memory_word_value)end
 space:write_u16(base,0x46fc);space:write_u16(base+2,case.sr)
 space:write_u16(base+4,0x4ef9);space:write_u32(base+6,case.pc)
 space:write_u16(base+0x80,0x60fe)
 current={case=case};cpu.state.PC.value=base;guard=false;return true
end
cpu_coverage_frame=emu.add_machine_frame_notifier(function()
 frames=frames+1;if frames<40 then return end
 local co=coroutine.create(function()
  for i=1,16 do if not launch()then return end;emu.wait(0.00005)end
 end);assert(coroutine.resume(co))
end)
