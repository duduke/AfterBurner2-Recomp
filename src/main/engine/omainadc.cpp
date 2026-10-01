#include "engine/omainadc.hpp"

void main_adc_sample(MainBus& bus)
{
    // $014626-$014632: latch ADC into the six-byte sample ring.
    const uint8_t d0 = bus.read8(0x130001);
    constexpr uint32_t a0 = 0xffff8890;
    uint16_t d1 = bus.read16(a0);
    bus.write8(a0 + 2 + static_cast<int16_t>(d1), d0);
    // $014636-$014642: word increment, unsigned BCS wrap at six.
    d1 = static_cast<uint16_t>(d1 + 1);
    if (d1 >= 6) d1 = 0;
    bus.write16(a0, d1);
    // $014644-$014650: select the next ADC channel in port C's shadow.
    d1 = static_cast<uint16_t>(d1 << 2);
    constexpr uint32_t shadow = 0xffff8b6f;
    bus.write8(shadow, bus.read8(shadow) & 0xe3);
    bus.write8(shadow, bus.read8(shadow) | static_cast<uint8_t>(d1));
    const uint8_t d2 = bus.read8(shadow);
    // $014654/$01465A: output mux selection, then begin ADC conversion.
    bus.write8(0x140005, d2);
    bus.write8(0x130001, static_cast<uint8_t>(d1));
}
