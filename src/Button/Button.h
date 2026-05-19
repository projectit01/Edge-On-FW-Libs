#pragma once
#include <Arduino.h>
namespace EdgeOn {
class Button {
private:
  uint8_t pin;
  unsigned long debounceTime;
  unsigned long rearmDelayTime;
  unsigned long pressStart = 0;
  unsigned long lastActivationTime = 0;
  bool lastValue = false;

public:
  Button(uint8_t buttonPin, unsigned long debounceMs = 50, unsigned long rearmDelayMs = 150)
    : pin(buttonPin), debounceTime(debounceMs), rearmDelayTime(rearmDelayMs) {
  }
  bool isActive() {
    bool pressed = (digitalRead(pin) == LOW);
    unsigned long now = millis();

    if (!pressed) {
      pressStart = 0;
      lastValue = false;
      return false;
    }

    if (lastValue) {
      return false;
    }

    if (lastActivationTime != 0 && now - lastActivationTime < rearmDelayTime) {
      return false;
    }

    if (pressStart == 0) {
      pressStart = now;
      return false;
    }

    if (now - pressStart >= debounceTime) {
      lastValue = true;
      lastActivationTime = now;
      return true;
    }

    return false;
  }
};
}
