#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

namespace EdgeOn {

struct ModbusConfig {
  uint8_t slaveId = 1;
  int8_t txPin = -1;
  int8_t rxPin = -1;
  int8_t derePin = -1;
  uint32_t baudRate = 9600;
  uint32_t serialConfig = SERIAL_8N1;
  uint32_t timeoutMs = 500;
  uint16_t turnaroundDelayMs = 2;
};

class RS485Modbus {
private:
  HardwareSerial* serial;
  ModbusConfig config;

  void setTransmitMode(bool enabled);
  bool readExact(uint8_t* buffer, size_t length, uint32_t timeoutMs);
  void clearInputBuffer();
  uint16_t crc16(const uint8_t* buf, size_t len) const;

public:
  RS485Modbus(HardwareSerial& ser, const ModbusConfig& cfg);

  void begin();
  void setSlaveId(uint8_t slaveId);
  uint8_t getSlaveId() const;

  void sendFrame(const uint8_t* frame, size_t len);

  // Returns the number of data bytes copied into buffer, or -1 on failure.
  int readHoldingRegisters(uint16_t startAddr, uint16_t qty, uint8_t* buffer, size_t bufferSize);

  // Parses 4 bytes at offset as IEEE754 float. By default uses high-word first.
  float parseFloat(const uint8_t* buffer, size_t offset, bool swapWords = false) const;
};

}
