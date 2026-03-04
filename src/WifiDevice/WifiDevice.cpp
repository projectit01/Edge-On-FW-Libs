#include "WifiDevice.h"

namespace EdgeOn {

void WifiDevice::setup() {
  loadConfig();
  if (ssid == "") {
    needConfig = true;
    startAPMode();
  } else {
    WiFi.begin(ssid.c_str(), password.c_str());
    mqttClient.setServer(httpBase.c_str(), mqttPort);
    mqttClient.setCallback([this](char *t, byte *p, unsigned int l) {
      this->onMqttMessage(t, p, l);
    });
  }
}

void WifiDevice::loop() {
  if (needConfig) {
    server.handleClient();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() - lastWiFiRetry > 5 * 60 * 1000) {
      WiFi.begin(ssid.c_str(), password.c_str());
      lastWiFiRetry = millis();
    }
  } else {
    if (!mqttClient.connected()) {
      unsigned long now = millis();
      if (lastMqttAttempt == 0) {
        for (int i = 0; i < 5; i++) {
          if (reconnectMQTT()) {
            break;
          }
          delay(500);
        }
      } else {
        if (now - lastMqttAttempt > 5 * 60 * 1000) {
          mqttLog("MQTT connect Failed");
          reconnectMQTT();
          lastMqttAttempt = now;
        }
      }

    } else {
      mqttClient.loop();
      lastMqttAttempt = 0;
    }
  }
}

void WifiDevice::loadConfig() {
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

void WifiDevice::saveConfig(String ssid, String pass, String ip) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.putString("ip", ip);
  preferences.end();
}

void WifiDevice::saveConfigDefault(String deviceId, String zone, String domain) {
  preferences.begin("wifi", true);
  String getDeviceId = preferences.getString("deviceId", "");
  String getZone = preferences.getString("zone", "");
  String getDomain = preferences.getString("domain", "");

  mqttLog(getDeviceId);
  mqttLog(getZone);
  mqttLog(getDomain);

  preferences.end();
  preferences.begin("wifi", false);
  if (getDeviceId.length() < 2 || getZone.length() < 2) {
    mqttLog("Save default config");
    preferences.putString("deviceId", deviceId);
    preferences.putString("zone", zone);
    preferences.putString("domain", domain);
  }
  preferences.end();
  return;
}

void WifiDevice::clearConfig(const String &nsName) {
  preferences.begin(nsName.c_str(), false);  // false = ghi
  preferences.clear();                       // xoá toàn bộ key trong namespace
  preferences.end();
  mqttLog("✅ Config cleared from EEPROM");
}

void WifiDevice::startAPMode() {
  WiFi.softAP("ESP32_Config");
  server.on("/", [this]() {
    String html = "<form action='/save'>SSID:<input name='ssid'><br>Password:<input name='pass'><br>Server IP:<input name='ip'><br><input type='submit'></form>";
    server.send(200, "text/html", html);
  });

  server.on("/save", [this]() {
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    String ip = server.arg("ip");
    if (ip == "") ip = "";
    saveConfig(newSsid, newPass, ip);
    server.send(200, "text/html", "Restart...");
    delay(500);
    ESP.restart();
  });

  server.begin();
}

bool WifiDevice::reconnectMQTT() {
  loadConfig();
  if (httpBase == "" || httpBase.length() == 0) {
    mqttLog("No IP in config → update from server...");
    updateHostIp();
  }
  mqttLog("load config");
  mqttLog(httpBase);
  mqttLog(String(mqttPort));
  mqttLog(deviceId);
  mqttClient.setServer(httpBase.c_str(), mqttPort);
  mqttLog("Connecting to MQTT...");
  // if (mqttClient.connect(deviceId.c_str(), "iotdevice", "123456")) {
  if (mqttClient.connect(deviceId.c_str(), mqttUser, mqttPass)) {
    mqttLog("✅ MQTT connected");
    displayLcdMessage("MQTT connected", 0, 0);

    mqttClient.subscribe((String("platform/+/") + deviceId + "/ota").c_str());
    mqttClient.subscribe((String("platform/+/") + deviceId + "/command/#").c_str());
    mqttClient.subscribe((String("platform/+/") + deviceId + "/setting/#").c_str());
    mqttClient.subscribe((String("platform/+/") + deviceId + "/ping").c_str());
    // hook
    onConnected();

    return true;
  }
  displayLcdMessage("MQTT failed", 0, 0);
  mqttLog("❌ MQTT failed");
  updateHostIp();
  return false;
}

