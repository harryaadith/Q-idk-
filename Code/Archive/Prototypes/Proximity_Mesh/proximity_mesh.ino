/*
  proximity_mesh.ino
  ---------------------------------------------------------
  RSSI-based proximity mesh over ESP-NOW for 2+ ESP32 boards.

  WHAT IT DOES
  1. Each board broadcasts small PING packets containing its BRICK_NO.
  2. Every board that hears a PING records the RSSI of that packet
     (smoothed with an EMA) as its direct-link estimate to that peer.
  3. Each board periodically broadcasts a REPORT packet: its own
     BRICK_NO's table of {peer, rssi}. Every board that hears these
     reports assembles them into a single global adjacency matrix
     (proximity_matrix[BRICK_NO][BRICK_NO]) — this is the shared
     "who is close to whom" data structure, replicated on every node.
  4. Each board watches the matrix for pairs whose RSSI is STABLE
     (low variance) and uses the weakest such stable pair as the
     boundary between "close" and "far" (adaptive threshold, with
     hysteresis so it doesn't flap).
  5. Each board blinks its LED BLUE if it currently has a direct
     neighbour at or above that threshold, otherwise solid RED.

  REQUIREMENTS
  - Arduino-ESP32 core >= 2.0.0 (needed for esp_now_recv_info_t with
    rx_ctrl->rssi in the receive callback). If this doesn't compile,
    update the ESP32 boards package in Arduino IDE / PlatformIO.
  - Two LEDs (or an RGB LED) wired to PIN_LED_BLUE / PIN_LED_RED,
    each through a ~220-330 ohm resistor to GND.

  BEFORE FLASHING EACH BOARD
  - Change BRICK_NO below to a unique small integer per board
    (0 .. MAX_NODES-1). Every board must share the same WIFI_CHANNEL.
  ---------------------------------------------------------
*/

#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

// ============================================================
// PER-BOARD CONFIG - EDIT BEFORE FLASHING
// ============================================================
#define BRICK_NO         1        // <-- unique per board, 0..MAX_NODES-1
#define WIFI_CHANNEL     1        // must match on every board

// ============================================================
// GENERAL CONFIG
// ============================================================
#define MAX_NODES              16     // max simultaneous bricks supported
#define PING_INTERVAL_MS        150   // how often we announce ourselves
#define REPORT_INTERVAL_MS      800   // how often we gossip our RSSI table
#define PRUNE_INTERVAL_MS       500   // how often we check for stale data
#define THRESHOLD_INTERVAL_MS   1000  // how often we recompute the threshold
#define DEBUG_PRINT_INTERVAL_MS 2000

#define PEER_TIMEOUT_MS          3000 // no ping -> peer considered gone
#define MATRIX_CELL_TIMEOUT_MS   3000 // no report -> matrix cell invalidated

#define RSSI_EMA_ALPHA    0.30f       // smoothing for direct RSSI readings
#define VAR_EMA_ALPHA     0.20f       // smoothing for per-link variance
#define STABILITY_VAR_MAX 25.0f       // dB^2 below which a link is "stable"

#define THRESHOLD_MIN     -95         // clamp: never below this (dBm)
#define THRESHOLD_MAX      -40        // clamp: never above this (dBm)
#define THRESHOLD_DEFAULT  -70        // used until we have stable data
#define THRESHOLD_HYSTERESIS 4        // dB margin to stop LED flicker

#define BLINK_PERIOD_MS 300

// ---- LED output mode: pick ONE ----
//   LED_MODE_EXTERNAL     -> two discrete LEDs, see README wiring (default)
//   LED_MODE_ONBOARD_MONO -> single onboard LED (e.g. GPIO2 on classic ESP32
//                            DevKitC / NodeMCU-32S). Can't show two colors,
//                            so: blinks when close, OFF when far.
//   LED_MODE_ONBOARD_RGB  -> onboard addressable RGB LED (WS2812-style),
//                            e.g. GPIO48 on many S3 DevKitC boards, GPIO8 on
//                            many C3 DevKitM boards. Check YOUR board's
//                            pinout diagram, this varies a lot between boards.
//                            Needs the "Adafruit NeoPixel" library
//                            (Library Manager -> search "Adafruit NeoPixel").
#define LED_MODE_EXTERNAL     0
#define LED_MODE_ONBOARD_MONO 1
#define LED_MODE_ONBOARD_RGB  2
#define LED_MODE LED_MODE_EXTERNAL

