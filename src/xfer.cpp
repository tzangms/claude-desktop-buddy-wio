#include "xfer.h"
#include "config.h"
#include "manifest.h"

#include <cstring>
#include <cctype>

#ifdef ARDUINO
#include <Arduino.h>
#include <Seeed_Arduino_FS.h>
#include <Seeed_SFUD.h>
#include "persist.h"
#else
#include <vector>
#include <string>
#endif

namespace {
  enum class State { Idle, CharOpen, FileOpen };
  State state = State::Idle;

  char charName[33] = {0};
  int64_t charTotalBytes = 0;
  int64_t charBytesReceived = 0;

  char filePath[64] = {0};
  int64_t fileExpectedBytes = 0;
  int64_t fileBytesWritten = 0;

  // "d" chunk caps at StaticJsonDocument<2048> in protocol.cpp, so 1600
  // bytes of base64 covers every chunk we will ever accept.
  static constexpr size_t MAX_CHUNK_B64 = 1600;

  // Single-slot pending command. Filled by xferQueue* on the BLE callback
  // stack, drained by xferTick on the main loop.
  struct Pending {
    XferAckInfo::Kind kind = XferAckInfo::None;
    char name[33] = {0};
    char path[64] = {0};
    int64_t total = 0;
    int64_t size = 0;
    char chunk[MAX_CHUNK_B64] = {0};
  } pending;

#ifdef ARDUINO
  File currentFile;
#else
  // Native test fake: last completed file content + current file accumulator.
  std::vector<uint8_t> fakeCurrent;
  std::vector<uint8_t> fakeLast;
  std::string fakeLastPath;
#endif

  void resetCharState() {
    state = State::Idle;
    charName[0] = '\0';
    charTotalBytes = 0;
    charBytesReceived = 0;
    filePath[0] = '\0';
    fileExpectedBytes = 0;
    fileBytesWritten = 0;
    pending.kind = XferAckInfo::None;
#ifdef ARDUINO
    if (currentFile) currentFile.close();
#else
    fakeCurrent.clear();
#endif
  }

  bool decodeChar(char c, uint8_t& out) {
    if (c >= 'A' && c <= 'Z') { out = c - 'A';       return true; }
    if (c >= 'a' && c <= 'z') { out = c - 'a' + 26;  return true; }
    if (c >= '0' && c <= '9') { out = c - '0' + 52;  return true; }
    if (c == '+')             { out = 62;            return true; }
    if (c == '/')             { out = 63;            return true; }
    return false;
  }
}

