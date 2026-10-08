-- Address-bounded caller/IRQ-entry oracle, optionally linked through IRQ2/6
-- and immediate post-RTE exception selection (AB2_ORACLE_CALLER_RETURN).
-- Optional AB2_ORACLE_CALLER_IRQ4 links the complete IRQ4 service.
-- Physical input and sub/shared-RAM producer frontiers remain boundaries.
-- Includes caller prefetches; captures remain ignored.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local board=manager.machine.devices[":mainpcb"]
local timer=emu.item(assert(board.items["0/m_timer_irq_state"]))
local blank=emu.item(assert(board.items["0/m_vblank_irq_state"]))
local continuation=os.getenv("AB2_ORACLE_CALLER_SERVICE")=="1"
local returned_steps=os.getenv("AB2_ORACLE_CALLER_STEPS")=="1"
local returned_words=os.getenv("AB2_ORACLE_CALLER_WORDS")=="1"
local snapshots=os.getenv("AB2_ORACLE_CALLER_SNAPSHOTS")=="1"
local closure=os.getenv("AB2_ORACLE_CALLER_CLOSURE")=="1"
local successors=os.getenv("AB2_ORACLE_CALLER_SUCCESSORS")=="1"
local return_selection=os.getenv("AB2_ORACLE_CALLER_RETURN")=="1"
assert(not closure or successors,"closure requires successors")
assert(not snapshots or closure,"snapshots require closure")
assert(not successors or returned_words,"successors require returned words")
assert(not returned_words or returned_steps,"returned words require returned steps")
assert(not returned_steps or return_selection,"returned steps require linked RTE selection")
assert(not return_selection or continuation,"RTE selection requires linked services")
local linked_irq4=os.getenv("AB2_ORACLE_CALLER_IRQ4")=="1"
assert(not linked_irq4 or (snapshots and continuation),"IRQ4 link requires snapshots and services")
local handler,services,resume_check
services=0
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_OUT")),"wb"));out:write(linked_irq4 and "AB2CAL10" or snapshots and "AB2CALL9" or closure and "AB2CALL8" or successors and "AB2CALL7" or returned_words and "AB2CALL6" or returned_steps and "AB2CALL5" or return_selection and "AB2CALL4" or continuation and "AB2CALL3" or "AB2CALL2")
-- Sparse board/CPU visibility transcript. Previous completed main bus phase is
-- an explicit boundary across omitted/unported instruction graphs. Retain every
-- bounded caller phase, hardware access and scanline-adjacent observation.
local visibility=assert(io.open(os.getenv("AB2_ORACLE_OUT")..".visibility","wb"))
local inputgraph=os.getenv("AB2_ORACLE_CALLER_INPUT")=="1"
local inputonly=os.getenv("AB2_ORACLE_INPUT_ONLY")=="1"
assert(not inputgraph or snapshots,"input graph requires instruction snapshots")
assert(not inputonly or inputgraph,"input-only capture requires input graph")
visibility:write(inputgraph and "AB2VIS03" or "AB2VIS02")
local previous_end=0
local previous_pc,previous_opcode,previous_source,previous_fetch=0,0,0,false
local opcodes={}
local visibility_records=0
local mux=emu.item(assert(board.items["0/m_pc_0"]))
local ports=manager.machine.ioport.ports
local shared_guard={active=false}
local guard=false
local active,pending,entry
local frames,records,irqs,spins,pubspins,cuts=0,0,0,0,0,0
local sizes={ [0x1458a]=3,[0x14590]=4,[0x14594]=6,[0x1459c]=2,[0x145a0]=2,[0x145a4]=3,
 [0x145a8]=2,[0x145ac]=4,[0x145b0]=4,[0x145b2]=4,[0x145b6]=2,[0x145b8]=2,
 [0x14856]=4,[0x1485c]=2,[0x1485e]=5,[0x14866]=2,[0x1486a]=4,[0x1486e]=5,[0x14876]=1,[0x14878]=4,
 [0xb676]=4,[0xb67c]=2,[0xb67e]=5,[0xb684]=5,[0xb68a]=5,[0xb690]=5,[0xb696]=4 }
if returned_steps then
 sizes[0x88c2]=6;sizes[0x8af2]=7;sizes[0x7bb6]=7;sizes[0x14ee2]=7;sizes[0xb628]=4;sizes[0x15bfe]=4
end
if returned_words then sizes[0x8af8]=4;sizes[0xb62e]=2 end
if successors then sizes[0x8afe]=4;sizes[0xb632]=2 end
if closure then
 sizes[0x8b64]=6;sizes[0x8b6c]=5;sizes[0x8b72]=4;sizes[0x8b76]=1;sizes[0x8b78]=1
 sizes[0x8b7a]=3;sizes[0x8b7e]=3;sizes[0x8b82]=4;sizes[0x8b86]=1;sizes[0x8b88]=1;sizes[0x8b8a]=4
 sizes[0x8b90]=3;sizes[0x8b94]=3;sizes[0x8b9a]=1;sizes[0x8b9c]=3;sizes[0x8ba0]=1;sizes[0x8ba2]=1
 sizes[0x8ba4]=3;sizes[0x8ba8]=1;sizes[0x8baa]=2;sizes[0x8bae]=1;sizes[0x8bb0]=2;sizes[0x8bb4]=1;sizes[0x8bb6]=1;sizes[0x8bb8]=2
 sizes[0x8bbc]=6;sizes[0x8bc4]=5;sizes[0x8bca]=4;sizes[0x8bce]=1;sizes[0x8bd0]=1;sizes[0x8bd2]=3;sizes[0x8bd6]=3
 sizes[0x8bda]=4;sizes[0x8bde]=1;sizes[0x8be0]=1;sizes[0x8be2]=2;sizes[0x8be6]=4;sizes[0x8bec]=3
 sizes[0x8bf0]=3;sizes[0x8bf6]=1;sizes[0x8bf8]=3;sizes[0x8bfc]=3;sizes[0x8c00]=4;sizes[0x8c04]=7
 sizes[0x8c0a]=4;sizes[0x8c0e]=4;sizes[0x8c14]=3;sizes[0x8c16]=4
 sizes[0x9556]=4;sizes[0x955a]=4;sizes[0x955e]=4;sizes[0x9562]=2;sizes[0x9566]=3;sizes[0x9568]=1;sizes[0x956a]=2;sizes[0x956c]=5;sizes[0x9572]=4
 sizes[0x8b30]=4;sizes[0x8b34]=3;sizes[0x8b36]=4;sizes[0x8b3a]=3;sizes[0x8b3c]=3
 sizes[0x8b3e]=1;sizes[0x8b40]=1;sizes[0x8b42]=3;sizes[0x8b48]=3;sizes[0x8b4c]=1
 sizes[0x8b4e]=3;sizes[0x8b52]=6;sizes[0x8b5a]=4;sizes[0x8b5e]=1;sizes[0x8b60]=4
 sizes[0x8c1a]=4
 sizes[0x8b0c]=2;sizes[0x8b10]=6;sizes[0x8b18]=3;sizes[0x8b1c]=3
 sizes[0x8b20]=1;sizes[0x8b22]=1;sizes[0x8b24]=1;sizes[0x8b26]=3;sizes[0x8b2a]=3;sizes[0x8b2e]=1
 sizes[0x8b02]=3;sizes[0x8b06]=1;sizes[0x8b08]=2
 sizes[0xb636]=1;sizes[0xb638]=5;sizes[0xb63e]=1;sizes[0xb640]=1
 sizes[0xb642]=5;sizes[0xb644]=5;sizes[0xb646]=5;sizes[0xb648]=5
 sizes[0xb64a]=4;sizes[0xb64e]=5;sizes[0xb654]=4
end
local follower=os.getenv("AB2_ORACLE_CALLER_FOLLOWER")=="1"
assert(not follower or snapshots,"follower requires instruction snapshots")
if follower then
 sizes[0x7bbc]=5;sizes[0x7bc2]=5;sizes[0x7bc8]=5
 sizes[0x7bce]=5;sizes[0x7bd6]=2;sizes[0x7bda]=5;sizes[0x7be2]=2
 sizes[0x7be6]=3;sizes[0x7bec]=2;sizes[0x7bee]=3;sizes[0x7bf4]=3
 sizes[0x7bf8]=2;sizes[0x7bfc]=4;sizes[0x7c00]=1;sizes[0x7c02]=3
 sizes[0x7c06]=7;sizes[0x7c0c]=4;sizes[0x7c10]=4
