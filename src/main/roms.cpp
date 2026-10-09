#include <algorithm>
#include "roms.hpp"

Roms roms;

// Tidier way to address a pointer-to-member-function call.
#define LOAD(rom, args) rom.load_rom args

bool Roms::load_aburner2()
{
    int status = 0;

    // Main CPU program: 2x 0x20000 word-interleaved -> 0x40000
    maincpu.init(0x40000);
    status += LOAD(maincpu, ("epr-11107.58", 0x00000, 0x20000, 0x6d87bab7, RomLoader::INTERLEAVE2));
    status += LOAD(maincpu, ("epr-11108.63", 0x00001, 0x20000, 0x202a3e1d, RomLoader::INTERLEAVE2));

    // Sub CPU program: 2x 0x20000 word-interleaved -> 0x40000
    subcpu.init(0x40000);
    status += LOAD(subcpu, ("epr-11109.20", 0x00000, 0x20000, 0x85a0fe07, RomLoader::INTERLEAVE2));
    status += LOAD(subcpu, ("epr-11110.29", 0x00001, 0x20000, 0xf3d6797c, RomLoader::INTERLEAVE2));

    // Tile/text graphics: 3x 0x10000 sequential bitplanes -> 0x30000
    tiles.init(0x30000);
    status += LOAD(tiles, ("epr-11115.154", 0x00000, 0x10000, 0xe8e32921, RomLoader::NORMAL));
    status += LOAD(tiles, ("epr-11114.153", 0x10000, 0x10000, 0x2e97f633, RomLoader::NORMAL));
    status += LOAD(tiles, ("epr-11113.152", 0x20000, 0x10000, 0x36058c8c, RomLoader::NORMAL));

    // Sprite graphics: 16x 0x20000, 4-way byte-interleaved -> 0x200000
    sprites.init(0x200000);
    status += LOAD(sprites, ("mpr-10932.90",  0x000000, 0x20000, 0xcc0821d6, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10934.94",  0x000001, 0x20000, 0x4a51b1fa, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10936.98",  0x000002, 0x20000, 0xada70d64, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10938.102", 0x000003, 0x20000, 0xe7675baf, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10933.91",  0x080000, 0x20000, 0xc8efb2c3, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10935.95",  0x080001, 0x20000, 0xc1e23521, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10937.99",  0x080002, 0x20000, 0xf0199658, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("mpr-10939.103", 0x080003, 0x20000, 0xa0d49480, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11103.92",  0x100000, 0x20000, 0xbdd60da2, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11104.96",  0x100001, 0x20000, 0x06a35fce, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11105.100", 0x100002, 0x20000, 0x027b0689, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11106.104", 0x100003, 0x20000, 0x9e1fec09, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11116.93",  0x180000, 0x20000, 0x49b4c1ba, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11117.97",  0x180001, 0x20000, 0x821fbb71, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11118.101", 0x180002, 0x20000, 0x8f38540b, RomLoader::INTERLEAVE4));
    status += LOAD(sprites, ("epr-11119.105", 0x180003, 0x20000, 0xd0343a8e, RomLoader::INTERLEAVE4));

    // Road/ground graphics: single 0x10000 file
    road.init(0x10000);
    status += LOAD(road, ("epr-10922.40", 0x00000, 0x10000, 0xb49183d4, RomLoader::NORMAL));

    // Sound CPU program: single 0x10000 file
    soundcpu.init(0x10000);
    status += LOAD(soundcpu, ("epr-11112.17", 0x00000, 0x10000, 0xd777fc6d, RomLoader::NORMAL));

    // SegaPCM sample data: 3x 0x20000 sequential -> 0x60000 of a 0x80000 region
    // (MAME defines the complete region as ROMREGION_ERASEFF)
    pcm.init(0x80000);
    std::fill_n(pcm.rom, pcm.length, uint8_t(0xff));
    status += LOAD(pcm, ("mpr-10931.11", 0x00000, 0x20000, 0x9209068f, RomLoader::NORMAL));
    status += LOAD(pcm, ("mpr-10930.12", 0x20000, 0x20000, 0x6493368b, RomLoader::NORMAL));
    status += LOAD(pcm, ("epr-11102.13", 0x40000, 0x20000, 0x6c07c78d, RomLoader::NORMAL));

    return status == 0;
}
