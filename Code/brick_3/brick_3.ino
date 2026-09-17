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
 *  5. RFID reader (MFRC522) via VSPI tracking neighbor IDs and token presence.
 *  6. 4-Face Directional IR Sensors (North=34, East=35, South=32, West=33) for docking.
 *  7. Audio Buzzer (GPIO 27) for acoustic feedback:
 *      - RFID read confirmation chirp
 *      - Physical face docking & undocking chime
 *      - Proximity consensus alert
 *      - System boot chime
 *  8. 9-DoF IMU + Magnetometer (MPU9250 / AK8963 on I2C: SDA=GPIO 21, SCL=GPIO 22):
 *      - Real-time 3-axis accelerometer and gyroscope tracking
 *      - AK8963 3-axis magnetometer for Magnetic North compass heading
 *      - Calculates Pitch, Roll, and Yaw (Compass Heading) for self-orientation
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
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ======================================================================================
// 1. CONFIGURATION PARAMETERS (Configure BRICK_ID per node before flashing)
// ======================================================================================
#define BRICK_ID              3        // Unique ID for this brick: 1, 2, 3, or 4
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

// Audio Buzzer (Active or driven passive buzzer)
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
  uint8_t sender_id;       // Unique ID of the transmitting brick (1..4)
  uint8_t neighbor_count;  // Count of currently active neighbors seen by sender
} BrickPacket;

// Local tracking record for a peer brick
struct PeerInfo {
  bool     active;                   // True if peer has broadcasted within PEER_TIMEOUT_MS
  uint32_t last_seen_ms;             // Timestamp of last received packet
  float    rssi_ema;                 // Smoothed RSSI signal strength
  uint8_t  reported_neighbor_count;  // Neighbor count reported by peer
  bool     is_close;                 // Proximity status with hysteresis
};

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
  float    heading;                  // Magnetic North heading in degrees (0 to 360)
};
ImuData imu = {};
uint32_t lastImuPollTime = 0;

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

// Known neighbor RFID Card UIDs (Customizable for physical microbot tokens)
struct KnownTag {
  const char* uid;
  uint8_t brick_id;
};
const KnownTag KNOWN_TAGS[] = {
  {"1A2B3C4D", 1},
  {"5E6F7A8B", 2},
  {"9C0D1E2F", 3},
  {"3A4B5C6D", 4}
};
const size_t KNOWN_TAGS_COUNT = sizeof(KNOWN_TAGS) / sizeof(KNOWN_TAGS[0]);

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

void triggerErmHaptic(uint16_t duration_ms) {
  digitalWrite(PIN_ERM_MOTOR, HIGH);
  delay(duration_ms);
  digitalWrite(PIN_ERM_MOTOR, LOW);
}

void triggerErmDoublePulse() {
  digitalWrite(PIN_ERM_MOTOR, HIGH);
  delay(50);
  digitalWrite(PIN_ERM_MOTOR, LOW);
  delay(40);
  digitalWrite(PIN_ERM_MOTOR, HIGH);
  delay(70);
  digitalWrite(PIN_ERM_MOTOR, LOW);
}

// Backward compatibility wrapper
void triggerHapticFeedback(uint16_t duration_ms) {
  triggerErmHaptic(duration_ms);
}

// Base Buzzer Pulse
void buzzBeep(uint16_t duration_ms) {
#if BUZZER_ENABLED
  digitalWrite(PIN_BUZZER, HIGH);
  delay(duration_ms);
  digitalWrite(PIN_BUZZER, LOW);
#endif
}

// Sound Profile: RFID Scan Success (Happy double-chirp)
void buzzRfidSuccess() {
#if BUZZER_ENABLED
  buzzBeep(60);
  delay(40);
  buzzBeep(100);
#endif
}

// Sound Profile: Physical Docking / Close-to-All Consensus (Ascending chime)
void buzzDockChime() {
#if BUZZER_ENABLED
  buzzBeep(40);
  delay(30);
  buzzBeep(80);
#endif
}

// Sound Profile: Undock / Disconnect Event (Single short alert tone)
void buzzUndockChime() {
#if BUZZER_ENABLED
  buzzBeep(120);
#endif
}

// Sound Profile: Boot Startup Melodic Chime
void buzzBootChime() {
#if BUZZER_ENABLED
  buzzBeep(50);
  delay(40);
  buzzBeep(50);
  delay(40);
  buzzBeep(120);
#endif
}

// ======================================================================================
// 5. 9-DoF IMU & MAGNETOMETER DRIVERS (MPU9250 & AK8963)
// ======================================================================================
void i2cWriteByte(uint8_t devAddr, uint8_t regAddr, uint8_t data) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t i2cReadByte(uint8_t devAddr, uint8_t regAddr) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom((uint8_t)devAddr, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 0xFF;
}

