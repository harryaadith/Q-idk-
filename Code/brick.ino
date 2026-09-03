/*
 * ======================================================================================
 * Project: Multi-Agent Microbots (Smart Bricks) — Unified Firmware
 * File:    brick.ino
 * Target:  ESP32 Dev Module (4-Brick Swarm Deployment)
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
 *  6. Future TODO integration stubs for:
 *      - 4x Directional IR sensors (North, East, South, West)
 *      - 9-DoF IMU + Magnetometer (I2C) for self-orientation
 *      - Haptic Vibrator Motor Actuator
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

// LED Animation Timings (in milliseconds)
#define BLINK_BLUE_ON_MS      220      // Blue pulse duration
#define BLINK_BLUE_OFF_MS     180      // Gap between Blue pulses
#define BLINK_RED_ON_MS       450      // Red pulse duration at end of sequence
#define BLINK_CYCLE_PAUSE_MS  600      // Pause before repeating count sequence

// Optional: require all 3 swarm peers to be present and close, or all currently active peers
#define REQUIRE_ALL_SWARM_FOR_ONBOARD false

// ======================================================================================
// 2. HARDWARE PIN DEFINITIONS
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

// --------------------------------------------------------------------------------------
// FUTURE EXPANSION PINS (TODO: Documented and allocated for future revisions)
// --------------------------------------------------------------------------------------
// 4 Directional IR Phototransistors / Demodulated Receivers (Input-only GPIOs)
#define PIN_IR_NORTH          34
#define PIN_IR_EAST           35
#define PIN_IR_SOUTH          36
#define PIN_IR_WEST           39

// IMU + Magnetometer (I2C Bus: MPU6050/9250 or BNO055)
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22

// Haptic Vibrator Motor Actuator (Transistor / MOSFET gate)
#define PIN_VIBRATOR_MOTOR    32

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

// Hardware Instances
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Operational State Variables
uint32_t lastBroadcastTime = 0;
uint32_t lastDebugPrintTime = 0;
uint32_t rfidDetectedTime = 0;
bool     rfidActive = false;
String   lastReadUid = "";

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
// 4. FUTURE EXPANSION TODO STUBS
// ======================================================================================
// Flags to enable/disable future hardware features
bool enableIrSensors = false;
bool enableImuOrientation = false;
bool enableVibratorMotor = false;

struct SwarmOrientation {
  float yaw;      // Compass heading relative to Magnetic North
  float pitch;    // Inclination pitch
  float roll;     // Inclination roll
};
SwarmOrientation currentOrientation = {0.0f, 0.0f, 0.0f};

// Directional face docking matrix (North, East, South, West)
uint8_t irDockedFaces[4] = {0, 0, 0, 0};

void initFutureHardware() {
  /*
   * FUTURE TODO:
   * 1. Initialize Directional IR phototransistors on GPIO 34, 35, 36, 39
   * 2. Initialize Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL) and configure IMU/Magnetometer
   * 3. Initialize Vibrator Motor PWM/digital pin on GPIO 32
   */
  pinMode(PIN_IR_NORTH, INPUT);
  pinMode(PIN_IR_EAST, INPUT);
  pinMode(PIN_IR_SOUTH, INPUT);
  pinMode(PIN_IR_WEST, INPUT);
  pinMode(PIN_VIBRATOR_MOTOR, OUTPUT);
  digitalWrite(PIN_VIBRATOR_MOTOR, LOW);
}

void triggerHapticFeedback(uint16_t duration_ms) {
  // FUTURE TODO: Trigger haptic feedback when docking, receiving neighbor, or reading RFID
  digitalWrite(PIN_VIBRATOR_MOTOR, HIGH);
  delay(duration_ms);
  digitalWrite(PIN_VIBRATOR_MOTOR, LOW);
}

void updateIrDirectionDetection() {
  // FUTURE TODO: Read IR receivers to determine relative spatial orientation of neighbors
  // e.g. digitalRead(PIN_IR_NORTH), etc.
}

