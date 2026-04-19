#pragma once

#include <cstdint>

// Free heap bytes on the SAMD51 main MCU (not FreeRTOS).
uint32_t freeHeapBytes();
