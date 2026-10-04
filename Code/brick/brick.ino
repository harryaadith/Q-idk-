/*
 * ======================================================================================
 * Project: Multi-Agent Microbots (Smart Bricks) — Unified Firmware
 * File:    brick.ino
 * Target:  ESP32 Dev Module (Compatible with 30-Pin & 38-Pin Boards: All Pins <= 35)
 * Author:  Team 22 (Electronic System Workshop)
 *
 * Description:
 * Autonomous, decentralized embedded microbot firmware for ESP32 bricks.
 * Features:
 *  1. Peer-to-peer communication via ESP-NOW broadcasting current neighbor count.
 *  2. Real-time RSSI-based proximity detection with EMA smoothing and hysteresis.
 *  3. On-Board LED (GPIO 2) illuminates when close to all active neighbors.
 *  4. External Common-Cathode RGB LED:
 *      - Lights GREEN when an RFID tag/neighbor card is read.
 *      - Otherwise, blinks BLUE then RED in order of neighbor count (solid RED if 0).
 *      - Solid BLUE when docked / close to all neighbors.
 *  5. RFID reader (MFRC522) via VSPI detecting top-facing instrument tokens.
 *  6. 4-Face Directional IR Sensors (North=34, East=35, South=32, West=33) for docking.
 *  7. Audio Buzzer (GPIO 27) for acoustic feedback:
 *      - RFID read confirmation chirp
 *      - Physical face docking & undocking chime
 *      - Proximity consensus alert
 *      - System boot chime
 *  8. 9-DoF IMU + Magnetometer (MPU9250 / AK8963 on I2C: SDA=GPIO 21, SCL=GPIO 22):
 *      - Real-time 3-axis accelerometer and gyroscope tracking
 *      - Optional AK8963 readings for diagnostics; no magnetic heading correction
 *      - Pitch/Roll and aligned-start relative gyro yaw; magnetic heading is not required
 *  9. ERM Vibration Motor Actuator (GPIO 26):
 *      - Tactile haptic feedback on RFID token scans
 *      - Tactile click on physical IR face docking
 *      - Double haptic pulse on swarm proximity consensus
 * ======================================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <WebServer.h>
#include "DashboardPage.h"
#include "DebugCommand.h"
#include "PlanarYaw.h"
#include "PulseOutput.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ======================================================================================
// 1. CONFIGURATION PARAMETERS (same sketch on every brick)
// ======================================================================================
uint8_t BRICK_ID = 1;                  // Automatic display number; stable identity is nodeUid
uint64_t nodeUid = 0;
#define TOTAL_SWARM_BRICKS    4        // Total number of bricks in the multi-agent system
#define WIFI_CHANNEL          1        // ESP-NOW WiFi Channel (must be identical across bricks)

// Proximity & Timeout Tuning
#define BROADCAST_INTERVAL_MS 250      // Transmission period for neighbor packets
#define PEER_TIMEOUT_MS       2500     // Duration of silence before marking peer offline
#define RSSI_EMA_ALPHA        0.30f    // Smoothing factor for direct RSSI filter (0.0 to 1.0)
#define PROXIMITY_THRESHOLD   -65      // RSSI threshold in dBm to consider a neighbor "CLOSE"
#define PROXIMITY_HYSTERESIS  4        // Hysteresis in dB to eliminate boundary flicker

// RFID Settings
#define RFID_GREEN_HOLD_MS    2000     // How long RGB LED stays Green after RFID detection

// IR Sensor Settings
#define IR_ACTIVE_LOW         true     // Standard LM393 IR obstacle sensors output LOW on reflection
#define IR_POLL_INTERVAL_MS   60       // Polling frequency for 4-face IR sensors
#define IR_DEBOUNCE_COUNT     2        // Consecutive readings required to confirm state transition

// Audio Buzzer Settings
#define BUZZER_ENABLED        true     // Master toggle for acoustic buzzer feedback

// IMU Polling Interval
#define IMU_POLL_INTERVAL_MS  100      // Poll IMU every 100ms (10Hz)

// LED Animation Timings (in milliseconds)
#define BLINK_BLUE_ON_MS      220      // Blue pulse duration
#define BLINK_BLUE_OFF_MS     180      // Gap between Blue pulses
#define BLINK_RED_ON_MS       450      // Red pulse duration at end of sequence
#define BLINK_CYCLE_PAUSE_MS  600      // Pause before repeating count sequence

// Optional: require all 3 swarm peers to be present and close, or all currently active peers
#define REQUIRE_ALL_SWARM_FOR_ONBOARD false

// ======================================================================================
// 2. HARDWARE PIN DEFINITIONS (All pins strictly <= 35 for 30-pin board compatibility)
// ======================================================================================
// On-board LED (ESP32 Dev Module built-in blue LED)
#define PIN_ONBOARD_LED       2

// External Common-Cathode RGB LED (Driven through 220-330 ohm current-limiting resistors)
#define PIN_LED_RED           16
#define PIN_LED_GREEN         17
#define PIN_LED_BLUE          25

// MFRC522 RFID Reader (Standard ESP32 VSPI: SCK=18, MISO=19, MOSI=23)
#define PIN_RFID_SS           5
#define PIN_RFID_RST          4

// 4 Directional IR Proximity / Obstacle Sensors (North, East, South, West)
#define PIN_IR_NORTH          34       // North / Top face sensor (D34 - Input-only)
#define PIN_IR_EAST           35       // East / Right face sensor (D35 - Input-only)
#define PIN_IR_SOUTH          32       // South / Bottom face sensor (D32)
#define PIN_IR_WEST           33       // West / Left face sensor (D33)

// Active-HIGH active buzzer module (passive buzzers require tone/PWM)
#define PIN_BUZZER            27       // Driven via GPIO 27 (D27)

// 9-DoF IMU + Magnetometer (I2C Bus: MPU9250 / AK8963)
#define PIN_I2C_SDA           21       // I2C Serial Data (D21)
#define PIN_I2C_SCL           22       // I2C Serial Clock (D22)
#define MPU9250_I2C_ADDR      0x68     // Primary I2C address (AD0 to GND)
#define AK8963_I2C_ADDR       0x0C     // Magnetometer I2C address (via I2C bypass)

// ERM Vibration Motor Actuator (2 wires: driven via NPN transistor / MOSFET driver)
#define PIN_ERM_MOTOR         26       // Driven via GPIO 26 (D26)
#define PIN_VIBRATOR_MOTOR    PIN_ERM_MOTOR // Backward compatibility alias

// ======================================================================================
// 3. PACKET DEFINITION & DATA STRUCTURES
// ======================================================================================
// Packet exchanged between microbots.
// Specification: Each ESP talks to other ESPs exchanging its ID and current neighbor count.
typedef struct __attribute__((packed)) {
  uint32_t magic;          // Reject old/incompatible packets
  uint64_t sender_uid;     // Full hardware MAC identity; no manual assignment
  uint8_t neighbor_count;
  uint8_t face_mask;       // Body-frame N/E/S/W occupancy, never peer identity
  uint8_t heading_valid;  // Fresh aligned-start relative gyro yaw
  uint8_t orientation_state; // 0=no IMU, 1=calibrating, 2=ready, 3=tracking lost
  uint32_t alignment_age_ms;
  uint8_t command;         // 0=telemetry, 1=restart shared alignment
  float heading;
  char instrument_uid[21]; // Last scanned instrument token, not continuous presence
  uint8_t recent_scan;
} BrickPacket;

// Explicit prototypes keep Arduino preprocessing independent of custom-type order.
String brickJson(uint64_t uid, const BrickPacket &p, uint32_t age, bool host);
BrickPacket localTelemetry();

// Local tracking record for a peer brick
struct PeerInfo {
  BrickPacket telemetry;
  uint64_t uid;                      // Hardware identity, independent of display number
  bool     active;                   // True if peer has broadcasted within PEER_TIMEOUT_MS
  uint32_t last_seen_ms;             // Timestamp of last received packet
  float    rssi_ema;                 // Smoothed RSSI signal strength
  uint8_t  reported_neighbor_count;  // Neighbor count reported by peer
  bool     is_close;                 // Proximity status with hysteresis
};

uint8_t displayNumber(uint64_t uid);

// Peer records: indices 1 to TOTAL_SWARM_BRICKS
PeerInfo peers[TOTAL_SWARM_BRICKS + 1];

// Cardinal face indices and labels
enum Face {
  FACE_NORTH = 0,
  FACE_EAST  = 1,
  FACE_SOUTH = 2,
  FACE_WEST  = 3
};
const char* FACE_NAMES[] = {"NORTH", "EAST", "SOUTH", "WEST"};
const uint8_t IR_PINS[]  = {PIN_IR_NORTH, PIN_IR_EAST, PIN_IR_SOUTH, PIN_IR_WEST};

// Directional IR State Tracking
bool irFaceDetected[4]      = {false, false, false, false};
bool prevIrFaceDetected[4]  = {false, false, false, false};
uint8_t irDebounceCounters[4] = {0, 0, 0, 0};
uint32_t lastIrPollTime     = 0;

// MPU9250 & AK8963 Motion Tracking Data
struct ImuData {
  bool     mpu_detected;             // True if MPU-6500 responded
  bool     mag_detected;             // True if AK8963 responded
  float    accel_x, accel_y, accel_z;// In g
  float    gyro_x, gyro_y, gyro_z;   // In deg/s
  float    mag_x, mag_y, mag_z;      // In uT
  float    pitch;                    // Tilt angle in degrees (-90 to +90)
  float    roll;                     // Roll angle in degrees (-180 to +180)
  float    heading;                  // Clockwise gyro yaw from aligned start (0..360)
};
ImuData imu = {};
PlanarYaw gyroYaw;
bool alignmentBroadcastActive = false;
uint32_t alignmentBroadcastStarted = 0;
uint32_t lastImuPollTime = 0;
uint32_t lastMagSample = 0;
uint32_t lastAccelSample = 0;
int16_t rawAccel[3] = {}, rawGyro[3] = {}, rawMag[3] = {};
int16_t rawTemperature = 0;
int imuReadError = -1, imuReadBytes = 0, magReadError = -1, magReadBytes = 0;
uint8_t magStatus1 = 0, magStatus2 = 0;
int lastI2cReadError=-1, lastI2cReadBytes=0, magStatusReadError=-1, magStatusReadBytes=0;
// Detailed diagnostics are opt-in. Override to 1 at build time for startup capture.
#ifndef BRICK_DEBUG_DEFAULT
#define BRICK_DEBUG_DEFAULT 0
#endif
bool debugEnabled = BRICK_DEBUG_DEFAULT;
uint32_t lastSensorDebugPrint = 0;
bool whoamiEnabled = false;
uint32_t lastWhoAmIPrint = 0;
bool headingIsValid() {
  return imu.mpu_detected && gyroYaw.valid(millis());
}
void sampleCompass(float, float) {
  // Optional magnetic data remains available to debug, but never changes gyro yaw.
  lastMagSample = millis();
}

// Hardware Instances
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Operational State Variables
uint32_t lastBroadcastTime   = 0;
uint32_t lastDebugPrintTime  = 0;
uint32_t rfidDetectedTime    = 0;
bool     rfidActive          = false;
String   lastReadUid         = "";
bool     prevCloseToAll      = false;

// Top-facing RFID identifies instrument tokens, never neighboring bricks.

// ======================================================================================
// 4. LOW-LEVEL HARDWARE DRIVERS (Buzzer, ERM Motor, LEDs, IR)
// ======================================================================================

// Set External Common-Cathode RGB LED Color
void setRgbColor(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_RED,   r ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, g ? HIGH : LOW);
  digitalWrite(PIN_LED_BLUE,  b ? HIGH : LOW);
}

// Set On-Board Status LED
void setOnboardLed(bool state) {
  digitalWrite(PIN_ONBOARD_LED, state ? HIGH : LOW);
}

// ERM Vibration Motor Driver
void initErmMotor() {
  pinMode(PIN_ERM_MOTOR, OUTPUT);
  digitalWrite(PIN_ERM_MOTOR, LOW);
}

PulseOutput motorFeedback(PIN_ERM_MOTOR), buzzerFeedback(PIN_BUZZER);

void triggerErmHaptic(uint16_t duration_ms) {
  motorFeedback.start(millis(),duration_ms);
}
void triggerErmDoublePulse() {
  motorFeedback.start(millis(),50,40,70);
}
void triggerHapticFeedback(uint16_t duration_ms) { triggerErmHaptic(duration_ms); }
void buzzBeep(uint16_t duration_ms) {
#if BUZZER_ENABLED
  buzzerFeedback.start(millis(),duration_ms);
#endif
}
void buzzRfidSuccess() {
#if BUZZER_ENABLED
  buzzerFeedback.start(millis(),60,40,100);
#endif
}
void buzzDockChime() {
#if BUZZER_ENABLED
  buzzerFeedback.start(millis(),40,30,80);
#endif
}
void buzzUndockChime() { buzzBeep(120); }
void buzzBootChime() {
#if BUZZER_ENABLED
  buzzerFeedback.start(millis(),50,40,50,40,120);
#endif
}
void serviceFeedback() {
  const uint32_t now=millis();
  motorFeedback.service(now); buzzerFeedback.service(now);
}

// ======================================================================================
// 5. 9-DoF IMU & MAGNETOMETER DRIVERS (MPU9250 & AK8963)
// ======================================================================================
bool i2cWriteByte(uint8_t devAddr, uint8_t regAddr, uint8_t data) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  Wire.write(data);
  return Wire.endTransmission() == 0;
}

uint8_t i2cReadByte(uint8_t devAddr, uint8_t regAddr) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  lastI2cReadBytes=0;
  lastI2cReadError=Wire.endTransmission(false);
  if (lastI2cReadError != 0) return 0xFF;
  lastI2cReadBytes=Wire.requestFrom((uint8_t)devAddr, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

bool initMpu9250() {
  gyroYaw.reset();
  Wire.beginTransmission(MPU9250_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[IMU] MPU9250 not detected at 0x68. Verify SDA=GPIO21, SCL=GPIO22, NCS=3V3, AD0=GND.");
    imu.mpu_detected = false;
    imu.mag_detected = false;
    return false;
  }

  uint8_t whoami = i2cReadByte(MPU9250_I2C_ADDR, 0x75);
  if (lastI2cReadError != 0 || lastI2cReadBytes != 1 || (whoami != 0x70 && whoami != 0x71)) {
    Serial.printf("[IMU] Unsupported/unreadable identity 0x%02X at 0x68; expected MPU6500=0x70 or MPU9250=0x71.\n", whoami);
    imu.mpu_detected=false; imu.mag_detected=false; return false;
  }
  Serial.printf("[IMU] %s detected (WHO_AM_I=0x%02X); relative gyro yaw does not need a magnetometer.\n",whoami==0x70 ? "MPU6500":"MPU9250",whoami);

  // Wake up sensor: clear SLEEP bit in PWR_MGMT_1 (0x6B)
  if (!i2cWriteByte(MPU9250_I2C_ADDR, 0x6B, 0x00)) {
    imu.mpu_detected=false; imu.mag_detected=false;
    Serial.println("[IMU] Wake-up write failed; gyro reference unavailable."); return false;
  }
  delay(15);
  // Explicitly match the conversion factors used by the reader.
  if (!i2cWriteByte(MPU9250_I2C_ADDR, 0x1B, 0x00) ||
      !i2cWriteByte(MPU9250_I2C_ADDR, 0x1C, 0x00)) {
    imu.mpu_detected=false; imu.mag_detected=false;
    Serial.println("[IMU] Scale configuration failed; refusing incorrectly scaled yaw."); return false;
  }

  // Disable I2C Master mode in USER_CTRL (0x6A) to allow bypass access to AK8963
  i2cWriteByte(MPU9250_I2C_ADDR, 0x6A, 0x00);
  delay(10);

  // Enable I2C Bypass mode in INT_PIN_CFG (0x37) to access internal AK8963 on main I2C bus
  i2cWriteByte(MPU9250_I2C_ADDR, 0x37, 0x02);
  delay(15);

  if (whoami == 0x70) {
    imu.mpu_detected=true; imu.mag_detected=false;
    Serial.println("[IMU] MPU6500: magnetometer not required. Align marked N faces; keep still for 3 seconds.");
    return true;
  }
  // Probe AK8963 Magnetometer at 0x0C
  Wire.beginTransmission(AK8963_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    uint8_t magWhoAmI = i2cReadByte(AK8963_I2C_ADDR, 0x00);
    const bool identityValid = lastI2cReadError == 0 && lastI2cReadBytes == 1 && magWhoAmI == 0x48;
    // Keep optional magnetic diagnostics unavailable unless identity and setup succeed.
    imu.mag_detected = identityValid && i2cWriteByte(AK8963_I2C_ADDR, 0x0A, 0x16);
    Serial.printf("[IMU] AK8963 identity=0x%02X; diagnostics %s\n",magWhoAmI,
                  imu.mag_detected ? "available" : "unavailable (identity/read/configuration failure)");
  } else {
    Serial.println("[IMU] Note: AK8963 Magnetometer at 0x0C not responding. (6-DoF Accel/Gyro operational)");
    imu.mag_detected = false;
  }

  imu.mpu_detected = true;
  return true;
}

void readMpu9250() {
  if (!imu.mpu_detected) return;

  uint32_t now = millis();
  if (now - lastImuPollTime < IMU_POLL_INTERVAL_MS) return;
  lastImuPollTime = now;

  // Read 14 bytes from ACCEL_XOUT_H (0x3B)
  Wire.beginTransmission(MPU9250_I2C_ADDR);
  Wire.write(0x3B);
  imuReadError = Wire.endTransmission(false);
  imuReadBytes = 0;
  if (imuReadError == 0) {
    imuReadBytes = Wire.requestFrom((uint8_t)MPU9250_I2C_ADDR, (uint8_t)14);
    if (Wire.available() >= 14) {
      uint8_t buf[14];
      for (int i = 0; i < 14; i++) {
        buf[i] = Wire.read();
      }
      int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
      int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
      int16_t az = (int16_t)((buf[4] << 8) | buf[5]);
      int16_t tempRaw = (int16_t)((buf[6] << 8) | buf[7]);
      int16_t gx = (int16_t)((buf[8] << 8) | buf[9]);
      int16_t gy = (int16_t)((buf[10] << 8) | buf[11]);
      int16_t gz = (int16_t)((buf[12] << 8) | buf[13]);

      rawAccel[0]=ax; rawAccel[1]=ay; rawAccel[2]=az;
      rawGyro[0]=gx; rawGyro[1]=gy; rawGyro[2]=gz;
      rawTemperature=tempRaw; lastAccelSample=now;

      // Convert to physical units (+/-2g -> 16384 LSB/g; +/-250 deg/s -> 131 LSB/deg/s)
      imu.accel_x = ax / 16384.0f;
      imu.accel_y = ay / 16384.0f;
      imu.accel_z = az / 16384.0f;

      imu.gyro_x = gx / 131.0f;
      imu.gyro_y = gy / 131.0f;
      imu.gyro_z = gz / 131.0f;

      gyroYaw.sample(now, imu.accel_x, imu.accel_y, imu.accel_z, imu.gyro_x, imu.gyro_y, imu.gyro_z);
      imu.heading = gyroYaw.yaw();

      // Compute Pitch and Roll in degrees
      imu.pitch = atan2(imu.accel_y, sqrt(imu.accel_x * imu.accel_x + imu.accel_z * imu.accel_z)) * 180.0f / PI;
      imu.roll  = atan2(-imu.accel_x, imu.accel_z) * 180.0f / PI;
    }
  }

  // Read Magnetometer if available
  if (imu.mag_detected) {
    uint8_t st1 = i2cReadByte(AK8963_I2C_ADDR, 0x02); // Data ready check
    magStatus1 = st1;
    magStatusReadError = lastI2cReadError;
    magStatusReadBytes = lastI2cReadBytes;
    if (magStatusReadError == 0 && magStatusReadBytes == 1 && (st1 & 0x01)) {
      Wire.beginTransmission(AK8963_I2C_ADDR);
      Wire.write(0x03); // HXL
      magReadError = Wire.endTransmission(false);
      magReadBytes = 0;
      if (magReadError == 0) {
        magReadBytes = Wire.requestFrom((uint8_t)AK8963_I2C_ADDR, (uint8_t)7);
        if (Wire.available() >= 7) {
          uint8_t magBuf[7];
          for (int i = 0; i < 7; i++) {
            magBuf[i] = Wire.read();
          }
          int16_t mx = (int16_t)(magBuf[0] | (magBuf[1] << 8)); // Little-endian
          int16_t my = (int16_t)(magBuf[2] | (magBuf[3] << 8));
          int16_t mz = (int16_t)(magBuf[4] | (magBuf[5] << 8));
          uint8_t st2 = magBuf[6]; // Must read ST2 to unlock next reading

          rawMag[0]=mx; rawMag[1]=my; rawMag[2]=mz; magStatus2=st2;
          if (!(st2 & 0x08)) { // No magnetic sensor overflow
            imu.mag_x = mx * 0.15f;
            imu.mag_y = my * 0.15f;
            imu.mag_z = mz * 0.15f;

            sampleCompass(imu.mag_x, imu.mag_y);
          }
        }
      }
    }
  }
}

// ======================================================================================
// 6. DIRECTIONAL IR SENSOR SCANNING & FACE DOCKING
// ======================================================================================
int getDockedFaceCount() {
  int count = 0;
  for (int i = 0; i < 4; i++) {
    if (irFaceDetected[i]) count++;
  }
  return count;
}

void checkIrSensors() {
  uint32_t now = millis();
  if (now - lastIrPollTime < IR_POLL_INTERVAL_MS) return;
  lastIrPollTime = now;

  for (int i = 0; i < 4; i++) {
    int raw = digitalRead(IR_PINS[i]);
    bool detected = IR_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);

    if (detected == irFaceDetected[i]) {
      irDebounceCounters[i] = 0;
    } else {
      irDebounceCounters[i]++;
      if (irDebounceCounters[i] >= IR_DEBOUNCE_COUNT) {
        irFaceDetected[i] = detected;
        irDebounceCounters[i] = 0;

        if (detected) {
          Serial.printf("[IR] >>> FACE DOCKED: %s face detected adjacent brick! <<<\n", FACE_NAMES[i]);
          buzzDockChime();
          triggerErmHaptic(80); // Tactile docking snap pulse!
        } else {
          Serial.printf("[IR] <<< FACE UNDOCKED: %s face cleared. <<<\n", FACE_NAMES[i]);
          buzzUndockChime();
        }
      }
    }
  }
}

// ======================================================================================
// 7. SWARM TOPOLOGY & PROXIMITY EVALUATION
// ======================================================================================

// Count total currently active peers (seen within PEER_TIMEOUT_MS)
int getActiveNeighborCount() {
  int count = 0;
  for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++) {
    if (peers[i].active) {
      count++;
    }
  }
  return count;
}

// Check if this brick is close to all its active neighbors
bool isCloseToAllNeighbors() {
  int activeCount = 0;
  int closeCount = 0;

  for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++) {
    if (peers[i].active) {
      activeCount++;
      if (peers[i].is_close) {
        closeCount++;
      }
    }
  }

#if REQUIRE_ALL_SWARM_FOR_ONBOARD
  // Requires all other (TOTAL_SWARM_BRICKS - 1) bricks in the swarm to be online and close
  if (activeCount < (TOTAL_SWARM_BRICKS - 1)) return false;
#endif

  // Must have at least 1 neighbor online, and every active neighbor must be within proximity
  return (activeCount > 0 && closeCount == activeCount);
}

// Prune inactive peers that stopped transmitting
void pruneStalePeers() {
  uint32_t now = millis();
  for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++) {
    if (peers[i].active && (now - peers[i].last_seen_ms > PEER_TIMEOUT_MS)) {
      peers[i].active = false;
      peers[i].is_close = false;
      Serial.printf("[TOPO] Neighbor Brick %d timed out (no packets for %d ms)\n",
                    displayNumber(peers[i].uid), PEER_TIMEOUT_MS);
    }
  }
}

// Update proximity status with hysteresis to prevent rapid flapping
void evaluateProximity(uint8_t id, int8_t rssi) {
  if (id == 0 || id > TOTAL_SWARM_BRICKS) return;

  PeerInfo &p = peers[id];
  if (!p.active) {
    p.active = true;
    p.rssi_ema = (float)rssi;
    p.is_close = (rssi >= PROXIMITY_THRESHOLD);
  } else {
    // Smooth incoming RSSI with Exponential Moving Average
    p.rssi_ema = (RSSI_EMA_ALPHA * (float)rssi) + ((1.0f - RSSI_EMA_ALPHA) * p.rssi_ema);

    // Apply hysteresis
    if (!p.is_close && p.rssi_ema >= (PROXIMITY_THRESHOLD + PROXIMITY_HYSTERESIS)) {
      p.is_close = true;
    } else if (p.is_close && p.rssi_ema < (PROXIMITY_THRESHOLD - PROXIMITY_HYSTERESIS)) {
      p.is_close = false;
    }
  }
  p.last_seen_ms = millis();
}

// ======================================================================================
// 8. LED ANIMATION STATE MACHINE
// ======================================================================================
void updateLedStates() {
  uint32_t now = millis();
  bool closeToAll = isCloseToAllNeighbors();
  int neighborCount = getActiveNeighborCount();

  // Edge detection for full proximity consensus chime and haptic feedback
  if (closeToAll && !prevCloseToAll) {
    buzzDockChime();
    triggerErmDoublePulse(); // Tactile consensus double pulse!
  }
  prevCloseToAll = closeToAll;

  // 1. On-Board LED control: Lights when close to all active neighbors
  setOnboardLed(closeToAll);

  // 2. RGB LED Priority 1: RFID tag read -> Light Green LED
  if (rfidActive) {
    if (now - rfidDetectedTime < RFID_GREEN_HOLD_MS) {
      setRgbColor(false, true, false); // Green ON
      return;
    } else {
      rfidActive = false; // Hold time expired
    }
  }

  // 3. RGB LED Priority 2: Swarm state
  if (closeToAll) {
    // Fully docked / close to all neighbors: Solid BLUE on RGB LED
    setRgbColor(false, false, true);
    return;
  }

  // When not close to all neighbors: "else go Blue then Red in order of neighbour count"
  if (neighborCount == 0) {
    // 0 Neighbors: Solid RED (Standalone / isolated state)
    setRgbColor(true, false, false);
    return;
  }

  // Dynamic Sequential Sequence: Blinks Blue N times (N = neighborCount), then 1 Red pulse
  uint32_t bluePhaseDuration = neighborCount * (BLINK_BLUE_ON_MS + BLINK_BLUE_OFF_MS);
  uint32_t totalCycleDuration = bluePhaseDuration + BLINK_RED_ON_MS + BLINK_CYCLE_PAUSE_MS;
  uint32_t cycleTime = now % totalCycleDuration;

  if (cycleTime < bluePhaseDuration) {
    // Inside the Blue pulsing phase
    uint32_t subTime = cycleTime % (BLINK_BLUE_ON_MS + BLINK_BLUE_OFF_MS);
    if (subTime < BLINK_BLUE_ON_MS) {
      setRgbColor(false, false, true); // Blue ON
    } else {
      setRgbColor(false, false, false); // Off gap
    }
  } else if (cycleTime < bluePhaseDuration + BLINK_RED_ON_MS) {
    // Red pulse phase
    setRgbColor(true, false, false); // Red ON
  } else {
    // Inter-cycle rest pause
    setRgbColor(false, false, false); // All OFF
  }
}

// ======================================================================================
// 9. RFID SCANNING & IDENTIFICATION
// ======================================================================================
void checkRfidReader() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Extract UID string
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  lastReadUid = uidStr;

  Serial.println("========================================");
  Serial.printf("[RFID] Instrument token UID: %s\n", uidStr.c_str());
  Serial.println("[LED] Lighting Green LED (RGB)");
  Serial.println("[BUZZER] Triggering RFID confirmation beep");
  Serial.println("[HAPTIC] Triggering ERM tactile pulse");
  Serial.println("========================================");

  // Activate Green LED state, sound confirmation chirp, and trigger haptic pulse
  rfidActive = true;
  rfidDetectedTime = millis();
  buzzRfidSuccess();
  triggerErmHaptic(100); // 100ms tactile click!

  // Stop encryption & halt card
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ======================================================================================
// 10. ESP-NOW WIRELESS COMMUNICATION
// ======================================================================================

// Wi-Fi callbacks only enqueue; peer state belongs to the main loop.
struct ReceivedPacket { BrickPacket packet; int8_t rssi; };
QueueHandle_t receivedPackets;
const uint32_t PACKET_MAGIC = 0x54524132; // TRA2: gyro orientation + shared alignment; reflash all peers

uint8_t displayNumber(uint64_t uid) {
  uint8_t number = 1;
  if (nodeUid < uid) number++;
  for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++)
    if (peers[i].active && peers[i].uid < uid) number++;
  return number;
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (!receivedPackets || len != sizeof(BrickPacket)) return;
  ReceivedPacket received = {};
  memcpy(&received.packet, data, sizeof(BrickPacket));
  if (received.packet.magic != PACKET_MAGIC || !received.packet.sender_uid ||
      received.packet.sender_uid == nodeUid || received.packet.command > 1) return;
  for (size_t i=0; i<sizeof(received.packet.instrument_uid)-1; i++) {
    char c=received.packet.instrument_uid[i];
    if (!c) break;
    if (!((c>='0' && c<='9') || (c>='A' && c<='F'))) return;
  }
  received.packet.instrument_uid[sizeof(received.packet.instrument_uid)-1]=0;
  received.rssi = info && info->rx_ctrl ? info->rx_ctrl->rssi : -100;
  xQueueSend(receivedPackets, &received, 0);
}

void processReceivedPackets() {
  ReceivedPacket received;
  while (xQueueReceive(receivedPackets, &received, 0) == pdTRUE) {
    if (received.packet.command == 1) {
      gyroYaw.reset(); imu.heading=0;
      continue;
    }
    int slot = 0;
    for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++)
      if (peers[i].uid == received.packet.sender_uid) { slot = i; break; }
    if (!slot) {
      for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++)
        if (!peers[i].active) { slot = i; peers[i] = {}; break; }
    }
    if (!slot) continue; // Bounded capacity; never index arrays by an identity.
    peers[slot].uid = received.packet.sender_uid;
    evaluateProximity(slot, received.rssi);
    peers[slot].reported_neighbor_count = received.packet.neighbor_count;
    peers[slot].telemetry = received.packet;
  }
  BRICK_ID = displayNumber(nodeUid);
}

// Broadcast local state packet to all peers
void broadcastPacket() {
  BrickPacket packet = {};
  packet.magic = PACKET_MAGIC;
  packet.sender_uid = nodeUid;
  if (alignmentBroadcastActive) {
    if (millis()-alignmentBroadcastStarted < 1000) packet.command=1;
    else alignmentBroadcastActive=false;
  }
  packet.neighbor_count = (uint8_t)getActiveNeighborCount();
  for (int i = 0; i < 4; i++) if (irFaceDetected[i]) packet.face_mask |= 1 << i;
  packet.heading = imu.heading;
  packet.heading_valid = headingIsValid();
  packet.orientation_state = imu.mpu_detected ? uint8_t(gyroYaw.state()) : 0;
  packet.alignment_age_ms = gyroYaw.alignmentAge(millis());
  lastReadUid.toCharArray(packet.instrument_uid, sizeof(packet.instrument_uid));
  packet.recent_scan = rfidActive;

  esp_err_t res = esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(packet));
  if (res != ESP_OK) {
    Serial.printf("[ERROR] ESP-NOW send failed with error: %d\n", res);
  }
}

// Lowest live hardware identity hosts the dashboard. Elections need direct radio visibility.
WebServer dashboardServer(80);
bool dashboardHost = false;
uint64_t electionCandidate = 0;
uint32_t candidateSince = 0;
const char* TRAY_SSID = "SmartSurgeryTray";
const char* TRAY_PASSWORD = "smarttray22";
String uidText(uint64_t uid) {
  char text[17]; snprintf(text,sizeof(text),"%012llX",(unsigned long long)uid); return String(text);
}
String brickJson(uint64_t uid, const BrickPacket &p, uint32_t age, bool host) {
  String json="{\"uid\":\""+uidText(uid)+"\",\"number\":"+String(displayNumber(uid));
  json+=",\"ageMs\":"+String(age)+",\"gateway\":"+(host ? "true":"false");
  json+=",\"faces\":"+String(p.face_mask)+",\"heading\":";
  json+=p.heading_valid && isfinite(p.heading) ? String(p.heading,1) : "null";
  json+=",\"orientationState\":"+String(p.orientation_state)+",\"alignmentAgeMs\":"+String(p.alignment_age_ms);
  json+=",\"orientationSource\":\"gyro-relative\"";
  // Firmware-generated RFID text contains only hexadecimal digits.
  json+=",\"instrumentUid\":\""+String(p.instrument_uid)+"\",\"recentScan\":"+(p.recent_scan ? "true":"false")+"}";
  return json;
}
BrickPacket localTelemetry() {
  BrickPacket p={}; p.sender_uid=nodeUid;
  for(int i=0;i<4;i++) if(irFaceDetected[i]) p.face_mask |= 1<<i;
  p.heading=imu.heading; p.heading_valid=headingIsValid();
  p.orientation_state=imu.mpu_detected ? uint8_t(gyroYaw.state()) : 0;
  p.alignment_age_ms=gyroYaw.alignmentAge(millis());
  lastReadUid.toCharArray(p.instrument_uid,sizeof(p.instrument_uid)); p.recent_scan=rfidActive; return p;
}
void configureDashboardRoutes() {
  dashboardServer.on("/",HTTP_GET,[](){ dashboardServer.send_P(200,"text/html",DASHBOARD_HTML); });
  dashboardServer.on("/dashboard.js",HTTP_GET,[](){ dashboardServer.send_P(200,"text/javascript",DASHBOARD_JS); });
  dashboardServer.on("/layout.js",HTTP_GET,[](){ dashboardServer.send_P(200,"text/javascript",LAYOUT_JS); });
  dashboardServer.on("/api/state",HTTP_GET,[](){
    String json="{\"topology\":\"infer-grid\",\"bricks\":["+brickJson(nodeUid,localTelemetry(),0,true);
    for(int i=1;i<=TOTAL_SWARM_BRICKS;i++) if(peers[i].active) {
      json+=","+brickJson(peers[i].uid,peers[i].telemetry,millis()-peers[i].last_seen_ms,false);
    }
    dashboardServer.sendHeader("Cache-Control","no-store");
    dashboardServer.send(200,"application/json",json+"]}");
  });
  dashboardServer.on("/api/align",HTTP_POST,[](){
    if(!imu.mpu_detected) { dashboardServer.send(409,"text/plain","Gateway IMU unavailable; fix it before alignment."); return; }
    gyroYaw.reset(); imu.heading=0;
    alignmentBroadcastActive=true; alignmentBroadcastStarted=millis();
    dashboardServer.send(200,"text/plain","Alignment restart broadcast. All marked N faces must point the same way; keep every brick supported and still for at least 5 seconds. Check every card is ready.");
  });
}
void serviceDashboard() {
  uint64_t lowest=nodeUid;
  for(int i=1;i<=TOTAL_SWARM_BRICKS;i++)
    if(peers[i].active && millis()-peers[i].last_seen_ms<=PEER_TIMEOUT_MS && peers[i].uid<lowest) lowest=peers[i].uid;
  if(lowest!=electionCandidate) { electionCandidate=lowest; candidateSince=millis(); }
  bool shouldHost=lowest==nodeUid && millis()-candidateSince>=4000;
  if(dashboardHost && lowest!=nodeUid) {
    dashboardServer.stop(); WiFi.softAPdisconnect(false); WiFi.mode(WIFI_STA); dashboardHost=false;
  }
  if(shouldHost && !dashboardHost) {
    WiFi.mode(WIFI_AP_STA);
    if(WiFi.softAP(TRAY_SSID,TRAY_PASSWORD,WIFI_CHANNEL)) {
      dashboardServer.begin(); dashboardHost=true;
      Serial.println("[DASHBOARD] Join SmartSurgeryTray; open http://192.168.4.1");
    }
  }
  if(dashboardHost) dashboardServer.handleClient();
}

// Periodic serial console diagnostics
void printDiagnosticStatus() {
  int count = getActiveNeighborCount();
  int dockedCount = getDockedFaceCount();
  bool closeToAll = isCloseToAllNeighbors();

  Serial.println("\n-------------------------------------------------------------");
  Serial.printf("BRICK #%d | Active Neighbors: %d | Close To All: %s | On-Board LED: %s\n",
                BRICK_ID,
                count,
                closeToAll ? "YES" : "NO",
                closeToAll ? "ON" : "OFF");

  Serial.printf("IR Faces Docked (%d/4): [N:%s E:%s S:%s W:%s]\n",
                dockedCount,
                irFaceDetected[FACE_NORTH] ? "YES" : "--",
                irFaceDetected[FACE_EAST]  ? "YES" : "--",
                irFaceDetected[FACE_SOUTH] ? "YES" : "--",
                irFaceDetected[FACE_WEST]  ? "YES" : "--");

  if (imu.mpu_detected) {
    Serial.printf("IMU: Relative gyro yaw: %.1f deg | Pitch: %.1f | Roll: %.1f | yaw_state=%u valid=%u | Magnetometer optional: %s\n",
                  imu.heading,imu.pitch,imu.roll,uint8_t(gyroYaw.state()),headingIsValid(),imu.mag_detected ? "present":"absent");
  } else {
    Serial.println("IMU: [ACCEL/GYRO UNAVAILABLE / NOT CONNECTED]");
  }

  Serial.println("ERM Motor (D26):  [READY]");

  if (rfidActive) {
    Serial.printf("RGB LED State:    [GREEN] (RFID Active: %s)\n", lastReadUid.c_str());
  } else if (closeToAll) {
    Serial.println("RGB LED State:    [SOLID BLUE] (All Neighbors Close)");
  } else if (count == 0) {
    Serial.println("RGB LED State:    [SOLID RED] (0 Neighbors)");
  } else {
    Serial.printf("RGB LED State:    [BLUEx%d then RED] (Cycling Count Sequence)\n", count);
  }

  for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++) {
    if (peers[i].active) {
      Serial.printf("  -> Peer #%d: RSSI: %.1f dBm [%s] | Reports %d neighbors\n",
                    displayNumber(peers[i].uid),
                    peers[i].rssi_ema,
                    peers[i].is_close ? "CLOSE" : "FAR",
                    peers[i].reported_neighbor_count);
    }
  }
  Serial.println("-------------------------------------------------------------");
}

// Opt-in serial sensor diagnostics. Never re-read RFID cards or drain magnetic samples here.
long sampleAge(uint32_t timestamp) {
  return timestamp ? (long)(millis() - timestamp) : -1;
}
void debugRegister(uint8_t address, uint8_t reg, const char* label) {
  Wire.beginTransmission(address); Wire.write(reg);
  uint8_t error = Wire.endTransmission(false);
  Serial.printf("[DEBUG I2C] %s addr=0x%02X reg=0x%02X tx_status=%u", label,address,reg,error);
  if (!error) {
    int count=Wire.requestFrom(address,(uint8_t)1);
    if(count==1 && Wire.available()) Serial.printf(" value=0x%02X",Wire.read());
    else Serial.printf(" read_bytes=%d (unavailable)",count);
  }
  Serial.println();
}
void printDebugBus() {
  Serial.println("[DEBUG I2C] configured SDA=21 SCL=22; NCS=3V3 AD0=GND expected. tx_status=0 means ACK; nonzero is Wire error.");
  Serial.printf("[DEBUG I2C] pin levels SDA=%d SCL=%d (snapshot, not a bus integrity test)\n",digitalRead(PIN_I2C_SDA),digitalRead(PIN_I2C_SCL));
  const uint8_t addresses[]={0x68,0x69,0x0C};
  for(uint8_t address : addresses) {
    Wire.beginTransmission(address);
    Serial.printf("[DEBUG I2C] probe 0x%02X tx_status=%u\n",address,Wire.endTransmission());
  }
  debugRegister(0x68,0x75,"IMU WHO_AM_I");
  debugRegister(0x69,0x75,"alternate IMU WHO_AM_I");
  debugRegister(0x68,0x6B,"IMU PWR_MGMT_1");
  debugRegister(0x68,0x6A,"IMU USER_CTRL");
  debugRegister(0x68,0x37,"IMU INT_PIN_CFG (bypass)");
  debugRegister(0x0C,0x00,"mag WIA identity");
  debugRegister(0x0C,0x0A,"mag CNTL1 mode");
}
void printSensorDebug() {
  Serial.printf("\n[DEBUG] uid=%s brick=%u uptime_ms=%lu\n",uidText(nodeUid).c_str(),BRICK_ID,(unsigned long)millis());
  Serial.printf("[DEBUG IMU] boot_detected=%u sample_age_ms=%ld last_tx_status=%d last_read_bytes=%d/14\n",imu.mpu_detected,sampleAge(lastAccelSample),imuReadError,imuReadBytes);
  Serial.printf("[DEBUG ACCEL] raw_xyz=%d,%d,%d g_xyz=%.4f,%.4f,%.4f\n",rawAccel[0],rawAccel[1],rawAccel[2],imu.accel_x,imu.accel_y,imu.accel_z);
  Serial.printf("[DEBUG GYRO] raw_xyz=%d,%d,%d deg_s_xyz=%.3f,%.3f,%.3f temp_raw=%d\n",rawGyro[0],rawGyro[1],rawGyro[2],imu.gyro_x,imu.gyro_y,imu.gyro_z,rawTemperature);
  Serial.printf("[DEBUG MAG] boot_detected=%u sample_age_ms=%ld last_tx_status=%d last_read_bytes=%d/7 ST1=0x%02X ST1_tx_status=%d ST1_read_bytes=%d/1 ST2=0x%02X overflow=%u\n",imu.mag_detected,sampleAge(lastMagSample),magReadError,magReadBytes,magStatus1,magStatusReadError,magStatusReadBytes,magStatus2,!!(magStatus2&0x08));
  Serial.printf("[DEBUG MAG] raw_xyz=%d,%d,%d uT_xyz=%.3f,%.3f,%.3f\n",rawMag[0],rawMag[1],rawMag[2],imu.mag_x,imu.mag_y,imu.mag_z);
  Serial.printf("[DEBUG ORIENTATION] pitch=%.2f roll=%.2f heading=%.2f heading_valid=%u calibrated=%u calibrating=%u yaw_state=%u gyro_bias_z=%.5f alignment_age_ms=%lu\n",imu.pitch,imu.roll,imu.heading,headingIsValid(),gyroYaw.state()==PlanarYaw::Ready,gyroYaw.state()==PlanarYaw::Calibrating,uint8_t(gyroYaw.state()),gyroYaw.bias(),(unsigned long)gyroYaw.alignmentAge(millis()));
  Serial.println("[DEBUG] Readings are cached; sample_age_ms=-1 means NO sample, not a real zero measurement.");
  for(int i=0;i<4;i++) Serial.printf("[DEBUG IR] face=%s gpio=%u raw=%d docked=%u debounce=%u\n",FACE_NAMES[i],IR_PINS[i],digitalRead(IR_PINS[i]),irFaceDetected[i],irDebounceCounters[i]);
  Serial.printf("[DEBUG RFID] reader_version=0x%02X last_instrument_uid=%s scan_age_ms=%ld recent_scan=%u (not continuous presence)\n",rfid.PCD_ReadRegister(rfid.VersionReg),lastReadUid.length()?lastReadUid.c_str():"none",lastReadUid.length()?sampleAge(rfidDetectedTime):-1,rfidActive);
  for(int i=1;i<=TOTAL_SWARM_BRICKS;i++) if(peers[i].active)
    Serial.printf("[DEBUG RADIO] peer_uid=%s RSSI_ema_dBm=%.2f close=%u age_ms=%lu\n",uidText(peers[i].uid).c_str(),peers[i].rssi_ema,peers[i].is_close,(unsigned long)(millis()-peers[i].last_seen_ms));
}
bool verifySensorWhoAmI(const char* name, int expected, int received, bool isHex = true) {
  bool match = (expected == received);
  if (isHex) {
    Serial.printf("[WHOAMI] %-28s: Expected=0x%02X, Received=0x%02X -> Match: %s (Boolean: %d)\n",
                  name, (uint8_t)expected, (uint8_t)received, match ? "TRUE" : "FALSE", match ? 1 : 0);
  } else {
    Serial.printf("[WHOAMI] %-28s: Expected=%d, Received=%d -> Match: %s (Boolean: %d)\n",
                  name, expected, received, match ? "TRUE" : "FALSE", match ? 1 : 0);
  }
  return match;
}

bool printSensorWhoAmI() {
  Serial.printf("\n==================== SENSOR WHO_AM_I VERIFICATION (UID: %s) ====================\n", uidText(nodeUid).c_str());
  bool allMatch = true;

  // 1. MPU9250 Accelerometer / Gyroscope (Expected: 0x71 for genuine MPU9250)
  uint8_t mpuWho = i2cReadByte(MPU9250_I2C_ADDR, 0x75);
  bool mpuMatch = verifySensorWhoAmI("MPU6500/9250 (0x68 Reg 0x75)", mpuWho == 0x70 ? 0x70 : 0x71, mpuWho, true);
  mpuMatch = mpuMatch && lastI2cReadError == 0 && lastI2cReadBytes == 1;
  if (!mpuMatch) {
    allMatch = false;
    if (lastI2cReadError != 0 || lastI2cReadBytes != 1 || mpuWho == 0xFF || mpuWho == 0x00) {
      Serial.println("         --> NOTE: No response on I2C address 0x68. Verify SDA=21, SCL=22, NCS=3V3, AD0=GND.");
    }
  }

  // 2. AK8963 Magnetometer (Expected: 0x48 on I2C bypass address 0x0C)
  uint8_t magWho = i2cReadByte(AK8963_I2C_ADDR, 0x00);
  bool magMatch = verifySensorWhoAmI("AK8963 Mag (0x0C Reg 0x00)", 0x48, magWho, true);
  magMatch = magMatch && lastI2cReadError == 0 && lastI2cReadBytes == 1;
  if (!magMatch) {
    Serial.println("         --> Magnetometer is optional; relative gyro yaw still works.");
    if (magWho == 0xFF || magWho == 0x00) {
      Serial.println("         --> NOTE: Address 0x0C not responding. AK8963 die missing or I2C bypass disabled.");
    }
  }

  // 3. RFID MFRC522 (Expected: 0x92 for v2.0 or 0x91 for v1.0)
  uint8_t rfidVer = rfid.PCD_ReadRegister(rfid.VersionReg);
  bool rfidMatch = verifySensorWhoAmI("RFID MFRC522 (Reg 0x37)", rfidVer == 0x91 ? 0x91 : 0x92, rfidVer, true);
  if (!rfidMatch) {
    allMatch = false;
    if (rfidVer == 0x00 || rfidVer == 0xFF) {
      Serial.println("         --> NOTE: RFID reader not responding on SPI bus (check SS=5, SCK=18, MOSI=23, MISO=19, RST=4).");
    }
  }

  // IR obstacle sensors have no identity register. An occupied face is not a sensor fault.
  for (int i = 0; i < 4; i++) {
    const int raw = digitalRead(IR_PINS[i]);
    const bool occupied = IR_ACTIVE_LOW ? raw == LOW : raw == HIGH;
    Serial.printf("[WHOAMI] IR %s (GPIO %u): raw=%d occupied=%u (state only; no identity/health check)\n",
                  FACE_NAMES[i],IR_PINS[i],raw,occupied);
  }

  Serial.printf("---------------------------------------------------------------------------------\n");
  Serial.printf("[WHOAMI] OVERALL STATUS: %s (Boolean: %d)\n", allMatch ? "ALL_MATCH" : "MISMATCH_DETECTED", allMatch ? 1 : 0);
  Serial.println("=================================================================================\n");
  return allMatch;
}

void printHelp() {
  Serial.println("\n==================== AVAILABLE SERIAL COMMANDS ====================");
  Serial.println("  --debug        : Enable continuous sensor telemetry stream (every 500ms)");
  Serial.println("  --no-debug     : Disable continuous sensor telemetry stream");
  Serial.println("  --debug-off    : Alias for --no-debug");
  Serial.println("  --debug-i2c    : Probe I2C bus addresses (0x68, 0x69, 0x0C) and registers");
  Serial.println("  --debug-reinit : Reinitialize MPU9250 / AK8963 IMU");
  Serial.println("  --whoami       : Verify WHO_AM_I & registers for all sensors (MPU, RFID, 4x IR)");
  Serial.println("  --kill-whoami  : Stop periodic WHO_AM_I verification");
  Serial.println("  --help         : Display this command reference list");
  Serial.println("===================================================================\n");
}

void applyDebugCommand(DebugCommand command) {
  if(command==DebugCommand::Enable) {
    debugEnabled=true; Serial.println("[DEBUG] enabled; detailed sensors every 500ms; --no-debug disables.");
    printDebugBus(); printSensorDebug(); lastSensorDebugPrint=millis();
  } else if(command==DebugCommand::Disable) {
    debugEnabled=false; Serial.println("[DEBUG] disabled (normal event/status logs remain).");
  } else if(command==DebugCommand::Probe) {
    printDebugBus();
  } else if(command==DebugCommand::Reinitialize) {
    // Explicit command only: allows retry after correcting an absent-at-boot sensor.
    imu.mpu_detected=false; imu.mag_detected=false;
    lastAccelSample=0; lastMagSample=0; imuReadError=-1; magReadError=-1;
    imuReadBytes=0; magReadBytes=0; magStatus1=0; magStatus2=0; magStatusReadError=-1; magStatusReadBytes=0;
    initMpu9250(); printDebugBus();
  } else if(command==DebugCommand::WhoAmI) {
    whoamiEnabled=true;
    printSensorWhoAmI();
    lastWhoAmIPrint=millis();
    Serial.println("[WHOAMI] enabled; checking every 1000ms; --kill-whoami stops.");
  } else if(command==DebugCommand::KillWhoAmI) {
    whoamiEnabled=false;
    Serial.println("[WHOAMI] stopped/killed.");
  } else if(command==DebugCommand::Help) {
    printHelp();
  } else if(command==DebugCommand::Unknown) {
    Serial.println("[DEBUG] commands: --debug, --no-debug, --debug-i2c, --debug-reinit, --whoami, --kill-whoami, --help (send newline)");
  }
}
void serviceSerialDebug() {
  static DebugCommandParser parser;
  for(int budget=0;budget<64 && Serial.available();budget++)
    applyDebugCommand(parser.feed((char)Serial.read()));
  if(debugEnabled && millis()-lastSensorDebugPrint>=500) {
    lastSensorDebugPrint=millis(); printSensorDebug();
  }
  if(whoamiEnabled && millis()-lastWhoAmIPrint>=1000) {
    lastWhoAmIPrint=millis(); printSensorWhoAmI();
  }
}

// ======================================================================================
// 11. ARDUINO SETUP & MAIN LOOP
// ======================================================================================
void setup() {
  // Leave brownout protection enabled; inadequate supplies must be corrected in hardware.

  nodeUid = ESP.getEfuseMac();
  receivedPackets = xQueueCreate(16, sizeof(ReceivedPacket));
  if (!receivedPackets) { while (true) delay(1000); }
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("=============================================================");
  Serial.println("     MULTI-AGENT SMART BRICKS (MICROBOTS) FIRMWARE           ");
  Serial.printf ("     Node Identity: BRICK #%d of %d                          \n", BRICK_ID, TOTAL_SWARM_BRICKS);
  Serial.println("     Pin Map: All GPIOs <= 35 (verify classic ESP32-WROOM board pinout) \n");
  Serial.println("=============================================================");

  // Initialize LED Pins
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  pinMode(PIN_LED_RED,     OUTPUT);
  pinMode(PIN_LED_GREEN,   OUTPUT);
  pinMode(PIN_LED_BLUE,    OUTPUT);

  setOnboardLed(false);
  setRgbColor(true, false, false); // Start Red (Standalone indicator)

  // Initialize Buzzer Pin (GPIO 27)
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  // Initialize ERM Vibration Motor (GPIO 26)
  initErmMotor();

  // Initialize 4 Directional IR Sensor Pins (GPIO 34, 35, 32, 33)
  for (int i = 0; i < 4; i++) {
    pinMode(IR_PINS[i], INPUT);
    irFaceDetected[i] = false;
    prevIrFaceDetected[i] = false;
    irDebounceCounters[i] = 0;
  }
  Serial.println("[IR] 4-Face Directional IR Sensors initialized (North:34, East:35, South:32, West:33)");

  // Initialize I2C Bus for MPU9250
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  initMpu9250();

  // Initialize MFRC522 RFID Reader
  SPI.begin();
  rfid.PCD_Init();
  byte rfidVer = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.printf("[RFID] MFRC522 Reader initialized. Hardware version: 0x%02X\n", rfidVer);
  if (rfidVer == 0x00 || rfidVer == 0xFF) {
    Serial.println("[WARN] RFID reader not responding on SPI! Check wiring.");
  }

  // Initialize Peer Table
  for (int i = 1; i <= TOTAL_SWARM_BRICKS; i++) {
    peers[i].active = false;
    peers[i].last_seen_ms = 0;
    peers[i].rssi_ema = -100.0f;
    peers[i].reported_neighbor_count = 0;
    peers[i].is_close = false;
  }

  // Initialize Wi-Fi in Station mode for ESP-NOW
  delay(150);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setTxPower(WIFI_POWER_11dBm);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("[WIFI] MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("[WIFI] Channel locked to %d\n", WIFI_CHANNEL);

  // Initialize ESP-NOW Protocol
  if (esp_now_init() != ESP_OK) {
    Serial.println("[FATAL] ESP-NOW initialization failed! System halted.");
    while (true) {
      setRgbColor(true, false, false);
      delay(200);
      setRgbColor(false, false, false);
      delay(200);
    }
  }
  esp_now_register_recv_cb(onDataRecv);

  // Register Broadcast Peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx   = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[FATAL] Failed to register broadcast peer in ESP-NOW!");
  } else {
    Serial.println("[ESP-NOW] Broadcast peer registered successfully.");
  }

  configureDashboardRoutes();
  buzzBootChime();
  triggerErmHaptic(50); // Start feedback after setup; loop services it without blocking.
  Serial.println("[YAW] Align every marked N face the same way, keep supported and still for 3 seconds. Gyro yaw is relative and drifts.");
  Serial.println("[DEBUG] Send --debug with newline at 115200 baud for all sensor readings; --debug-i2c probes the IMU bus.");
  if(debugEnabled) { printDebugBus(); printSensorDebug(); }
  Serial.printf("[INFO] Brick #%d initialization complete. Running swarm loop...\n\n", BRICK_ID);
}

void loop() {
  uint32_t now = millis();

  processReceivedPackets();
  serviceDashboard();

  // 1. Check 4 Directional IR Sensors for Face Docking
  checkIrSensors();

  // 2. Poll 9-DoF IMU & Magnetometer for Self-Orientation
  readMpu9250();

  // 3. Check top-facing RFID reader for instrument tags
  checkRfidReader();

  // Refresh time after sensor/HTTP work before scheduling telemetry.
  now = millis();
  // 4. Broadcast local state packet periodically
  if (now - lastBroadcastTime > BROADCAST_INTERVAL_MS) {
    lastBroadcastTime = now;
    broadcastPacket();
  }

  // 5. Prune stale peers that have timed out
  pruneStalePeers();
  BRICK_ID = displayNumber(nodeUid);

  // 6. Update On-Board and RGB LED animations & Haptics
  updateLedStates();

  // 7. Diagnostics serial logger
  if (now - lastDebugPrintTime > 2000) {
    lastDebugPrintTime = now;
    printDiagnosticStatus();
  }

  serviceSerialDebug();
  serviceFeedback();

  // Small non-blocking yield for FreeRTOS scheduler
  delay(10);
}
