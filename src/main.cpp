#include <Arduino.h>
#include "TFT_eSPI.h"

TFT_eSPI tft;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}  // wait briefly for USB CDC enumeration
  pinMode(LCD_BACKLIGHT, OUTPUT);
  digitalWrite(LCD_BACKLIGHT, HIGH);
  tft.begin();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Claude Buddy boot OK");
}

void loop() {
  Serial.println("alive");
  delay(1000);
}