#if LED_MODE == LED_MODE_EXTERNAL
  #define PIN_LED_BLUE  25
  #define PIN_LED_RED   26
#elif LED_MODE == LED_MODE_ONBOARD_MONO
  #define PIN_ONBOARD_MONO 2
#elif LED_MODE == LED_MODE_ONBOARD_RGB
  #include <Adafruit_NeoPixel.h>
  #define PIN_ONBOARD_RGB 48   // <-- verify against your board's schematic
  Adafruit_NeoPixel onboardPixel(1, PIN_ONBOARD_RGB, NEO_GRB + NEO_KHZ800);
#endif

// ============================================================
// PACKET DEFINITIONS
// ============================================================
enum PacketType : uint8_t { PKT_PING = 1, PKT_REPORT = 2 };

typedef struct __attribute__((packed)) {
  uint8_t  type;       // PKT_PING
  uint8_t  brick_no;
  uint32_t seq;
} PingPacket;

typedef struct __attribute__((packed)) {
  uint8_t brick_no;
  int8_t  rssi;
} ReportEntry;

typedef struct __attribute__((packed)) {
  uint8_t      type;    // PKT_REPORT
  uint8_t      brick_no;
  uint8_t      count;
  ReportEntry  entries[MAX_NODES];
} ReportPacket;

// ============================================================
// STATE
// ============================================================
struct PeerInfo {
  bool     active   = false;
  uint8_t  mac[6]    = {0};
  float    rssi_ema  = -100.0f;   // direct RSSI EMA to this peer
  uint32_t last_seen = 0;
};
PeerInfo peers[MAX_NODES];        // direct neighbours of THIS board

struct LinkStat {
  bool     valid       = false;
  float    mean         = 0;
  float    var          = 0;
  uint32_t last_update  = 0;
};
LinkStat proximity_matrix[MAX_NODES][MAX_NODES]; // global shared datastructure

float proximity_threshold = THRESHOLD_DEFAULT;
bool  isClose = false;            // current LED state (with hysteresis)

uint32_t seqCounter = 0;
uint8_t  broadcastAddress[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

uint32_t tPing = 0, tReport = 0, tPrune = 0, tThreshold = 0, tDebug = 0, tBlink = 0;
bool blinkState = false;

// ============================================================
// HELPERS
// ============================================================
void updateLinkStat(LinkStat &s, int8_t newRssi) {
  if (!s.valid) {
    s.mean  = newRssi;
    s.var   = 0;
    s.valid = true;
  } else {
    float diff = newRssi - s.mean;
    s.mean += RSSI_EMA_ALPHA * diff;
    s.var   = (1 - VAR_EMA_ALPHA) * s.var + VAR_EMA_ALPHA * diff * diff;
  }
  s.last_update = millis();
}

void updatePeerRssi(uint8_t brick, const uint8_t *mac, int8_t rssi) {
  if (brick >= MAX_NODES) return;
  PeerInfo &p = peers[brick];
  if (!p.active) {
    p.active   = true;
    p.rssi_ema = rssi;
    memcpy(p.mac, mac, 6);
  } else {
    p.rssi_ema += RSSI_EMA_ALPHA * (rssi - p.rssi_ema);
  }
  p.last_seen = millis();
}

// ============================================================
// ESP-NOW CALLBACK
// ============================================================
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == PKT_PING && len == sizeof(PingPacket)) {
    PingPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.brick_no == BRICK_NO) return;   // ignore our own echoes
    int8_t rssi = info->rx_ctrl->rssi;
    updatePeerRssi(pkt.brick_no, info->src_addr, rssi);

  } else if (type == PKT_REPORT && len >= (int)(sizeof(ReportPacket) - sizeof(ReportEntry) * MAX_NODES)) {
    ReportPacket pkt;
    memcpy(&pkt, data, min((size_t)len, sizeof(pkt)));
    if (pkt.brick_no >= MAX_NODES) return;
    for (int i = 0; i < pkt.count && i < MAX_NODES; i++) {
      uint8_t j = pkt.entries[i].brick_no;
      if (j >= MAX_NODES) continue;
      updateLinkStat(proximity_matrix[pkt.brick_no][j], pkt.entries[i].rssi);
    }
  }
}

