#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <esp_now.h>

#define SS_PIN   5
#define RST_PIN  4
#define LED_R    16
#define LED_G    17
#define LED_B    25

const uint8_t BRICK_ID = 1;   //Brick number = 1,2,3...N for N number of bricks

MFRC522 rfid(SS_PIN, RST_PIN);

typedef struct {
  uint8_t id;
  bool tagPresent;
} Message;

Message outgoing;
bool flashBlue = false;
unsigned long flashUntil = 0;
bool tagPresent = false;

void setColor(bool r, bool g, bool b) {
  digitalWrite(LED_R, r);
  digitalWrite(LED_G, g);
  digitalWrite(LED_B, b);
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Message msg;
  memcpy(&msg, data, sizeof(msg));
  Serial.print("Received from Brick ");
  Serial.print(msg.id);
  Serial.print(" | tag present: ");
  Serial.println(msg.tagPresent ? "YES" : "NO");
  flashBlue = true;
  flashUntil = millis() + 150;
}

void setup() {
  Serial.begin(115200);
  SPI.begin();          // uses default VSPI pins: 18, 19, 23
  rfid.PCD_Init();

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setColor(true, false, false); // start red

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6); // broadcast address
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.print("Brick ");
  Serial.print(BRICK_ID);
  Serial.println(" ready.");
}

unsigned long lastBroadcast = 0;

void loop() {
  // --- RFID read ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    tagPresent = true;
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) uid += String(rfid.uid.uidByte[i], HEX);
    Serial.print("Local tag UID: ");
    Serial.println(uid);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // --- LED state ---
  if (flashBlue && millis() < flashUntil) {
    setColor(false, false, true);
  } else {
    flashBlue = false;
    setColor(!tagPresent, tagPresent, false); // red if empty, green if present
  }

  // --- Broadcast every 500ms ---
  uint8_t broadcastAddr[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  if (millis() - lastBroadcast > 500) {
    outgoing.id = BRICK_ID;
    outgoing.tagPresent = tagPresent;
    esp_now_send(broadcastAddr, (uint8_t*)&outgoing, sizeof(outgoing));
    lastBroadcast = millis();
  }
}