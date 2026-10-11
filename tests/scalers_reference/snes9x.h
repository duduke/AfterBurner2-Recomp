// Minimal type/color declarations to compile the unchanged 16-bit upstream
// SaI source as an independent reference in the ROM-free scaler check.
#pragma once
#include <cstdint>
using uint8=uint8_t;using uint16=uint16_t;using uint32=uint32_t;using bool8=bool;
#define TRUE true
#define FIRST_COLOR_MASK 0xf800u
#define SECOND_COLOR_MASK 0x07e0u
#define THIRD_COLOR_MASK 0x001fu
#define RGB_LOW_BITS_MASK 0x0821u
#define TWO_LOW_BITS_MASK 0x1863u
