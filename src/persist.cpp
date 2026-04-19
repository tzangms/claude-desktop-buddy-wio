#include "persist.h"
#include "config.h"

#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
static constexpr const char* PERSIST_DIR  = "/wioclaude";
static constexpr const char* PERSIST_PATH = "/wioclaude/stats.bin";
static int consecutiveWriteFailures = 0;
static constexpr int CONSECUTIVE_WRITE_FAILURE_LIMIT = 5;
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
  bool sessionBaselined = false;
  bool fsReady = false;

  void setDefaults() {
    std::memset(&data, 0, sizeof(data));
    data.magic = PERSIST_MAGIC;
    data.version = PERSIST_VERSION;
  }

  bool readStore(uint8_t* buf, size_t size) {
#ifdef ARDUINO
    if (!fsReady) return false;
    File f = SFUD.open(PERSIST_PATH, FILE_READ);
    if (!f) return false;
    if ((size_t)f.size() != size) { f.close(); return false; }
    size_t n = f.read(buf, size);
    f.close();
    return n == size;
#else
    if (fakeFile.size() != size) return false;
    std::memcpy(buf, fakeFile.data(), size);
    return true;
#endif
  }

  bool writeStore(const uint8_t* buf, size_t size) {
#ifdef ARDUINO
    if (!fsReady) return false;
    if (consecutiveWriteFailures >= CONSECUTIVE_WRITE_FAILURE_LIMIT) return false;
    if (!SFUD.exists(PERSIST_DIR)) SFUD.mkdir(PERSIST_DIR);
    File f = SFUD.open(PERSIST_PATH, FILE_WRITE);
    if (!f) { ++consecutiveWriteFailures; return false; }
    f.seek(0);
    size_t n = f.write(buf, size);
    f.close();
    if (n != size) { ++consecutiveWriteFailures; return false; }
    consecutiveWriteFailures = 0;
    return true;
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
  fsReady = false;
#ifdef ARDUINO
  if (!SFUD.begin(104000000UL)) {
    Serial.println("[persist] SFUD mount failed; using defaults");
  } else {
    fsReady = true;
  }
#else
  fsReady = true;
#endif
  if (!fsReady ||
      !readStore(reinterpret_cast<uint8_t*>(&data), sizeof(data)) ||
      data.magic != PERSIST_MAGIC ||
      data.version != PERSIST_VERSION) {
    setDefaults();
  }
  prevSessionTokens = 0;
  sessionBaselined = false;
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

void persistUpdateFromHeartbeat(int64_t sessionTokens, int64_t tokensToday) {
  // First heartbeat after boot only establishes the baseline — we don't know
  // how much of sessionTokens was already folded into deviceLifetimeTokens
  // before the previous shutdown, so treating the first delta as zero
  // prevents double-counting across reboots.
  if (sessionBaselined && sessionTokens >= prevSessionTokens) {
    int64_t delta = sessionTokens - prevSessionTokens;
    data.deviceLifetimeTokens += delta;
  }
  prevSessionTokens = sessionTokens;
  sessionBaselined = true;
  data.lvl = static_cast<int32_t>(data.deviceLifetimeTokens / TOKENS_PER_LEVEL);
  data.tokens_today = tokensToday;
}

void persistSetDeviceName(const char* name) {
  std::strncpy(data.deviceName, name, sizeof(data.deviceName) - 1);
  data.deviceName[sizeof(data.deviceName) - 1] = '\0';
  persistCommit(true);
}

void persistSetOwnerName(const char* name) {
  std::strncpy(data.ownerName, name, sizeof(data.ownerName) - 1);
  data.ownerName[sizeof(data.ownerName) - 1] = '\0';
  persistCommit(true);
}

void persistIncAppr() {
  data.appr++;
  persistCommit(true);
}

void persistIncDeny() {
  data.deny++;
  persistCommit(true);
}
