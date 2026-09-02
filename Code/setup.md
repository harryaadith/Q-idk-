# Multi-Agent Smart Bricks — Setup & Topology Guide

This guide describes how to configure, upload, and verify the **Topology Discovery & Neighbor Management** firmware on your ESP32 smart bricks.

---

## 1. Hardware Connections (Per Brick)

| Component | Pin Function | ESP32 GPIO | Notes |
| :--- | :--- | :--- | :--- |
| **MFRC522 RFID** | SCK | `GPIO 18` | Standard VSPI SCK |
| | MISO | `GPIO 19` | Standard VSPI MISO |
| | MOSI | `GPIO 23` | Standard VSPI MOSI |
| | SDA / SS | `GPIO 5` | Chip Select |
| | RST | `GPIO 4` | Reset Pin |
| | 3.3V & GND | `3V3` & `GND` | Power |
| **RGB LED** | RED | `GPIO 16` | Via 220Ω resistor |
| | GREEN | `GPIO 17` | Via 220Ω resistor |
| | BLUE | `GPIO 25` | Via 220Ω resistor |
| | Common | `GND` | Common Cathode |

---

## 2. Firmware Flashing Instructions

1. Open [`Code/smart_brick_topology/smart_brick_topology.ino`](file:///home/harry/programming/ESW/esw-m26-22_q-idk-if-i-m-getting-an-internship/Code/smart_brick_topology/smart_brick_topology.ino) in the **Arduino IDE**.
2. Install the **MFRC522** library via Arduino Library Manager (by *GithubCommunity* / *miguelbalboa*).
3. Select your ESP32 board (e.g., `ESP32 Dev Module`).
4. Set the `BRICK_ID` macro at the top of the sketch:
   - First Brick: `#define BRICK_ID 1`
   - Second Brick: `#define BRICK_ID 2`
   - Third Brick: `#define BRICK_ID 3`
   - Fourth Brick: `#define BRICK_ID 4`
5. Upload to each respective ESP32 brick.
6. Open the Serial Monitor at **115200 baud** to view real-time neighbor detection packets.

---

## 3. LED Neighbor Count Color Legend

| Active Neighbors | LED Color | State Description |
| :---: | :---: | :--- |
| **0** | 🔴 **RED** | Isolated / Standalone (No adjacent bricks) |
| **1** | 🟡 **YELLOW** | 1 adjacent brick connected (e.g. line endpoint) |
| **2** | 🔵 **BLUE** | 2 adjacent bricks connected (e.g. line corner or 2x2 corner) |
| **3** | 🩵 **CYAN** | 3 adjacent bricks connected (e.g. T-junction) |
| **4** | 🟢 **GREEN** | 4 adjacent bricks connected (Fully surrounded) |

---

## 4. Running the Automated Topology Verification Test

You can run the simulated multi-agent test suite on your local machine:

```bash
python3 Code/tests/test_topology.py
```

This verifies:
- 2x2 Grid docking topology resolution
- 1x4 Linear strip topology resolution
- Dynamic disconnection and timeout aging
