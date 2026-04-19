#include "ui.h"
#include "character.h"
#include "config.h"
#include "persist.h"
#include "pet.h"
#include <Arduino.h>
#include <cstring>
#include "TFT_eSPI.h"

TFT_eSPI tft;

static void clearAll() {
  tft.fillScreen(COLOR_BG);
}

static void drawHeader(const char* title, uint16_t bg, uint16_t fg) {
  tft.fillRect(0, 0, SCREEN_W, 28, bg);
  tft.setTextColor(fg, bg);
  tft.setTextSize(2);
  tft.setCursor(8, 6);
  tft.print(title);
}

static void drawFooter(const char* text) {
  tft.fillRect(0, SCREEN_H - 22, SCREEN_W, 22, COLOR_BG);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(8, SCREEN_H - 16);
  tft.print(text);
}

void initUi() {
  tft.begin();
  tft.setRotation(3);
  clearAll();
}

void renderBoot(const char* msg) {
  clearAll();
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 110);
  tft.print(msg);
}

void renderAdvertising(const char* deviceName) {
  clearAll();
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 90);
  tft.print("advertising as");
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(20, 110);
  tft.print(deviceName);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(20, 150);
  tft.print("open Claude Desktop Dev menu to pair");
}

void renderConnected() {
  clearAll();
  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.setTextColor(COLOR_OK, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(40, 100);
  tft.print("connected, waiting...");
}

void renderIdle(const AppState& s, bool fullRedraw) {
  // Per-field cache so steady-state heartbeats skip redundant SPI writes.
  static int32_t lastLvl = INT32_MIN;
  static int64_t lastTokens = INT64_MIN;
  static int lastTotal = INT32_MIN, lastRunning = INT32_MIN, lastWaiting = INT32_MIN;
  static std::string lastMsg;
  static std::vector<std::string> lastEntries;
  static PetState lastPet = (PetState)-1;
  static size_t lastFrame = (size_t)-1;
  static std::string lastOwner;

  if (fullRedraw) {
    clearAll();
    drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
    tft.fillCircle(SCREEN_W - 20, 14, 5, COLOR_OK);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(SCREEN_W - 100, 10);
    tft.print("connected");
    // Labels shifted right of the 96-wide buddy slot at BUDDY_X.
    tft.setCursor(112, 36);              tft.print("Level");
    tft.setCursor(SCREEN_W - 120, 36);   tft.print("Tokens today");
    tft.setCursor(116, 66);              tft.print("Total");
    tft.setCursor(182, 66);              tft.print("Running");
    tft.setCursor(252, 66);              tft.print("Waiting");
    // Invalidate caches so every block repaints on the fresh canvas.
    lastLvl = INT32_MIN; lastTokens = INT64_MIN;
    lastTotal = INT32_MIN; lastRunning = INT32_MIN; lastWaiting = INT32_MIN;
    lastMsg.clear(); lastEntries.clear();
    lastPet = (PetState)-1; lastFrame = (size_t)-1; lastOwner.clear();
  }

  int32_t lvl = persistGet().lvl;
  if (lvl != lastLvl) {
    tft.fillRect(112, 46, 60, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "L%d", lvl);
    tft.setCursor(112, 46);
    tft.print(buf);
    lastLvl = lvl;
  }

  int64_t tokens = s.hb.tokens_today;
  if (tokens != lastTokens) {
    tft.fillRect(SCREEN_W - 120, 46, 112, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(SCREEN_W - 120, 46);
    char buf[16];
    if (tokens < 1000) {
      snprintf(buf, sizeof(buf), "%d t", (int)tokens);
    } else if (tokens < 100000) {
      snprintf(buf, sizeof(buf), "%.1f kt", (double)tokens / 1000.0);
    } else {
      snprintf(buf, sizeof(buf), "%d kt", (int)(tokens / 1000));
    }
    tft.print(buf);
    lastTokens = tokens;
  }

  // size-3 digits: 18px wide × 24px tall. 3 cells fit in the right panel.
  auto drawNum = [](int x, int n) {
    tft.fillRect(x, 80, 54, 28, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(3);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
    tft.setCursor(x, 82); tft.print(buf);
  };
  if (s.hb.total   != lastTotal)   { drawNum(118, s.hb.total);   lastTotal   = s.hb.total; }
  if (s.hb.running != lastRunning) { drawNum(186, s.hb.running); lastRunning = s.hb.running; }
  if (s.hb.waiting != lastWaiting) { drawNum(254, s.hb.waiting); lastWaiting = s.hb.waiting; }

  if (s.hb.msg != lastMsg) {
    tft.fillRect(112, 125, SCREEN_W - 112, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(112, 128);
    tft.print(s.hb.msg.c_str());
    lastMsg = s.hb.msg;
  }

  // Transcript shrunk from 3 to 2 lines to make room for the pet.
  if (s.hb.entries != lastEntries) {
    tft.fillRect(112, 145, SCREEN_W - 112, 40, COLOR_BG);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    size_t n = s.hb.entries.size() < 2 ? s.hb.entries.size() : 2;
    for (size_t i = 0; i < n; ++i) {
      tft.setCursor(112, 148 + (int)i * 18);
      tft.print(s.hb.entries[i].c_str());
    }
    lastEntries = s.hb.entries;
  }

  uint32_t now = millis();
  PetState st = petComputeState(s, now);
  if (st != lastPet) {
    petResetFrame(now);
    lastFrame = (size_t)-1;
  }
  size_t frame = petCurrentFrame();
  if (st != lastPet || frame != lastFrame) {
    uint16_t petColour;
    switch (st) {
      case PetState::Attention: petColour = COLOR_ALERT_FG; break;
      case PetState::Celebrate: petColour = COLOR_WARN;     break;
      case PetState::Heart:     petColour = COLOR_ALERT_FG; break;
      case PetState::Dizzy:     petColour = COLOR_WARN;     break;
      case PetState::Nap:       petColour = COLOR_DIM;      break;
      default:                  petColour = COLOR_OK;       break;
    }
    // ASCII pet renders in the buddy slot. SP6b's characterTick paints
    // over this on the same rect when characterReady(); ui.cpp doesn't
    // need to know which path is active because both respect BUDDY_*.
    tft.fillRect(BUDDY_X, BUDDY_Y, BUDDY_W, BUDDY_H, COLOR_BG);
    tft.setTextColor(petColour, COLOR_BG);
    tft.setTextSize(2);   // bigger ASCII for the larger slot
    const char* const* rows = petFace(st, frame);
    for (size_t i = 0; i < PET_FACE_LINES; ++i) {
      tft.setCursor(BUDDY_X + 8, BUDDY_Y + 20 + (int)i * 16);
      tft.print(rows[i]);
    }
    lastPet = st;
    lastFrame = frame;
  }

  if (s.ownerName != lastOwner) {
    if (!s.ownerName.empty()) {
      char buf[64];
      snprintf(buf, sizeof(buf), "Hi, %s", s.ownerName.c_str());
      drawFooter(buf);
    } else {
      drawFooter("");
    }
    lastOwner = s.ownerName;
  }
}

void renderPrompt(const AppState& s, bool fullRedraw) {
  if (!fullRedraw) return;
  clearAll();
  drawHeader("! PERMISSION REQUESTED", COLOR_ALERT_BG, COLOR_ALERT_TEXT);

  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 50);
  tft.print("Tool");

  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(3);
  tft.setCursor(16, 65);
  tft.print(s.hb.prompt.tool.c_str());

  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  std::string hint = s.hb.prompt.hint;
  if (hint.size() > 26) { hint.resize(23); hint += "..."; }
  tft.setCursor(16, 120);
  tft.print(hint.c_str());

  tft.fillRect(0, SCREEN_H - 24, SCREEN_W, 24, COLOR_FOOTER_BG);
  tft.setTextColor(COLOR_FG);
  tft.setTextSize(2);
  tft.setCursor(12, SCREEN_H - 20);
  tft.print("[C] Deny");
  tft.setCursor(SCREEN_W - 170, SCREEN_H - 20);
  tft.print("[A] Allow once");
}

void renderAck(const AppState& s) {
  clearAll();
  const char* txt = s.ackApproved ? "Approved" : "Denied";
  uint16_t color  = s.ackApproved ? COLOR_OK : COLOR_ALERT_BG;
  tft.setTextColor(color, COLOR_BG);
  tft.setTextSize(5);
  int16_t w = (int16_t)strlen(txt) * 6 * 5;
  tft.setCursor((SCREEN_W - w) / 2, 100);
  tft.print(txt);
}

void renderDisconnected() {
  clearAll();
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(3);
  tft.setCursor(40, 90);
  tft.print("Disconnected");
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(40, 140);
  tft.print("scanning...");
}

void renderFactoryResetConfirm(bool fullRedraw) {
  if (!fullRedraw) return;
  clearAll();
  drawHeader("FACTORY RESET?", COLOR_ALERT_BG, COLOR_ALERT_TEXT);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(16, 60);
  tft.print("Wipe stats,");
  tft.setCursor(16, 85);
  tft.print("name, and owner.");
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(16, 120);
  tft.print("Device will reboot immediately.");

  tft.fillRect(0, SCREEN_H - 24, SCREEN_W, 24, COLOR_FOOTER_BG);
  tft.setTextColor(COLOR_FG);
  tft.setTextSize(2);
  tft.setCursor(12, SCREEN_H - 20);
  tft.print("[C] Cancel");
  tft.setCursor(SCREEN_W - 170, SCREEN_H - 20);
  tft.print("[A] Confirm");
}

void renderFatal(const char* msg) {
  clearAll();
  drawHeader("FATAL", COLOR_ALERT_BG, COLOR_ALERT_TEXT);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(16, 80);
  tft.print(msg);
}
