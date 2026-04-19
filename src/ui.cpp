#include "ui.h"
#include "config.h"
#include "persist.h"
#include "pet.h"
#include <Arduino.h>
#include <cstring>
#include "TFT_eSPI.h"

static TFT_eSPI tft;

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
  static std::string lastOwner;

  if (fullRedraw) {
    clearAll();
    drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
    tft.fillCircle(SCREEN_W - 20, 14, 5, COLOR_OK);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(SCREEN_W - 100, 10);
    tft.print("connected");
    tft.setCursor(8, 36);                tft.print("Level");
    tft.setCursor(SCREEN_W - 120, 36);   tft.print("Tokens today");
    tft.setCursor(28, 66);   tft.print("Total");
    tft.setCursor(130, 66);  tft.print("Running");
    tft.setCursor(240, 66);  tft.print("Waiting");
    // Invalidate caches so every block repaints on the fresh canvas.
    lastLvl = INT32_MIN; lastTokens = INT64_MIN;
    lastTotal = INT32_MIN; lastRunning = INT32_MIN; lastWaiting = INT32_MIN;
    lastMsg.clear(); lastEntries.clear();
    lastPet = (PetState)-1; lastOwner.clear();
  }

  int32_t lvl = persistGet().lvl;
  if (lvl != lastLvl) {
    tft.fillRect(8, 46, 60, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(2);
    char buf[8];
    snprintf(buf, sizeof(buf), "L%d", lvl);
    tft.setCursor(8, 46);
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

  // Size 5 ~30px per char; 90px per cell.
  auto drawNum = [](int x, int n) {
    tft.fillRect(x, 80, 90, 40, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(5);
    char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
    tft.setCursor(x, 82); tft.print(buf);
  };
  if (s.hb.total   != lastTotal)   { drawNum(38,  s.hb.total);   lastTotal   = s.hb.total; }
  if (s.hb.running != lastRunning) { drawNum(148, s.hb.running); lastRunning = s.hb.running; }
  if (s.hb.waiting != lastWaiting) { drawNum(258, s.hb.waiting); lastWaiting = s.hb.waiting; }

  if (s.hb.msg != lastMsg) {
    tft.fillRect(0, 125, SCREEN_W, 14, COLOR_BG);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextSize(1);
    tft.setCursor(8, 128);
    tft.print(s.hb.msg.c_str());
    lastMsg = s.hb.msg;
  }

  // Transcript shrunk from 3 to 2 lines to make room for the pet.
  if (s.hb.entries != lastEntries) {
    tft.fillRect(0, 145, SCREEN_W, 40, COLOR_BG);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextSize(1);
    size_t n = s.hb.entries.size() < 2 ? s.hb.entries.size() : 2;
    for (size_t i = 0; i < n; ++i) {
      tft.setCursor(8, 148 + (int)i * 18);
      tft.print(s.hb.entries[i].c_str());
    }
    lastEntries = s.hb.entries;
  }

  PetState st = petComputeState(s);
  if (st != lastPet) {
    uint16_t petColour = (st == PetState::Attention) ? COLOR_ALERT_FG : COLOR_OK;
    tft.fillRect(120, 188, 80, 32, COLOR_BG);
    tft.setTextColor(petColour, COLOR_BG);
    tft.setTextSize(1);
    const char* const* rows = petFace(st);
    for (size_t i = 0; i < PET_FACE_LINES; ++i) {
      tft.setCursor(130, 188 + (int)i * 8);
      tft.print(rows[i]);
    }
    lastPet = st;
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

void renderFatal(const char* msg) {
  clearAll();
  drawHeader("FATAL", COLOR_ALERT_BG, COLOR_ALERT_TEXT);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(16, 80);
  tft.print(msg);
}
