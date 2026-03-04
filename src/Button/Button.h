#pragma once
#include <Arduino.h>
namespace EdgeOn {
class Button {
private:
  uint8_t pin;
  unsigned long debounceTime;
  unsigned long pressStart = 0;
  bool lastValue = false;

public:
  Button(uint8_t buttonPin, unsigned long debounceMs = 50)
    : pin(buttonPin), debounceTime(debounceMs) {
  }
  bool isActive() {
    int reading = digitalRead(pin);
    // debounce
    if (reading == LOW) {
      unsigned long now = millis();
      if (now - pressStart > debounceTime && pressStart > 1) {
        pressStart = 0;
        lastValue = true;
        return true;
      } else {
        if (lastValue) {
          pressStart = 0;
          return false;
        } else {
          pressStart = millis();
        }
      }
    } else {
      // HIGH
      if (lastValue) {
        lastValue = false;
      }
    }
    return false;
  }
};
}