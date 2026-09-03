# Multi-Agent Smart Bricks (Microbots) — Setup & Quickstart Guide

This guide describes how to configure, upload, and verify the unified **Microbot Swarm (Smart Bricks)** firmware on your 4 ESP32 modules.

> [!NOTE]
> **30-Pin Board Ready**: All GPIO pins used are strictly **$\le 35$** (compatible with 30-pin and 38-pin ESP32 boards).

For complete schematics, circuit diagrams, and future expansion plans, refer to [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD).

---

## 1. Hardware Connections (Per Brick)

| Component | Pin Function | ESP32 Pin | Label on Board | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **MFRC522 RFID** | SCK | `GPIO 18` | `D18` | Standard VSPI SCK |
| | MISO | `GPIO 19` | `D19` | Standard VSPI MISO |
| | MOSI | `GPIO 23` | `D23` | Standard VSPI MOSI |
| | SDA / SS | `GPIO 5` | `D5` | Chip Select |
| | RST | `GPIO 4` | `D4` | Reset Pin |
| | 3.3V & GND | `3V3` & `GND` | `3V3` & `GND` | Power (Do NOT use 5V for RFID) |
| **RGB LED** | RED | `GPIO 16` | `D16` / `RX2` | Via 220Ω series resistor |
| | GREEN | `GPIO 17` | `D17` / `TX2` | Via 220Ω series resistor |
| | BLUE | `GPIO 25` | `D25` | Via 220Ω series resistor |
| | Common | `GND` | `GND` | Common Cathode |
| **On-Board LED** | Built-in LED | `GPIO 2` | `D2` | Proximity indicator (Close to all neighbors) |
| **4x IR Sensors** | North OUT | `GPIO 34` | **`D34`** | Cardinal top face docking sensor |
| | East OUT | `GPIO 35` | **`D35`** | Cardinal right face docking sensor |
| | South OUT | `GPIO 32` | **`D32`** | Cardinal bottom face docking sensor |
| | West OUT | `GPIO 33` | **`D33`** | Cardinal left face docking sensor |
| | VCC & GND | `3V3` & `GND` | `3V3` & `GND` | Shared 3.3V power bus |
| **Audio Buzzer** | Positive / SIG | `GPIO 27` | **`D27`** | Audible feedback (Chirp / Chimes) |
| | Negative / GND | `GND` | `GND` | Common ground |

---

## 2. Firmware Flashing Instructions

1. Open [`Code/brick.ino`](brick.ino) (or [`Code/brick/brick.ino`](brick/brick.ino)) in the **Arduino IDE**.
2. Install the **MFRC522** library via Arduino Library Manager (by *GithubCommunity* / *miguelbalboa*).
3. Select your ESP32 board: **Tools > Board > ESP32 Dev Module**.
4. Set the `BRICK_ID` macro at line 35 of the sketch before uploading to each board:
   - First Brick: `#define BRICK_ID 1`
   - Second Brick: `#define BRICK_ID 2`
   - Third Brick: `#define BRICK_ID 3`
   - Fourth Brick: `#define BRICK_ID 4`
5. Upload to each respective ESP32 brick.
6. Open the Serial Monitor at **115200 baud** to view real-time neighbor detection and IR face docking packets.

---

## 3. Visual Status & Sensory Feedback Legend

| Event / Condition | On-Board LED (`GPIO 2`) | RGB LED (`GPIO 16, 17, 25`) | Buzzer (`GPIO 27`) |
| :--- | :---: | :--- | :--- |
| **Boot Complete** | Pulse | Flash Red | 🎵 Startup melodic chime |
| **RFID Tag Read** | Normal | 🟢 **Solid GREEN** (2.0s hold) | 🔔 Cheerful double-chirp |
| **IR Face Docks** | Normal | Normal | 🔔 Ascending double-beep |
| **IR Face Undocks** | Normal | Normal | ⚠️ Single alert tone |
| **Close to All Neighbors** | 💡 **Solid ON** | 🔵 **Solid BLUE** | 🔔 Consensus chime on entry |
| **0 Neighbors (Standalone)** | ⚫ OFF | 🔴 **Solid RED** | Silent |
| **1 Neighbor (Not Close)** | ⚫ OFF | 🔵 **1 Blue Pulse** ➔ 🔴 **1 Red Pulse** | Silent |
| **2 Neighbors (Not Close)** | ⚫ OFF | 🔵 **2 Blue Pulses** ➔ 🔴 **1 Red Pulse** | Silent |
| **3 Neighbors (Not Close)** | ⚫ OFF | 🔵 **3 Blue Pulses** ➔ 🔴 **1 Red Pulse** | Silent |

---

## 4. Repository Layout

- [`brick.ino`](brick.ino) & [`brick/brick.ino`](brick/brick.ino): Unified production firmware.
- [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD): Detailed system architecture, schematics, and future hardware roadmap.
- [`Archive/`](Archive/): Earlier prototype sketches and experimental implementations.
- [`tests/`](tests/): Multi-agent simulation and automated test scripts (`test_brick_logic.py`).
