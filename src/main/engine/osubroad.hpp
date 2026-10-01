/***************************************************************************
    Sub CPU road/ground generator: the per-frame work after the road RAM flip.

    Port of sub CPU ROM $00046A-$00052C and the routines it calls:
      $000C0A  slew the bank angle and horizon toward their targets
      $000CEA  push the bank value into a short history ring
      $000D0E  slew pitch
      $000D64  rotation: sin/cos of the roll angle, horizon line, and a
               32-step coordinate table at shared $100
      $000F02  the scanline raster: from the horizon line, a fixed-point
               ramp of horizontal offsets out to the screen edges, written
               to road 0's per-line scroll table — this is what banks the
               ground (one of four mirrored variants per quadrant)
    Inputs (bank, pitch, stage, limits) are shared-RAM variables written by
    the main CPU. See docs/ENGINE_NOTES.md, "Sub CPU road generator".

    Translated instruction-by-instruction with the arcade's memory layout,
    verified against MAME captures by tests/oracle_subroad.cpp.

    $0016D4/$001758 special perspective raster and $0005C4/$0005EE road
    initialization are also ported and covered by separate MAME captures.
***************************************************************************/

#pragma once

#include <cstdint>
#include "engine/subbus.hpp"

class OSubRoad
{
public:
    // One frame of ordinary or special road work.
    bool frame(SubBus& bus);
    void initialize(SubBus& bus, uint32_t entry); // $0005C4 / $0005EE
    void service_requests(SubBus& bus); // $000436-$00044A

private:
    void special(SubBus& bus);        // $0016D4
    void special_raster(SubBus& bus); // $001758
    void slew_bank(SubBus& bus);     // $000C0A, falls into bank_history()
    void bank_history(SubBus& bus);  // $000CEA
    void slew_pitch(SubBus& bus);    // $000D0E
    void rotation(SubBus& bus);      // $000D64
    void raster(SubBus& bus);        // $000F02
};
