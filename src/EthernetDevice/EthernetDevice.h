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
#include <SPI.h>
#include <Ethernet.h>
#include "../Common/DeviceConstants.h"
#include "../Common/DevicePins.h"

namespace EdgeOn
{
  #define SPI_MOSI 23
  #define SPI_MISO 19
  #define SPI_SCK 18
  #define SPI_CS 5
  #define W5500_RESET_PIN 25

  class EthernetDevice {
  public:
    EthernetDevice(DevicePins cfg)
      : mqttClient(ethernetClient), server(80) {
      Serial.begin(115200);
      ledPairingPin = cfg.ledPairingPin;
      pairingPin = cfg.pairingPin;
      connectSecretKey = cfg.connectSecretKey;
      deviceModel = cfg.deviceModel;
    }
    void setup();
    void loop();
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
    String log = "0";

    void onMqttMessage(char *topic, byte *payload, unsigned int length);
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
      return;
    }
    virtual void displayLcdMessage(const String &msg, int row = 0, int col = 0) {
      return;
    }
    virtual String buildConnectedPublishTopic() {
      return String("platform/") + zone + "/" + deviceId + "/response/" + buildConnectedPublishTopicExtension();
    }
    virtual String buildConnectedPublishTopicExtension() {
      return "connection-log";
    }
    virtual String buildConnectedPublishPayload() {
      StaticJsonDocument<256> doc;
      doc["deviceId"] = deviceId;
      doc["deviceModel"] = deviceModel;
      doc["firmware"] = fwVersion;
      doc["ip"] = Ethernet.localIP().toString();
      doc["secret"] = API_SECRET_KEY;

      String payload;
      serializeJson(doc, payload);
      return payload;
    }
    virtual void onConnected() {
      return;
    }
    void saveConfigDefault(String deviceId, String zone, String domain);

  protected:
    void publishConnectedMessage();

    WiFiClient espClient;
    PubSubClient mqttClient;
    Preferences preferences;
    WebServer server;
    EthernetClient ethernetClient;

    const int mqttPort = 1883;
    const char *mqttUser = "iotdevice";
    const char *mqttPass = "123456";
    const char *expectedSecret = "123456";

    String ssid;
    String password;
    String httpBase;
    String connectSecretKey;
    String deviceModel;
    String fwVersion = "1.0.0";
  };
}
