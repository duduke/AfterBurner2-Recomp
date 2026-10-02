-- Continuous sound ring at real $B840/$B84C producer and $B7F8 consumer
-- boundaries. Timestamps and producer D0 are caller inputs. Other writes
-- to the ring are explicit external events, never credited to these ports.
local main=manager.machine.devices[":mainpcb:maincpu"]
local space=main.spaces.program
local out=assert(io.open(assert(os.getenv("AB2_ORACLE_SOUND_OUT")),"wb"))
out:write("AB2SND01")
local frame,guard,started,active,calls,external=0,false,false,nil,0,0
local function u32(v) return string.pack(">I4",v&0xffffffff) end
local function ns() return math.floor(manager.machine.time:as_double()*1e9+0.5) end
local function ring() guard=true;local v=space:read_range(0x3f8900,0x3f894b,8);guard=false;return v end
local function regs() local v={};for i=0,7 do v[#v+1]=u32(main.state["D"..i].value) end;for i=0,6 do v[#v+1]=u32(main.state["A"..i].value) end;return table.concat(v) end
local function begin(p)
 assert(not active,"nested sound call requires an interleaved sound fixture")
 if not started then out:write(ring());started=true end
 guard=true;local selector=space:read_u16(0x29c09c);guard=false
 active={entry=p,time=ns(),pre=regs(),selector=selector,writes={}}
end
local function finish()
 out:write(string.char(1),string.pack(">I8",active.time),u32(active.entry),active.pre,regs(),u32(active.selector),u32(#active.writes),table.concat(active.writes),ring())
 active=nil;calls=calls+1
end
local function sound_read(a,d,m)
 if guard or frame<tonumber(os.getenv("AB2_ORACLE_SKIP") or "540") then return end
 local p=main.state.CURPC.value
 if p==0xb7f8 or p==0xb840 or p==0xb84c then
  if not active or active.entry~=p then begin(p) end
 end
 if active and (p==0xb834 or p==0xb83e or p==0xb87a or p==0xb884) and a>=0x80000 then finish() end
end
sound_stream_reads=space:install_read_tap(0xb7f8,0xb885,"sound_stream_reads",sound_read)
sound_stream_returns=space:install_read_tap(0x3f8000,0x3fffff,"sound_stream_returns",sound_read)
local function sound_write(a,d,m)
 if guard or not started then return end
 local p=main.state.CURPC.value
 local e=u32(a)..u32(d)..u32(m)
 if active and p>=0xb7f8 and p<=0xb884 and (a<0x3ffe00) then active.writes[#active.writes+1]=e
 elseif a>=0x3f8900 and a<0x3f894c then
  out:write(string.char(0),string.pack(">I8",ns()),e);external=external+1
 end
end
sound_stream_writes=space:install_write_tap(0x3f8900,0x3f894b,"sound_stream_writes",sound_write)
sound_stream_latches=space:install_write_tap(0xe8016,0xe8017,"sound_stream_latches",sound_write)
sound_stream_frames=emu.add_machine_frame_notifier(function() frame=frame+1 end)
ab2_sound_close=function()
 assert(started and not active,"incomplete sound stream")
 out:close();print(string.format("captured %d sound calls and %d external ring writes",calls,external))
end
