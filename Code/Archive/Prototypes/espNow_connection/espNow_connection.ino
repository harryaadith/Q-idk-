#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BRICK_ID 1          // Change to 2 on the second ESP
#define WIFI_CHANNEL 6

// Broadcast MAC address
uint8_t broadcastAddress[] = {
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Packet sent between bricks
struct BrickMessage {
  uint8_t id;
  uint8_t type;
  uint8_t action;
  uint32_t counter;
};

uint32_t counter = 0;

// Called whenever a packet is received
void onReceive(
  const esp_now_recv_info_t *info,
  const uint8_t *data,
  int len
) {
  if (len != sizeof(BrickMessage)) {
    Serial.println("Received packet with wrong size");
    return;
  }

  BrickMessage received;
  memcpy(&received, data, sizeof(received));

  // Ignore our own packets
  if (received.id == BRICK_ID) {
    return;
  }

  Serial.println("----- RECEIVED -----");

  Serial.printf("Brick ID: %d\n", received.id);
  Serial.printf("Type: %d\n", received.type);
  Serial.printf("Action: %d\n", received.action);
  Serial.printf(
    "Counter: %lu\n",
    (unsigned long)received.counter
  );

  Serial.println("--------------------");
}
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Put ESP in Wi-Fi station mode
  WiFi.mode(WIFI_STA);

  // Set Wi-Fi channel using the API available in ESP32 core 2.0.18
  esp_wifi_set_channel(
    WIFI_CHANNEL,
    WIFI_SECOND_CHAN_NONE
  );

  Serial.println();
  Serial.println("================================");
  Serial.printf("Brick ID: %d\n", BRICK_ID);

  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization FAILED");
    while (true) {
      delay(1000);
    }
  }

  // Register receive callback
  esp_now_register_recv_cb(onReceive);

  // Add broadcast address as a peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(
    peerInfo.peer_addr,
    broadcastAddress,
    6
  );

  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("ESP-NOW READY");
  Serial.println("================================");
}

void loop() {

  BrickMessage message;

  message.id = BRICK_ID;
  message.type = 1;
  message.action = 0;
  message.counter = counter++;

  Serial.printf(
    "Sending packet: ID=%d, Counter=%lu\n",
    message.id,
    (unsigned long)message.counter
  );

  esp_err_t result = esp_now_send(
    broadcastAddress,
    (uint8_t *)&message,
    sizeof(message)
  );

  if (result != ESP_OK) {
    Serial.printf(
      "Send failed. Error code: %d\n",
      result
    );
  }

  delay(1000);
}