void WifiDevice::performOTA(const char *firmwarePath) {
  String url = "http://" + httpBase + ":" + String(80) + firmwarePath;
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    WiFiClient *stream = http.getStreamPtr();
    if (Update.begin(contentLength)) {
      Update.writeStream(*stream);
      if (Update.end(true)) {
        ESP.restart();
      }
    }
  }
  http.end();
}

void WifiDevice::updateHostIp() {
  return;
  if (log != "2") {
    return;
  }
  delay(1000);  // chờ ổn định WiFi

  if (WiFi.status() == WL_CONNECTED) {
    mqttLog("updateHostIp");

    HTTPClient http;
    String getHostIp = domain + "/dashboard/device-setting/server-ip";
    http.begin(getHostIp);

    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      // mqttLog("Response: " + payload);

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        // truy cập ip trong payload.ip
        const char *ip = doc["payload"]["ip"];
        if (ip) {
          String hostIp = String(ip);  // gán vào biến tạm runtime
          // mqttLog("Parsed IP: " + hostIp);

          preferences.begin("wifi", false);
          preferences.putString("ip", hostIp);
          preferences.end();
          mqttLog("Saved to EEPROM");
        }
      } else {
        mqttLog("JSON parse error: ");
        // mqttLog(error.c_str());
      }
    } else {
      // mqttLog(String("HTTP error:") + httpCode);
    }
    http.end();
  } else {
    mqttLog("updateHostIp - WiFi not connected");
  }

  delay(1000);
}

void WifiDevice::handleButtons() {
  int readingPair = digitalRead(pairingPin);

  if (readingPair == LOW) {
    // nút đang được nhấn
    if (pairingPressStart == 0) {
      pairingPressStart = millis();  // bắt đầu đếm thời gian nhấn
    } else if (!pairingActive && (millis() - pairingPressStart >= PAIRING_HOLD_TIME)) {
      pairingActive = true;
      mqttLog("✅ Pairing success, blinking LED for 3s...");

      unsigned long startBlink = millis();
      while (millis() - startBlink < 3000) {
        ledBlink();
      }
      needConfig = true;
      startAPMode();
    }
  } else {
    // nút được thả
    pairingPressStart = 0;
    pairingActive = false;
  }
  return;
}

void WifiDevice::ledBlink(uint8_t brightness, uint16_t speedMs, uint8_t times) {
  // brightness: 0 - 255
  // speedMs  : delay cho mỗi trạng thái (ms)
  // times    : số lần nháy

  for (uint8_t i = 0; i < times; i++) {
    analogWrite(ledPairingPin, brightness);
    delay(speedMs);

    analogWrite(ledPairingPin, 0);
    delay(speedMs);
  }
}

