/***************************************************************************
    Video Emulation: X-Board Road/Ground+Sky Rendering Hardware.

    Faithful reimplementation of the Sega X-Board "road" chip
    (segaic16_road_device, ROAD_XBOARD mode in MAME), which is the same
    chip family and decode/draw algorithm as OutRun's road hardware
    (ROAD_OUTRUN) — Cannonball already reimplemented this mechanism for
    OutRun; this is the same algorithm re-derived directly from MAME
    (src/mame/sega/segaic16_road.cpp) with After Burner II's own
    color/offset parameters (see docs/HARDWARE.md section 3.2).

    On After Burner II this layer renders the scrolling sea/ground and sky
    the jet flies over — there are two independently-scrolling "road"
    strips (e.g. ground + sky, or two ground bands during a stage
    transition) blended per-scanline according to the road control
    register.
***************************************************************************/

#pragma once

#include <cstdint>

class HWRoad
{
public:
    // color_base1/2/3 and x_offset match segaic16_road_init()'s params for
    // whichever game is being emulated (After Burner II: 0x1700, 0x1720,
    // 0x1780, -166 — see docs/HARDWARE.md section 3.2).
    HWRoad(uint16_t color_base1, uint16_t color_base2, uint16_t color_base3, int32_t x_offset);

    // Decode the raw road ROM (as loaded, `rom_length` bytes) into pixel
    // form. Safe to call again (e.g. on a display-mode change).
    void init(const uint8_t* rom, uint32_t rom_length);

    // CPU-side writes into road control RAM (shared main/sub-CPU region).
    inline void write16(uint32_t addr, uint16_t data) { ram[(addr >> 1) & 0x7FF] = data; }

    // Reading the road control register swaps the live/buffered RAM halves
    // — this is the hardware's vblank-sync mechanism for this layer.
    void read_road_control();
    void write_road_control(uint8_t control);

    // Render into a 320x224 buffer of logical color-index pixels (not yet
    // resolved to RGB — that happens in the shared palette resolve pass).
    void render_background(uint16_t* pixels, int width, int height) const;
    void render_foreground(uint16_t* pixels, int width, int height) const;

private:
    static constexpr int ROAD_RAM_WORDS = 0x1000 / 2;

    uint8_t road_control = 0;
    const uint16_t color_offset1;
    const uint16_t color_offset2;
    const uint16_t color_offset3;
    const int32_t x_offset;

    // Decoded road graphics: 512 scanlines of the road pattern (each 512
    // pixels wide, one byte per pixel holding a 2-bit road-edge code, with
    // bit 2 pre-marking the central-stripe area) plus one dummy "all
    // exterior" scanline used when a road entry is flagged solid-fill.
    uint8_t roads[512 * 512 + 512];

    uint16_t ram[ROAD_RAM_WORDS];
    uint16_t ram_buffer[ROAD_RAM_WORDS];

    void decode_road(const uint8_t* rom, uint32_t rom_length);
};