bool xferIsValidName(const char* s) {
  if (!s || !*s) return false;
  size_t len = std::strlen(s);
  if (len >= 64) return false;
  if (s[0] == '.') return false;
  if (std::strstr(s, "..")) return false;
  for (size_t i = 0; i < len; ++i) {
    char c = s[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

int64_t xferBase64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap) {
  if (!in || !out) return -1;
  if (inLen % 4 != 0) return -1;
  size_t written = 0;
  for (size_t i = 0; i < inLen; i += 4) {
    uint8_t v[4] = {0, 0, 0, 0};
    int pad = 0;
    for (int j = 0; j < 4; ++j) {
      char c = in[i + j];
      if (c == '=') { pad++; v[j] = 0; continue; }
      if (pad > 0) return -1;  // non-pad after pad
      if (!decodeChar(c, v[j])) return -1;
    }
    if (pad > 2) return -1;
    size_t emit = 3 - pad;
    if (written + emit > outCap) return -1;
    if (emit >= 1) out[written++] = (v[0] << 2) | (v[1] >> 4);
    if (emit >= 2) out[written++] = (v[1] << 4) | (v[2] >> 2);
    if (emit >= 3) out[written++] = (v[2] << 6) | v[3];
  }
  return (int64_t)written;
}

void xferInit() {
  resetCharState();
#ifndef ARDUINO
  fakeLast.clear();
  fakeLastPath.clear();
#endif
}

bool xferBeginChar(const char* name, int64_t total) {
  if (state != State::Idle) return false;
  if (!xferIsValidName(name)) return false;
  if (total <= 0 || total > (int64_t)CHAR_MAX_TOTAL_BYTES) return false;

  std::strncpy(charName, name, sizeof(charName) - 1);
  charName[sizeof(charName) - 1] = '\0';
  charTotalBytes = total;
  charBytesReceived = 0;
  state = State::CharOpen;

#ifdef ARDUINO
  // Ensure /chars/{name} exists. Seeed FS has no recursive mkdir so build both.
  if (!SFUD.exists("/chars")) SFUD.mkdir("/chars");
  char dir[48];
  std::snprintf(dir, sizeof(dir), "/chars/%s", charName);
  if (!SFUD.exists(dir)) SFUD.mkdir(dir);
#endif
  return true;
}

bool xferBeginFile(const char* path, int64_t size) {
  if (state != State::CharOpen) return false;
  if (!xferIsValidName(path)) return false;
  if (size < 0 || size > (int64_t)CHAR_MAX_FILE_BYTES) return false;

  std::strncpy(filePath, path, sizeof(filePath) - 1);
  filePath[sizeof(filePath) - 1] = '\0';
  fileExpectedBytes = size;
  fileBytesWritten = 0;
  state = State::FileOpen;

#ifdef ARDUINO
  char full[96];
  std::snprintf(full, sizeof(full), "/chars/%s/%s", charName, filePath);
  // Remove any previous partial-transfer file so the new FILE_WRITE starts
  // from a clean empty file rather than overwriting-in-place.
  if (SFUD.exists(full)) SFUD.remove(full);
  currentFile = SFUD.open(full, FILE_WRITE);
  if (!currentFile) {
    state = State::CharOpen;
    return false;
  }
#else
  fakeCurrent.clear();
#endif
  return true;
}

bool xferChunk(const char* base64Data, int64_t& bytesWrittenSoFar) {
  if (state != State::FileOpen) return false;
  if (!base64Data) return false;
  size_t inLen = std::strlen(base64Data);
  if (inLen == 0) { bytesWrittenSoFar = fileBytesWritten; return true; }

  // Decode in-place into a stack buffer. Max chunk 1.5 KB base64 → ~1.1 KB raw.
  uint8_t buf[1152];
  int64_t n = xferBase64Decode(base64Data, inLen, buf, sizeof(buf));
  if (n < 0) return false;

#ifdef ARDUINO
  size_t w = currentFile.write(buf, (size_t)n);
  if ((int64_t)w != n) return false;
#else
  fakeCurrent.insert(fakeCurrent.end(), buf, buf + n);
#endif
  fileBytesWritten += n;
  charBytesReceived += n;
  bytesWrittenSoFar = fileBytesWritten;
  return true;
}

bool xferEndFile(int64_t& finalSize) {
  if (state != State::FileOpen) return false;
  finalSize = fileBytesWritten;
  bool sizeOk = (fileBytesWritten == fileExpectedBytes);
#ifdef ARDUINO
  if (currentFile) currentFile.close();
#else
  fakeLast = std::move(fakeCurrent);
  fakeLastPath.assign(filePath);
  fakeCurrent.clear();
#endif
  state = State::CharOpen;
  filePath[0] = '\0';
  fileExpectedBytes = 0;
  fileBytesWritten = 0;
  return sizeOk;
}

bool xferEndChar() {
  if (state != State::CharOpen) return false;
  state = State::Idle;
#ifdef ARDUINO
  // charName is still latched; treat a bad/missing manifest as an upload
  // failure so the host can retry. Prior active char is preserved.
  if (!manifestSetActive(charName)) return false;
  persistSetActiveChar(charName);
#endif
  return true;
}

const char* xferActiveCharName() {
  return charName;
}

bool xferQueueCharBegin(const char* name, int64_t total) {
  if (pending.kind != XferAckInfo::None) return false;
  if (!name) name = "";
  std::strncpy(pending.name, name, sizeof(pending.name) - 1);
  pending.name[sizeof(pending.name) - 1] = '\0';
  pending.total = total;
  pending.kind = XferAckInfo::CharBegin;
  return true;
}

bool xferQueueFileBegin(const char* path, int64_t size) {
  if (pending.kind != XferAckInfo::None) return false;
  if (!path) path = "";
  std::strncpy(pending.path, path, sizeof(pending.path) - 1);
  pending.path[sizeof(pending.path) - 1] = '\0';
  pending.size = size;
  pending.kind = XferAckInfo::FileBegin;
  return true;
}

bool xferQueueChunk(const char* base64Data) {
  if (pending.kind != XferAckInfo::None) return false;
  if (!base64Data) base64Data = "";
  // strncpy would null-pad the full buffer (~1.6 KB writes per chunk) on the
  // BLE callback stack — the very path we're trying to keep cheap. memcpy
  // only touches the actual bytes.
  size_t n = std::strlen(base64Data);
  if (n >= sizeof(pending.chunk)) return false;
  std::memcpy(pending.chunk, base64Data, n + 1);  // +1 for the NUL
  pending.kind = XferAckInfo::Chunk;
  return true;
}

bool xferQueueFileEnd() {
  if (pending.kind != XferAckInfo::None) return false;
  pending.kind = XferAckInfo::FileEnd;
  return true;
}

bool xferQueueCharEnd() {
  if (pending.kind != XferAckInfo::None) return false;
  pending.kind = XferAckInfo::CharEnd;
  return true;
}

bool xferHasPending() {
  return pending.kind != XferAckInfo::None;
}

XferAckInfo xferTick() {
  XferAckInfo out;
  switch (pending.kind) {
    case XferAckInfo::None:
      return out;
    case XferAckInfo::CharBegin:
      out.kind = XferAckInfo::CharBegin;
      out.ok = xferBeginChar(pending.name, pending.total);
      break;
    case XferAckInfo::FileBegin:
      out.kind = XferAckInfo::FileBegin;
      out.ok = xferBeginFile(pending.path, pending.size);
      break;
    case XferAckInfo::Chunk: {
      out.kind = XferAckInfo::Chunk;
      int64_t written = 0;
      out.ok = xferChunk(pending.chunk, written);
      out.n = written;
      break;
    }
    case XferAckInfo::FileEnd: {
      out.kind = XferAckInfo::FileEnd;
      int64_t finalSize = 0;
      out.ok = xferEndFile(finalSize);
      out.n = finalSize;
      break;
    }
    case XferAckInfo::CharEnd:
      out.kind = XferAckInfo::CharEnd;
      out.ok = xferEndChar();
      break;
  }
  pending.kind = XferAckInfo::None;
  return out;
}

#ifndef ARDUINO
size_t _xferLastFileSize() { return fakeLast.size(); }
const uint8_t* _xferLastFileBytes() { return fakeLast.data(); }
int _xferStateOrdinal() { return static_cast<int>(state); }
void _xferResetForTest() {
  resetCharState();
  fakeLast.clear();
  fakeLastPath.clear();
  charName[0] = '\0';
  pending.kind = XferAckInfo::None;
}
#endif