end
local ready=os.getenv("AB2_ORACLE_CALLER_READY")=="1"
assert(not ready or follower,"ready continuation requires follower graph")
if ready then
 sizes[0xb66e]=6;sizes[0xc082]=7;sizes[0xc088]=5;sizes[0xc090]=2;sizes[0xc094]=4;sizes[0xc098]=4
end
local worldinit=os.getenv("AB2_ORACLE_CALLER_WORLDINIT")=="1"
assert(not worldinit or ready,"world initializer requires ready graph")
if worldinit then
 for p,n in pairs({[0x88ca]=4,[0x88ce]=6,[0x88d6]=4,[0x88da]=3,[0x88dc]=3,
 [0x88de]=6,[0x88e6]=5,[0x88ec]=4,[0x88f0]=1,[0x88f2]=1,[0x88f4]=1,[0x88f6]=3,
 [0x88fa]=1,[0x88fc]=2,[0x8900]=4,[0x8904]=3,[0x8908]=3,[0x890c]=4,[0x8912]=3,
 [0x8916]=6,[0x891e]=5,[0x8924]=4,[0x8928]=1,[0x892a]=1,[0x892c]=1,[0x892e]=4,
 [0x8932]=3,[0x8936]=2,[0x893a]=4,[0x8940]=3,[0x8944]=4})do sizes[p]=n end
end
local cleargraph=os.getenv("AB2_ORACLE_CALLER_CLEAR")=="1"
assert(not cleargraph or worldinit,"sprite clear requires world initializer graph")
if cleargraph then
 sizes[0xb698]=5
 sizes[0xb69e]=4
 sizes[0xb6a4]=2
 sizes[0xb6a6]=3
 sizes[0xb6ac]=1
 sizes[0xb6ae]=3
 sizes[0xb6b4]=2
 sizes[0xb6b8]=3
 sizes[0xb6ba]=3
 sizes[0xb6bc]=3
 sizes[0xb6be]=3
 sizes[0xb6c0]=2
 sizes[0xb6c4]=7
 sizes[0xb6ce]=5
 sizes[0xb6d6]=5
 sizes[0xb6dc]=4
 sizes[0xb6e2]=2
 sizes[0xb6e4]=3
 sizes[0xb6ea]=2
 sizes[0xb6ee]=3
 sizes[0xb6f0]=3
 sizes[0xb6f2]=3
 sizes[0xb6f4]=3
 sizes[0xb6f6]=2
 sizes[0xb6fa]=7
 sizes[0xb704]=5
 sizes[0xb70a]=4
 sizes[0xb710]=2
 sizes[0xb712]=4
 sizes[0xb714]=5
 sizes[0xb71a]=4
 sizes[0xb720]=2
 sizes[0xb722]=3
 sizes[0xb728]=3
 sizes[0xb72e]=2
 sizes[0xb732]=3
 sizes[0xb734]=4
 sizes[0xb738]=4
 sizes[0xb73c]=4
 sizes[0xb740]=4
 sizes[0xb744]=4
 sizes[0xb748]=4
 sizes[0xb74c]=4
 sizes[0xb750]=2
 sizes[0xb754]=2
 sizes[0xb758]=7
 sizes[0xb762]=5
 sizes[0xb76a]=5
 sizes[0xb770]=4
 sizes[0xb776]=2
 sizes[0xb778]=3
 sizes[0xb77e]=2
 sizes[0xb782]=3
 sizes[0xb784]=4
 sizes[0xb788]=4
 sizes[0xb78c]=4
 sizes[0xb790]=4
 sizes[0xb794]=4
 sizes[0xb798]=4
 sizes[0xb79c]=4
 sizes[0xb7a0]=2
 sizes[0xb7a4]=2
 sizes[0xb7a8]=7
 sizes[0xb7b2]=5
 sizes[0xb7b8]=4
 sizes[0xb7be]=2
 sizes[0xb7c0]=4
end
local trail=os.getenv("AB2_ORACLE_CALLER_TRAIL")=="1"
assert(not trail or cleargraph,"trail requires sprite-clear graph")
if trail then
 sizes[0xaaf4]=4
 sizes[0xaafa]=4
 sizes[0xaafe]=3
 sizes[0xab02]=1
 sizes[0xab04]=1
 sizes[0xab06]=2
 sizes[0xab0a]=2
 sizes[0xab0e]=3
 sizes[0xab12]=6
 sizes[0xab1a]=3
 sizes[0xab1e]=3
 sizes[0xab22]=4
 sizes[0xab28]=2
 sizes[0xab2c]=4
 sizes[0xab30]=3
 sizes[0xab34]=1
 sizes[0xab36]=3
 sizes[0xab3a]=3
 sizes[0xab3e]=4
 sizes[0xab42]=1
 sizes[0xab44]=3
 sizes[0xab48]=1
 sizes[0xab4a]=1
 sizes[0xab4c]=3
 sizes[0xab52]=3
 sizes[0xab56]=1
 sizes[0xab58]=3
 sizes[0xab5c]=6
 sizes[0xab64]=4
 sizes[0xab68]=1
 sizes[0xab6a]=4
 sizes[0xab6e]=6
 sizes[0xab76]=5
 sizes[0xab7c]=4
 sizes[0xab80]=3
 sizes[0xab84]=4
 sizes[0xab8a]=3
 sizes[0xab8e]=6
 sizes[0xab96]=4
 sizes[0xab9a]=1
 sizes[0xab9c]=1
 sizes[0xab9e]=1
 sizes[0xaba0]=1
 sizes[0xaba2]=2
 sizes[0xaba6]=3
 sizes[0xabaa]=4
 sizes[0xabae]=3
 sizes[0xabb2]=4
 sizes[0xabb8]=3
 sizes[0xabbc]=1
 sizes[0xabbe]=3
 sizes[0xabc2]=1
 sizes[0xabc4]=2
 sizes[0xabc8]=1
 sizes[0xabca]=2
 sizes[0xabce]=1
 sizes[0xabd0]=1
 sizes[0xabd2]=2
 sizes[0xabd6]=6
 sizes[0xabde]=5
 sizes[0xabe4]=4
 sizes[0xabe8]=3
 sizes[0xabec]=4
 sizes[0xabf2]=3
 sizes[0xabf6]=6
 sizes[0xabfe]=4
 sizes[0xac02]=1
 sizes[0xac04]=1
 sizes[0xac06]=1
 sizes[0xac08]=4
 sizes[0xac0c]=3
 sizes[0xac10]=2
 sizes[0xac14]=4
 sizes[0xac1a]=3
 sizes[0xac1e]=1
 sizes[0xac20]=3
 sizes[0xac24]=1
 sizes[0xac26]=1
 sizes[0xac28]=1
 sizes[0xac2a]=2
 sizes[0xac2e]=1
 sizes[0xac30]=2
 sizes[0xac34]=1
 sizes[0xac36]=2
 sizes[0xac3a]=3
 sizes[0xac3e]=2
 sizes[0xac42]=3
 sizes[0xac46]=4
 sizes[0xac4a]=4
 sizes[0xae32]=4
 sizes[0xae36]=4
 sizes[0xae3a]=4
 sizes[0xae3e]=2
 sizes[0xae42]=3
 sizes[0xae44]=1
 sizes[0xae46]=2
 sizes[0xae48]=5
 sizes[0xae4e]=4
 sizes[0xaaee]=7
end
local soundproducer=os.getenv("AB2_ORACLE_CALLER_SOUNDPRODUCER")=="1"
assert(not soundproducer or snapshots,"sound producer requires instruction snapshots")
if soundproducer then
 -- $D0DA is already timed and may resume after an IRQ in any linked corpus.
 sizes[0xd0da]=4
 for p,n in pairs({[0xb84c]=3,[0xb850]=2,[0xb854]=5,[0xb85c]=2,
 [0xb860]=3,[0xb864]=2,[0xb868]=3,[0xb86c]=1,[0xb86e]=2,
 [0xb872]=3,[0xb876]=4,[0xb87a]=4,[0xb87c]=4,[0xb880]=4,[0xb884]=4})do sizes[p]=n end
