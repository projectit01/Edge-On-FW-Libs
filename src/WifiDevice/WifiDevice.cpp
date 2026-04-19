#include "WifiDevice.h"
#include "../Common/DeviceIdentity.h"

namespace EdgeOn
{

  void WifiDevice::setup()
  {
    loadConfig();
    if (ssid == "")
    {
      needConfig = true;
      startAPMode();
    }
    else
    {
      WiFi.begin(ssid.c_str(), password.c_str());
      mqttClient.setServer(httpBase.c_str(), mqttPort);
      mqttClient.setCallback([this](char *t, byte *p, unsigned int l)
                             { this->onMqttMessage(t, p, l); });
    }
  }

  void WifiDevice::loop()
  {
    if (needConfig)
    {
      server.handleClient();
      return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      if (millis() - lastWiFiRetry > 5 * 60 * 1000)
      {
        WiFi.begin(ssid.c_str(), password.c_str());
        lastWiFiRetry = millis();
      }
    }
    else
    {
      if (!mqttClient.connected())
      {
        unsigned long now = millis();
        if (lastMqttAttempt == 0)
        {
          for (int i = 0; i < 5; i++)
          {
            if (reconnectMQTT())
            {
              break;
            }
            delay(500);
          }
        }
        else
        {
          if (now - lastMqttAttempt > 5 * 60 * 1000)
          {
            mqttLog("MQTT connect Failed");
            reconnectMQTT();
            lastMqttAttempt = now;
          }
        }
      }
      else
      {
        mqttClient.loop();
        lastMqttAttempt = 0;
      }
    }
  }

  void WifiDevice::loadConfig()
  {
    preferences.begin("wifi", true);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("pass", "");
    httpBase = preferences.getString("ip", "");
    deviceId = preferences.getString("deviceId", "");
    zone = preferences.getString("zone", "");
    domain = preferences.getString("domain", "");
    log = preferences.getString("log", "");
    preferences.end();
  }

  void WifiDevice::saveConfig(String ssid, String pass, String ip)
  {
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.putString("ip", ip);
    preferences.end();
  }

  void WifiDevice::saveConfigDefault(String deviceId, String zone, String domain)
  {
    preferences.begin("wifi", true);
    String getDeviceId = preferences.getString("deviceId", "");
    String getZone = preferences.getString("zone", "");
    String getDomain = preferences.getString("domain", "");

    mqttLog(getDeviceId);
    mqttLog(getZone);
    mqttLog(getDomain);

    preferences.end();
    preferences.begin("wifi", false);
    if ((getDeviceId.length() < 2 || isInvalidMacId(getDeviceId)) &&
        deviceId.length() > 0 && !isInvalidMacId(deviceId))
    {
      mqttLog("Save default config");
      preferences.putString("deviceId", deviceId);
    }
    if (getZone.length() < 2)
    {
      preferences.putString("zone", zone);
      preferences.putString("domain", domain);
    }
    preferences.end();
    return;
  }

  void WifiDevice::clearConfig(const String &nsName)
  {
    preferences.begin(nsName.c_str(), false); // false = ghi
    preferences.clear();                      // xoá toàn bộ key trong namespace
    preferences.end();
    mqttLog("✅ Config cleared from EEPROM");
  }

  void WifiDevice::startAPMode()
  {
    WiFi.softAP("ESP32_Config");
    server.on("/", [this]()
              {
    String html = "<form action='/save'>SSID:<input name='ssid'><br>Password:<input name='pass'><br>Server IP:<input name='ip'><br><input type='submit'></form>";
    server.send(200, "text/html", html); });

    server.on("/save", [this]()
              {
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    String ip = server.arg("ip");
    if (ip == "") ip = "";
    saveConfig(newSsid, newPass, ip);
    server.send(200, "text/html", "Restart...");
    delay(500);
    ESP.restart(); });

    server.begin();
  }

