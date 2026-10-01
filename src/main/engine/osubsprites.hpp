/***************************************************************************
    Sub CPU sprite pass: depth sort + per-sprite perspective/clip transform.

    Port of sub CPU ROM $000648-$0009E2. Each pass turns the main CPU's
    unsorted 16-byte sprite requests into final 8-word hardware sprite
    descriptors, depth-sorted, plus a second copy with positions moved half
    a step toward the previous frame (the game logic runs at 30 Hz; this is
    what's shown on the in-between 60 Hz frame). See docs/ENGINE_NOTES.md,
    "Sub CPU sprite pass".

    Translated instruction-by-instruction, keeping the arcade's memory
    layout, and verified byte-for-byte against MAME captures by
    tests/oracle_subsprites.cpp. Keep it that way until a readable rewrite
    can be checked against the same captures.
***************************************************************************/

#pragma once

#include <cstdint>
#include "engine/subbus.hpp"

class OSubSprites
{
public:
    // One pass, run once the main CPU releases subram0[$D6] bit 0 (the
    // spin at $000640 that precedes it is the caller's synchronization).
    void pass(SubBus& bus);

private:
    void transform(SubBus& bus, uint32_t a5, uint32_t& a4);   // $0007B8
    void interpolate(SubBus& bus);                            // $000726
};