end
local hostgraph=os.getenv("AB2_ORACLE_CALLER_HOST")=="1"
assert(not hostgraph or snapshots,"host graph requires snapshots")
local host_manifest=hostgraph and dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/hostcaller_schedule.lua") or {}
local weapongraph=os.getenv("AB2_ORACLE_CALLER_WEAPON")=="1"
assert(not weapongraph or snapshots,"weapon graph requires snapshots")
local weapon_manifest=weapongraph and dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/weapon_schedule.lua") or {}
local companiongraph=os.getenv("AB2_ORACLE_CALLER_COMPANION")=="1"
local companiononly=os.getenv("AB2_ORACLE_COMPANION_ONLY")=="1"
assert(not companiongraph or snapshots,"companion graph requires snapshots")
local companion_manifest=companiongraph and dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/companion_schedule.lua") or {}
local flightgraph=os.getenv("AB2_ORACLE_CALLER_FLIGHT")=="1"
local flightonly=os.getenv("AB2_ORACLE_FLIGHT_ONLY")=="1"
assert(not flightgraph or snapshots,"flight graph requires snapshots")
local flight_manifest=flightgraph and dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/flight_schedule.lua") or {}
local input_manifest=inputgraph and dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/input_schedule.lua") or {}
local retained_sizes=sizes
if inputonly or flightonly or companiononly then sizes={} end
if flightonly or companiononly then
 for p=0xb628,0xb654,2 do sizes[p]=retained_sizes[p]end
 for p=0x1458a,0x145b8,2 do sizes[p]=retained_sizes[p]end
 -- Already timed returns reached by other callers of the shared append graph.
 sizes[0x8c1a]=4;sizes[0x7c10]=4;sizes[0xac4a]=4
 sizes[0x8af2]=retained_sizes[0x8af2];sizes[0xd0da]=4
 for _,range in ipairs({{0x88c2,0x8944},{0x8af8,0x8c1c},{0x7bb6,0x7c10}})do
  for p=range[1],range[2],2 do if retained_sizes[p]then sizes[p]=retained_sizes[p]end end
 end
end
if companiononly then
 local returned=dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/flight_schedule.lua")
 for _,p in ipairs({0x427c,0x5d68,0x60ca,0x7650,0x18e8a})do flight_manifest[p]=returned[p]end
end
for p,i in pairs(host_manifest)do sizes[p]=i.phases end
for p,i in pairs(weapon_manifest)do sizes[p]=i.phases end
for p,i in pairs(companion_manifest)do sizes[p]=i.phases end
for p,i in pairs(flight_manifest)do sizes[p]=i.phases end
for p,i in pairs(input_manifest)do sizes[p]=i.phases end
local completion={
 [0xb636]={reg=0,tail=10},[0x8b06]={reg=1,tail=12},[0x8b24]={reg=0,tail=8},[0x8b2e]={reg=1,variable=true},
 [0x8b40]={reg=6,tail=10},[0x8b5e]={reg=6,tail=16},[0x8b76]={reg=0,tail=4},[0x8bce]={reg=0,tail=4},
 [0x8b78]={reg=0,tail=18},[0x8b86]={reg=0,tail=20},[0x8b88]={reg=0,tail=8},[0x8ba8]={reg=1,tail=4},
 [0x8bd0]={reg=0,tail=18},[0x8bde]={reg=0,tail=20},[0x8be0]={reg=0,tail=8}
}
if worldinit then
 for p,info in pairs({[0x88f0]={reg=0,tail=4},[0x88f2]={reg=0,tail=18},[0x88f4]={reg=0,tail=14},
 [0x88fa]={reg=2,tail=12},[0x8928]={reg=0,tail=4},[0x892a]={reg=0,tail=18},[0x892c]={reg=0,tail=14}})do completion[p]=info end
end
if trail then
 completion[0xab02]={reg=7,tail=12}
 completion[0xab42]={reg=0,tail=10}
 completion[0xab4a]={reg=6,tail=6}
 completion[0xab68]={reg=6,tail=16}
 completion[0xab9a]={reg=0,tail=4}
 completion[0xab9c]={reg=0,tail=18}
 completion[0xab9e]={reg=0,tail=14}
 completion[0xabc2]={reg=1,tail=6}
 completion[0xac02]={reg=0,tail=4}
 completion[0xac04]={reg=0,tail=18}
 completion[0xac06]={reg=0,tail=14}
 completion[0xac24]={reg=1,tail=4}
