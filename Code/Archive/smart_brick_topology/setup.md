# Multi-Agent Smart Bricks (Microbots) — Setup & Quickstart Guide

This guide describes how to configure, upload, and verify the unified **Microbot Swarm (Smart Bricks)** firmware on your 4 ESP32 modules.

For complete schematics, circuit diagrams, and future expansion plans, refer to [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD).

---

## 1. Hardware Connections (Per Brick)

| Component | Pin Function | ESP32 GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **MFRC522 RFID** | SCK | `GPIO 18` | Standard VSPI SCK |
| | MISO | `GPIO 19` | Standard VSPI MISO |
| | MOSI | `GPIO 23` | Standard VSPI MOSI |
| | SDA / SS | `GPIO 5` | Chip Select |
| | RST | `GPIO 4` | Reset Pin |
| | 3.3V & GND | `3V3` & `GND` | Power (Do NOT use 5V for RFID) |
| **RGB LED** | RED | `GPIO 16` | Via 220Ω series resistor |
| | GREEN | `GPIO 17` | Via 220Ω series resistor |
| | BLUE | `GPIO 25` | Via 220Ω series resistor |
| | Common | `GND` | Common Cathode |
| **On-Board LED** | Built-in LED | `GPIO 2` | Proximity indicator (Close to all neighbors) |

---

## 2. Firmware Flashing Instructions

1. Open [`Code/brick.ino`](brick.ino) (or [`Code/brick/brick.ino`](brick/brick.ino)) in the **Arduino IDE**.
2. Install the **MFRC522** library via Arduino Library Manager (by *GithubCommunity* / *miguelbalboa*).
3. Select your ESP32 board: **Tools > Board > ESP32 Dev Module**.
4. Set the `BRICK_ID` macro at line 32 of the sketch before uploading to each board:
   - First Brick: `#define BRICK_ID 1`
   - Second Brick: `#define BRICK_ID 2`
   - Third Brick: `#define BRICK_ID 3`
   - Fourth Brick: `#define BRICK_ID 4`
5. Upload to each respective ESP32 brick.
6. Open the Serial Monitor at **115200 baud** to view real-time neighbor detection packets.

---

## 3. Visual Status & LED Indicator Legend

| Indicator | State / Condition | Behavior |
| :--- | :--- | :--- |
| **On-Board LED (GPIO 2)** | Close to all active neighbors | 💡 **Solid ON** |
| **On-Board LED (GPIO 2)** | Standalone OR any active neighbor is far | ⚫ **OFF** |
| **RGB LED (External)** | RFID Tag of a neighbor read | 🟢 **Solid GREEN** (for 2.0 seconds) |
| **RGB LED (External)** | 0 neighbors detected | 🔴 **Solid RED** (Standalone) |
| **RGB LED (External)** | 1 neighbor detected (not close) | 🔵 **1 Blue Pulse** ➔ 🔴 **1 Red Pulse** |
| **RGB LED (External)** | 2 neighbors detected (not close) | 🔵 **2 Blue Pulses** ➔ 🔴 **1 Red Pulse** |
| **RGB LED (External)** | 3 neighbors detected (not close) | 🔵 **3 Blue Pulses** ➔ 🔴 **1 Red Pulse** |
| **RGB LED (External)** | Close to all active neighbors | 🔵 **Solid BLUE** |

---

## 4. Repository Layout

- [`brick.ino`](brick.ino) & [`brick/brick.ino`](brick/brick.ino): Unified production firmware.
- [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD): Detailed system architecture, schematics, and future hardware roadmap.
- [`Archive/`](Archive/): Earlier prototype sketches and experimental implementations.
- [`tests/`](tests/): Multi-agent simulation and automated test scripts.
