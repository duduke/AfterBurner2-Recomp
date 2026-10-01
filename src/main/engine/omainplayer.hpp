#pragma once
#include "engine/omainobjects.hpp"

class OMainPlayer {
public:
    static void initialize(MainBus&, OMainObjects::Registers&); // $009810
    static void update(MainBus&, OMainObjects::Registers&); // $0098C0
    static void project(MainBus&, OMainObjects::Registers&); // $009AE2
    static void append(MainBus&, OMainObjects::Registers&); // $00B628
    static void shadow_initialize(MainBus&, OMainObjects::Registers&); // $009C34
    static void shadow_update(MainBus&, OMainObjects::Registers&); // $009C9E
    static void exhaust_initialize(MainBus&, OMainObjects::Registers&, uint32_t); // $00AE8C/$00AEE4/$00AF08
    static void exhaust_update(MainBus&, OMainObjects::Registers&, uint32_t); // $00AF3C/$00AFCC/$00B05C
    static void sequence_update(MainBus&, OMainObjects::Registers&, uint32_t); // $01653A..$01657A
    static void sequence_project(MainBus&, OMainObjects::Registers&); // $01678E
    static void recover(MainBus&, OMainObjects::Registers&, uint32_t); // $009626/$00964C
};
