/***************************************************************************
    Load the After Burner II (aburner2) ROM set.

    Region assignment transcribed from MAME's src/mame/sega/segaxbd.cpp,
    ROM_START( aburner2 ). See docs/HARDWARE.md section 6.
***************************************************************************/

#pragma once

#include "romloader.hpp"

class Roms
{
public:
    RomLoader maincpu;  // main 68000 program
    RomLoader subcpu;    // sub 68000 program
    RomLoader tiles;    // tile/text graphics (3 planar bitplanes)
    RomLoader sprites;  // sprite graphics (hardware scaler source data)
    RomLoader road;      // road/ground+sky graphics
    RomLoader soundcpu;  // Z80 sound program
    RomLoader pcm;       // SegaPCM sample data

    bool load_aburner2();
};

extern Roms roms;
