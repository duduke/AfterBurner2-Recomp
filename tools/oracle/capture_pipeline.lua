-- MAME autoboot script: record rendered frames together with the video state
-- they were drawn from, for tests/oracle_video.cpp.
--
-- Most of what the X-Board video hardware draws from isn't live CPU-visible
-- RAM, so each piece is snapshotted at the moment the hardware takes it:
--   sprites  - sprite RAM at the main CPU's draw-trigger write
--              ($110000-$11FFFF), just before the hardware swaps it in
--   road     - CPU-visible road RAM just before the sub CPU flips it in
--              (its write at $000458), committed at its control write at
--              $000466, plus the control byte written there
--   tilemap  - page-select/scroll registers (text RAM $E80-$E9F) as they
--              were at scanline 261 of the previous frame, when the hardware
--              latches them (computed from emulated time: 64us per line)
--   tile RAM, text RAM - live at the end of the frame
--   palette  - live when MAME converts the frame to RGB (see below)
--
-- Record ("AB2PIPE1"): u32 frame, 286720 bytes MAME pixels (320x224 ARGB,
-- little-endian), 4096 sprites, 4096 road, u16 road control, 32 latched
-- tilemap registers, 16384 palette, 65536 tile RAM, 4096 text RAM.
-- AB2PAIR3 appends the displayed road pass: five (producer, write ordinal)
-- pairs, before shared/road/math state, after shared/road state and CPU-owned
-- shared/road write sets plus other-main writes. Producer $FFFFFFFF marks
-- an explicit initial/uncaptured word. The main dispatcher can write targets
-- before publishing its sprite list, so the five producers need not match.
--
-- Environment: AB2_ORACLE_OUT, AB2_ORACLE_PASSES (records), AB2_ORACLE_STRIDE
-- (record every Nth frame, default 5), AB2_ORACLE_SKIP (warm-up frames, default 120).

local out_path = os.getenv("AB2_ORACLE_OUT") or "frames.bin"
local max_records = tonumber(os.getenv("AB2_ORACLE_PASSES") or "300")
local stride = tonumber(os.getenv("AB2_ORACLE_STRIDE") or "5")
local skip = tonumber(os.getenv("AB2_ORACLE_SKIP") or "120")
if os.getenv("AB2_ORACLE_GAMEPLAY") == "1" and not ab2_gameplay_loaded then
  ab2_gameplay_loaded=true
  dofile(assert(os.getenv("AB2_ORACLE_SCRIPT_DIR")) .. "/gameplay.lua")
end

local main = manager.machine.devices[":mainpcb:maincpu"]
local sub = manager.machine.devices[":mainpcb:subcpu"]
local main_space = main.spaces["program"]
local sub_space = sub.spaces["program"]
local sub_pc = sub.state["CURPC"]
local screen = manager.machine.screens[":mainpcb:screen"]

local LINE = 400 / 6250000            -- htotal / pixel clock
local LATCH_DELAY = (261 - 224) * LINE -- frame notifier runs at VBLANK start (line 224)

local out = assert(io.open(out_path, "wb"))
local paired=os.getenv("AB2_ORACLE_PAIRED")=="1"
local paired_road=os.getenv("AB2_ORACLE_PAIRED_ROAD")=="1"
assert(not paired_road or paired,"paired road requires paired producers")
out:write(paired_road and "AB2PAIR3" or paired and "AB2PAIR2" or "AB2PIPE1")

