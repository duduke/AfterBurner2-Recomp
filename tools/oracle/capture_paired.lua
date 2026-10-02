-- One natural gameplay run supplies dispatcher prefixes and their associated
-- sub passes, both display lists and RGB. No extra dispatch or IPL masking.
assert(os.getenv("AB2_ORACLE_INTERLEAVE_PREFIX")=="1")
assert(os.getenv("AB2_ORACLE_PAIRED")=="1")
assert(os.getenv("AB2_ORACLE_OBJECT_OUT"))
local dir=assert(os.getenv("AB2_ORACLE_SCRIPT_DIR"))
dofile(dir.."/capture_objects.lua")
dofile(dir.."/capture_pipeline.lua")
if os.getenv("AB2_ORACLE_SOUND_OUT") then dofile(dir.."/capture_sound_stream.lua") end

if os.getenv("AB2_ORACLE_FRAME_OUT") then dofile(dir.."/capture_frame_host.lua") end
