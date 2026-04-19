#pragma once

namespace EdgeOn
{
  constexpr unsigned long WIFI_RETRY_INTERVAL = 5UL * 60UL * 1000UL;
  constexpr unsigned long MQTT_RETRY_INTERVAL = 2000UL;
  constexpr const char *API_SECRET_KEY = "123456";
}

#define PAIRING_HOLD_TIME 3000