  bool WifiDevice::reconnectMQTT()
  {
    loadConfig();
    if (httpBase == "" || httpBase.length() == 0)
    {
      mqttLog("No IP in config → update from server...");
      updateHostIp();
    }
    mqttLog("load config");
    mqttLog(httpBase);
    mqttLog(String(mqttPort));
    mqttLog(deviceId);
    mqttClient.setServer(httpBase.c_str(), mqttPort);
    mqttLog("Connecting to MQTT...");
    if (mqttClient.connect(deviceId.c_str(), mqttUser, mqttPass))
    {
      mqttLog("✅ MQTT connected");
      displayLcdMessage("MQTT connected", 0, 0);

      mqttClient.subscribe((String("platform/+/") + deviceId + "/ota").c_str());
      mqttClient.subscribe((String("platform/+/") + deviceId + "/command/#").c_str());
      mqttClient.subscribe((String("platform/+/") + deviceId + "/setting/#").c_str());
      mqttClient.subscribe((String("platform/+/") + deviceId + "/ping").c_str());
      // hook
      publishConnectedMessage();
      onConnected();

      return true;
    }
    displayLcdMessage("MQTT failed", 0, 0);
    mqttLog("❌ MQTT failed");
    updateHostIp();
    return false;
  }

  void WifiDevice::performOTA(const char *firmwarePath)
  {
    String filePath = String(firmwarePath);
    String url;

    if (filePath.startsWith("http://") || filePath.startsWith("https://"))
    {
      url = filePath;
    }
    else
    {
      mqttLog("Invalid OTA path: must be full URL");
      return;
    }

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
      int contentLength = http.getSize();
      WiFiClient *stream = http.getStreamPtr();
      if (Update.begin(contentLength))
      {
        Update.writeStream(*stream);
        if (Update.end(true))
        {
          ESP.restart();
        }
      }
    }
    http.end();
  }

  void WifiDevice::updateHostIp()
  {
    if (log != "2")
    {
      return;
    }
    delay(1000); // chờ ổn định WiFi

    if (WiFi.status() == WL_CONNECTED)
    {

      HTTPClient http;
      String getHostIp = domain + "/dashboard/device-setting/server-ip";
      http.begin(getHostIp);

      int httpCode = http.GET();
      if (httpCode == 200)
      {
        String payload = http.getString();
        // mqttLog("Response: " + payload);

        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
          // truy cập ip trong payload.ip
          const char *ip = doc["payload"]["ip"];
          if (ip)
          {
            String hostIp = String(ip); // gán vào biến tạm runtime
            // mqttLog("Parsed IP: " + hostIp);

            preferences.begin("wifi", false);
            preferences.putString("ip", hostIp);
            preferences.end();
            mqttLog("Saved to EEPROM");
          }
        }
        else
        {
          mqttLog("JSON parse error: ");
          // mqttLog(error.c_str());
        }
      }
      else
      {
        // mqttLog(String("HTTP error:") + httpCode);
      }
      http.end();
    }
    else
    {
      mqttLog("Update Host Ip - WiFi not connected");
    }

    delay(1000);
  }

  void WifiDevice::handleButtons()
  {
    int readingPair = digitalRead(pairingPin);

    if (readingPair == LOW)
    {
      // nút đang được nhấn
      if (pairingPressStart == 0)
      {
        pairingPressStart = millis(); // bắt đầu đếm thời gian nhấn
      }
      else if (!pairingActive && (millis() - pairingPressStart >= PAIRING_HOLD_TIME))
      {
        pairingActive = true;
        mqttLog("✅ Pairing success, blinking LED for 3s...");

        unsigned long startBlink = millis();
        while (millis() - startBlink < 3000)
        {
          ledBlink();
        }
        needConfig = true;
        startAPMode();
      }
    }
    else
    {
      // nút được thả
      pairingPressStart = 0;
      pairingActive = false;
    }
    return;
  }

  void WifiDevice::ledBlink(uint8_t brightness, uint16_t speedMs, uint8_t times)
  {
    // brightness: 0 - 255
    // speedMs  : delay cho mỗi trạng thái (ms)
    // times    : số lần nháy

    for (uint8_t i = 0; i < times; i++)
    {
      analogWrite(ledPairingPin, brightness);
      delay(speedMs);

      analogWrite(ledPairingPin, 0);
      delay(speedMs);
    }
  }

  void WifiDevice::onMqttMessage(char *topic, byte *payload, unsigned int length)
  {
    mqttBusy = true; // bắt đầu xử lý MQTT → chặn nút bấm
    String msg;
    msg.reserve(length);
    String topicStr(topic);

    String baseTopic = String("platform/") + zone + "/" + deviceId + "/";

    for (int i = 0; i < length; i++)
    {
      msg += (char)payload[i];
    }
    mqttLog(topicStr);
    // OTA
    String otaTopic = baseTopic + "ota";
    if (topicStr == otaTopic)
    {
      StaticJsonDocument<256> otaDoc;
      DeserializationError otaError = deserializeJson(otaDoc, payload, length);
      if (!otaError)
      {
        const char *secretKey = otaDoc["secretKey"] | otaDoc["secret"] | "";
        const char *pathValue = otaDoc["url"] | otaDoc["targetUrl"] | otaDoc["targetFile"] | "";
        if (strcmp(secretKey, expectedSecret) == 0 && pathValue[0] != '\0')
        {
          String otaPath = String(pathValue);
          otaPath.trim();
          performOTA(otaPath.c_str());
          String respTopic = baseTopic + "ota-response";
          mqttClient.publish(respTopic.c_str(), "OTA started...");
        }
      }
    }

    // Reset
    String restartTopic = baseTopic + "command/restart";
    if (topicStr == restartTopic && msg.equals(expectedSecret))
    {
      mqttLog("ESP restart");
      delay(500);
      ESP.restart();
    }

    // COMMAND
    // String cmdTopic = String("platform/") + zone + "/" + deviceId + "/command/get";
    // if (String(topic) == cmdTopic) {
    // }

    // SETTING - clear eeprom
    String settingTopicDeleteEeprom = baseTopic + "setting/delete-eeprom";
    if (topicStr == settingTopicDeleteEeprom && msg.equals(expectedSecret))
    {
      mqttLog("settingTopicDeleteEeprom");
      clearConfig("wifi");
      delay(500);
      ESP.restart();
    }

    // SETTING - set
    String settingTopic = baseTopic + "setting/set";

    if (topicStr == settingTopic)
    {
      Serial.println("[SETTING] Nhận cấu hình mới");

      // Chuyển payload (byte*) thành String
      Serial.print("[SETTING] Payload raw: ");
      Serial.println(msg);

      // Parse JSON
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, payload, length);
      if (error)
      {
        Serial.print("[SETTING] Failed to parse JSON: ");
        Serial.println(error.f_str());
        mqttLog("Failed to parse JSON");
        return;
      }

      // Kiểm tra secret
      const char *secret = doc["secret"] | "";
      if (strcmp(secret, expectedSecret) != 0)
      {
        Serial.println("[SETTING] Sai secret -> bỏ qua");
        return;
      }

      // Đọc các field
      const char *newDeviceId = doc["deviceId"] | "";
      const char *newZone = doc["zone"] | "";
      const char *newIp = doc["ip"] | "";
      const char *newSsid = doc["ssid"] | "";
      const char *newPass = doc["password"] | "";

      const char *newLog = doc["log"] | "";

      // In ra debug
      Serial.println("[SETTING] Dữ liệu mới:");
      if (newDeviceId[0] != '\0')
      {
        Serial.print("  deviceId = ");
        Serial.println(newDeviceId);
      }
      if (newZone[0] != '\0')
      {
        Serial.print("  zone     = ");
        Serial.println(newZone);
      }
      if (newIp[0] != '\0')
      {
        Serial.print("  ip       = ");
        Serial.println(newIp);
      }
      if (newSsid[0] != '\0')
      {
        Serial.print("  ssid     = ");
        Serial.println(newSsid);
      }
      if (newPass[0] != '\0')
      {
        Serial.print("  password = ");
        Serial.println(newPass);
      }
      if (newLog[0] != '\0')
      {
        Serial.print("  newLog= ");
        Serial.println(newLog);
      }

      // Lưu vào Preferences
      preferences.begin("wifi", false);
      if (newDeviceId[0] != '\0')
        preferences.putString("deviceId", newDeviceId);
      if (newZone[0] != '\0')
        preferences.putString("zone", newZone);
      if (newIp[0] != '\0')
        preferences.putString("ip", newIp);
      if (newSsid[0] != '\0')
        preferences.putString("ssid", newSsid);
      if (newPass[0] != '\0')
        preferences.putString("password", newPass);
      if (newLog[0] != '\0')
      {
        preferences.putString("log", newLog);
      }
      preferences.end();

      mqttLog("settingTopicSet");

      delay(500);
      ESP.restart();
    }

    // SETTING - getSeting
    String getSettingTopic = baseTopic + "setting/get";
    if (topicStr == getSettingTopic)
    {
      mqttLog("setting/get");
      StaticJsonDocument<256> doc;
      doc["deviceId"] = deviceId;
      // doc["zone"] = zone;
      doc["ip"] = httpBase;
      doc["ssid"] = ssid;
      doc["password"] = password;
      // doc["log"] = log;
      doc["fwVersion"] = fwVersion;

      char responsePayload[256];
      size_t written = serializeJson(doc, responsePayload, sizeof(responsePayload));
      if (written > 0)
      {
        String getSettingResponseTopic = baseTopic + "response/setting-get";
        mqttClient.publish(getSettingResponseTopic.c_str(), responsePayload);
      }
    }

    // PING
    String pingTopic = baseTopic + "ping";
    if (topicStr == pingTopic)
    {
      mqttLog("📡 Ping received");
      ledBlink();
      String responseTopic = baseTopic + "response";
      mqttClient.publish(responseTopic.c_str(), "pong");
    }

    // === Hook cho custom feature ===
    handleCustomMqttMessage(topicStr, msg);

    mqttBusy = false; // xong MQTT → cho phép nút hoạt động lại
  }

  void WifiDevice::mqttLog(const String &msg)
  {
    Serial.println("----- ----- -----");
    Serial.println(msg);
    return;
    if (log == "2")
      return; // prod -> bỏ qua log
    Serial.println("----- ----- -----");
    Serial.println(msg);

    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      String url = "http://" + httpBase + ":8002/dashboard/device-control/logs";
      if (log == "1")
      {
        // proxy 80
        url = "http://" + httpBase + ":80/dashboard/device-control/logs";
      }
      Serial.print("[HTTP LOG] URL: ");
      Serial.println(url);
      http.begin(url);
      http.addHeader("Content-Type", "application/json");

      String payload = "{\"deviceId\":\"" + deviceId + "\",\"log\":\"" + msg + "\"}";
      int httpCode = http.POST(payload);

      if (httpCode > 0)
      {
        Serial.printf("[HTTP LOG] sent, code=%d\n", httpCode);
      }
      else
      {
        Serial.printf("[HTTP LOG] failed, error=%s\n", http.errorToString(httpCode).c_str());
      }

      http.end();
    }
  }

  void WifiDevice::publishConnectedMessage()
  {
    String topic = buildConnectedPublishTopic();
    String payload = buildConnectedPublishPayload();

    if (topic.length() == 0 || payload.length() == 0 || !mqttClient.connected())
    {
      return;
    }

    mqttClient.publish(topic.c_str(), payload.c_str());
  }

}
