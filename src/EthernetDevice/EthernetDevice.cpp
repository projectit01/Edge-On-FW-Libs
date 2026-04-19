#include "EthernetDevice.h"
#include "../Common/DeviceIdentity.h"

namespace EdgeOn
{

    void EthernetDevice::setup()
    {
        loadConfig();
        needConfig = false;

        SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS);

        // MAC giả định (tùy chọn)
        byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01};

        // Reset W5500
        pinMode(W5500_RESET_PIN, OUTPUT);
        digitalWrite(W5500_RESET_PIN, LOW);
        delay(100);
        digitalWrite(W5500_RESET_PIN, HIGH);
        delay(200);

        // Bắt đầu Ethernet (sử dụng DHCP)
        Ethernet.init(SPI_CS);

        if (Ethernet.begin(mac) == 0)
        {
            Serial.println("DHCP fail or no link");
            // Thử IP tĩnh nếu cần
            IPAddress ip(192, 168, 1, 177);
            IPAddress mask(255, 255, 255, 0);
            IPAddress myDns(8, 8, 8, 8);
            Ethernet.begin(mac, ip, myDns);
        }
        else
        {
            Serial.println("DHCP success");
        }

        delay(1000);

        Serial.print("Local IP: ");
        Serial.println(Ethernet.localIP());

        Serial.print("Subnet: ");
        Serial.println(Ethernet.subnetMask());

        Serial.print("Gateway: ");
        Serial.println(Ethernet.gatewayIP());

        mqttClient.setServer(httpBase.c_str(), mqttPort);
        mqttClient.setCallback([this](char *t, byte *p, unsigned int l)
                               { this->onMqttMessage(t, p, l); });
    }

    void EthernetDevice::loop()
    {
        // if (needConfig) {
        //   server.handleClient();
        //   return;
        // }

        if (Ethernet.linkStatus() != LinkON)
        {
            // delay(500);
            // if (millis() - lastWiFiRetry > 5 * 60 * 1000) {
            //   WiFi.begin(ssid.c_str(), password.c_str());
            //   lastWiFiRetry = millis();
            // }
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

    void EthernetDevice::loadConfig()
    {
        preferences.begin("wifi", true);
        ssid = preferences.getString("ssid", "");
        password = preferences.getString("pass", "");
        httpBase = preferences.getString("ip", "");
        // httpBase = "broker.hivemq.com";
        deviceId = preferences.getString("deviceId", "");
        zone = preferences.getString("zone", "");
        domain = preferences.getString("domain", "");
        log = preferences.getString("log", "");
        preferences.end();
    }

    void EthernetDevice::saveConfig(String ssid, String pass, String ip)
    {
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.putString("ip", ip);
        preferences.end();
    }

    void EthernetDevice::saveConfigDefault(String deviceId, String zone, String domain)
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

    void EthernetDevice::clearConfig(const String &nsName)
    {
        preferences.begin(nsName.c_str(), false); // false = ghi
        preferences.clear();                      // xoá toàn bộ key trong namespace
        preferences.end();
        mqttLog("✅ Config cleared from EEPROM");
    }

    void EthernetDevice::startAPMode()
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

    bool EthernetDevice::reconnectMQTT()
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
            // hook
            publishConnectedMessage();
            onConnected();
            Serial.println(deviceId);
            mqttClient.subscribe((String("platform/+/") + deviceId + "/ota").c_str());
            mqttClient.subscribe((String("platform/+/") + deviceId + "/command/#").c_str());
            mqttClient.subscribe((String("platform/+/") + deviceId + "/setting/#").c_str());
            mqttClient.subscribe((String("platform/+/") + deviceId + "/ping").c_str());

            return true;
        }
        displayLcdMessage("MQTT failed", 0, 0);
        mqttLog("❌ MQTT failed");
        mqttLog(httpBase);
        updateHostIp();
        return false;
    }

    // void EthernetDevice::performOTA(const char *firmwarePath) {
    //   String url = "http://" + httpBase + ":" + String(80) + firmwarePath;
    //   HTTPClient http;
    //   http.begin(url);
    //   int httpCode = http.GET();
    //   if (httpCode == HTTP_CODE_OK) {
    //     int contentLength = http.getSize();
    //     auto stream = http.getStreamPtr();
    //     if (Update.begin(contentLength)) {
    //       Update.writeStream(*stream);
    //       if (Update.end(true)) {
    //         ESP.restart();
    //       }
    //     }
    //   }
    //   http.end();
    // }
    void EthernetDevice::performOTA(const char *firmwarePath)
    {
        const char *host = httpBase.c_str(); // ví dụ: "192.168.1.50"
        const uint16_t port = 80;

        Serial.println("[OTA] Starting OTA via Ethernet...");
        Serial.printf("[OTA] Host: %s, Port: %d, Path: %s\n", host, port, firmwarePath);

        EthernetClient client;
        client.setTimeout(8000); // ✅ tăng timeout cho socket đọc

        // Kết nối
        if (!client.connect(host, port))
        {
            Serial.println("[OTA] ❌ Connection failed");
            mqttLog("OTA connect failed");
            return;
        }

        // Gửi request HTTP GET
        client.print(String("GET ") + firmwarePath + " HTTP/1.1\r\n" + "Host: " + host + "\r\n" + "Connection: close\r\n\r\n");

        // Đợi phản hồi
        unsigned long startTime = millis();
        while (!client.available())
        {
            if (millis() - startTime > 8000)
            { // ✅ tăng timeout phản hồi
                Serial.println("[OTA] ❌ Timeout waiting for response");
                client.stop();
                return;
            }
            delay(10);
        }

        // Đọc dòng trạng thái
        String statusLine = client.readStringUntil('\n');
        statusLine.trim();
        Serial.println("[OTA] Status: " + statusLine);

        if (!statusLine.startsWith("HTTP/1.1 200"))
        {
            Serial.println("[OTA] ❌ HTTP not OK");
            client.stop();
            return;
        }

        // Bỏ qua header để đến nội dung nhị phân
        while (client.available())
        {
            String line = client.readStringUntil('\n');
            if (line == "\r" || line == "")
                break;
        }

        // Đọc nội dung (firmware binary)
        size_t written = 0;
        const size_t bufSize = 1024;
        uint8_t buf[bufSize];

        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            Serial.println("[OTA] ❌ Update.begin() failed");
            mqttLog("OTA begin failed");
            client.stop();
            return;
        }

        Serial.println("[OTA] Writing firmware...");
        while (client.connected())
        {
            while (client.available())
            {
                size_t len = client.read(buf, bufSize);
                if (len > 0)
                {
                    written += Update.write(buf, len);
                }
            }
            delay(5); // ✅ thêm delay để tránh nghẽn SPI giữa W5500 và flash
        }

        // ✅ đảm bảo buffer đọc xong trước khi kết thúc
        client.flush();
        delay(100);

        if (Update.end(true))
        {
            if (Update.isFinished())
            {
                Serial.printf("[OTA] ✅ OTA success! %u bytes written\n", (unsigned)written);
                mqttLog("OTA success, restarting...");
                delay(1000);
                ESP.restart();
            }
            else
            {
                Serial.println("[OTA] ❌ OTA not finished");
                mqttLog("OTA not finished");
            }
        }
        else
        {
            Serial.printf("[OTA] ❌ Update failed: %s\n", Update.errorString());
            mqttLog("OTA error: " + String(Update.errorString()));
        }

        client.stop();
        Serial.println("[OTA] Connection closed");
    }

    void EthernetDevice::updateHostIp()
    {
        delay(1000); // chờ Ethernet ổn định
        Serial.println("updateHostIp()");

        if (Ethernet.linkStatus() != LinkON)
        {
            mqttLog("updateHostIp - Ethernet not connected");
            return;
        }

        IPAddress localIP = Ethernet.localIP();
        if (!localIP)
        {
            mqttLog("No local IP, cannot scan.");
            return;
        }
        Serial.print("local IP: ");
        Serial.println(localIP);
        uint8_t base1 = localIP[0];
        uint8_t base2 = localIP[1];
        uint8_t base3 = localIP[2];
        const uint16_t port = 80;
        const char *path = "/dashboard/device-setting/server-ip";

        Serial.printf("🔍 Scanning LAN %d.%d.%d.1–254 ...\n", base1, base2, base3);

        bool serverFound = false;
        String foundIp = "";

        for (int host = 1; host <= 254; host++)
        {
            IPAddress target(base1, base2, base3, host);
            if (target == localIP)
                continue; // skip self

            EthernetClient client;
            Serial.printf("Trying %s ... ", target.toString().c_str());

            // --- kết nối ---
            if (!client.connect(target, port))
            {
                Serial.println("no connect");
                client.stop();
                delay(20);
                continue;
            }

            // --- gửi request ---
            client.print(String("GET ") + path + " HTTP/1.1\r\n" + "Host: " + target.toString() + "\r\n" + "Connection: close\r\n\r\n");
            client.flush();

            // --- chờ phản hồi ---
            unsigned long startWait = millis();
            while (!client.available())
            {
                if (millis() - startWait > 600)
                { // timeout
                    Serial.println("timeout");
                    client.stop();
                    break;
                }
                delay(10);
            }

            if (!client.available())
                continue; // sang IP kế

            // --- đọc dòng trạng thái ---
            String statusLine = client.readStringUntil('\n');
            statusLine.trim();
            if (!statusLine.startsWith("HTTP/1.1 200"))
            {
                Serial.println("not 200");
                client.stop();
                delay(20);
                continue;
            }

            // --- bỏ qua headers ---
            while (client.available())
            {
                String line = client.readStringUntil('\n');
                if (line == "\r" || line == "")
                    break;
            }

            // --- đọc nội dung ---
            String payload = "";
            while (client.available())
            {
                payload += (char)client.read();
            }
            client.stop();

            if (payload.length() == 0)
            {
                Serial.println("empty payload");
                delay(20);
                continue;
            }

            // --- xử lý JSON ---
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (error)
            {
                Serial.println("bad json");
                delay(20);
                continue;
            }

            if (doc["payload"]["ip"])
            {
                foundIp = target.toString();
                serverFound = true;

                Serial.printf("✅ Server found at %s (reported: %s)\n",
                              target.toString().c_str(),
                              foundIp.c_str());

                preferences.begin("wifi", false);
                preferences.putString("ip", foundIp);
                preferences.end();

                mqttLog("Server found & saved: " + foundIp);
                break; // đã tìm thấy → thoát vòng lặp
            }

            delay(50); // giảm tải SPI
        }

        if (!serverFound)
        {
            mqttLog("No valid server found in LAN.");
        }
    }

    void EthernetDevice::handleButtons()
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
                    // digitalWrite(ledPairingPin, HIGH);
                    // delay(200);
                    // digitalWrite(ledPairingPin, LOW);
                    // delay(200);
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

    void EthernetDevice::ledBlink(uint8_t brightness, uint16_t speedMs, uint8_t times)
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

    void EthernetDevice::onMqttMessage(char *topic, byte *payload, unsigned int length)
    {
        mqttBusy = true; // bắt đầu xử lý MQTT → chặn nút bấm
        String msg;
        for (int i = 0; i < length; i++)
        {
            msg += (char)payload[i];
        }

        // OTA
        String otaTopic = String("platform/") + zone + "/" + deviceId + "/ota";
        if (String(topic) == otaTopic)
        {
            Serial.println("[OTA] Nhận yêu cầu OTA");

            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, msg);
            if (error)
            {
                Serial.print("[OTA] Lỗi parse JSON: ");
                Serial.println(error.f_str());
                mqttLog("OTA JSON parse error");
                return;
            }

            // Lấy các trường từ payload JSON
            String secret = doc["secret"] | "";
            String filename = doc["file"] | "";

            if (secret != expectedSecret)
            {
                Serial.println("[OTA] Sai secret, bỏ qua");
                mqttLog("OTA secret mismatch");
                return;
            }

            if (filename.length() == 0)
            {
                Serial.println("[OTA] Không có file name trong payload");
                mqttLog("OTA missing filename");
                return;
            }

            // Gọi OTA
            String firmwarePath = String("/dashboard/firmware/files/") + filename;
            Serial.println("[OTA] Đang OTA file: " + firmwarePath);
            mqttLog("Starting OTA: " + firmwarePath);

            performOTA(firmwarePath.c_str());

            // Gửi phản hồi về topic ota-response
            String respTopic = String("platform/") + zone + "/" + deviceId + "/ota-response";
            mqttClient.publish(respTopic.c_str(), ("OTA started: " + filename).c_str());
            return;
        }

        // Reset
        String restartTopic = String("platform/") + zone + "/" + deviceId + "/command/restart";
        mqttLog("ESP restart before");
        if (String(topic) == restartTopic && msg.equals(expectedSecret))
        {
            mqttLog("ESP restart");
            delay(500);
            ESP.restart();
        }

        // COMMAND -> move to hook
        // String cmdTopic = String("platform/") + zone + "/" + deviceId + "/command/get";
        // if (String(topic) == cmdTopic) {
        // }

        // SETTING - clear eeprom
        String settingTopicDeleteEeprom = String("platform/") + zone + "/" + deviceId + "/setting/delete-eeprom";
        if (String(topic) == settingTopicDeleteEeprom && msg.equals(expectedSecret))
        {
            mqttLog("settingTopicDeleteEeprom");
            clearConfig("wifi");
            delay(500);
            ESP.restart();
        }

        // SETTING - set
        String settingTopic = String("platform/") + zone + "/" + deviceId + "/setting/set";

        if (String(topic) == settingTopic)
        {
            Serial.println("[SETTING] Nhận cấu hình mới");

            // Chuyển payload (byte*) thành String
            String msg;
            for (unsigned int i = 0; i < length; i++)
            {
                msg += (char)payload[i];
            }
            Serial.print("[SETTING] Payload raw: ");
            Serial.println(msg);

            // Parse JSON
            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, msg);
            if (error)
            {
                Serial.print("[SETTING] Failed to parse JSON: ");
                Serial.println(error.f_str());
                mqttLog("Failed to parse JSON");
                return;
            }

            // Kiểm tra secret
            String secret = doc["secret"] | "";
            if (secret != expectedSecret)
            {
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
            String newDOSensorId = doc["DOSensorId"] | "";

            // In ra debug
            Serial.println("[SETTING] Dữ liệu mới:");
            if (newDeviceId.length() > 0)
                Serial.println("  deviceId = " + newDeviceId);
            if (newZone.length() > 0)
                Serial.println("  zone     = " + newZone);
            if (newIp.length() > 0)
                Serial.println("  ip       = " + newIp);
            if (newSsid.length() > 0)
                Serial.println("  ssid     = " + newSsid);
            if (newPass.length() > 0)
                Serial.println("  password = " + newPass);
            if (newLog.length() > 0)
                Serial.println("  newLog= " + String(newLog));
            if (newDOSensorId.length() > 0)
                Serial.println("  newDOSensorId= " + String(newDOSensorId));

            // Lưu vào Preferences
            preferences.begin("wifi", false);
            if (newDeviceId.length() > 0)
                preferences.putString("deviceId", newDeviceId);
            if (newZone.length() > 0)
                preferences.putString("zone", newZone);
            if (newIp.length() > 0)
                preferences.putString("ip", newIp);
            if (newSsid.length() > 0)
                preferences.putString("ssid", newSsid);
            if (newPass.length() > 0)
                preferences.putString("password", newPass);
            if (newLog.length() > 0)
            {
                Serial.println("newlog===");
                Serial.println(newLog);
                preferences.putString("log", String(newLog));
            }
            if (newDOSensorId.length() > 0)
                preferences.putString("DOSensorId", newDOSensorId);
            preferences.end();

            mqttLog("settingTopicSet");

            delay(500);
            ESP.restart();
        }

        // PING
        String pingTopic = String("platform/") + zone + "/" + deviceId + "/ping";
        if (String(topic) == pingTopic)
        {
            mqttLog("📡 Ping received");
            ledBlink();
            mqttClient.publish(
                (String("platform/") + zone + "/" + deviceId + "/response").c_str(),
                "pong");
        }

        // === Hook cho custom feature ===
        handleCustomMqttMessage(String(topic), msg);

        mqttBusy = false; // xong MQTT → cho phép nút hoạt động lại
    }

    void EthernetDevice::mqttLog(const String &msg)
    {

        if (log == "2")
            return; // prod -> bỏ qua log
        Serial.println("mqttLog mode:");
        Serial.println(log);
        Serial.println("msg:");
        Serial.println(msg);
        // if (Ethernet.linkStatus() == LinkON) {
        //   EthernetClient client;
        //   if (!client.connect(httpBase.c_str(), 80)) {
        //     Serial.println("[HTTP LOG] Connection failed");
        //     client.stop();
        //     return;
        //   }
        //   // Gửi HTTP POST request
        //   String path = "/dashboard/device-control/logs";
        //   String payload = "{\"deviceId\":\"" + deviceId + "\",\"log\":\"" + msg + "\"}";
        //   String request =
        //     "POST " + path + " HTTP/1.1\r\n" + "Host: " + httpBase + "\r\n" + "Content-Type: application/json\r\n" + "Connection: close\r\n" + "Content-Length: " + String(payload.length()) + "\r\n\r\n" + payload;

        //   client.print(request);
        //   // Đọc phản hồi (nếu có)
        //   unsigned long timeout = millis();
        //   while (client.connected() && millis() - timeout < 2000) {
        //     while (client.available()) {
        //       char c = client.read();
        //       Serial.write(c);  // in ra response server
        //       timeout = millis();
        //     }
        //   }

        //   client.stop();
        //   Serial.println("\n[HTTP LOG] Connection closed");
        // }
    }

    void EthernetDevice::publishConnectedMessage()
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