local function u32(v)
  return string.char((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff)
end
local function now() return manager.machine.time:as_double() end
local function regs() return main_space:read_range(0x0D0E80, 0x0D0E9F, 8) end

local pipeline_guard=false;ab2_capture_guard=false
local pipeline_by_sorted={}
local pass_before,pass_producer
local blit_before,blit_entry
local pipeline
local main_pc=main.state.CURPC
pipeline_sub_start=sub_space:install_read_tap(0x9c0d6,0x9c0d7,"pipeline_sub_start",function(a,d,m)
 if not pipeline_guard and not ab2_capture_guard and sub_pc.value==0x640 and ((d>>8)&1)==0 and not pass_before then
  pipeline_guard=true;ab2_capture_guard=true
  pass_before=main_space:read_range(0x29c000,0x2a3fff,8)
  if paired then pass_producer=(ab2_producers or {})[main_space:read_u32(0x2a3c08)&0x3fffff] end
  pipeline_guard=false;ab2_capture_guard=false
 end
end)
pipeline_sub_end=sub_space:install_write_tap(0x9c0d6,0x9c0d7,"pipeline_sub_end",function(a,d,m)
 if not ab2_capture_guard and sub_pc.value==0x458 and pass_before then
  ab2_capture_guard=true
  local ptr=main_space:read_u32(0x2a3c18)&0x3fffff
  pipeline_by_sorted[ptr]={ram=pass_before,producer=pass_producer,sizes=main_space:read_range(0x2a3800,0x2a3bff,8)};pass_before=nil;ab2_capture_guard=false
 end
end)
pipeline_blit=main_space:install_read_tap(0x2a3c14,0x2a3c17,"pipeline_blit",function(a,d,m)
 if not pipeline_guard and not ab2_capture_guard and a==0x2a3c14 and (main_pc.value==0xb7c2 or main_pc.value==0xb7d2) then
  pipeline_guard=true;ab2_capture_guard=true
  blit_entry=main_pc.value
  blit_before=main_space:read_range(0x29c000,0x2a3fff,8)..main_space:read_range(0x100000,0x100fff,8)
  pipeline_guard=false;ab2_capture_guard=false
 end
end)
local sprite_buffer = string.rep("\0", 4096)
local road_pending, road_display = string.rep("\0", 4096), string.rep("\0", 4096)
local road_control = 0
local road_active,road_completed,road_pending_context,road_display_context
local function mark(set,o,m)
  if m&0xff00~=0 then set[o]=true end
  if m&255~=0 then set[o+1]=true end
end
local function offsets(set)
  local list={};for o in pairs(set) do list[#list+1]=o end;table.sort(list)
  local parts={u32(#list)}
  for _,o in ipairs(list) do parts[#parts+1]=string.pack(">I2",o) end
  return table.concat(parts)
end
local function road_snapshot()
  return main_space:read_range(0x29c000,0x2a3fff,8),main_space:read_range(0x2ec000,0x2ecfff,8)
end
if paired_road then
  paired_road_shared=sub_space:install_write_tap(0x80000,0xbffff,"paired_road_shared",function(a,d,m)
    if not road_active or pipeline_guard or ab2_capture_guard then return end
    local p=sub_pc.value;local o=(a<0xa0000 and 0 or 0x4000)+(a&0x3ffe)
    if o>=0x7f00 or (p>=0x55e and p<0x594) then return end
    assert((p>=0x46a and p<0x538) or (p>=0xc0a and p<0x11d4) or (p>=0x16d4 and p<0x19e8),"foreign sub write in road window")
    mark(road_active.shared_w,o,m)
  end)
  paired_road_writes=sub_space:install_write_tap(0xec000,0xedfff,"paired_road_writes",function(a,d,m)
    if road_active and not pipeline_guard and not ab2_capture_guard then mark(road_active.road_w,a&0xffe,m) end
  end)
  paired_road_main=main_space:install_write_tap(0x280000,0x2bffff,"paired_road_main",function(a,d,m)
    if road_active and not pipeline_guard and not ab2_capture_guard then mark(road_active.other,(a<0x2a0000 and 0 or 0x4000)+(a&0x3ffe),m) end
  end)
  paired_road_end=sub_space:install_read_tap(0x9c54a,0x9c54b,"paired_road_end",function(a,d,m)
    if road_active and not pipeline_guard and not ab2_capture_guard and sub_pc.value==0x530 then
      pipeline_guard=true;ab2_capture_guard=true
      local shared,road=road_snapshot();local q=road_active
      road_completed=q.sources..q.before..q.road..q.chips..shared..road..offsets(q.shared_w)..offsets(q.road_w)..offsets(q.other)
      road_active=nil;pipeline_guard=false;ab2_capture_guard=false
    end
  end)
end
local latched, latched_next = regs(), nil
local latch_at, latch_pending = 0, false

-- AB2_ORACLE_NO_SPRITES=1 empties every sprite list just before the hardware
-- takes it, so MAME renders road + tilemaps alone (layer isolation; it only
-- changes display state, not game logic).
local no_sprites = os.getenv("AB2_ORACLE_NO_SPRITES") == "1"

sprite_trigger = main_space:install_write_tap(0x110000, 0x11FFFF, "ab2_sprite_trigger", function(offset, data, mask)
  if no_sprites then main_space:write_u16(0x100000, 0x8000) end
  sprite_buffer = main_space:read_range(0x100000, 0x100FFF, 8)
  ab2_capture_guard=true
  local ptr=main_space:read_u32(0x2a3c14)&0x3fffff
  ab2_capture_guard=false
  local source=pipeline_by_sorted[ptr]
  if source and blit_before and (not paired or source.producer) then pipeline=u32(blit_entry)..source.ram..blit_before..(paired and (u32(source.producer)..source.sizes) or "") else pipeline=nil end
  blit_before=nil
end)

road_before_flip = sub_space:install_write_tap(0x9C0D6, 0x9C0D7, "ab2_road_before_flip", function(offset, data, mask)
  if sub_pc.value == 0x000458 then
    road_pending = main_space:read_range(0x2EC000, 0x2ECFFF, 8)
    road_pending_context=road_completed
  end
end)

road_flipped = sub_space:install_write_tap(0xEE000, 0xEFFFF, "ab2_road_flipped", function(offset, data, mask)
  if sub_pc.value == 0x000466 then
    road_display = road_pending
    road_control = data & 0xff
    road_display_context=road_pending_context
    if paired_road then
      assert(not road_active,"unfinished paired road pass")
      pipeline_guard=true;ab2_capture_guard=true
      local sources={};local known=false
      for _,a in ipairs({0x29c0ce,0x29c0d0,0x29c0c2,0x29c0c0,0x29c0aa}) do
        local source=(ab2_road_target_sources or {})[a]
        if source then known=true end
        sources[#sources+1]=u32(source and source[1] or 0xffffffff)..u32(source and source[2] or 0)
      end
      if known then
        local shared,road=road_snapshot();local chips={}
        for _,a in ipairs({0xe0000,0xe0002,0xe4000,0xe4002,0xe4004,0xe4008,0xe400a,0xe400c}) do chips[#chips+1]=string.pack(">I2",sub_space:read_u16(a)) end
        road_active={sources=table.concat(sources),before=shared,road=road,chips=table.concat(chips),shared_w={},road_w={},other={}}
      else road_completed=nil end
      pipeline_guard=false;ab2_capture_guard=false
    end
  end
end)

-- Register writes after the latch point belong to the next frame's latch.
register_writes = main_space:install_write_tap(0x0D0E80, 0x0D0E9F, "ab2_tile_registers", function(offset, data, mask)
  if latch_pending and now() >= latch_at then
    latched_next = regs()
    latch_pending = false
  end
end)

-- MAME finishes a frame, flips the screen's double buffer, then calls frame
-- notifiers, so screen:pixels() at notifier N returns the image rendered
-- from the state as of notifier N-1. Each record therefore pairs this
-- notifier's pixels with the state snapshotted at the previous one — except
-- the palette: pixels() turns that frame's pen indices into RGB through the
-- palette as it is *now*, so the palette is snapshotted here.
local frame, records = 0, 0
local pending = nil
local finished=false
frame_sub = emu.add_machine_frame_notifier(function()
  if finished then return end
  frame = frame + 1
  if pending ~= nil then
    out:write(u32(frame - 1), screen:pixels(), pending.video, main_space:read_range(0x120000, 0x123FFF, 8),
              pending.tiles,pending.pipeline,pending.road or "")
    pending = nil
    records = records + 1
    if records >= max_records then
      finished=true;out:close();if ab2_objects_close then ab2_objects_close() end;if ab2_sound_close then ab2_sound_close() end;if ab2_frame_close then ab2_frame_close() end
      print(string.format("captured %d frames through frame %d -> %s", records, frame - 1, out_path))
      manager.machine:exit()
    end
  end

  if latch_pending then latched_next = regs(); latch_pending = false end
  if latched_next ~= nil then latched = latched_next end   -- the latch this frame was drawn with

  if pipeline and (not paired_road or road_display_context) and frame > skip and (frame - skip) % stride == 0 then
    pending = {
      pipeline=pipeline,
      road=road_display_context,
      video = sprite_buffer .. road_display ..
              string.char((road_control >> 8) & 0xff, road_control & 0xff) .. latched,
      tiles = main_space:read_range(0x0C0000, 0x0CFFFF, 8) .. main_space:read_range(0x0D0000, 0x0D0FFF, 8),
    }
  end

  latch_at = now() + LATCH_DELAY
  latch_pending = true
  latched_next = nil
end)