// ============================================================
// PERIODIC TASKS
// ============================================================
void sendPing() {
  PingPacket pkt = { PKT_PING, BRICK_NO, seqCounter++ };
  esp_now_send(broadcastAddress, (uint8_t*)&pkt, sizeof(pkt));
}

void sendReport() {
  ReportPacket pkt;
  pkt.type     = PKT_REPORT;
  pkt.brick_no = BRICK_NO;
  pkt.count    = 0;
  for (int i = 0; i < MAX_NODES && pkt.count < MAX_NODES; i++) {
    if (peers[i].active) {
      pkt.entries[pkt.count].brick_no = i;
      pkt.entries[pkt.count].rssi     = (int8_t)round(peers[i].rssi_ema);
      pkt.count++;
    }
  }
  size_t sz = sizeof(pkt) - sizeof(pkt.entries) + pkt.count * sizeof(ReportEntry);
  esp_now_send(broadcastAddress, (uint8_t*)&pkt, sz);

  // also fold our own direct readings into our own view of the matrix
  for (int i = 0; i < pkt.count; i++) {
    updateLinkStat(proximity_matrix[BRICK_NO][pkt.entries[i].brick_no], pkt.entries[i].rssi);
  }
}

void pruneStale() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_NODES; i++) {
    if (peers[i].active && now - peers[i].last_seen > PEER_TIMEOUT_MS) {
      peers[i].active = false;
    }
    for (int j = 0; j < MAX_NODES; j++) {
      LinkStat &s = proximity_matrix[i][j];
      if (s.valid && now - s.last_update > MATRIX_CELL_TIMEOUT_MS) {
        s.valid = false;
      }
    }
  }
}

// Recompute the self-deciding proximity threshold:
// among pairs (i,j) that are STABLE in both reported directions,
// pick the weakest (most negative) mean RSSI as the boundary of "close".
void updateThreshold() {
  float weakestStable = 1000; // sentinel
  bool found = false;

  for (int i = 0; i < MAX_NODES; i++) {
    for (int j = i + 1; j < MAX_NODES; j++) {
      LinkStat &a = proximity_matrix[i][j];
      LinkStat &b = proximity_matrix[j][i];
      if (!a.valid || !b.valid) continue;
      if (a.var > STABILITY_VAR_MAX || b.var > STABILITY_VAR_MAX) continue; // not stable enough

      float pairMean = (a.mean + b.mean) / 2.0f;
      if (pairMean < weakestStable) {
        weakestStable = pairMean;
        found = true;
      }
    }
  }

  if (found) {
    float clamped = constrain(weakestStable, THRESHOLD_MIN, THRESHOLD_MAX);
    // smooth the threshold itself so it doesn't jump around
    proximity_threshold += 0.3f * (clamped - proximity_threshold);
  }
  // if nothing stable found yet, keep the previous / default threshold
}