void updateImuSelfOrientation() {
  // FUTURE TODO: Query MPU9250 / QMC5883L for yaw/pitch/roll orientation
}

// ======================================================================================
// 5. LOW-LEVEL HARDWARE DRIVERS (RGB & On-Board LED)
// ======================================================================================
void setRgbColor(bool r, bool g, bool b) {
  digitalWrite(PIN_LED_RED,   r ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, g ? HIGH : LOW);
  digitalWrite(PIN_LED_BLUE,  b ? HIGH : LOW);
}

void setOnboardLed(bool state) {
  digitalWrite(PIN_ONBOARD_LED, state ? HIGH : LOW);
}

// ======================================================================================
// 6. SWARM TOPOLOGY & PROXIMITY EVALUATION
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
// 7. LED ANIMATION STATE MACHINE
// ======================================================================================
/*
 * Rules from requirements:
 * 1. If close to all neighbours -> Light On-Board LED.
 * 2. When RFID of a neighbour read -> Light Green LED of the RGB LED.
 * 3. Else -> go Blue then Red in order of neighbour count:
 *     - If count == 0: Solid Red.
 *     - If count > 0: Sequentially pulses Blue (N times = count), followed by Red.
 *     - If close to all neighbours: Solid Blue on RGB LED while On-Board LED is illuminated.
 */
void updateLedStates() {
  uint32_t now = millis();
  bool closeToAll = isCloseToAllNeighbors();
  int neighborCount = getActiveNeighborCount();

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
  // Total cycle duration calculation:
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
// 8. RFID SCANNING & IDENTIFICATION
// ======================================================================================
void checkRfidReader() {
  // Check for new card presence
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }
  // Read card serial
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
  Serial.println("========================================");

  // Activate Green LED state
  rfidActive = true;
  rfidDetectedTime = millis();

  // Stop encryption & halt card
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ======================================================================================
// 9. ESP-NOW WIRELESS COMMUNICATION
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
  bool closeToAll = isCloseToAllNeighbors();

  Serial.println("\n-------------------------------------------------------------");
  Serial.printf("BRICK #%d | Neighbors: %d | Close To All: %s | On-Board LED: %s\n",
                BRICK_ID,
                count,
                closeToAll ? "YES" : "NO",
                closeToAll ? "ON" : "OFF");

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
// 10. ARDUINO SETUP & MAIN LOOP
// ======================================================================================
void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("=============================================================");
  Serial.println("     MULTI-AGENT SMART BRICKS (MICROBOTS) FIRMWARE           ");
  Serial.printf ("     Node Identity: BRICK #%d of %d                          \n", BRICK_ID, TOTAL_SWARM_BRICKS);
  Serial.println("=============================================================");

  // Initialize LED Pins
  pinMode(PIN_ONBOARD_LED, OUTPUT);
  pinMode(PIN_LED_RED,     OUTPUT);
  pinMode(PIN_LED_GREEN,   OUTPUT);
  pinMode(PIN_LED_BLUE,    OUTPUT);

  setOnboardLed(false);
  setRgbColor(true, false, false); // Start Red (Standalone indicator)

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

  // 1. Check RFID Reader for local neighbor tag
  checkRfidReader();

  // 2. Broadcast local state packet periodically
  if (now - lastBroadcastTime > BROADCAST_INTERVAL_MS) {
    lastBroadcastTime = now;
    broadcastPacket();
  }

  // 3. Prune stale peers that have timed out
  pruneStalePeers();

  // 4. Update On-Board and RGB LED animations
  updateLedStates();

  // 5. Diagnostics serial logger
  if (now - lastDebugPrintTime > 2000) {
    lastDebugPrintTime = now;
    printDiagnosticStatus();
  }

  // Small non-blocking yield for FreeRTOS scheduler
  delay(10);
}
