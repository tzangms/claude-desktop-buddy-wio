#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

// Folder-push transport receiver. Matches the protocol described in
// claude-desktop-buddy/REFERENCE.md: char_begin / file / chunk / file_end
// / char_end. All cmds arrive as separate JSON lines via BLE NUS and are
// dispatched here after parseLine identifies them.

// Path / name validation rules: only [A-Za-z0-9._-], length < 64, no
// leading dot (skips dotfiles), no "..".
bool xferIsValidName(const char* s);

// In-place base64 decode. Writes raw bytes into `out` (must be at least
// `3 * inLen / 4` bytes). Returns number of bytes written, or -1 on error.
int64_t xferBase64Decode(const char* in, size_t inLen, uint8_t* out, size_t outCap);

void xferInit();

// Each function returns true on success, false on protocol/io error.
// On success, caller should ack ok:true (with optional byte count where noted).
// On failure, caller should ack ok:false.
bool xferBeginChar(const char* name, int64_t total);
bool xferBeginFile(const char* path, int64_t size);
bool xferChunk(const char* base64Data, int64_t& bytesWrittenSoFar);
bool xferEndFile(int64_t& finalSize);
bool xferEndChar();

// Current active character name (or empty). Useful for tests / status.
const char* xferActiveCharName();

#ifndef ARDUINO
// Test-only introspection.
size_t _xferLastFileSize();
const uint8_t* _xferLastFileBytes();
int _xferStateOrdinal();  // 0=Idle, 1=CharOpen, 2=FileOpen
void _xferResetForTest();
#endif
