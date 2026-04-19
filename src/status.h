#pragma once

#include <cstdint>
#include <string>

struct AppState;

struct StatusSnapshot {
  std::string name;
  std::string ownerName;
  int32_t     appr = 0;
  int32_t     deny = 0;
  int32_t     lvl = 0;
  int32_t     nap = 0;
  int32_t     vel = 0;
  bool        sec = false;
  uint32_t    upSec = 0;
  uint32_t    heapFree = 0;
};

// Platform-specific: reads millis() and free heap. Only compiled on device.
StatusSnapshot captureStatus(const AppState& s, uint32_t nowMs);

// Pure logic: serializes snapshot to JSON line. Native-testable.
std::string formatStatusAck(const StatusSnapshot& snap);
