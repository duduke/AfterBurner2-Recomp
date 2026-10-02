#pragma once
#include "engine/mainbus.hpp"
#include "engine/omainobjects.hpp"

// Main ROM $014626-$01465A, ending before the sound sequencer call at
// $014660. Used by the complete comparator IRQ below.
void main_adc_sample(MainBus& bus);

// $01461C-$01466A: acknowledge, sample/start ADC, consume sound, restore
// D0-D2/A0-A1. A7, exception frames, SR and CPU cycles are host boundaries.
void main_compare_interrupt(MainBus&, const OMainObjects::Registers&);
void main_compare_ack(MainBus&, const OMainObjects::Registers&); // $01466C
