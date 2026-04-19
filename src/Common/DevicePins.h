#pragma once

#include <Arduino.h>

namespace EdgeOn
{
  struct DevicePins
  {
    int ledPairingPin = 2;
    int pairingPin = 5;
    String connectSecretKey;
    String deviceModel;
  };
}
