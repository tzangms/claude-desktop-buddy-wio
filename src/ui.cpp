#include "ui.h"
#include "config.h"
#include <Arduino.h>
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
  pinMode(LCD_BACKLIGHT, OUTPUT);
  analogWrite(LCD_BACKLIGHT, BACKLIGHT_FULL);
  tft.begin();
  tft.setRotation(3);
  clearAll();
}

void setBacklight(uint8_t pct) {
  if (pct > 100) pct = 100;
  analogWrite(LCD_BACKLIGHT, (uint8_t)((uint16_t)BACKLIGHT_FULL * pct / 100));
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

void renderIdle(const AppState& s) {
  clearAll();

  drawHeader("Claude Buddy", COLOR_BG, COLOR_FG);
  tft.fillCircle(SCREEN_W - 20, 14, 5, COLOR_OK);
  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(SCREEN_W - 100, 10);
  tft.print("connected");

  tft.setTextColor(COLOR_DIM, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(28, 50);   tft.print("Total");
  tft.setCursor(130, 50);  tft.print("Running");
  tft.setCursor(240, 50);  tft.print("Waiting");

  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(5);
  auto drawNum = [](int x, int n) {
    char buf[8]; snprintf(buf, sizeof(buf), "%d", n);
    tft.setCursor(x, 70); tft.print(buf);
  };
  drawNum(38,  s.hb.total);
  drawNum(148, s.hb.running);
  drawNum(258, s.hb.waiting);

  tft.fillRect(0, 160, SCREEN_W, 20, COLOR_BG);
  tft.setTextColor(COLOR_FG, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(8, 165);
  tft.print(s.hb.msg.c_str());

  if (!s.ownerName.empty()) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Hi, %s", s.ownerName.c_str());
    drawFooter(buf);
  } else {
    drawFooter("");
  }
}

void renderPrompt(const AppState& s) {
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
