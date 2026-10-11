-- Independent original-ROM fixtures for the reported gameplay continuations.
-- The manifest declares PCs; MAME's ROM classifies DBF counters independently.
-- Every CCR pattern is exercised at branches, two extremes elsewhere.
local meta=dofile(assert(os.getenv('AB2_ORACLE_SCRIPT_DIR'))..'/recovery_schedule.lua')
local pcs={};for pc in pairs(meta)do pcs[#pcs+1]=pc end;table.sort(pcs)
-- Optional focused capture for new ports. Full recovery acceptance still
-- requires the complete manifest union and all branch outcomes.
local filter_path=os.getenv('AB2_RECOVERY_PC_FILTER')
if filter_path then
 local selected={}
 for line in io.lines(filter_path)do
  if line~='' and line:sub(1,1)~='#' then
   local pc=assert(tonumber(line,16),'Invalid focused recovery PC')
   assert(meta[pc],'Focused PC absent from recovery manifest');selected[pc]=true
  end
 end
 local focused={};for _,pc in ipairs(pcs)do if selected[pc]then focused[#focused+1]=pc end end
 assert(#focused>0,'Empty focused recovery selection');pcs=focused
end
local space=manager.machine.devices[':mainpcb:maincpu'].spaces.program
local cases={}
for _,pc in ipairs(pcs)do
 local op=space:read_u16(pc)
 local branch=(op&0xf000==0x6000 and (op>>8)&15>=2) or op&0xf0f8==0x50c8
 local flags=branch and 32 or 2
 for n=0,flags-1 do
  local c={cpu=1,pc=pc,sr=0x2700|(branch and n or n*31),d0=0x12345678,d1=17,d2=0x8001}
  -- $7ED8 clears D1 and $7EF0 increments it by two: its word-table
  -- offset is even. Odd indexed word accesses enter the separate address-
  -- error exception, which is outside these normal instruction fixtures.
  if pc==0x7efa then c.d1=18 end
  -- $922/$924 double D1 twice before this longword-table load.
  if pc==0x926 or pc==0x53b4 or pc==0x17b0 then c.d1=16 end
  if pc==0xc4e then c.d1=16 end
  -- $1204/$1206/$1208 shift and double the longword-table offset.
  -- Match that producer's aligned index; odd-address exception execution
  -- is outside these normal instruction fixtures.
  if pc==0x120a then c.d1=16 end
  -- $4782/$4790 select in steps of four; $479C..$47B6 move the
  -- object's table offset in steps of +/-4 before $4806 loads it.
  if pc==0x4810 then c.d1=16 end
  if pc==0xc60 then c.d0=16 end
  if pc==0x12a12 then c.d1=12 end
  if pc==0x16b4c or pc==0x25c8 or pc==0x8ada or pc==0x11e60 or pc==0x12084 or pc==0x130b2 then c.d1=16 end
  -- $13C9E doubles the stage index for both word tables.
  if pc==0x13cac or pc==0x13cb4 then c.d2=18 end
  -- Crash debris indices are shifted left six at $E778/$E77A before
  -- indexed longword writes. The normal path cannot have an odd index.
  if pc==0xe7a2 or pc==0xe7cc then c.d1=64 end
  if pc==0xe7aa or pc==0xe7d0 then c.d2=64 end
  -- $F578/$F57A and $F630/$F632 double the longword table index twice.
  if pc==0xf582 or pc==0xf634 then c.d1=16 end
  -- $FA90 masks the ring-buffer word offset to $E.
  if pc==0xfa9a then c.d1=14 end
  if pc==0x19c78 then c.memory_word_address=0x3f89ea;c.memory_word_value=0x005a end
  if pc==0x19cb4 then c.memory_word_address=0x3f910c;c.memory_word_value=0 end
  if op&0xf0f8==0x50c8 then c.counter_reg=op&7;c.counter_value=n&1 end
  cases[#cases+1]=c
 end
 -- Ground award queue countdown and queue length: wrap/carry, zero and sign
 -- boundaries, preserving the adjacent queue byte in each direction.
 if pc==0xe570 or pc==0xe5aa then
  for _,value in ipairs({0,1,0x7f,0x80,0xff})do
   for _,ccr in ipairs({0,31})do
    local word=pc==0xe570 and ((value<<8)|0x5a) or (0x5a00|value)
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,
     memory_word_address=0x3f904e,memory_word_value=word}
   end
  end
 end
 -- $BC22 masks the cyclic attract selector to $C, yielding exactly the
 -- four branch-table offsets consumed by $BC2A. Keep generic cases and
 -- independently capture all valid targets and masked input bits.
 if pc==0xbc22 or pc==0xbc2a then
  local values=pc==0xbc22 and {0,4,8,12,0xffff} or {0,4,8,12}
  for _,value in ipairs(values)do
   for _,ccr in ipairs({0,31})do
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12340000|value,d1=17}
   end
  end
 end
 -- Preserve the neighbouring condition byte and cover both selected bits.
 if pc==0x11116 or pc==0x11aa8 or pc==0x2e2e then
  local address=pc==0x2e2e and 0x3f89ce or 0x3f89e2
  for _,value in ipairs({0x5a00,0x5a01,0x5a02,0x5a03,0x8000,0xffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,memory_word_address=address,memory_word_value=value}end
  end
 end
 -- Stage-entry HUD bit flags, countdown wrap/sign/zero and adjacent bytes.
 local stage_address=pc==0xd914 and 0x3f8b5b or pc==0xd948 and 0x29c202
  or pc==0xd954 and 0x29c201 or pc==0xdd58 and 0x3f912c or pc==0xdd62 and 0x3f9128
  or pc==0x8098 and 0x3f8c51 or pc==0x161b8 and 0x29c200
 if stage_address then
  local values=pc==0xdd58 and {0,1,0x7fff,0x8000,0xffff} or {0x005a,0x015a,0x805a,0xff5a}
  for _,value in ipairs(values)do
   for _,ccr in ipairs({0,31})do
    local address=stage_address
    local word=value
    if address&1~=0 then address=address-1;word=0x5a00|(value>>8) end
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,
     memory_word_address=address,memory_word_value=word}
   end
  end
 end
 if pc==0x8352 then
  for _,value in ipairs({0,1,0x7fff,0x8000,0xffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,
    memory_word_address=0x3f8d1a,memory_word_value=value}end
  end
 end
 -- Motion stepping and byte-only tile formatting preserve register highs.
 if pc==0xdc90 or pc==0xdc94 or pc==0xe316 then
  for _,value in ipairs({0x12340000,0x12340001,0x12347fff,0x12348000,0x1234ffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=value,d1=17}end
  end
 end
 if pc==0xd91a or pc==0xd91e or pc==0xd926 or pc==0xe314 then
  for _,value in ipairs({0x12340000,0x12340008,0x12340080,0x1234ffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=value,d1=value}end
  end
 end
 -- $096E object-callback countdowns and orientation flags. Keep byte
 -- neighbours and test signed boundaries independently of the live sweep.
 local callback_address=pc==0xa2c and 0x3ffc32 or pc==0xbbe and 0x3ffc1c
  or pc==0xc16 and 0x3ffc00 or pc==0xc22 and 0x3ffc00 or pc==0xc32 and 0x3ffc00
 if callback_address then
  local values=pc==0xbbe and {0,1,0x7fff,0x8000,0xffff} or {0x005a,0x105a,0x205a,0x805a,0xff5a}
  for _,value in ipairs(values)do
   for _,ccr in ipairs({0,31})do
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,
     memory_word_address=callback_address,memory_word_value=value}
   end
  end
 end
 -- Enemy effect activation depends on the original unsigned speed limit.
 if pc==0x1316 then
  for _,value in ipairs({0,0x21f,0x220,0x221,0x7fff,0x8000,0xffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,memory_word_address=0x29c0aa,memory_word_value=value}end
  end
 end
 -- Enemy-program activation threshold and held-duration wrap boundaries.
 if pc==0x3420 or pc==0x33ea then
  for _,value in ipairs({0,0x1d,0x1e,0x1f,0x7fff,0x8000,0xffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,memory_word_address=0x3ffc22,memory_word_value=value}end
  end
 end
 -- Carrier signed products: normal and alternating Booth transitions,
 -- signed extrema, zero and register-high preservation across all CCRs.
 if pc==0x13086 or pc==0x13206 then
  for _,value in ipairs({0,1,0xffff,0x8000,0x7fff,0x5555,0xaaaa})do
   for ccr=0,31 do
    local c={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17}
    if pc==0x13086 then c.d1=0x12340000|value
    else c.memory_word_address=0x3ffc20;c.memory_word_value=value end
    cases[#cases+1]=c
   end
  end
 end
 if pc==0x13206 then
  for _,value in ipairs({0,1,0xffff,0x8000,0x7fff,0x5555,0xaaaa})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12340000|value,d1=17,memory_word_address=0x3ffc20,memory_word_value=0xffff}end
  end
 end
 -- The original scripted-flight divider has a fixed positive divisor 3.
 -- Test zero quotient, signed quotient/remainder, both late boundaries,
 -- early magnitude overflow and all initial CCRs; no divisor is replaced.
 if pc==0x16ada then
  for _,value in ipairs({0,1,2,3,4,0x17ffd,0x17ffe,0x17fff,0x18000,
    0xffffffff,0xfffffffe,0xfffffffd,0xfffffffc,0xfffe8000,0xfffe7fff,
    0xfffe7ffe,0xfffe7ffd,0x80000000,0x7fffffff})do
   for ccr=0,31 do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=value,d1=17}end
  end
 end
 -- Indicator countdown byte/word boundaries and adjacent mode thresholds.
 if pc==0x19c78 or pc==0x19cb4 or pc==0x19c80 or pc==0x19c8c then
  local address=pc==0x19c78 and 0x3f89ea or pc==0x19cb4 and 0x3f910c or 0x29c09c
  local values=pc==0x19c78 and {0x015a,0x805a,0xff5a} or pc==0x19cb4 and {1,0x8000,0xffff} or {0x0f,0x10,0x11,0x15,0x16,0x17}
  for _,value in ipairs(values)do
   for _,ccr in ipairs({0,31})do
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,memory_word_address=address,memory_word_value=value}
   end
  end
 end
 -- Orientation BSET must preserve unrelated bits and report the old bit in Z.
 if pc==0x7784 then
  for _,value in ipairs({0x10a5,0xefa5})do
   for _,ccr in ipairs({0,31})do
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,memory_word_address=0x3ffc00,memory_word_value=value}
   end
  end
 end
 if pc==0x5382 or pc==0x178c then
  for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=120}end
 end
 if pc==0x52cc or pc==0x530c then
  for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d1=0x80000200,counter_reg=5,counter_value=10}end
 end
 if pc==0x1388 then
  for _,value in ipairs({0x12340080,0x12340041})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,counter_reg=6,counter_value=value}end
  end
 end
 if pc==0x13de or pc==0x13ea then
  for _,value in ipairs({0,0xffff,0x7fff,0x8000})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d2=value}end
  end
 end
 -- TAS preserves the register's high bytes and derives NZ from its old byte.
 if op&0xfff8==0x4ac0 then
  for _,value in ipairs({0x80,0x1234007f,0x123400ff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,counter_reg=op&7,counter_value=value}end
  end
 end
 -- Memory TAS derives NZ from the old byte and preserves its word neighbour.
 if op&0xfff8==0x4ad0 or op&0xfff8==0x4ae8 then
  local address=0x3ffc00
  if op&0xfff8==0x4ae8 then local displacement=space:read_u16(pc+2);if displacement&0x8000~=0 then displacement=displacement-0x10000 end;address=address+displacement end
  for _,value in ipairs({0x80a5,0x7fa5,0xffa5})do
   for _,ccr in ipairs({0,31})do
    cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0x12345678,d1=17,memory_word_address=address,memory_word_value=value}
   end
  end
 end
 -- ASL.W 46(A5): sign changes, carry/X, overflow and zero result.
 if op==0xe1ed then
  for _,value in ipairs({0x4000,0x8000,0xc000,0xffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0,d1=0,memory_word_address=0x3ffc2e,memory_word_value=value}end
  end
 end
end
return cases
