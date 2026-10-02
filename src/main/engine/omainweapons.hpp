#pragma once
#include "engine/omainobjects.hpp"

class OMainWeapons {
public:
    static void enemy(MainBus&, OMainObjects::Registers&, uint32_t); // $014DC4..$01586A
    static void reload(MainBus&, OMainObjects::Registers&); // $016C8A
    static void missile_hud(MainBus&, OMainObjects::Registers&); // $00CFC4
    static void decimal(OMainObjects::Registers&); // $00D062
    static void initialize(MainBus&, OMainObjects::Registers&); // $009D2C
    static void indicator_initialize(MainBus&, OMainObjects::Registers&); // $009D34
    static void update(MainBus&, OMainObjects::Registers&); // $009D56
    static void missile_fire(MainBus&, OMainObjects::Registers&); // $009DF8
    static void vulcan_fire(MainBus&, OMainObjects::Registers&); // $009E50
    static void missile_allocate(MainBus&, OMainObjects::Registers&); // $009E82
    static void vulcan_allocate(MainBus&, OMainObjects::Registers&); // $009ED8
    static void pool(MainBus&, OMainObjects::Registers&, bool, bool); // $009F08..$009F7A
    static void missile_initialize(MainBus&, OMainObjects::Registers&); // $009F7C
    static void vulcan_initialize(MainBus&, OMainObjects::Registers&); // $00A030
    static void vulcan_update(MainBus&, OMainObjects::Registers&); // $00AC4C
    static void remove(MainBus&, OMainObjects::Registers&, bool, bool=false, bool=true); // $00AE1C/$00AE2E/$00AE32/$00AE50
    static void missile_update(MainBus&, OMainObjects::Registers&, uint32_t); // $00A234/$00A382/$00A53A
    static void trail_allocate(MainBus&, OMainObjects::Registers&); // $00A9F2
    static void trail(MainBus&, OMainObjects::Registers&, bool); // $00AA32/$00AAEE
};
