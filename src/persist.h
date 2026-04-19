#pragma once

#include <cstdint>
#include <cstddef>

struct __attribute__((packed)) PersistData {
  uint32_t magic;
  uint32_t version;
  int32_t  appr;
  int32_t  deny;
  int32_t  lvl;
  int32_t  nap;
  int32_t  vel;
  int64_t  tokens_today;
  int64_t  deviceLifetimeTokens;
  char     deviceName[33];
  char     ownerName[33];
};

static_assert(sizeof(PersistData) == 110,
              "PersistData file format: bump PERSIST_VERSION if this changes");

void persistInit();
const PersistData& persistGet();
PersistData& persistMut();
void persistCommit(bool immediate);
void persistTick(uint32_t nowMs);
void persistUpdateFromHeartbeat(int64_t sessionTokens, int64_t tokensToday);

// Convenience helpers: mutate + persistCommit(true) in one call.
void persistSetDeviceName(const char* name);
void persistSetOwnerName(const char* name);
void persistIncAppr();
void persistIncDeny();

#ifndef ARDUINO
const uint8_t* _persistFakeFile();
size_t _persistFakeFileSize();
void _persistResetFakeFile();
int _persistWriteCount();
#endif