end
for p,i in pairs(host_manifest)do if i.tail>0 then completion[p]={reg=i.source,tail=i.tail}end end
for p,i in pairs(weapon_manifest)do if i.tail>0 then completion[p]={reg=i.source,tail=i.tail}end end
for p,i in pairs(companion_manifest)do if i.tail>0 then completion[p]={reg=i.source,tail=i.tail}end end
for p,i in pairs(input_manifest)do if i.tail>0 then completion[p]={reg=i.source,tail=i.tail}end end
for p,i in pairs(flight_manifest)do if i.tail>0 then completion[p]={reg=i.source,tail=i.tail,variable=i.variable>0,count=i.variable-1}end end
local function ns()return math.floor(manager.machine.time:as_double()*1e9+0.5)end
local function level()return (timer:read(0)~=0 and 2 or 0)|(blank:read(0)~=0 and 4 or 0)end
local function stack()return main.state.SR.value&0x2000~=0 and main.state.SP.value or main.state.USP.value end
local function regs(reverse,initialpc)
 local r={};for i=0,7 do r[#r+1]=string.pack(">I4",main.state["D"..i].value&0xffffffff)end
 for i=0,6 do local v=main.state["A"..i].value;if reverse and i==5 then v=v-64 end
  if initialpc==0xb63e and i==0 then v=(v&0xffff0000)|((v-main.state.D0.value)&0xffff)end;r[#r+1]=string.pack(">I4",v&0xffffffff)end
 return table.concat(r)
end
-- Read-only debugger breakpoints run printf/g at the instruction hook.
-- They preserve the actual boundary state before any first-bus side effects
-- and after internal ALU tails; no expected arithmetic is synthesized here.
local snapshot_cache,snapshot_sequence
local snapshot_addresses={}
local function snapshot(p)
 if not snapshots then return nil end
 local log=manager.machine.debugger.consolelog;local sequence=#log
 if sequence~=snapshot_sequence then
  snapshot_sequence=sequence;snapshot_cache=nil
  local line=log[sequence]
  if line and line:match("^AB2 ") then
   local values={};for hex in line:gmatch("%x+") do values[#values+1]=tonumber(hex,16)end
   -- The prefix AB2 is also hexadecimal.
   assert(#values==19,"invalid instruction snapshot")
   local packed={};for i=5,19 do packed[#packed+1]=string.pack(">I4",values[i]&0xffffffff)end
   snapshot_cache={pc=values[2],sr=values[3],sp=values[4],regs=table.concat(packed)}
  end
 end
 return snapshot_cache and snapshot_cache.pc==p and snapshot_cache or nil
end
if snapshots then
 assert(closure,"instruction snapshots require closure capture")
 local addresses=snapshot_addresses
 for p in pairs(input_manifest)do addresses[p]=true end
 for p in pairs(flight_manifest)do addresses[p]=true end
 for p in pairs(host_manifest)do addresses[p]=true end
 for p in pairs(weapon_manifest)do addresses[p]=true end
 for p in pairs(companion_manifest)do addresses[p]=true end
 if flightonly or companiononly then for p in pairs(sizes)do addresses[p]=true end end
 if not inputonly and not flightonly and not companiononly then
 for p=0x8b02,0x8c1c,2 do addresses[p]=true end
 for p=0xb636,0xb654,2 do addresses[p]=true end
 for p=0x9556,0x9574,2 do addresses[p]=true end
 if soundproducer then if sizes[0xd0da] then addresses[0xd0da]=true end;for p=0xb84c,0xb884,2 do addresses[p]=true end end
 if follower then for p=0x7bb6,0x7c10,2 do addresses[p]=true end end
 if worldinit then for p=0x88c2,0x8944,2 do addresses[p]=true end end
 if trail then for p=0xaaee,0xac4a,2 do addresses[p]=true end;for p=0xae32,0xae4e,2 do addresses[p]=true end end
 if cleargraph then for p=0xb698,0xb7c0,2 do addresses[p]=true end end
 if ready then addresses[0xb66e]=true end
 if ready then for p=0xc082,0xc098,2 do addresses[p]=true end end
 end -- legacy caller graph snapshots
 for p in pairs(addresses) do
  local fmt=string.format("AB2 %06X %%04X %%08X",p)
  local args="sr,(sp*((sr & 2000)!=0)+usp*((sr & 2000)==0))"
  for i=0,7 do fmt=fmt.." %08X";args=args..",d"..i end
  for i=0,6 do fmt=fmt.." %08X";args=args..",a"..i end
  main.debug:bpset(p,"1",'printf "'..fmt..'",'..args..';g')
 end
 manager.machine.debugger.execution_state="run"
end
local function ram()
 guard=true;shared_guard.active=true
 local value=space:read_range(0x80000,0x83fff,8)..space:read_range(0xa0000,0xa3fff,8)
 guard=false;shared_guard.active=false;return value
end
local function emit(q,nextpc,irq)
 records=records+1
 out:write(string.char(1),q.pre,string.pack(">I4",#q.events),table.concat(q.events),q.post,string.pack(">I4B",nextpc,irq))
end
local function event(w,p,a,d,m)
 return string.pack(">I8I4I4I2I2BB",ns(),p,a,d,m,w and 1 or 0,level())
end
local visibility_inputs
local function visibility_record(kind,p,a,d,m,t)
 if not visibility_inputs or kind==2 then
  local values={};for i=0,7 do local port=ports[":mainpcb:ADC"..i];values[#values+1]=string.char(port and port:read() or 0x10)end
  visibility_inputs=table.concat(values)
 end
 visibility:write(string.pack(">I8I8I4I4I2I2BBB",t,previous_end,p,a,d,m,kind,level(),mux:read(0)),visibility_inputs,inputgraph and string.char(ports[":mainpcb:IO1PORTA"]:read()&255) or "",string.pack(">I2I2B",p~=previous_pc and previous_fetch and previous_opcode or 0,previous_source,previous_fetch and 1 or 0))
 visibility_records=visibility_records+1
end
local closing,finish_companion,companion_idle=false,nil,nil
local function close_capture()
 if finish_companion then finish_companion()end
 print(string.format("captured %d linked caller IRQ services",services))
 visibility:close();print(string.format("captured %d CPU visibility observations",visibility_records))
 out:close();print(string.format("captured %d caller instructions, %d IRQ entries (%d fixture cuts, %d unfinished instructions, %d unfinished entries)",records,irqs,cuts,active and 1 or 0,entry and 1 or 0))
 manager.machine:exit()
end
local function access(w,a,d,m)
 if guard or shared_guard.active then return end
 local p=main.state.CURPC.value
 local t=ns()
 local phase=t%64000
 local hardware=(a>=0xe8000 and a<0xec000) or (a>=0x130000 and a<0x140000) or a==0x140004
 if (inputgraph and (p==0x14632 or (w and a>=0x3f8892 and a<=0x3f8896))) or sizes[p] or (return_selection and (p==0x1466a or p==0x14672 or (linked_irq4 and (p==0x147c6 or p==0x147e6)))) or hardware or phase<1000 or phase>=63000 then
  visibility_record(w and 1 or 0,p,a,d,m,t)
 end
 previous_end=t+320
 previous_pc=p
 if not opcodes[p] then guard=true;shared_guard.active=true;opcodes[p]=space:read_u16(p);guard=false;shared_guard.active=false end
 previous_opcode=opcodes[p]
 previous_source=(previous_opcode>>12==14 and previous_opcode&0x20~=0) and (main.state["D"..(previous_opcode>>9&7)].value&0xffff) or 0
 previous_fetch=not w and a>=p+4 and a<=p+10
 if handler then
  if handler.level==4 then
   if not w and (p==0x147c6 or p==0x147e6) and a==((p==0x147e6 and 0x1a132 or handler.returnpc+2)&0x3ffffe) then
    local returnedpc=p==0x147e6 and 0x1a130 or handler.returnpc
    out:write(string.char(8),string.pack(">I8I4I2I4",ns()+320,stack(),main.state.SR.value,returnedpc),regs())
    handler=nil;services=services+1;resume_check={endns=ns()+320,sr=main.state.SR.value,regs=regs()}
   end
   return
  end
  assert((p>=0x1461c and p<=0x14672) or (p>=0xb7f8 and p<=0xb83e),"nested/unported linked IRQ handler")
  if w or a>=0x80000 then handler.events[#handler.events+1]=event(w,p,a,d,m)end
  if not w and (p==0x1466a or p==0x14672) and a==((handler.returnpc+2)&0x3ffffe) then
   out:write(string.char(4),handler.before,string.pack(">I4",#handler.events),table.concat(handler.events),
    string.pack(">I8I4I2I4",ns()+320,stack(),main.state.SR.value,handler.returnpc),regs(),ram())
   handler=nil;services=services+1;resume_check={endns=ns()+320,sr=main.state.SR.value,regs=regs()}
  end
  return
 end
 if resume_check then
  local q=resume_check;resume_check=false
  local irq=w and a==((main.state.SP.value-2)&0x3ffffe) and (main.state.SR.value&0x700)>0x100
  if return_selection then
   out:write(string.char(6,irq and (main.state.SR.value>>8&7) or 0))
   if irq then
    entry={pc=p,endns=q.endns,ssp=main.state.SP.value,returnpc=main.state.PC.value,
     sr=q.sr,regs=q.regs,level=main.state.SR.value>>8&7,events={event(w,p,a,d,m)}}
    entry.target=entry.level==2 and 0x1461c or entry.level==4 and 0x14674 or 0x1466c
    return
   end
  end
  -- Unported returned instructions remain explicit execution boundaries.
  if not sizes[p] then out:write(string.char(5))end
 end
 if entry then
  if w or a<0x80 then entry.events[#entry.events+1]=event(w,p,a,d,m)end
  if not w and a==entry.target+2 then
   out:write(string.char(2),string.pack(">I4I8I4I4I2B",entry.pc,entry.endns,entry.ssp,entry.returnpc,entry.sr,entry.level),entry.regs,
    string.pack(">I8I4",ns()+320,#entry.events),table.concat(entry.events),string.pack(">I4I2",main.state.SP.value,main.state.SR.value))
   if continuation and (entry.level~=4 or linked_irq4) then
    handler={before=entry.level~=4 and ram() or nil,events={},returnpc=entry.returnpc,level=entry.level}
   end
   entry=nil;irqs=irqs+1
  end
  return
 end
 if pending then
  local q=pending;pending=nil
  local boundary=snapshot(p)
  if snapshots and (completion[q.pc] or q.pc==0xb63e) and p~=q.pc then
   assert(boundary,"missing post-tail instruction-hook snapshot")
  end
  if snapshots and completion[q.pc] and boundary and p~=q.pc then
   q.sr=boundary.sr;q.regs=boundary.regs;q.sp=boundary.sp
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  elseif completion[q.pc] then
   -- Exception entry preserves the completed ALU registers/CCR until
   -- its first stack write. Legacy CALL8 successors also preserve that
   -- late state; CALL9 uses the mandatory instruction-hook snapshot.
   q.sr=(q.sr&0xff00)|(main.state.SR.value&0xff)
   local register=completion[q.pc].reg;local offset=register*4
   q.regs=q.regs:sub(1,offset)..string.pack(">I4",main.state["D"..register].value&0xffffffff)..q.regs:sub(offset+5)
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  end
  if snapshots and q.pc==0xb63e and boundary and p~=q.pc then
   q.sr=boundary.sr;q.regs=boundary.regs;q.sp=boundary.sp
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  elseif q.pc==0xb63e then
   q.regs=q.regs:sub(1,32)..string.pack(">I4",main.state.A0.value&0xffffffff)..q.regs:sub(37)
   q.post=string.pack(">I8I4I2",q.endns,q.sp,q.sr)..q.regs
  end
  local irq=w and a==((main.state.SP.value-2)&0x3ffffe) and (main.state.SR.value&0x700)>0x100
  if irq then
   emit(q,main.state.PC.value,main.state.SR.value>>8&7)
   entry={pc=q.pc,endns=q.endns,ssp=main.state.SP.value,returnpc=main.state.PC.value,sr=q.sr,regs=q.regs,
    level=main.state.SR.value>>8&7,events={event(w,p,a,d,m)}}
   entry.target=entry.level==2 and 0x1461c or entry.level==4 and 0x14674 or 0x1466c
   return
  elseif q.selected then
   emit(q,p,0)
   if returned_steps and not sizes[p] then out:write(string.char(7))end
  end
 end
 if closing and not active and (not companion_idle or companion_idle())then close_capture();return end
 if not active and sizes[p] then
  local n=sizes[p];local offset=0
  if p==0xb6c0 or p==0xb6f6 or p==0xb754 or p==0xb7a4 then
   if main.state.D0.value&0xffff==0 then n=3 end
  end
  if input_manifest[p] or flight_manifest[p] or companion_manifest[p] or weapon_manifest[p] or host_manifest[p] then
   local i=input_manifest[p] or flight_manifest[p] or companion_manifest[p] or weapon_manifest[p] or host_manifest[p]
   offset=i.offset==-1 and (a==p+4 and 320 or 160) or i.offset*80
   if i.offset==-1 and i.branch==2 and a==p+4 then n=1 end
   if i.offset==-2 then
    local snap=assert(snapshot(p));local dividend,divisor=string.unpack(">I4I4",snap.regs)
    divisor=divisor&65535;assert(divisor>0 and dividend>>16<divisor,"decimal DIVU outside bounded contract")
    local cycles=38;local partial=dividend;local high=divisor<<16
    for k=1,15 do local old=partial;partial=(partial<<1)&0xffffffff
     if old&0x80000000~=0 then partial=(partial-high)&0xffffffff
     else cycles=cycles+2;if partial>=high then partial=partial-high;cycles=cycles-1 end end
    end
    offset=(cycles*2-4)*80
   end
   if p==0xd004 or p==0xd036 or p==0xd050 or p==0x9f96 then
    local register=p==0xd004 and 2 or p==0xd036 and 1 or p==0xd050 and 0 or 7
    local old=string.unpack(">I4",assert(snapshot(p)).regs,register*4+1)&65535
    n=old==0 and 3 or 2
   end
   -- These short BEQs can prefetch PC+4 on either path. Address alone
   -- cannot distinguish their timing; use the independent SR snapshot.
   if p==0x9d6a or p==0x9fca then
    local taken=(assert(snapshot(p)).sr&4)~=0
    offset=taken and 160 or 320
    if p==0x9fca then n=taken and 2 or 1 end
   end
  elseif p==0xb868 or p==0xab52 or p==0xac46 then offset=160
  elseif p==0xb850 or p==0xb85c then offset=a==p+4 and 320 or 160
  elseif p==0xab06 or p==0xab28 or p==0xabca or p==0xabd2 or p==0xac2a or p==0xac36 then offset=a==p+4 and 320 or 160
  elseif p==0xb6c0 or p==0xb6f6 or p==0xb754 or p==0xb7a4 then offset=160
  elseif p==0xb6a4 or p==0xb6e2 or p==0xb710 or p==0xb720 or p==0xb776 or p==0xb7be then
   if a==p+4 then offset=320;n=1 else offset=160 end
  elseif p==0x8b48 or p==0x8ba4 or p==0x8bfc or p==0x8c04 or p==0x8c16 then offset=160
  elseif p==0x14590 or p==0x7c0c or p==0x7c06 or p==0xc094 then offset=160
  elseif p==0x145a8 or p==0x14866 or p==0xb632 or p==0x8b08 or p==0x8bb0 or p==0x8bb8 or p==0x7bd6 or p==0x7be2 or p==0x7bf8 or p==0xc090 then offset=a==p+4 and 320 or 160
  elseif p==0x145b6 or p==0x1485c or p==0xb67c then
   if a==p+4 then offset=320;n=1 else offset=160 end
  elseif p==0x145b8 or p==0x15bfe or p==0x7bec then offset=160 end
  -- First BSR stack write already exposes decremented SP; LEA d16(A5)
  -- already exposes its new A5. Undo only these entry-phase effects.
  local sp=stack()+((p==0x9c92 or p==0x9d26 or p==0xb0a4 or p==0x9ace or p==0x9adc or p==0xd0de or p==0x14590 or p==0x15bfe or p==0x8c16 or p==0x7c0c or p==0xc094 or p==0xac46) and 4 or 0)
  local selected=true
  if p==0x14856 then spins=spins+1 end
  if (p==0x14856 or p==0x1485c) and spins%128~=0 then
   guard=true;shared_guard.active=true;selected=space:read_u16(0x2a3c00)>=0x8000;guard=false;shared_guard.active=false
  end
  if p==0xb676 then pubspins=pubspins+1 end
  if (p==0xb676 or p==0xb67c) and pubspins%128~=0 then
   guard=true;shared_guard.active=true;local v=space:read_u16(0x2a3c00);selected=v==0 or v>=0x8000;guard=false;shared_guard.active=false
  end
  active={pc=p,n=n,selected=selected,events={},pre=string.pack(">I4I8I4I2",p,ns()-offset,sp,main.state.SR.value)..regs(p==0x145a0,p)}
  local boundary=snapshot(p)
  assert(not (snapshots and snapshot_addresses[p]) or boundary,"missing instruction-hook snapshot")
  if boundary then active.pre=string.pack(">I4I8I4I2",p,ns()-offset,boundary.sp,boundary.sr)..boundary.regs end
 end
 if active then
  assert(active.pc==p,"caller changed PC within instruction")
  active.events[#active.events+1]=event(w,p,a,d,m)
  if #active.events==active.n then
   local tail=completion[p]
   local extra=tail and (tail.variable and ((tail.tail or 2)+2*(main.state["D"..(tail.count or 0)].value&63)) or tail.tail)*80 or p==0xb63e and 320 or 0
   active.endns=ns()+320+extra;active.sp=stack();active.sr=main.state.SR.value;active.regs=regs(false)
   active.post=string.pack(">I8I4I2",active.endns,stack(),active.sr)..active.regs
   pending=active;active=nil
  end
 end
end
caller_r=space:install_read_tap(0,0x3fffff,"caller_r",function(a,d,m)access(false,a,d,m)end)
caller_w=space:install_write_tap(0,0x3fffff,"caller_w",function(a,d,m)access(true,a,d,m)end)
dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/gameplay.lua")
-- Place a real IPL3 instruction and ROM caller before the physical line-223
-- edge. Vary lead time; no debugger SR setter is used to unmask pending IRQs.
local directed=os.getenv("AB2_ORACLE_CALLER_DIRECTED")=="1"
local host_directed=os.getenv("AB2_ORACLE_HOST_DIRECTED")=="1"
assert(not host_directed or (directed and hostgraph),"host fixture requires host graph")
local host_entries={0x9810,0x9626,0x964c,0xcfc4,0xd062,0xd090,0x9f7c}
local weapon_directed=os.getenv("AB2_ORACLE_WEAPON_DIRECTED")=="1"
assert(not weapon_directed or (directed and weapongraph),"weapon fixture requires weapon graph")
local weapon_entries={0x9d34,0x9d56,0x9df8,0x9e50,0x9e82,0x9ed8}
local companion_directed=os.getenv("AB2_ORACLE_COMPANION_DIRECTED")=="1"
assert(not companion_directed or (directed and companiongraph),"companion fixture requires companion graph")
local companion_entries={0x9c34,0x9c9e,0xae8c,0xaee4,0xaf08,0xaf3c,0xafcc,0xb05c}
local flight_directed=os.getenv("AB2_ORACLE_FLIGHT_DIRECTED")=="1"
assert(not flight_directed or (directed and flightgraph),"flight fixture requires flight graph")
local input_directed=os.getenv("AB2_ORACLE_INPUT_DIRECTED")=="1"
assert(not input_directed or (directed and inputgraph),"input fixture requires input graph")
local soundproducer_directed=os.getenv("AB2_ORACLE_SOUNDPRODUCER_DIRECTED")=="1"
assert(not soundproducer_directed or (directed and soundproducer),"sound producer fixture requires snapshots")
local trail_directed=os.getenv("AB2_ORACLE_TRAIL_DIRECTED")=="1"
assert(not trail_directed or (directed and trail),"trail fixture requires trail graph")
local worldinit_directed=os.getenv("AB2_ORACLE_WORLDINIT_DIRECTED")=="1"
assert(not worldinit_directed or (directed and worldinit),"world initializer fixture requires worldinit graph")
local ready_directed=os.getenv("AB2_ORACLE_READY_DIRECTED")=="1"
assert(not ready_directed or (directed and ready),"ready fixture requires ready graph")
local follower_directed=os.getenv("AB2_ORACLE_FOLLOWER_DIRECTED")=="1"
assert(not follower_directed or (directed and follower),"follower fixture requires directed follower capture")
local successor_directed=os.getenv("AB2_ORACLE_SUCCESSOR_DIRECTED")=="1"
assert(not successor_directed or (directed and successors),"successor fixture requires directed CALL7")
local companion_fixture_index=0
local function fixture(k)
 local co=coroutine.create(function()
  local dispatcher=not host_directed and not weapon_directed and not companion_directed and not flight_directed and not input_directed and not soundproducer_directed and not trail_directed and not worldinit_directed and not ready_directed and not follower_directed and not successor_directed and k%4==3
  local lead=dispatcher and (20+(k//4)%100*2) or (1+(k%64)*0.24)
  emu.wait((261*64-lead)/1e6)
  if closing or entry or handler or (main.state.SR.value&0x700)~=0x100 then return end
  if companion_directed or weapon_directed or host_directed then
   -- These read/branch wait instructions have no partially completed stack
   -- effects. Re-entering CURPC at a just-completed RTS would pop twice.
   local p=main.state.CURPC.value
   if p~=0xb676 and p~=0xb67c and p~=0x14856 and p~=0x1485c then return end
   k=companion_fixture_index;companion_fixture_index=k+1
  end
  guard=true;shared_guard.active=true
  local follower_saved
  local sound_saved
  local input_saved
  local flight_saved
  local companion_saved
  local weapon_saved
  local host_saved
  if trail_directed or follower_directed or ready_directed or worldinit_directed then follower_saved={ram=space:read_range(0x3ffbc0,0x3ffc3f,8),stage=space:read_u16(0x29c09c),count=space:read_u16(0x2a3cf0),ready=space:read_u8(0x29c200)}end
  local saved={pc=main.state.CURPC.value,sr=main.state.SR.value,ssp=main.state.SP.value,usp=main.state.USP.value,d={},a={}}
  for i=0,7 do saved.d[i]=main.state["D"..i].value end
  for i=0,6 do saved.a[i]=main.state["A"..i].value end
  out:write(string.char(3)) -- explicit scheduler context cut
  if active or pending then cuts=cuts+1 end;active=nil;pending=nil;resume_check=false
  space:write_u16(0x3ffd00,0x46fc);space:write_u16(0x3ffd02,(host_directed or weapon_directed or companion_directed or flight_directed or input_directed or soundproducer_directed) and 0x2700 or successor_directed and (k%2==0 and 0x2300 or 0x231a) or 0x2300)
  space:write_u16(0x3ffd04,0x4eb9);space:write_u32(0x3ffd06,host_directed and host_entries[k%7+1] or weapon_directed and weapon_entries[k%6+1] or companion_directed and companion_entries[k%8+1] or flight_directed and 0x98c0 or input_directed and 0x14976 or soundproducer_directed and 0xb84c or trail_directed and 0xaaee or worldinit_directed and 0x88c2 or ready_directed and 0xc082 or follower_directed and 0x7bb6 or successor_directed and 0xb632 or dispatcher and 0x14594 or 0x14856)
  space:write_u16(0x3ffd0a,0x46fc);space:write_u16(0x3ffd0c,0x2700)
  space:write_u16(0x3ffd0e,0x4e71);space:write_u16(0x3ffd10,0x60fc)
  if input_directed then
   space:write_u16(0x3ffd0a,0x4eb9);space:write_u32(0x3ffd0c,0xb0ac)
   space:write_u16(0x3ffd10,0x46fc);space:write_u16(0x3ffd12,0x2700)
   space:write_u16(0x3ffd14,0x4e71);space:write_u16(0x3ffd16,0x60fc)
  end
  space:write_u16(0x3ffd40,0x46fc);space:write_u16(0x3ffd42,saved.sr)
  space:write_u16(0x3ffd44,0x4ef9);space:write_u32(0x3ffd46,saved.pc)
  space:write_u16(0x2a3c00,({0xffff,0xfffe,0x8000})[k%3+1])
  space:write_u16(0x3f8ba4,k%2==0 and 0xffff or 9)
  space:write_u8(0x3f8050,1)
  if dispatcher then
   for i=0,253 do space:write_u8(0x3fa000+64*i,0)end
  end
  if host_directed then
   -- Operand fixtures only, using the live fuselage slot; all arithmetic,
   -- HUD writes and command lookup execute in the real ROM.
   host_saved={}
   local function save(a,size)
    for i=0,size-1 do if host_saved[a+i]==nil then host_saved[a+i]=space:read_u8(a+i)end end
   end
   local function set(a,v,size)
    save(a,size)
    if size==1 then space:write_u8(a,v&255)elseif size==2 then space:write_u16(a,v&65535)else space:write_u32(a,v&0xffffffff)end
   end
   local n=k//7;local mode=n%12
   main.state.A5.value=0x3fc0c0;main.state.A6.value=0xe4000
   main.state.D0.value=({0,1,9,10,99,100,101,999,10000,65535})[n%10+1]
   main.state.D1.value=({10,100,1000,10000})[n%4+1]
   save(0x3fa000,254*64);save(0x3f8800,0xc00);save(0x0d0c00,0x200)
   for a=0x3fc080,0x3fc1ff do set(a,0,1)end
   for a=0x3fe800,0x3fe83f do set(a,0,1)end
   set(0x3fc0c0,0xfc00,2);set(0x3fc0d4,131,1)
   set(0x3fc0c4,0x7fff,2);set(0x3fc0c6,0x70,2)
   set(0x3fc0e4,({0,0x1e7,0x1e8,0x3e7,0x3e8,0x3ff,0x400,0xffff,0xff00})[n%9+1],2)
   set(0x3fc0f4,n%2,1)
   set(0x3f91c6,({0,29,30,0x7e,0x7f,0x80,0xff})[n%7+1],1)
   set(0x3f8d76,mode==1 and 1 or mode==2 and 0xffff or 0,2)
   set(0x3f8d60,1,1);set(0x3f91c4,1,1);set(0x3f89ec,1,1)
   set(0x29c09c,({1,14,15,16,18,19})[n%6+1],2)
   set(0x29c200,mode==3 and 0 or 0x10,1)
   set(0x3f91d0,mode==4 and 0x10 or 0,1)
   set(0x29c09a,0,2);set(0x3f90fe,0,1)
   set(0x29c096,({0,32,0xffe0,96,0xffa0,256,0xff00})[n%7+1],2)
   set(0x29c098,0,2);set(0x29c080,0x70,2)
   set(0x29c088,0,2);set(0x29c08a,0,2)
   set(0x3f8d4c,({0,0x7fff,0x8000,0xffff})[n%4+1],2)
   set(0x3f91fe,({0,0xfc,0xfffc})[n%3+1],2)
   set(0x3f8d66,({0,1,2,9,10,11,99,100,101,0xffff})[n%10+1],2)
   for a=0x3f8e30,0x3f8e6f,4 do set(a,0,4)end
   if n%2~=0 then set(0x3f8e34,0x3fc300,4)end
   set(0x29c0b8,n%2,2);set(0x3fc0f0,(n%16)*4,2)
   set(0x3f8902,mode==5 and 0 or 1,1);set(0x3f8906,0,1)
   set(0x3f8908,n%2==0 and 0 or 0x3f,2)
   local count=({0,0xbf,0xc0,0xc1})[n%4+1]
   set(0x2a3cf0,count,2);set(0x2a3c04,0x3fe800-count*16,4)
  end
  if weapon_directed then
   -- Labelled initial RAM fixtures. All controller/fire/allocation effects
   -- execute in the ROM; inject only at a wait to preserve stack completion.
   weapon_saved={}
   local function save(a,size)
    for i=0,size-1 do if weapon_saved[a+i]==nil then weapon_saved[a+i]=space:read_u8(a+i)end end
   end
   local function set(a,v,size)
    save(a,size)
    if size==1 then space:write_u8(a,v&255)elseif size==2 then space:write_u16(a,v&65535)else space:write_u32(a,v&0xffffffff)end
   end
   local n=k//6;local mode=n%16
   main.state.A5.value=0x3fc080;main.state.A6.value=0xe4000
   main.state.D0.value=0x12345678;main.state.D1.value=0x89abcdef
   save(0x3fa000,254*64);save(0x3f8d00,0x100);save(0x3f8900,0x50)
   save(0x3f8e7f,0x82);save(0x0d0c00,0x200)
   for a=0x3fc080,0x3fc0ff do set(a,0,1)end
   for a=0x3fe800,0x3fe83f do set(a,0,1)end
   set(0x3fc080,0x80,1);set(0x3fc08c,0x10a00,4);set(0x3fc094,130,1)
   set(0x3fc0c0,mode==4 and 0 or 0x80,1)
   set(0x3fc0c4,({0,0x7fff,0x8000,0xffff})[n%4+1],2)
   set(0x3fc0c6,({0x70,0x8000,0x7fff,0xffff})[n%4+1],2)
   set(0x3fc0be,({0,0x3c,0x7fff,0xffff})[n%4+1],2)
   set(0x29c09c,({1,16,15,18,19})[n%5+1],2)
   set(0x29c09a,mode==0 and 1 or 0,2)
   set(0x29c200,mode==1 and 0 or mode==3 and 8 or 0x18,1)
   set(0x3f91d0,mode==2 and 8 or 0,1)
   set(0x29c202,mode==5 and 1 or 0,1)
   set(0x3f90fe,mode==6 and 1 or 0,1)
   set(0x29c077,mode==7 and 0x20 or 0,1)
   set(0x29c076,mode==8 and 0 or mode==9 and 0x10 or mode==10 and 0x20 or 0x30,1)
   set(0x3f8b5b,mode==11 and 0 or n%2==0 and 1 or 5,1)
   set(0x3f8d66,mode==12 and 0 or mode==13 and 0xffff or mode==14 and 0x8000 or 20,2)
   set(0x3f8d4a,0,2)
   set(0x3f8e7f,({0,24,25,0x7f})[(n//4)%4+1],1)
   for i=1,127 do set(0x3f8e7f+i,140,1)end
   set(0x3f8ee0,n%2==0 and 0 or 1,1);set(0x3f8ee1,141,1)
   set(0x3f8902,1,1);set(0x3f8906,0,1);set(0x3f8908,n%2==0 and 0 or 0x3f,2)
   local count=({0,0xbf,0xc0,0xc1})[n%4+1]
   set(0x2a3cf0,count,2);set(0x2a3c04,0x3fe800-count*16,4)
  end
  if companion_directed then
   -- Initial-state fixture only; the real ROM executes all sprite/table/chip work.
   companion_saved={}
   local function set(a,v,size)
    for i=0,size-1 do if companion_saved[a+i]==nil then companion_saved[a+i]=space:read_u8(a+i)end end
    if size==1 then space:write_u8(a,v&255)elseif size==2 then space:write_u16(a,v&65535)else space:write_u32(a,v&0xffffffff)end
   end
   local n=k//8
   main.state.A5.value=0x3ff400;main.state.A6.value=0xe4000
   main.state.D0.value=0x12345678;main.state.D1.value=0x89abcdef
   for a=0x3ff380,0x3ff4ff do set(a,0,1)end
   for a=0x3fe800,0x3fe83f do set(a,0,1)end
   for _,a in ipairs({0x3ff380,0x3ff3c0,0x3ff400})do
    set(a,0xfcf0,2);set(a+4,0x7fff8000,4);set(a+8,0x12345678,4)
    set(a+42,({0,0x7fff,0x8000,0xffff})[n%4+1],2)
    set(a+44,({0xffff,0x8000,0x7fff,0})[n%4+1],2)
    set(a+46,n%4,2);set(a+48,({0xfffb,0xffff,0,1,5})[n%5+1],2)
    set(a+58,({0,0x7f,0x80,0xfe,0xff})[n%5+1],1)
   end
   local mode=n%12
   set(0x29c09c,mode==0 and 15 or n%2==0 and 1 or 3,2)
   set(0x29c200,mode==1 and 0 or 0x10,1)
   set(0x29c09a,mode==2 and 1 or 0,2)
   set(0x3f91d0,mode==3 and 0x10 or 0,1)
   set(0x3f8d78,mode==4 and 0 or mode==5 and 0xffff or mode==6 and 0x8000 or n%2+1,2)
   set(0x3f8d76,mode==7 and 4 or 0,2)
   set(0x3f8d4e,({0,0x7fff,0x8000,0xffe0,32})[n%5+1],2)
   set(0x3f8d46,0x7fff,2);set(0x3f8d48,0x8000,2)
   local count=({0,0xbf,0xc0,0xc1})[(n//2)%4+1]
   set(0x2a3cf0,count,2);set(0x2a3c04,0x3fe800-count*16,4)
  end
  if flight_directed then
   -- Explicit initial-state fixture, executed by the real ROM at IPL7.
   flight_saved={}
   local function set(a,v,size)
    for i=0,size-1 do if flight_saved[a+i]==nil then flight_saved[a+i]=space:read_u8(a+i)end end
    if size==1 then space:write_u8(a,v&255)elseif size==2 then space:write_u16(a,v&65535)else space:write_u32(a,v&0xffffffff)end
   end
   main.state.A5.value=0x3ffc00;main.state.A6.value=0xe4000
   for a=0x3ffc00,0x3ffcff do set(a,0,1)end
   for a=0x3fe800,0x3fe83f do set(a,0,1)end
   for a=0x3f9200,0x3f9303 do set(a,0,1)end
   for a=0x3f8d14,0x3f8d77 do set(a,0,1)end
   set(0x3ffc00,0x80|((k&1)==0 and 0x10 or 0),1)
   set(0x3ffc02,0x7f,1);set(0x3ffc03,0xf2,1);set(0x3ffc0c,0x100a0,4);set(0x3ffc14,131,1)
   set(0x3ffc06,0x70,2);set(0x3ffc1e,0x400,2);set(0x3ffc24,0x1000,2)
   set(0x3ffc30,({0,32,0xffe0})[(k//7)%3+1],2)
   local delta={0xfffb,0xfffc,0xfffd,0,3,4,5}
   set(0x29c0ce,(delta[k%7+1]*4)&0xffff,2)
   set(0x29c0d0,(delta[(k//7)%7+1]*4)&0xffff,2)
   set(0x29c0b4,({0,50,0xffce})[(k//3)%3+1],2)
   set(0x29c096,({0,32,0xffe0,96,0xffa0,256,0xff00})[(k//5)%7+1],2)
   set(0x29c098,0,2);set(0x29c080,0x70,2);set(0x29c088,0,2);set(0x29c08a,0,2)
   set(0x29c09e,({0,0x40,0x80,0xc0,0xff})[k%5+1],2)
   set(0x29c0aa,({0x11f,0x120,0x121,0x21f,0x220,0x221,0x300})[k%7+1],2)
   set(0x3f8d5a,({0,1,31,32,0xffff})[k%5+1],2)
   set(0x3f91fe,({0,0xfc,0xfffc})[k%3+1],2)
   for a=0x3f89da,0x3f89e0,2 do set(a,0,2)end
   local mode=k<32 and k%10 or 9
   set(0x3f90fe,mode==0 and 1 or 0,1)
   set(0x29c09a,mode==1 and 1 or 0,2)
   set(0x3f8d76,mode==4 and 1 or mode==5 and 0xffff or (mode==9 and (k//8)%2==0)and 5 or 0,2)
   set(0x3f8d60,(mode==2 or mode==3)and 1 or 0,1)
   set(0x3f89e3,mode==3 and 1 or 0,1)
   set(0x29c09c,mode==6 and 15 or 1,2)
   set(0x29c200,mode==7 and 0 or 0x10,1)
   set(0x3f91d0,mode==8 and 0x10 or 0,1)
   local count=({0,0xbf,0xc0,0xc1})[k%4+1]
   set(0x2a3cf0,count,2);set(0x2a3c04,0x3fe800-count*16,4)
   for _,a in ipairs({0x29c33a})do set(a,0,2)end
  end
  if input_directed then
   -- Labelled initial raw input/accumulator fixtures; physical button fields
   -- feed the production endpoint. Natural samples come from the ADC timeline.
   input_saved={bytes={},buttons={}}
   local function set(a,v,size)
    for i=0,size-1 do if input_saved.bytes[a+i]==nil then input_saved.bytes[a+i]=space:read_u8(a+i)end end
    if size==1 then space:write_u8(a,v&255)elseif size==2 then space:write_u16(a,v&65535)else space:write_u32(a,v&0xffffffff)end
   end
   main.state.A5.value=0x3ffc00;main.state.A6.value=0xe4000
   main.state.D0.value=0x12345678;main.state.D1.value=0x89abcdef
   for a=0x3ffc00,0x3ffc3f do set(a,0,1)end
   for a=0x29c076,0x29c077 do set(a,k&0xff,1)end
   local xs={0,0x20,0x24,0x25,0x26,0x7d,0x7e,0x80,0x82,0x83,0xda,0xdb,0xdc,0xe0,0xfe,0xff}
   local ys={0,0x3f,0x40,0x41,0x7d,0x7e,0x80,0x82,0x83,0xbf,0xc0,0xc1,0xff}
   local ts={0,0x3f,0x40,0x41,0x80,0xaf,0xb0,0xb1,0xff}
   set(0x3f8892,xs[k%#xs+1],1);set(0x3f8893,ys[k%#ys+1],1);set(0x3f8894,ts[k%#ts+1],1)
   set(0x3ffc16,0x80,2);set(0x3ffc1a,0x80,2)
   set(0x3ffc18,({0,1,2,3,0xffff,0xfffe,0xfffd,0x7fff,0x8000})[(k//16)%9+1],2)
   set(0x3ffc1c,({0,1,2,3,0xffff,0xfffe,0xfffd,0x7fff,0x8000})[(k//13)%9+1],2)
   if k>=128 then
    set(0x3f8892,0x80,1);set(0x3f8893,0x80,1)
    set(0x3ffc18,({0,1,2,3,0xffff,0xfffe,0xfffd,0x7fff,0x8000})[k%9+1],2)
    set(0x3ffc1c,({0,1,2,3,0xffff,0xfffe,0xfffd,0x7fff,0x8000})[k%9+1],2)
   end
   local mode=k>=128 and 0 or (k//16)%8
   set(0x3f91c4,mode==1 and 1 or mode==2 and 3 or 0,1)
   set(0x3f91c5,({0,0x40,0x80,0xc0,0xff})[k%5+1],1)
   set(0x3f90fe,mode==3 and 1 or mode==4 and 4 or 0,1)
   set(0x3f8d36,mode==5 and 1 or 0,1);set(0x3f8d38,(k&1)==0 and 0x8000 or 0x7fff,2)
   set(0x29c09a,mode==6 and 1 or 0,2)
   set(0x29c09c,(k//5)%3==0 and 1 or (k//5)%3==1 and 3 or 16,2)
   set(0x29c0cc,mode==7 and 0x1234 or 0,2)
   set(0x29c0ce,0x1111,2);set(0x29c0d0,0x2222,2);set(0x29c0d2,0x3333,2)
   set(0x3f8070,k%2==0 and 4 or 0,1)
   set(0x3f8820,k%4,2);set(0x3fe000,({0,0x2ee,0x2ef,0x348,0x349})[k%5+1],2)
   local port=ports[":mainpcb:IO1PORTA"];local previous=port:read()
   for i,name in ipairs({"Vulcan","Missile"})do
    local field=port.fields[name]
    input_saved.buttons[#input_saved.buttons+1]={field,(previous&field.mask)==0 and 1 or 0}
    field:set_value((k>>(i-1))&1)
   end
  end
  if soundproducer_directed then
   -- Labelled ring frontier; execute the original producer in MAME.
   sound_saved={ram=space:read_range(0x3f8902,0x3f894b,8),stage=space:read_u16(0x29c09c)}
   space:write_u8(0x3f8902,k%4==0 and 0 or 0xff)
   space:write_u16(0x29c09c,k%4==1 and 1 or 3)
   space:write_u16(0x3f8908,({0,1,62,63})[k%4+1])
   space:write_u8(0x3f8906,({0,0x7f,0xff,1})[(k//4)%4+1])
   main.state.D0.value=({0,0x7f,0x80,0xff})[(k//4)%4+1]
  end
  if follower_directed then
   -- Labelled initial-state fixture: real ROM execution, never a handler replay.
   main.state.A5.value=0x3ffc00
   space:write_u32(0x3ffbc8,0x12345678)
   space:write_u8(0x3ffbc2,0x80|(k&0x7f))
   space:write_u16(0x3ffbc4,0x8000+k)
   space:write_u16(0x3ffbc6,0x7fff-k)
   space:write_u8(0x3ffc15,({0,4,8,0x80})[k%4+1])
   space:write_u8(0x3ffc14,0x30)
   space:write_u16(0x29c09c,({1,7,2})[(k//4)%3+1])
   space:write_u16(0x2a3cf0,k%2==0 and 0 or 0xc0)
  end
  if ready_directed then
   main.state.A5.value=0x3ffc00
   space:write_u32(0x3ffc04,({0,0x12345678,0x80000000,0xffffffff})[k%4+1])
   space:write_u8(0x29c200,k%2==0 and 0xfe or 0xff)
   space:write_u16(0x2a3cf0,(k//2)%2==0 and 0 or 0xc0)
  end
  if trail_directed then
   -- Scratch object and exact scalar fixtures; all branches execute in ROM.
   main.state.A5.value=0x3ffc00;main.state.A6.value=0xe4000
   follower_saved.pool=space:read_range(0x3f8e7f,0x3f8f7f,8)
   follower_saved.shared={}
   for _,a in ipairs({0x29c0ac,0x29c0c4,0x29c080,0x29c090,0x29c092,0x29c088,0x29c08a})do follower_saved.shared[a]=space:read_u16(a)end
   space:write_u32(0x3ffc04,0x12345678);space:write_u8(0x3ffc00,0x80)
   space:write_u8(0x3ffc14,200)
   space:write_u16(0x3ffc24,k%8==0 and 0 or 0x1000)
   space:write_u16(0x3ffc32,({0,7,8,0xffff})[k%4+1])
   space:write_u16(0x3ffc2a,({0,0x7fff,0x8000,0})[(k//4)%4+1])
   space:write_u16(0x3ffc2c,({0x70,0x8000,0x7fff,0})[k%4+1])
   space:write_u16(0x29c0ac,0x20);space:write_u16(0x29c0c4,k%8<4 and 0 or 0x7fff)
   space:write_u16(0x29c080,0x70);space:write_u16(0x29c090,0);space:write_u16(0x29c092,0x100)
   space:write_u16(0x29c088,0);space:write_u16(0x29c08a,0)
   space:write_u8(0x3f8e7f,k%2==0 and 0xff or 0)
   space:write_u16(0x2a3cf0,k%2==0 and 0 or 0xc0)
   space:write_u16(0x29c09c,16)
  end
  if worldinit_directed then
   main.state.A5.value=0x3ffc00;main.state.A6.value=0xe4000
   -- Directed object/offset operands; chip reads execute in the real ROM.
   space:write_u32(0x3ffc36,0x3ffbc0)
   space:write_u32(0x3ffbc0,({0,0x7fffffff,0x80000000,0xffffffff})[k%4+1])
   space:write_u32(0x3ffbc4,({0x80000000,0,0xffffffff,0x7fffffff})[k%4+1])
   space:write_u16(0x3ffc2a,(k*0x1800)&0xffff)
  end
  main.state.SR.value=saved.sr|0x2000;main.state.PC.value=0x3ffd00
  guard=false;shared_guard.active=false
  emu.wait(host_directed and 0.004 or 0.002)
  if closing then return end
  assert(not entry and not handler and (main.state.SR.value&0x700)~=0x400 and (main.state.SR.value&0x700)~=0x600,"fixture ended during IRQ service")
  guard=true;shared_guard.active=true;out:write(string.char(3)) -- fixture restore cuts the CPU schedule
  if active or pending then cuts=cuts+1 end;active=nil;pending=nil;resume_check=false
  if host_saved then for a,v in pairs(host_saved)do space:write_u8(a,v)end end
  if weapon_saved then for a,v in pairs(weapon_saved)do space:write_u8(a,v)end end
  if companion_saved then for a,v in pairs(companion_saved)do space:write_u8(a,v)end end
  if flight_saved then for a,v in pairs(flight_saved)do space:write_u8(a,v)end end
  if input_saved then
   for a,v in pairs(input_saved.bytes)do space:write_u8(a,v)end
   for _,v in ipairs(input_saved.buttons)do v[1]:set_value(v[2])end
  end
  if sound_saved then
   for i=1,#sound_saved.ram do space:write_u8(0x3f8901+i,sound_saved.ram:byte(i))end
   space:write_u16(0x29c09c,sound_saved.stage)
  end
  if follower_saved then
   if follower_saved.pool then
    for i=1,#follower_saved.pool do space:write_u8(0x3f8e7e+i,follower_saved.pool:byte(i))end
    for a,v in pairs(follower_saved.shared)do space:write_u16(a,v)end
   end
   for i=1,#follower_saved.ram do space:write_u8(0x3ffbc0+i-1,follower_saved.ram:byte(i))end
   space:write_u16(0x29c09c,follower_saved.stage)
   space:write_u16(0x2a3cf0,follower_saved.count)
   space:write_u8(0x29c200,follower_saved.ready)
  end
  for i=0,7 do main.state["D"..i].value=saved.d[i]end
  for i=0,6 do main.state["A"..i].value=saved.a[i]end
  main.state.SP.value=saved.ssp;main.state.USP.value=saved.usp;main.state.PC.value=0x3ffd40
  guard=false;shared_guard.active=false
 end)
 assert(coroutine.resume(co))
end
caller_frames=emu.add_machine_frame_notifier(function()
 frames=frames+1
 visibility_record(2,0,0,0,0,ns())
 if (host_directed or weapon_directed or companion_directed or input_directed or flight_directed) and frames>=32 and frames+2<tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then fixture(frames-32)
 elseif not host_directed and not weapon_directed and not companion_directed and not input_directed and not flight_directed and directed and frames>=32 and frames%4==0 then fixture(frames//4)end
 if frames>=tonumber(os.getenv("AB2_ORACLE_PASSES") or "600") then
  if linked_irq4 then closing=true else assert(not handler,"unfinished caller service");close_capture()end
 end
end)

if linked_irq4 then
 finish_companion,companion_idle=assert(loadfile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")).."/capture_irq4_service.lua"))(os.getenv("AB2_ORACLE_OUT")..".irq4",shared_guard)
end
