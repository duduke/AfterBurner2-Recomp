#pragma once
#include "engine/mainbus.hpp"

// Main ROM $014626-$01465A, ending before the sound sequencer call at
// $014660. Oracle-only until the complete timer IRQ handler is ported.
void main_adc_sample(MainBus& bus);
