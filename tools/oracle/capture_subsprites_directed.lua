-- MAME autoboot script: record the sub CPU's sprite pass ($000640) as
-- before/after snapshots of the 32KB block of RAM shared with the main CPU.
--
-- A pass starts when the sub CPU, spinning at $000640 on subram0[$D6] bit 0,
-- reads it clear (main CPU VBLANK has released it). It ends when the sub CPU
-- sets subram0[$D6] bit 1 at $000458. Each record is
--   u32 pass, u32 frame, 32768 bytes before, 32768 bytes after
-- where the 32KB block is the sub CPU's A6-relative view ($09C000-$0A3FFF:
-- subram0 then subram1), read through the main CPU's alias ($29C000-$2A3FFF)
-- so the snapshot itself doesn't trip the taps below.
--
-- Environment: AB2_ORACLE_OUT (output file), AB2_ORACLE_PASSES (stop after N).

local out_path = os.getenv("AB2_ORACLE_OUT") or "subsprites.bin"
local max_passes = tonumber(os.getenv("AB2_ORACLE_PASSES") or "600")
local skip = tonumber(os.getenv("AB2_ORACLE_SKIP") or "0")
if os.getenv("AB2_ORACLE_GAMEPLAY") == "1" then
  dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")) .. "/gameplay.lua")
end

local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local main_space = main.spaces["program"]
local sub_space = sub.spaces["program"]
local sub_pc = sub.state["CURPC"]

local out = assert(io.open(out_path, "wb"))
out:write("AB2SPR02")

local function u32(v)
  return string.char((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff)
end

local function snapshot()
  return main_space:read_range(0x29C000, 0x2A3FFF, 8)
end

local frame, passes = 0, 0
local before = nil
local written,other={},{}
local guard=false
local function map(a)
 a=a&0xfffff
 if a>=0x80000 and a<0xc0000 then return (a<0xa0000 and 0 or 0x4000)+(a&0x3fff) end
end
local function marks(t)
 local p={};for i=0,0x7fff do p[#p+1]=string.char(t[i] and 1 or 0) end;return table.concat(p)
end
local function mark(t,a,mask)
 local o=map(a);if not o then return end
 if mask&0xff00~=0 then t[o]=true end;if mask&0xff~=0 then t[o+1]=true end
end

-- Keep handler references alive: a collected tap is uninstalled.
frame_sub = emu.add_machine_frame_notifier(function() frame = frame + 1 end)

pass_start = sub_space:install_read_tap(0x9C0D6, 0x9C0D7, "ab2_pass_start", function(offset, data, mask)
  if frame >= skip and before == nil and sub_pc.value == 0x000640 and ((data >> 8) & 1) == 0 then
    if os.getenv("AB2_ORACLE_SPRITES_DIRECTED")=="1" then
      guard=true
      local n=passes%8
      local req=sub_space:read_u32(0x9c000+0x7c08)&0xfffff
      local count=sub_space:read_u32(0x9c000+0x7c10)&0xfffff
      local number=n==0 and 0 or n==1 and 1 or 40
      sub_space:write_u16(count,number)
      sub_space:write_u8(0x9c03a,n==1 and 1 or 0)
      for i=0,number-1 do
        local a=req+i*16
        sub_space:write_u16(a,n==2 and 0 or n==3 and 0x3ff or 0x7f)
        sub_space:write_u8(a+2,n==4 and 0xff or 0x80)
        sub_space:write_u8(a+3,0xf2)
        sub_space:write_u16(a+4,n==5 and 0xfe00 or 0)
        sub_space:write_u16(a+6,n==3 and 100 or n==6 and 0xffce or 112)
        sub_space:write_u16(a+8,10);sub_space:write_u16(a+10,120)
        sub_space:write_u32(a+12,(i<<24)|0x100f0)
      end
      guard=false
    end
    written,other={},{}
    before = snapshot()
  end
end)

pass_end = sub_space:install_write_tap(0x9C0D6, 0x9C0D7, "ab2_pass_end", function(offset, data, mask)
  if before ~= nil and sub_pc.value == 0x000458 then
    out:write(u32(passes), u32(frame), before, snapshot(),marks(written),marks(other))
    before = nil
    passes = passes + 1
    if passes >= max_passes then
      out:close()
      print(string.format("captured %d sprite passes through frame %d -> %s", passes, frame, out_path))
      manager.machine:exit()
    end
  end
end)

-- CPU-owned write sets; exception stack writes excluded by address.
sprite_writes=sub_space:install_write_tap(0x80000,0xbffff,"sprite_writes",function(a,d,m)
 if guard or not before or sub_pc.value<0x648 or sub_pc.value>0x9e2 then return end
 local o=map(a);if o and o>=0x7e00 then return end
 mark(written,a,m)
end)
sprite_other=main_space:install_write_tap(0x280000,0x2bffff,"sprite_other",function(a,d,m)
 if guard or not before then return end;mark(other,a,m)
end)
