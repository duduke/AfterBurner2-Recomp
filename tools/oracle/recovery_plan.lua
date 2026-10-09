-- Independent original-ROM fixtures for the reported gameplay continuations.
-- The manifest declares PCs; MAME's ROM classifies DBF counters independently.
-- Every CCR pattern is exercised at branches, two extremes elsewhere.
local meta=dofile(assert(os.getenv('AB2_ORACLE_SCRIPT_DIR'))..'/recovery_schedule.lua')
local pcs={};for pc in pairs(meta)do pcs[#pcs+1]=pc end;table.sort(pcs)
local space=manager.machine.devices[':mainpcb:maincpu'].spaces.program
local cases={}
for _,pc in ipairs(pcs)do
 local op=space:read_u16(pc)
 local branch=(op&0xf000==0x6000 and (op>>8)&15>=2) or op&0xfff8==0x51c8
 local flags=branch and 32 or 2
 for n=0,flags-1 do
  local c={cpu=1,pc=pc,sr=0x2700|(branch and n or n*31),d0=0x12345678,d1=17,d2=0x8001}
  -- $7ED8 clears D1 and $7EF0 increments it by two: its word-table
  -- offset is even. Odd indexed word accesses enter the separate address-
  -- error exception, which is outside these normal instruction fixtures.
  if pc==0x7efa then c.d1=18 end
  -- $922/$924 double D1 twice before this longword-table load.
  if pc==0x926 or pc==0x53b4 or pc==0x17b0 then c.d1=16 end
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
  if op&0xfff8==0x51c8 then c.counter_reg=op&7;c.counter_value=n&1 end
  cases[#cases+1]=c
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
 -- ASL.W 46(A5): sign changes, carry/X, overflow and zero result.
 if op==0xe1ed then
  for _,value in ipairs({0x4000,0x8000,0xc000,0xffff})do
   for _,ccr in ipairs({0,31})do cases[#cases+1]={cpu=1,pc=pc,sr=0x2700|ccr,d0=0,d1=0,memory_word_address=0x3ffc2e,memory_word_value=value}end
  end
 end
end
return cases
