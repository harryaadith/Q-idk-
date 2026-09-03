/*
 * Multi-Agent Smart Bricks - Topology & Neighbor Discovery
 * Team 22 - Electronic System Workshop (ESW)
 *
 * Description:
 * Autonomous ESP32 brick firmware. Each brick tracks:
 * 1. Local RFID instrument presence
 * 2. Neighboring bricks on its 4 physical faces (NORTH, EAST, SOUTH, WEST)
 * 3. Dynamic RGB LED state based on neighbor count
 * 4. ESP-NOW peer-to-peer broadcast of local topology
 */

#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <MFRC522.h>

// ==========================================
// 1. CONFIGURATION (Change BRICK_ID per node)
// ==========================================
#define BRICK_ID 1       // Set to 1, 2, 3, or 4 for each physical brick
#define WIFI_CHANNEL 6   // Must be identical across all bricks

// Directional faces
enum Face {
  FACE_NORTH = 0, // Top
  FACE_EAST  = 1, // Right
  FACE_SOUTH = 2, // Bottom
  FACE_WEST  = 3  // Left
};

const char* FACE_NAMES[] = {"NORTH", "EAST", "SOUTH", "WEST"};

// ==========================================
// 2. PIN DEFINITIONS
// ==========================================
// RGB LED Pins
#define PIN_LED_R 16
#define PIN_LED_G 17
#define PIN_LED_B 25

// RFID RC522 Pins (VSPI)
#define PIN_SS   5
#define PIN_RST  4

// Directional IR Receiver Pins (Digital active-low/high)
#define PIN_IR_NORTH 34
#define PIN_IR_EAST  35
#define PIN_IR_SOUTH 36
#define PIN_IR_WEST  39

MFRC522 rfid(PIN_SS, PIN_RST);

// ==========================================
// 3. DATA STRUCTURES & TIMERS
// ==========================================
// Packet broadcasted between all bricks over ESP-NOW
struct BrickPacket {
  uint8_t sender_id;          // ID of this brick
  bool    has_tool;           // True if RFID tool is detected
  uint8_t neighbors[4];       // Neighbor ID on [NORTH, EAST, SOUTH, WEST] (0 = none)
};

// Local tracking of neighbors on each face
struct FaceNeighbor {
  uint8_t brick_id;
  unsigned long last_seen_ms;
};

FaceNeighbor face_table[4];   // State for NORTH, EAST, SOUTH, WEST
bool local_tool_present = false;
unsigned long last_broadcast_time = 0;
const unsigned long BROADCAST_INTERVAL_MS = 400;
const unsigned long NEIGHBOR_TIMEOUT_MS   = 1500;

// Broadcast MAC address
uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ==========================================
// 4. HELPER FUNCTIONS
// ==========================================

// Set RGB LED color directly
void setLedColor(bool red, bool green, bool blue) {
  digitalWrite(PIN_LED_R, red   ? HIGH : LOW);
  digitalWrite(PIN_LED_G, green ? HIGH : LOW);
  digitalWrite(PIN_LED_B, blue  ? HIGH : LOW);
}

// Return total active neighbors connected to this brick
int getActiveNeighborCount() {
  int count = 0;
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (face_table[i].brick_id != 0) {
      if (now - face_table[i].last_seen_ms > NEIGHBOR_TIMEOUT_MS) {
        // Neighbor timed out / disconnected
        Serial.printf("[TOPO] Neighbor %d disconnected from %s face\n", 
                      face_table[i].brick_id, FACE_NAMES[i]);
        face_table[i].brick_id = 0;
      } else {
        count++;
      }
    }
  }
  return count;
}

// Update RGB LED color according to neighbor count
void updateLedStatus() {
  int count = getActiveNeighborCount();

  switch (count) {
    case 0: // 0 Neighbors: RED (Standalone)
      setLedColor(true, false, false);
      break;
    case 1: // 1 Neighbor: YELLOW
      setLedColor(true, true, false);
      break;
    case 2: // 2 Neighbors: BLUE
      setLedColor(false, false, true);
      break;
    case 3: // 3 Neighbors: CYAN
      setLedColor(false, true, true);
      break;
    case 4: // 4 Neighbors: GREEN (Fully docked)
      setLedColor(false, true, false);
      break;
    default:
      setLedColor(true, false, true); // Magenta
      break;
  }
}

// ==========================================
// 5. ESP-NOW RECEIVE CALLBACK
// ==========================================
void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(BrickPacket)) return;

  BrickPacket incoming;
  memcpy(&incoming, data, sizeof(BrickPacket));

  // Ignore our own packet
  if (incoming.sender_id == BRICK_ID) return;

  // If incoming packet mentions us on their face, record them on our matching opposite face
  // NORTH (0) mates with SOUTH (2), EAST (1) mates with WEST (3)
  for (int remote_face = 0; remote_face < 4; remote_face++) {
    if (incoming.neighbors[remote_face] == BRICK_ID) {
      int my_face = (remote_face + 2) % 4; // Opposite face
      if (face_table[my_face].brick_id != incoming.sender_id) {
        Serial.printf("[TOPO] Connected to Brick %d on %s face\n", 
                      incoming.sender_id, FACE_NAMES[my_face]);
      }
      face_table[my_face].brick_id = incoming.sender_id;
      face_table[my_face].last_seen_ms = millis();
    }
  }
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n========================================");
  Serial.printf("  Smart Brick ID: %d | Topology Node\n", BRICK_ID);
  Serial.println("========================================");

  // Initialize LED Pins
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setLedColor(true, false, false); // Start RED

  // Initialize RFID
  SPI.begin();
  rfid.PCD_Init();

  // Initialize Face Table
  for (int i = 0; i < 4; i++) {
    face_table[i].brick_id = 0;
    face_table[i].last_seen_ms = 0;
  }

  // Initialize Wi-Fi in Station Mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW initialization failed!");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);

  // Register broadcast peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcast_mac, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("[INFO] ESP-NOW Ready. Listening for neighbors...");
}

void loop() {
  // 1. Check RFID tool presence
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    local_tool_present = true;
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // 2. Broadcast local state every interval
  if (millis() - last_broadcast_time > BROADCAST_INTERVAL_MS) {
    last_broadcast_time = millis();

    BrickPacket packet;
    packet.sender_id = BRICK_ID;
    packet.has_tool  = local_tool_present;
    for (int i = 0; i < 4; i++) {
      packet.neighbors[i] = face_table[i].brick_id;
    }

    esp_now_send(broadcast_mac, (uint8_t*)&packet, sizeof(packet));

    // Print summary to Serial Monitor
    int active_neighbors = getActiveNeighborCount();
    Serial.printf("[STATUS] Brick %d | Tool: %s | Neighbors: %d [N:%d E:%d S:%d W:%d]\n",
                  BRICK_ID,
                  local_tool_present ? "PRESENT" : "EMPTY",
                  active_neighbors,
                  face_table[0].brick_id,
                  face_table[1].brick_id,
                  face_table[2].brick_id,
                  face_table[3].brick_id);
  }

  // 3. Update status LEDs
  updateLedStatus();
  delay(50);
}
