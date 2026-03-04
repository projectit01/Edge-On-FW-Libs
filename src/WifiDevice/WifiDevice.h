#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Wire.h>
#include <ArduinoJson.h>

namespace EdgeOn {
constexpr unsigned long WIFI_RETRY_INTERVAL = 5UL * 60UL * 1000UL;  // 5 phút
constexpr unsigned long MQTT_RETRY_INTERVAL = 2000UL;               // 2 giây

#define PAIRING_HOLD_TIME 3000


struct DevicePins {
  int ledPairingPin = 2;
  int pairingPin = 34;
  String connectSecretKey;
  String deviceModel;
};
class WifiDevice {
public:
  WifiDevice(DevicePins cfg)
    : mqttClient(espClient), server(80) {
    Serial.begin(115200);
    ledPairingPin = cfg.ledPairingPin;
    pairingPin = cfg.pairingPin;
    connectSecretKey = cfg.connectSecretKey;
    deviceModel = cfg.deviceModel;
  }
  void setup();  // entrypoint
  void loop();   // vòng lặp
  // ==== Trạng thái thiết bị ====
  unsigned long pairingPressStart = 0;
  bool pairingActive = false;
  bool needConfig = false;
  unsigned long lastWiFiRetry;
  unsigned long lastMqttAttempt = 0;
  int pairingPin;
  int ledPairingPin;
  bool mqttBusy = false;

  String deviceId;
  String zone = "Nzj9gp3RYJjNQ1NDdlYWM2Y2Y3ZWZjZ1";
  String domain = "https://app.phusadtcfarm.com";

  // log 0=dev, 1=test, 2=prod
  String log = "0";


  // ==== Core methods (class con có thể override) ====
  void onMqttMessage(char *topic, byte *payload, unsigned int length);

  // ==== Utility methods (có sẵn) ====
  void handleButtons();
  void loadConfig();
  void saveConfig(String ssid, String pass, String ip);

  void startAPMode();
  bool connectWiFi();
  bool reconnectMQTT();
  void performOTA(const char *firmwarePath);
  void updateHostIp();
  void ledBlink(uint8_t brightness = 250, uint16_t speedMs = 200, uint8_t times = 5);
  void mqttLog(const String &msg);
  void clearConfig(const String &nsName);
  virtual void handleCustomMqttMessage(const String &topic, const String &msg) {
    // mặc định không làm gì
    return;
  }
  virtual void displayLcdMessage(const String &msg, int row = 0, int col = 0) {
    // mặc định không làm gì
    return;
  }
  virtual void onConnected() {
    return;
  }
  void saveConfigDefault(String deviceId, String zone, String domain);

protected:
  // ==== Common service ====
  WiFiClient espClient;
  PubSubClient mqttClient;
  Preferences preferences;
  WebServer server;

  // ==== Config mặc định ====
  const int mqttPort = 1883;
  const char *mqttUser = "dev";
  const char *mqttPass = "123456z";
  const char *expectedSecret = "123456";

  // ==== Config ====
  String ssid;
  String password;
  String httpBase;

  String connectSecretKey;
  String deviceModel;
};

}