void updateLed() {
  // find our strongest current direct neighbour
  float bestRssi = -1000;
  bool haveNeighbour = false;
  for (int i = 0; i < MAX_NODES; i++) {
    if (peers[i].active && i != BRICK_NO) {
      if (peers[i].rssi_ema > bestRssi) {
        bestRssi = peers[i].rssi_ema;
        haveNeighbour = true;
      }
    }
  }

  // hysteresis: only flip state when clearly past the threshold +/- margin
  if (haveNeighbour) {
    if (!isClose && bestRssi > proximity_threshold + THRESHOLD_HYSTERESIS) {
      isClose = true;
    } else if (isClose && bestRssi < proximity_threshold - THRESHOLD_HYSTERESIS) {
      isClose = false;
    }
  } else {
    isClose = false;
  }

  uint32_t now = millis();
  if (now - tBlink > BLINK_PERIOD_MS) {
    tBlink = now;
    blinkState = !blinkState;
  }

#if LED_MODE == LED_MODE_EXTERNAL
  if (isClose) {
    digitalWrite(PIN_LED_BLUE, blinkState ? HIGH : LOW);
    digitalWrite(PIN_LED_RED, LOW);
  } else {
    digitalWrite(PIN_LED_BLUE, LOW);
    digitalWrite(PIN_LED_RED, HIGH); // solid red always, per spec
  }
#elif LED_MODE == LED_MODE_ONBOARD_MONO
  // single color only: blink = close, off = far
  digitalWrite(PIN_ONBOARD_MONO, (isClose && blinkState) ? HIGH : LOW);
#elif LED_MODE == LED_MODE_ONBOARD_RGB
  if (isClose) {
    onboardPixel.setPixelColor(0, blinkState ? onboardPixel.Color(0, 0, 40) : 0);
  } else {
    onboardPixel.setPixelColor(0, onboardPixel.Color(40, 0, 0)); // solid red
  }
  onboardPixel.show();
#endif
}

void debugPrint() {
  Serial.printf("\n--- BRICK %d | threshold=%.1f dBm | close=%s ---\n",
                BRICK_NO, proximity_threshold, isClose ? "YES" : "no");
  Serial.print("direct neighbours: ");
  for (int i = 0; i < MAX_NODES; i++) {
    if (peers[i].active) Serial.printf("[%d: %.1fdBm] ", i, peers[i].rssi_ema);
  }
  Serial.println();
  Serial.println("global matrix (row -> col, mean dBm / var):");
  for (int i = 0; i < MAX_NODES; i++) {
    for (int j = 0; j < MAX_NODES; j++) {
      if (proximity_matrix[i][j].valid) {
        Serial.printf("  %d->%d: %.1f (var %.1f)\n", i, j,
                      proximity_matrix[i][j].mean, proximity_matrix[i][j].var);
      }
    }
  }
}

// ============================================================
// SETUP / LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);

#if LED_MODE == LED_MODE_EXTERNAL
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  digitalWrite(PIN_LED_BLUE, LOW);
  digitalWrite(PIN_LED_RED, HIGH); // default: far, until we learn otherwise
#elif LED_MODE == LED_MODE_ONBOARD_MONO
  pinMode(PIN_ONBOARD_MONO, OUTPUT);
  digitalWrite(PIN_ONBOARD_MONO, LOW);
#elif LED_MODE == LED_MODE_ONBOARD_RGB
  onboardPixel.begin();
  onboardPixel.setPixelColor(0, onboardPixel.Color(40, 0, 0)); // default: far
  onboardPixel.show();
#endif

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed, halting.");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx   = WIFI_IF_STA;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer.");
  }

  Serial.printf("BRICK_NO=%d ready on channel %d\n", BRICK_NO, WIFI_CHANNEL);
}

void loop() {
  uint32_t now = millis();

  if (now - tPing > PING_INTERVAL_MS) { tPing = now; sendPing(); }
  if (now - tReport > REPORT_INTERVAL_MS) { tReport = now; sendReport(); }
  if (now - tPrune > PRUNE_INTERVAL_MS) { tPrune = now; pruneStale(); }
  if (now - tThreshold > THRESHOLD_INTERVAL_MS) { tThreshold = now; updateThreshold(); }
  if (now - tDebug > DEBUG_PRINT_INTERVAL_MS) { tDebug = now; debugPrint(); }

  updateLed();
}
