#include <Arduino.h>
#include "ui.h"
#include "buttons.h"

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  initUi();
  initButtons();
  renderBoot("Button test — press A/B/C/Nav");
}

void loop() {
  uint32_t now = millis();
  ButtonEvent e = pollButtons(now);
  if (e != ButtonEvent::None) {
    const char* name = "?";
    switch (e) {
      case ButtonEvent::PressA:   name = "A pressed";   break;
      case ButtonEvent::PressB:   name = "B pressed";   break;
      case ButtonEvent::PressC:   name = "C pressed";   break;
      case ButtonEvent::PressNav: name = "Nav pressed"; break;
      default: break;
    }
    Serial.println(name);
    renderBoot(name);
  }
}
