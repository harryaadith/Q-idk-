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
 *  8. Future integration stubs:
 *      - 9-DoF IMU + Magnetometer (I2C: GPIO 21, 22) for self-orientation
 *      - Haptic Vibrator Motor Actuator (GPIO 26)
 * ======================================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <SPI.h>
#include <MFRC522.h>

// ======================================================================================
// 1. CONFIGURATION PARAMETERS (Configure BRICK_ID per node before flashing)
// ======================================================================================
#define BRICK_ID              1        // Unique ID for this brick: 1, 2, 3, or 4
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
#define PIN_IR_NORTH          34       // North / Top face sensor (D34)
#define PIN_IR_EAST           14       // East / Right face sensor (D35)
#define PIN_IR_SOUTH          32       // South / Bottom face sensor (D32)
#define PIN_IR_WEST           33       // West / Left face sensor (D33)

// Audio Buzzer (Active or driven passive buzzer)
#define PIN_BUZZER            27       // Driven via GPIO 27 (D27)

// --------------------------------------------------------------------------------------
// FUTURE EXPANSION PINS (Reserved for upcoming roadmap modules)
// --------------------------------------------------------------------------------------
// IMU + Magnetometer (I2C Bus: MPU6050/9250 or BNO055)
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22

// Haptic Vibrator Motor Actuator (Transistor / MOSFET gate)
#define PIN_VIBRATOR_MOTOR    26

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
// 4. LOW-LEVEL HARDWARE DRIVERS (Buzzer, LEDs, IR)
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
// 5. DIRECTIONAL IR SENSOR SCANNING & FACE DOCKING
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
      // Reset debounce counter if state matches current stable state
      irDebounceCounters[i] = 0;
    } else {
      irDebounceCounters[i]++;
      if (irDebounceCounters[i] >= IR_DEBOUNCE_COUNT) {
        // Confirmed state change!
        irFaceDetected[i] = detected;
        irDebounceCounters[i] = 0;

        if (detected) {
          Serial.printf("[IR] >>> FACE DOCKED: %s face detected adjacent brick! <<<\n", FACE_NAMES[i]);
          buzzDockChime();
        } else {
          Serial.printf("[IR] <<< FACE UNDOCKED: %s face cleared. <<<\n", FACE_NAMES[i]);
          buzzUndockChime();
        }
      }
    }
  }
}

// ======================================================================================
// 6. FUTURE EXPANSION TODO STUBS
// ======================================================================================
bool enableImuOrientation = false;
bool enableVibratorMotor = false;

struct SwarmOrientation {
  float yaw;      // Compass heading relative to Magnetic North
  float pitch;    // Inclination pitch
  float roll;     // Inclination roll
};
SwarmOrientation currentOrientation = {0.0f, 0.0f, 0.0f};

void initFutureHardware() {
  /*
   * FUTURE TODO:
   * 1. Initialize Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL) and configure IMU/Magnetometer
   * 2. Initialize Vibrator Motor PWM/digital pin on GPIO 26
   */
  pinMode(PIN_VIBRATOR_MOTOR, OUTPUT);
  digitalWrite(PIN_VIBRATOR_MOTOR, LOW);
}

void triggerHapticFeedback(uint16_t duration_ms) {
  digitalWrite(PIN_VIBRATOR_MOTOR, HIGH);
  delay(duration_ms);
  digitalWrite(PIN_VIBRATOR_MOTOR, LOW);
}

void updateImuSelfOrientation() {
  // FUTURE TODO: Query MPU9250 / QMC5883L for yaw/pitch/roll orientation
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

  // Edge detection for full proximity consensus chime
  if (closeToAll && !prevCloseToAll) {
    buzzDockChime();
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
  Serial.println("========================================");

  // Activate Green LED state & sound confirmation chirp
  rfidActive = true;
  rfidDetectedTime = millis();
  buzzRfidSuccess();

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
  int8_t rssi = info->rx_ctrl->rssi;

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

  if (rfidActive) {
    Serial.printf("RGB LED State: [GREEN] (RFID Active: %s)\n", lastReadUid.c_str());
  } else if (closeToAll) {
    Serial.println("RGB LED State: [SOLID BLUE] (All Neighbors Close)");
  } else if (count == 0) {
    Serial.println("RGB LED State: [SOLID RED] (0 Neighbors)");
  } else {
    Serial.printf("RGB LED State: [BLUEx%d then RED] (Cycling Count Sequence)\n", count);
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

  // Initialize 4 Directional IR Sensor Pins (GPIO 34, 35, 32, 33)
  for (int i = 0; i < 4; i++) {
    pinMode(IR_PINS[i], INPUT);
    irFaceDetected[i] = false;
    prevIrFaceDetected[i] = false;
    irDebounceCounters[i] = 0;
  }
  Serial.println("[IR] 4-Face Directional IR Sensors initialized (North:34, East:35, South:32, West:33)");

  // Initial Boot Acoustic Chime
  buzzBootChime();

  // Initialize Future Hardware Stubs
  initFutureHardware();

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

  // 2. Check RFID Reader for local neighbor tag
  checkRfidReader();

  // 3. Broadcast local state packet periodically
  if (now - lastBroadcastTime > BROADCAST_INTERVAL_MS) {
    lastBroadcastTime = now;
    broadcastPacket();
  }

  // 4. Prune stale peers that have timed out
  pruneStalePeers();

  // 5. Update On-Board and RGB LED animations
  updateLedStates();

  // 6. Diagnostics serial logger
  if (now - lastDebugPrintTime > 2000) {
    lastDebugPrintTime = now;
    printDiagnosticStatus();
  }

  // Small non-blocking yield for FreeRTOS scheduler
  delay(10);
}
