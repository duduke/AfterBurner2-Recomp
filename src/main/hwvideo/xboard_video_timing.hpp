#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include "hwmath/xboard_timeline.hpp"

// After Burner II's MAME screen/segaic16 contract: 320x224 visible pixels,
// 262 lines, 160 ns pixels, line-261 global latch. The scheduler supplies
// native scanline callbacks and CPU transaction times, never render clips.
class XBoardVideoTiming {
public:
    std::function<void(uint64_t,unsigned,unsigned,bool)> render;
    std::function<void(uint64_t)> latch, frame;

    // Explicit oracle boundary: the frame notifier sampled RAM/banks after
    // rendering. This does not establish cold-boot video initialization.
    void begin_after_render(uint64_t ns) { start=ns; last_partial=224; }
    static unsigned vpos(uint64_t ns) {
        // screen_device::vpos rounds to the nearest pixel from VBLANK start.
        return ((ns+80)/XBoardTimeline::LINE_NS+224)%262;
    }
    void partial(uint64_t ns,unsigned scanline) {
        // screen_device::update_partial: retain the cursor across CPU grants,
        // reject previous-frame writes, and intersect with the visible area.
        if(ns<=start||scanline<last_partial||(last_partial==0&&partial_reset>ns))return;
        const auto bottom=std::min(scanline,223u);
        if(last_partial>bottom)return;
        if(render)render(ns,last_partial,bottom,display);
        last_partial=scanline+1;
        ++updates;
    }
    void display_write(uint64_t ns,bool enabled) {
        // segaic16_video_device::set_display_enable renders the old state.
        if(enabled!=display)partial(ns,vpos(ns));
        display=enabled;
    }
    void before_text_write(uint64_t ns,unsigned byte_offset) {
        // segaic16_video_device::textram_w renders before COMBINE_DATA.
        if(byte_offset>=0xe80)partial(ns,vpos(ns));
    }
    void scanline(uint64_t ns,unsigned line) {
        if(line==261&&ns>start&&latch)latch(ns);
        // screen_device::first_scanline_tick/reset_partial_updates.
        if(line==0){last_partial=0;partial_reset=ns;}
        // screen_device::vblank_begin completes the visible frame.
        if(line==224&&ns>start){partial(ns,223);if(frame)frame(ns);}
    }
    unsigned partial_updates() const { return updates; }
private:
    uint64_t start=std::numeric_limits<uint64_t>::max(),partial_reset=0;
    unsigned last_partial=224,updates=0;
    bool display=false;
};
