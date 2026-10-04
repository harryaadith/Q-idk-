/* Standalone ESP32 IMU identity checker. No RFID/Wi-Fi/IMU library needed.
 * SDA=GPIO21, SCL=GPIO22, baud=115200. Test one IMU module at a time.
 * Temporarily replaces tray firmware when uploaded. Reflash brick.ino afterwards.
 */
#include <Arduino.h>
#include <Wire.h>

constexpr int SDA_PIN = 21;
constexpr int SCL_PIN = 22;
constexpr uint8_t WHO_AM_I = 0x75;
constexpr uint8_t MAG_ADDRESS = 0x0C;
uint32_t lastReport = 0;

bool readRegister(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  uint8_t status = Wire.endTransmission(false);
  if (status != 0) {
    Serial.printf("READ FAILED addr=0x%02X reg=0x%02X tx_status=%u\n", address, reg, status);
    return false;
  }
  size_t count = Wire.requestFrom(address, size_t(1));
  if (count != 1 || Wire.available() < 1) {
    Serial.printf("READ FAILED addr=0x%02X reg=0x%02X received=%u/1\n", address, reg, unsigned(count));
    return false;
  }
  value = uint8_t(Wire.read());
  return true;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  uint8_t status = Wire.endTransmission();
  if (status != 0) {
    Serial.printf("WRITE FAILED addr=0x%02X reg=0x%02X tx_status=%u\n", address, reg, status);
    return false;
  }
  return true;
}

void scanBus() {
  Serial.println("\nI2C scan (7-bit addresses 0x08..0x77):");
  unsigned found = 0;
  for (uint8_t address = 0x08; address <= 0x77; address++) {
    Wire.beginTransmission(address);
    uint8_t status = Wire.endTransmission();
    if (status == 0) {
      Serial.printf("  ACK at 0x%02X\n", address);
      found++;
    } else if (status != 2) {
      Serial.printf("  0x%02X: tx_status=%u (bus error or timeout; inspect wiring)\n", address, status);
    }
  }
  Serial.printf("Scan complete: %u responding addresses. ACK is not a device identity.\n", found);
}

void checkMagnetometer(uint8_t imuAddress) {
  // Only used after the IMU identifies as MPU-9250. Preserve register bits.
  uint8_t power = 0, user = 0, config = 0;
  if (!readRegister(imuAddress, 0x6B, power) ||
      !readRegister(imuAddress, 0x6A, user) ||
      !readRegister(imuAddress, 0x37, config)) {
    Serial.println("Cannot configure bypass: original IMU registers could not be read.");
    return;
  }
  Serial.printf("Original PWR_MGMT_1=0x%02X USER_CTRL=0x%02X INT_PIN_CFG=0x%02X\n", power, user, config);
  bool configured = writeRegister(imuAddress, 0x6B, power & uint8_t(~0x40));
  delay(10);
  configured = configured && writeRegister(imuAddress, 0x6A, user & uint8_t(~0x20));
  configured = configured && writeRegister(imuAddress, 0x37, config | 0x02);
  delay(10);
  if (configured) {
    uint8_t identity = 0;
    if (readRegister(MAG_ADDRESS, 0x00, identity)) {
      Serial.printf("MAG addr=0x0C WIA(reg=0x00)=0x%02X : %s\n", identity,
                    identity == 0x48 ? "expected AK8963 identity" : "unexpected identity; verify actual device");
    } else {
      Serial.println("MAG identity unavailable despite bypass attempt; check module type, wiring and power.");
    }
  } else {
    Serial.println("Bypass setup failed; no magnetometer identity conclusion is possible.");
  }
  // Restore even after a partial setup failure; never leave unreported writes.
  bool restored = writeRegister(imuAddress, 0x37, config);
  restored = writeRegister(imuAddress, 0x6A, user) && restored;
  restored = writeRegister(imuAddress, 0x6B, power) && restored;
  Serial.println(restored ? "Original IMU configuration restored." : "Restore failed; power-cycle before running other firmware.");
}

void checkImu(uint8_t address) {
  Wire.beginTransmission(address);
  uint8_t status = Wire.endTransmission();
  Serial.printf("\nIMU probe addr=0x%02X tx_status=%u\n", address, status);
  if (status != 0) {
    Serial.println("No acknowledged IMU at this address. Check SDA/SCL/power/GND and AD0.");
    return;
  }
  uint8_t identity = 0;
  if (!readRegister(address, WHO_AM_I, identity)) return;
  Serial.printf("WHO_AM_I(addr=0x%02X reg=0x75)=0x%02X\n", address, identity);
  switch (identity) {
    case 0x71:
      Serial.println("Matches the MPU-9250 identity. Checking its AK8963 through I2C bypass...");
      checkMagnetometer(address);
      break;
    case 0x70:
      Serial.println("Matches the MPU-6500 identity (accelerometer/gyro; no integrated AK8963). Not an MPU-9250 identity.");
      break;
    default:
      Serial.println("Unexpected for MPU-9250/MPU-6500. Preserve this raw value and check the actual part's register map.");
      Serial.println("Unknown-device registers were not changed. Identity alone cannot certify authenticity.");
      break;
  }
  if (address == 0x69) Serial.println("NOTE: tray firmware expects address 0x68; check AD0 wiring if only 0x69 responds.");
}

void reportIdentities() {
  Serial.println("\n========== IMU WHO_AM_I report ==========");
  checkImu(0x68);
  checkImu(0x69);
  Serial.println("Send s + newline to repeat the full I2C scan. Identity checks repeat every 5 seconds.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\nStandalone IMU identity checker; SDA=21 SCL=22, I2C=100kHz, 115200 baud.");
  Serial.println("Use 3.3V-compatible wiring. For MPU9250: NCS=3V3, AD0=GND for 0x68.");
  if (!Wire.begin(SDA_PIN, SCL_PIN, 100000)) {
    Serial.println("FATAL: Wire.begin failed; check ESP32 board profile and pins.");
    while (true) delay(1000);
  }
  Wire.setTimeOut(50);
  scanBus();
  reportIdentities();
  lastReport = millis();
}

void loop() {
  // Bound input processing so a flooded serial port cannot starve reporting.
  for (int budget = 0; budget < 32 && Serial.available(); budget++) {
    char command = char(Serial.read());
    if (command == 's' || command == 'S') scanBus();
  }
  if (millis() - lastReport >= 5000) {
    reportIdentities();
    lastReport = millis();
  }
  delay(10);
}