bool initMpu9250() {
  Wire.beginTransmission(MPU9250_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("[IMU] MPU9250 not detected at 0x68. Verify SDA=GPIO21, SCL=GPIO22, NCS=3V3, AD0=GND.");
    imu.mpu_detected = false;
    imu.mag_detected = false;
    return false;
  }

  uint8_t whoami = i2cReadByte(MPU9250_I2C_ADDR, 0x75);
  Serial.printf("[IMU] MPU9250 found! Device WHO_AM_I: 0x%02X\n", whoami);

  // Wake up sensor: clear SLEEP bit in PWR_MGMT_1 (0x6B)
  i2cWriteByte(MPU9250_I2C_ADDR, 0x6B, 0x00);
  delay(15);

  // Disable I2C Master mode in USER_CTRL (0x6A) to allow bypass access to AK8963
  i2cWriteByte(MPU9250_I2C_ADDR, 0x6A, 0x00);
  delay(10);

  // Enable I2C Bypass mode in INT_PIN_CFG (0x37) to access internal AK8963 on main I2C bus
  i2cWriteByte(MPU9250_I2C_ADDR, 0x37, 0x02);
  delay(15);

  // Probe AK8963 Magnetometer at 0x0C
  Wire.beginTransmission(AK8963_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    uint8_t magWhoAmI = i2cReadByte(AK8963_I2C_ADDR, 0x00);
    Serial.printf("[IMU] AK8963 Magnetometer online! ID: 0x%02X\n", magWhoAmI);
    // Set 16-bit resolution, 100Hz continuous measurement mode 2
    i2cWriteByte(AK8963_I2C_ADDR, 0x0A, 0x16);
    imu.mag_detected = true;
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
  if (Wire.endTransmission(false) == 0) {
    Wire.requestFrom((uint8_t)MPU9250_I2C_ADDR, (uint8_t)14);
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

      // Convert to physical units (+/-2g -> 16384 LSB/g; +/-250 deg/s -> 131 LSB/deg/s)
      imu.accel_x = ax / 16384.0f;
      imu.accel_y = ay / 16384.0f;
      imu.accel_z = az / 16384.0f;

      imu.gyro_x = gx / 131.0f;
      imu.gyro_y = gy / 131.0f;
      imu.gyro_z = gz / 131.0f;

      // Compute Pitch and Roll in degrees
      imu.pitch = atan2(imu.accel_y, sqrt(imu.accel_x * imu.accel_x + imu.accel_z * imu.accel_z)) * 180.0f / PI;
      imu.roll  = atan2(-imu.accel_x, imu.accel_z) * 180.0f / PI;
    }
  }

  // Read Magnetometer if available
  if (imu.mag_detected) {
    uint8_t st1 = i2cReadByte(AK8963_I2C_ADDR, 0x02); // Data ready check
    if (st1 & 0x01) {
      Wire.beginTransmission(AK8963_I2C_ADDR);
      Wire.write(0x03); // HXL
      if (Wire.endTransmission(false) == 0) {
        Wire.requestFrom((uint8_t)AK8963_I2C_ADDR, (uint8_t)7);
        if (Wire.available() >= 7) {
          uint8_t magBuf[7];
          for (int i = 0; i < 7; i++) {
            magBuf[i] = Wire.read();
          }
          int16_t mx = (int16_t)(magBuf[0] | (magBuf[1] << 8)); // Little-endian
          int16_t my = (int16_t)(magBuf[2] | (magBuf[3] << 8));
          int16_t mz = (int16_t)(magBuf[4] | (magBuf[5] << 8));
          uint8_t st2 = magBuf[6]; // Must read ST2 to unlock next reading

          if (!(st2 & 0x08)) { // No magnetic sensor overflow
            imu.mag_x = mx * 0.15f;
            imu.mag_y = my * 0.15f;
            imu.mag_z = mz * 0.15f;

            // Planar compass heading in degrees relative to Magnetic North
            float h = atan2(imu.mag_y, imu.mag_x) * 180.0f / PI;
            if (h < 0.0f) h += 360.0f;
            imu.heading = h;
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
    if (i == BRICK_ID) continue;
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
    if (i == BRICK_ID) continue;
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
    if (i == BRICK_ID) continue;
    if (peers[i].active && (now - peers[i].last_seen_ms > PEER_TIMEOUT_MS)) {
      peers[i].active = false;
      peers[i].is_close = false;
      Serial.printf("[TOPO] Neighbor Brick %d timed out (no packets for %d ms)\n", 
                    i, PEER_TIMEOUT_MS);
    }
  }
}

// Update proximity status with hysteresis to prevent rapid flapping
void evaluateProximity(uint8_t id, int8_t rssi) {
  if (id == 0 || id > TOTAL_SWARM_BRICKS || id == BRICK_ID) return;

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

  // Identify neighbor if tag matches known table
  uint8_t neighborId = 0;
  for (size_t i = 0; i < KNOWN_TAGS_COUNT; i++) {
    if (uidStr.equalsIgnoreCase(KNOWN_TAGS[i].uid)) {
      neighborId = KNOWN_TAGS[i].brick_id;
      break;
    }
  }

  Serial.println("========================================");
  Serial.printf("[RFID] Card Detected! UID: %s\n", uidStr.c_str());
  if (neighborId > 0) {
    Serial.printf("[RFID] Identified Neighbor Brick: #%d\n", neighborId);
  } else {
    Serial.println("[RFID] Generic Neighbor / Swarm Token Detected");
  }
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

// Callback executed upon receiving an ESP-NOW frame
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(BrickPacket)) {
    return;
  }

  BrickPacket incoming;
  memcpy(&incoming, data, sizeof(BrickPacket));

  // Ignore our own looped back packets
  if (incoming.sender_id == BRICK_ID) {
    return;
  }

  if (incoming.sender_id == 0 || incoming.sender_id > TOTAL_SWARM_BRICKS) {
    return;
  }

  // Extract packet RSSI from radio control structure
  int8_t rssi = -70;
  if (info && info->rx_ctrl) {
    rssi = info->rx_ctrl->rssi;
  }

  // Update peer state and proximity
  evaluateProximity(incoming.sender_id, rssi);
  peers[incoming.sender_id].reported_neighbor_count = incoming.neighbor_count;
}

// Broadcast local state packet to all peers
void broadcastPacket() {
  BrickPacket packet;
  packet.sender_id = BRICK_ID;
  packet.neighbor_count = (uint8_t)getActiveNeighborCount();

  esp_err_t res = esp_now_send(broadcastAddress, (uint8_t*)&packet, sizeof(packet));
  if (res != ESP_OK) {
    Serial.printf("[ERROR] ESP-NOW send failed with error: %d\n", res);
  }
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
    if (imu.mag_detected) {
      Serial.printf("IMU (MPU9250):    Heading: %.1f deg | Pitch: %.1f deg | Roll: %.1f deg [ONLINE]\n",
                    imu.heading, imu.pitch, imu.roll);
    } else {
      Serial.printf("IMU (MPU9250):    Pitch: %.1f deg | Roll: %.1f deg [6-DoF ONLINE]\n",
                    imu.pitch, imu.roll);
    }
  } else {
    Serial.println("IMU (MPU9250):    [OFFLINE / NOT CONNECTED]");
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
    if (i == BRICK_ID) continue;
    if (peers[i].active) {
      Serial.printf("  -> Peer #%d: RSSI: %.1f dBm [%s] | Reports %d neighbors\n",
                    i,
                    peers[i].rssi_ema,
                    peers[i].is_close ? "CLOSE" : "FAR",
                    peers[i].reported_neighbor_count);
    }
  }
  Serial.println("-------------------------------------------------------------");
}

// ======================================================================================
// 11. ARDUINO SETUP & MAIN LOOP
// ======================================================================================
void setup() {
  // Disable hardware brownout detector to prevent reboot loops from Wi-Fi RF power surges & heavy peripheral load
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("=============================================================");
  Serial.println("     MULTI-AGENT SMART BRICKS (MICROBOTS) FIRMWARE           ");
  Serial.printf ("     Node Identity: BRICK #%d of %d                          \n", BRICK_ID, TOTAL_SWARM_BRICKS);
  Serial.println("     Pin Map: All GPIOs <= 35 (Compatible with 30-Pin ESP32) \n");
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

  // Initial Boot Acoustic & Haptic Chime
  buzzBootChime();
  triggerErmHaptic(50); // 50ms tactile pulse confirming ERM motor is working

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
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
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

  Serial.printf("[INFO] Brick #%d initialization complete. Running swarm loop...\n\n", BRICK_ID);
}

void loop() {
  uint32_t now = millis();

  // 1. Check 4 Directional IR Sensors for Face Docking
  checkIrSensors();

  // 2. Poll 9-DoF IMU & Magnetometer for Self-Orientation
  readMpu9250();

  // 3. Check RFID Reader for local neighbor tag
  checkRfidReader();

  // 4. Broadcast local state packet periodically
  if (now - lastBroadcastTime > BROADCAST_INTERVAL_MS) {
    lastBroadcastTime = now;
    broadcastPacket();
  }

  // 5. Prune stale peers that have timed out
  pruneStalePeers();

  // 6. Update On-Board and RGB LED animations & Haptics
  updateLedStates();

  // 7. Diagnostics serial logger
  if (now - lastDebugPrintTime > 2000) {
    lastDebugPrintTime = now;
    printDiagnosticStatus();
  }

  // Small non-blocking yield for FreeRTOS scheduler
  delay(10);
}
