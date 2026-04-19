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
  char     activeCharName[33];
};

static_assert(sizeof(PersistData) == 143,
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

void persistSetActiveChar(const char* name);
const char* persistGetActiveChar();

// Reset persisted data to defaults and flush immediately. Caller usually
// follows with a system reset to re-run setup() with fresh state.
void persistFactoryReset();

#ifndef ARDUINO
const uint8_t* _persistFakeFile();
size_t _persistFakeFileSize();
void _persistResetFakeFile();
int _persistWriteCount();
// Test-only: overwrite the native fake flash with arbitrary bytes so
// tests can simulate pre-migration blobs.
void _persistSeedFakeFile(const uint8_t* bytes, size_t n);
#endif
