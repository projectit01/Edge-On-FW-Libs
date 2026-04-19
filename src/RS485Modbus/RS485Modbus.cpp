#include "RS485Modbus.h"

namespace EdgeOn {

RS485Modbus::RS485Modbus(HardwareSerial& ser, const ModbusConfig& cfg)
  : serial(&ser), config(cfg) {
}

void RS485Modbus::begin() {
  serial->begin(config.baudRate, config.serialConfig, config.rxPin, config.txPin);
  if (config.derePin >= 0) {
    pinMode(config.derePin, OUTPUT);
    digitalWrite(config.derePin, LOW);
  }
}

void RS485Modbus::setSlaveId(uint8_t slaveId) {
  config.slaveId = slaveId;
}

uint8_t RS485Modbus::getSlaveId() const {
  return config.slaveId;
}

void RS485Modbus::setTransmitMode(bool enabled) {
  if (config.derePin >= 0) {
    digitalWrite(config.derePin, enabled ? HIGH : LOW);
  }
}

void RS485Modbus::clearInputBuffer() {
  while (serial->available() > 0) {
    serial->read();
  }
}

bool RS485Modbus::readExact(uint8_t* buffer, size_t length, uint32_t timeoutMs) {
  size_t offset = 0;
  unsigned long startedAt = millis();

  while (offset < length) {
    if (serial->available() > 0) {
      int value = serial->read();
      if (value >= 0) {
        buffer[offset++] = static_cast<uint8_t>(value);
        startedAt = millis();
      }
    } else if (millis() - startedAt >= timeoutMs) {
      return false;
    }
    delay(1);
  }

  return true;
}

uint16_t RS485Modbus::crc16(const uint8_t* buf, size_t len) const {
  uint16_t crc = 0xFFFF;
  for (size_t pos = 0; pos < len; pos++) {
    crc ^= static_cast<uint16_t>(buf[pos]);
    for (uint8_t i = 0; i < 8; i++) {
      if ((crc & 0x0001U) != 0U) {
        crc >>= 1;
        crc ^= 0xA001U;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void RS485Modbus::sendFrame(const uint8_t* frame, size_t len) {
  clearInputBuffer();
  setTransmitMode(true);
  delay(config.turnaroundDelayMs);
  serial->write(frame, len);
  serial->flush();
  delay(config.turnaroundDelayMs);
  setTransmitMode(false);
}

int RS485Modbus::readHoldingRegisters(uint16_t startAddr, uint16_t qty, uint8_t* buffer, size_t bufferSize) {
  if (buffer == nullptr || qty == 0) {
    return -1;
  }

  const size_t payloadSize = static_cast<size_t>(qty) * 2U;
  if (bufferSize < payloadSize) {
    return -1;
  }

  uint8_t request[8];
  request[0] = config.slaveId;
  request[1] = 0x03;
  request[2] = static_cast<uint8_t>((startAddr >> 8) & 0xFF);
  request[3] = static_cast<uint8_t>(startAddr & 0xFF);
  request[4] = static_cast<uint8_t>((qty >> 8) & 0xFF);
  request[5] = static_cast<uint8_t>(qty & 0xFF);

  const uint16_t requestCrc = crc16(request, 6);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFF);
  request[7] = static_cast<uint8_t>((requestCrc >> 8) & 0xFF);

  sendFrame(request, sizeof(request));

  uint8_t header[3];
  if (!readExact(header, sizeof(header), config.timeoutMs)) {
    return -1;
  }

  if (header[0] != config.slaveId || header[1] != 0x03 || header[2] != payloadSize) {
    return -1;
  }

  const size_t responseSize = payloadSize + 2U;
  uint8_t response[252];
  if (responseSize > sizeof(response)) {
    return -1;
  }

  if (!readExact(response, responseSize, config.timeoutMs)) {
    return -1;
  }

  uint8_t frame[255];
  memcpy(frame, header, sizeof(header));
  memcpy(frame + sizeof(header), response, responseSize);

  const size_t fullLength = sizeof(header) + responseSize;
  const uint16_t actualCrc = static_cast<uint16_t>(frame[fullLength - 2]) |
                             (static_cast<uint16_t>(frame[fullLength - 1]) << 8);
  const uint16_t expectedCrc = crc16(frame, fullLength - 2);

  if (actualCrc != expectedCrc) {
    return -1;
  }

  memcpy(buffer, response, payloadSize);
  return static_cast<int>(payloadSize);
}

float RS485Modbus::parseFloat(const uint8_t* buffer, size_t offset, bool swapWords) const {
  uint32_t raw = 0;
  if (swapWords) {
    raw = (static_cast<uint32_t>(buffer[offset + 2]) << 24) |
          (static_cast<uint32_t>(buffer[offset + 3]) << 16) |
          (static_cast<uint32_t>(buffer[offset + 0]) << 8) |
          static_cast<uint32_t>(buffer[offset + 1]);
  } else {
    raw = (static_cast<uint32_t>(buffer[offset + 0]) << 24) |
          (static_cast<uint32_t>(buffer[offset + 1]) << 16) |
          (static_cast<uint32_t>(buffer[offset + 2]) << 8) |
          static_cast<uint32_t>(buffer[offset + 3]);
  }

  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

}
