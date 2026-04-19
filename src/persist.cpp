#include "persist.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
// Device-side file I/O lands in Task 8.
#else
#include <vector>
namespace {
  std::vector<uint8_t> fakeFile;
  int writeCount = 0;
}
const uint8_t* _persistFakeFile() { return fakeFile.data(); }
size_t _persistFakeFileSize() { return fakeFile.size(); }
void _persistResetFakeFile() { fakeFile.clear(); writeCount = 0; }
int _persistWriteCount() { return writeCount; }
#endif

namespace {
  PersistData data;
  bool dirty = false;
  uint32_t lastFlushMs = 0;
  int64_t lastFlushedLifetimeTokens = 0;
  int64_t prevSessionTokens = 0;
  bool fsReady = false;

  void setDefaults() {
    std::memset(&data, 0, sizeof(data));
    data.magic = PERSIST_MAGIC;
    data.version = PERSIST_VERSION;
  }

  bool readStore(uint8_t* buf, size_t size) {
#ifdef ARDUINO
    (void)buf; (void)size;
    return false;
#else
    if (fakeFile.size() != size) return false;
    std::memcpy(buf, fakeFile.data(), size);
    return true;
#endif
  }

  bool writeStore(const uint8_t* buf, size_t size) {
#ifdef ARDUINO
    (void)buf; (void)size;
    return false;
#else
    fakeFile.assign(buf, buf + size);
    ++writeCount;
    return true;
#endif
  }

  void flush() {
    writeStore(reinterpret_cast<const uint8_t*>(&data), sizeof(data));
    dirty = false;
    lastFlushedLifetimeTokens = data.deviceLifetimeTokens;
  }
}

void persistInit() {
  setDefaults();
  fsReady = true;
  PersistData tmp;
  if (readStore(reinterpret_cast<uint8_t*>(&tmp), sizeof(tmp))) {
    if (tmp.magic == PERSIST_MAGIC && tmp.version == PERSIST_VERSION) {
      data = tmp;
    }
  }
  prevSessionTokens = 0;
  lastFlushedLifetimeTokens = data.deviceLifetimeTokens;
  lastFlushMs = 0;
  dirty = false;
}

const PersistData& persistGet() { return data; }
PersistData& persistMut() { return data; }

void persistCommit(bool immediate) {
  dirty = true;
  if (immediate && fsReady) {
    flush();
  }
}

void persistTick(uint32_t nowMs) {
  if (!dirty || !fsReady) return;
  int64_t tokenDelta = data.deviceLifetimeTokens - lastFlushedLifetimeTokens;
  bool timeTriggered  = (nowMs - lastFlushMs) >= PERSIST_DEBOUNCE_MS;
  bool tokenTriggered = tokenDelta >= PERSIST_DEBOUNCE_TOKENS;
  if (!timeTriggered && !tokenTriggered) return;
  flush();
  lastFlushMs = nowMs;
}

void persistUpdateFromHeartbeat(int64_t /*sessionTokens*/, int64_t /*tokensToday*/) {
  // Lands in Task 7
}