void WifiDevice::onMqttMessage(char *topic, byte *payload, unsigned int length) {
  mqttBusy = true;  // bắt đầu xử lý MQTT → chặn nút bấm
  String msg;

  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  // OTA
  String otaTopic = String("platform/") + zone + "/" + deviceId + "/ota";
  if (String(topic) == otaTopic && msg.equals(expectedSecret)) {
    performOTA("/dashboard/firmware/files/esp32-do-sensor.bin");

    // publish kết quả OTA về ota-response
    String respTopic = String("platform/") + zone + "/" + deviceId + "/ota-response";
    mqttClient.publish(respTopic.c_str(), "OTA started...");
  }

  // Reset
  String restartTopic = String("platform/") + zone + "/" + deviceId + "/command/restart";
  if (String(topic) == restartTopic && msg.equals(expectedSecret)) {
    mqttLog("ESP restart");
    delay(500);
    ESP.restart();
  }

  // COMMAND
  // String cmdTopic = String("platform/") + zone + "/" + deviceId + "/command/get";
  // if (String(topic) == cmdTopic) {
  // }

  // SETTING - clear eeprom
  String settingTopicDeleteEeprom = String("platform/") + zone + "/" + deviceId + "/setting/delete-eeprom";
  if (String(topic) == settingTopicDeleteEeprom && msg.equals(expectedSecret)) {
    mqttLog("settingTopicDeleteEeprom");
    clearConfig("wifi");
    delay(500);
    ESP.restart();
  }

  // SETTING - set
  String settingTopic = String("platform/") + zone + "/" + deviceId + "/setting/set";

  if (String(topic) == settingTopic) {
    Serial.println("[SETTING] Nhận cấu hình mới");

    // Chuyển payload (byte*) thành String
    String msg;
    for (unsigned int i = 0; i < length; i++) {
      msg += (char)payload[i];
    }
    Serial.print("[SETTING] Payload raw: ");
    Serial.println(msg);

    // Parse JSON
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error) {
      Serial.print("[SETTING] Failed to parse JSON: ");
      Serial.println(error.f_str());
      mqttLog("Failed to parse JSON");
      return;
    }

    // Kiểm tra secret
    String secret = doc["secret"] | "";
    if (secret != expectedSecret) {
      Serial.println("[SETTING] Sai secret -> bỏ qua");
      return;
    }

    // Đọc các field
    String newDeviceId = doc["deviceId"] | "";
    String newZone = doc["zone"] | "";
    String newIp = doc["ip"] | "";
    String newSsid = doc["ssid"] | "";
    String newPass = doc["password"] | "";

    String newLog = doc["log"] | "";

    // In ra debug
    Serial.println("[SETTING] Dữ liệu mới:");
    if (newDeviceId.length() > 0) Serial.println("  deviceId = " + newDeviceId);
    if (newZone.length() > 0) Serial.println("  zone     = " + newZone);
    if (newIp.length() > 0) Serial.println("  ip       = " + newIp);
    if (newSsid.length() > 0) Serial.println("  ssid     = " + newSsid);
    if (newPass.length() > 0) Serial.println("  password = " + newPass);
    if (newLog.length() > 0) Serial.println("  newLog= " + String(newLog));

    // Lưu vào Preferences
    preferences.begin("wifi", false);
    if (newDeviceId.length() > 0) preferences.putString("deviceId", newDeviceId);
    if (newZone.length() > 0) preferences.putString("zone", newZone);
    if (newIp.length() > 0) preferences.putString("ip", newIp);
    if (newSsid.length() > 0) preferences.putString("ssid", newSsid);
    if (newPass.length() > 0) preferences.putString("password", newPass);
    if (newLog.length() > 0) {
      preferences.putString("log", String(newLog));
    }
    preferences.end();

    mqttLog("settingTopicSet");

    delay(500);
    ESP.restart();
  }

  // PING
  String pingTopic = String("platform/") + zone + "/" + deviceId + "/ping";
  if (String(topic) == pingTopic) {
    mqttLog("📡 Ping received");
    ledBlink();
    mqttClient.publish(
      (String("platform/") + zone + "/" + deviceId + "/response").c_str(),
      "pong");
  }

  // === Hook cho custom feature ===
  handleCustomMqttMessage(String(topic), msg);

  mqttBusy = false;  // xong MQTT → cho phép nút hoạt động lại
}

void WifiDevice::mqttLog(const String &msg) {
  Serial.println("----- ----- -----");
  Serial.println(msg);
  return;
  if (log == "2") return;  // prod -> bỏ qua log
  Serial.println("----- ----- -----");
  Serial.println(msg);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://" + httpBase + ":8002/dashboard/device-control/logs";
    if (log == "1") {
      // proxy 80
      url = "http://" + httpBase + ":80/dashboard/device-control/logs";
    }
    Serial.print("[HTTP LOG] URL: ");
    Serial.println(url);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"deviceId\":\"" + deviceId + "\",\"log\":\"" + msg + "\"}";
    int httpCode = http.POST(payload);

    if (httpCode > 0) {
      Serial.printf("[HTTP LOG] sent, code=%d\n", httpCode);
    } else {
      Serial.printf("[HTTP LOG] failed, error=%s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}


}