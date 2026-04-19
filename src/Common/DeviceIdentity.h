#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace EdgeOn
{
  inline bool isInvalidMacId(const String &id)
  {
    return id.length() == 0 || id.endsWith("000000000000");
  }

  inline String buildDefaultDeviceId(const String &prefix = "DO-")
  {
    uint64_t chipMac = ESP.getEfuseMac();
    if (chipMac == 0)
    {
      return "";
    }

    char mac[13];
    snprintf(mac, sizeof(mac), "%04X%08X",
             static_cast<uint16_t>((chipMac >> 32) & 0xFFFF),
             static_cast<uint32_t>(chipMac & 0xFFFFFFFF));

    String macId(mac);
    if (macId == "000000000000")
    {
      return "";
    }

    return prefix + macId;
  }
}
