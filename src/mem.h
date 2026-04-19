#pragma once

#include <cstdint>

// Free heap bytes on the SAMD51 main MCU (not FreeRTOS).
// Uses linker symbols + current stack pointer.
uint32_t freeHeapBytes();
