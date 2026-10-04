# Multi-Agent Microbots (Smart Bricks) — Instructions & Wiring Guide

This document provides complete instructions for wiring, configuring, flashing, testing, and expanding the **Multi-Agent Smart Bricks** system using **ESP32 Dev Modules** (compatible with both 30-pin and 38-pin boards), **MFRC522 RFID Readers**, **Common-Cathode RGB LEDs**, **4-Face Directional IR Sensors**, **Audio Buzzer**, **MPU9250 9-DoF IMU & Magnetometer**, and an **ERM Vibration Motor**.

> [!NOTE]
> **30-Pin Board Pinout Optimization**: All GPIO pins in this firmware are strictly **$\le 35$**. Many popular 30-pin ESP32 boards (e.g., DevKit V1, NodeMCU-32S 30-pin) omit GPIO 36 (VP) and GPIO 39 (VN) or lack breakout pins for them. This pinout is 100% compatible with all standard 30-pin and 38-pin ESP32 modules.

---

## 📋 Table of Contents
1. [System Architecture Overview](#1-system-architecture-overview)
2. [Hardware Connections & Complete Pinout](#2-hardware-connections--complete-pinout)
   - [A. MFRC522 RFID Reader](#a-mfrc522-rfid-reader-spi--vspi)
   - [B. Common-Cathode RGB LED](#b-common-cathode-rgb-led)
   - [C. On-Board Status LED](#c-on-board-status-led)
   - [D. 4-Face Directional IR Sensors](#d-4-face-directional-ir-sensors-north-east-south-west)
   - [E. Audio Buzzer](#e-audio-buzzer-active--passive)
   - [F. MPU9250 9-DoF IMU & Magnetometer (10-Pin Breakout)](#f-mpu9250-9-dof-imu--magnetometer-10-pin-breakout)
   - [G. ERM Vibration Motor Actuator (2-Wire)](#g-erm-vibration-motor-actuator-2-wire)
   - [H. Full Circuit Schematic & Breadboard Diagram](#h-full-circuit-schematic--breadboard-diagram)
3. [Firmware Configuration & Flashing Guide](#3-firmware-configuration--flashing-guide)
4. [Operational State Machine, LED, Buzzer & Haptic Legend](#4-operational-state-machine-led-buzzer--haptic-legend)
5. [ESP-NOW Communication Protocol](#5-esp-now-communication-protocol)
6. [Testing & Verification Procedures](#6-testing--verification-procedures)
7. [Troubleshooting & Calibration](#7-troubleshooting--calibration)

---

## 1. System Architecture Overview

The Multi-Agent Smart Brick system consists of **4 autonomous ESP32 microbots** ("bricks"). Each brick operates independently without any central broker, router, or master controller:
- **Peer-to-Peer ESP-NOW Wireless**: Nodes communicate over 2.4 GHz raw radio frames, broadcasting their identity and active neighbor count.
- **Proximity Sensing via RSSI**: Each node measures incoming radio signal strength (RSSI), smooths it through an Exponential Moving Average (EMA) filter, and applies hysteresis to classify neighbors as "CLOSE" or "FAR".
- **4-Face Physical Docking Detection (IR)**: Each brick is equipped with 4 directional IR obstacle/proximity sensors positioned along its cardinal faces:
  - **NORTH**: `GPIO 34` (D34)
  - **EAST**: `GPIO 35` (D35)
  - **SOUTH**: `GPIO 32` (D32)
  - **WEST**: `GPIO 33` (D33)
- **Acoustic Feedback (Buzzer)**: Driven on **`GPIO 27`** (D27) to provide distinct audible tones for RFID tag confirmation, physical face docking/undocking, proximity consensus, and boot status.
- **Dual Visual Indicators**:
  - **On-Board LED (`GPIO 2`)**: Lights up when the brick is in close physical proximity to all its active neighbors.
  - **External RGB LED (`GPIO 16, 17, 25`)**:
    - Lights **Green** when an RFID card/tag from a neighbor is scanned.
    - Otherwise sequences **Blue then Red in order of neighbor count** (or **Solid Red** when standalone with 0 neighbors).
- **RFID Presence Detection**: Uses the SPI-based MFRC522 reader to identify neighbor tokens or docked instruments.
- **Self-Orientation & Motion Tracking (MPU9250)**: Standard I2C bus (`SDA=GPIO 21, SCL=GPIO 22`) delivers real-time 3-axis accelerometer tilt (Pitch/Roll) and AK8963 3-axis magnetometer compass heading (Yaw relative to Magnetic North).
- **Tactile Haptic Feedback (ERM Motor)**: Driven on `GPIO 26` through a transistor driver, generating tactile feedback for RFID read confirmation, IR docking snaps, and swarm consensus pulses.

---

## 2. Hardware Connections & Complete Pinout

Each of the 4 bricks has an **identical** hardware connection layout using pins strictly $\le 35$:

### A. MFRC522 RFID Reader (SPI / VSPI)

| MFRC522 Pin | ESP32 Pin | Label on Board | Description / Notes |
| :--- | :--- | :--- | :--- |
| **3.3V / VCC** | `3V3` | `3V3` | **Must be 3.3V** (Do NOT connect to 5V/VIN; damages RFID chip) |
| **RST** | `GPIO 4` | `D4` | Reset Control Pin |
| **GND** | `GND` | `GND` | Ground reference |
| **IRQ** | *NC* | *NC* | Not connected |
| **MISO** | `GPIO 19` | `D19` | Standard VSPI MISO |
| **MOSI** | `GPIO 23` | `D23` | Standard VSPI MOSI |
| **SCK** | `GPIO 18` | `D18` | Standard VSPI Clock |
| **SDA / SS** | `GPIO 5` | `D5` | SPI Slave Select / Chip Select |

---

### B. Common-Cathode RGB LED

> [!IMPORTANT]
> Always insert a **220Ω to 330Ω current-limiting resistor** in series with each color anode (Red, Green, Blue) to prevent pin overload and ensure balanced brightness.

| RGB LED Pin | Series Resistor | ESP32 Pin | Label on Board | Logic State |
| :--- | :--- | :--- | :--- | :--- |
| **RED Anode** | 220Ω | `GPIO 16` | `D16` / `RX2` | `HIGH` = Red ON |
| **GREEN Anode** | 220Ω | `GPIO 17` | `D17` / `TX2` | `HIGH` = Green ON |
| **BLUE Anode** | 220Ω | `GPIO 25` | `D25` | `HIGH` = Blue ON |
| **Common Cathode (Longest pin)** | Direct | `GND` | `GND` | Common ground |

---

### C. On-Board Status LED

| Indicator | ESP32 Pin | Logic State | Function |
| :--- | :--- | :--- | :--- |
| **On-Board LED** | `GPIO 2` | `HIGH` = ON | Built-in blue LED; illuminates when **close to all active neighbors** |

---

### D. 4-Face Directional IR Sensors (North, East, South, West)

Standard 3-pin IR obstacle avoidance / proximity modules (e.g., FC-51, TCRT5000, or KY-032) feature `VCC`, `GND`, and `OUT`:

| IR Sensor Position | Sensor Pin | ESP32 Pin | Label on Board | Notes & Characteristics |
| :--- | :--- | :--- | :--- | :--- |
| **NORTH Face (Top)** | VCC | `3V3` | `3V3` | Shared 3.3V power bus |
| | GND | `GND` | `GND` | Common ground |
| | OUT | `GPIO 34` | **`D34`** | Input-only GPIO (GPI) |
| **EAST Face (Right)** | VCC | `3V3` | `3V3` | Shared 3.3V power bus |
| | GND | `GND` | `GND` | Common ground |
| | OUT | `GPIO 35` | **`D35`** | Input-only GPIO (GPI) |
| **SOUTH Face (Bottom)** | VCC | `3V3` | `3V3` | Shared 3.3V power bus |
| | GND | `GND` | `GND` | Common ground |
| | OUT | `GPIO 32` | **`D32`** | General-Purpose I/O |
| **WEST Face (Left)** | VCC | `3V3` | `3V3` | Shared 3.3V power bus |
| | GND | `GND` | `GND` | Common ground |
| | OUT | `GPIO 33` | **`D33`** | General-Purpose I/O |

---

### E. Audio Buzzer (Active / Passive)

| Buzzer Type | Buzzer Pin | ESP32 Pin | Label on Board | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **2-Pin Active Buzzer** | Positive (+) / Long Pin | `GPIO 27` | **`D27`** | Directly driven (or add 100Ω if too loud) |
| | Negative (-) / Short Pin | `GND` | `GND` | Common ground |
| **3-Pin Buzzer Module** | I/O / SIG | `GPIO 27` | **`D27`** | Digital trigger signal |
| | VCC | `3V3` or `5V` | `3V3` | Module power |
| | GND | `GND` | `GND` | Common ground |

---

### F. MPU9250 9-DoF IMU & Magnetometer (10-Pin Breakout)

| Pin # | MPU9250 Pin | ESP32 Connection | Description & Wiring Instructions |
| :---: | :--- | :--- | :--- |
| **1** | **VCC** | `3V3` | 3.3V Power rail |
| **2** | **GND** | `GND` | Common Ground |
| **3** | **SCL** | `GPIO 22` (`D22`) | I2C Clock |
| **4** | **SDA** | `GPIO 21` (`D21`) | I2C Data |
| **5** | **EDA** | *NC (No Connect)* | Auxiliary I2C Data |
| **6** | **ECL** | *NC (No Connect)* | Auxiliary I2C Clock |
| **7** | **AD0** | `GND` | **Address Select**: Pull to GND for I2C Address `0x68` |
| **8** | **INT** | *NC (No Connect)* | Interrupt output (Firmware polls non-blocking) |
| **9** | **NCS** | **`3V3`** | **CRITICAL: Chip Select**. Connect to **`3V3`** to enable I2C mode! *(If grounded, MPU boots in SPI mode)* |
| **10** | **FSYNC** | `GND` | Frame Sync (Tie to GND) |

---

### G. ERM Vibration Motor Actuator (2-Wire)

DC coin/cylinder vibration motors draw 60–90mA at 3V. **Never connect the motor directly between an ESP32 GPIO and GND**, as GPIOs have a 20–40mA limit and inductive spikes can permanently damage the pin.

#### Discrete Transistor Driver Wiring:
```
                       +3.3V (or 5V/VIN)
                          |
                          +-----------+
                          |           |
                         [+]         [K] Diode (1N4148 / 1N4001)
                     ERM Motor        |  (Cathode to +, Anode to -)
                         [-]         [A]
                          |           |
                          +-----------+
                          |
                       C (Collector)
                          |
  ESP32 GPIO 26 ---[1kΩ]--- B (Base)   [NPN Transistor: 2N2222 or BC547]
                          |
                       E (Emitter)
                          |
                         GND
```

| Component | Connects To | Notes |
| :--- | :--- | :--- |
| **Motor Red Wire (+)** | `3V3` | Positive supply |
| **Motor Blue/Black Wire (-)** | Transistor **Collector (C)** | Switched ground |
| **Diode (1N4148 or 1N4001)** | Across Motor (+) & (-) | Cathode (band) to (+), Anode to (-) |
| **Transistor Base (B)** | `GPIO 26` (`D26`) via **1kΩ** resistor | Current limit to base |
| **Transistor Emitter (E)** | `GND` | Common ground |

*(If using a 3-pin vibration motor module: `VCC` ➔ `3V3`, `GND` ➔ `GND`, `IN/SIG` ➔ `GPIO 26`).*

---

### H. Full Circuit Schematic & Breadboard Diagram

```
                             +-----------------------------+
                             |       ESP32 Dev Module      |
                             |       (30-Pin DevKit V1)    |
            +3.3V <----------| 3V3                     GND |----------> GND (Common)
         RFID RST <----------| GPIO 4              GPIO 23 |----------> RFID MOSI
          RFID SS <----------| GPIO 5              GPIO 19 |----------> RFID MISO
         RGB-RED  <--[220Ω]--| GPIO 16             GPIO 18 |----------> RFID SCK
         RGB-GRN  <--[220Ω]--| GPIO 17              GPIO 2 |----------> [On-Board Blue LED]
         RGB-BLU  <--[220Ω]--| GPIO 25             GPIO 27 |----------> BUZZER (+)
   ERM MOTOR GATE <--[1kΩ]---| GPIO 26             GPIO 33 |<---------- IR WEST  (OUT)
      MPU9250 SDA <--------->| GPIO 21             GPIO 32 |<---------- IR SOUTH (OUT)
      MPU9250 SCL <--------->| GPIO 22             GPIO 35 |<---------- IR EAST  (OUT)
                             |                     GPIO 34 |<---------- IR NORTH (OUT)
                             +-----------------------------+
```

---

## 3. Firmware Configuration & Flashing Guide

### Step 1: Install Required Libraries
Open the **Arduino IDE** (or `arduino-cli`):
1. Navigate to **Tools > Manage Libraries...**
2. Search for `MFRC522` (by *GithubCommunity* / *miguelbalboa*).
3. Click **Install**.
*(Note: `<Wire.h>` for MPU9250 is already built into the ESP32 core).*

### Step 2: Open the Sketch
Open [`Code/brick/brick.ino`](brick/brick.ino), keeping `DashboardPage.h` beside it. Use ESP32 Arduino core 3.x.

### Step 3: Use the Same Firmware on Every Board

No manual `BRICK_ID` edits are needed. Each ESP32 uses its hardware MAC identity and computes a display number from the active group. Display numbers can change when bricks join or leave. Reflash all boards because old packet formats are incompatible. See the [wireless dashboard guide](Dashboard/README.md).

### Step 4: Flash the Board
1. Connect the ESP32 to your PC via Micro-USB.
2. Select **ESP32 Dev Module** under **Tools > Board**.
3. Select your COM/serial port under **Tools > Port**.
4. Click **Upload**.

---

## 4. Operational State Machine, LED, Buzzer & Haptic Legend

### Sensory Indicator Reference

| Event / Condition | On-Board LED (`GPIO 2`) | External RGB LED (`16, 17, 25`) | Audio Buzzer (`GPIO 27`) | ERM Motor (`GPIO 26`) |
| :--- | :---: | :--- | :--- | :--- |
| **System Boot** | Pulse | Flashes Red | 🎵 Triple startup chime | ⚡ 50ms startup buzz |
| **RFID Tag Scanned** | Normal | 🟢 **Solid GREEN** (2.0s hold) | 🔔 Double-chirp | ⚡ 100ms tactile click |
| **IR Face Docks** | Normal | Normal | 🔔 Ascending double-beep | ⚡ 80ms docking snap |
| **IR Face Undocks** | Normal | Normal | ⚠️ Single alert tone | Silent |
| **Close to All Neighbors** | 💡 **Solid ON** | 🔵 **Solid BLUE** | 🔔 Consensus chime | ⚡ Double-pulse buzz |
| **0 Neighbors (Standalone)** | ⚫ OFF | 🔴 **Solid RED** | Silent | Silent |
| **1 Neighbor (Not Close)** | ⚫ OFF | 🔵 **1 Blue Pulse** ➔ 🔴 **1 Red** | Silent | Silent |
| **2 Neighbors (Not Close)** | ⚫ OFF | 🔵 **2 Blue Pulses** ➔ 🔴 **1 Red** | Silent | Silent |
| **3 Neighbors (Not Close)** | ⚫ OFF | 🔵 **3 Blue Pulses** ➔ 🔴 **1 Red** | Silent | Silent |

---

## 5. ESP-NOW Communication Protocol

### Packet Format
```cpp
typedef struct __attribute__((packed)) {
  uint8_t sender_id;       // Brick ID (1..4)
  uint8_t neighbor_count;  // Number of active neighbors currently heard by sender
} BrickPacket;
```

### Proximity Estimation Algorithm
1. Incoming radio frame RSSI extracted via `info->rx_ctrl->rssi`.
2. Filtered with Exponential Moving Average (EMA, $\alpha=0.30$).
3. Schmitt trigger hysteresis: `CLOSE` at $\ge -65\text{ dBm}$, `FAR` below $-69\text{ dBm}$.
4. Neighbor eviction occurs after $2500\text{ ms}$ of silence.

---

## 6. Testing & Verification Procedures

Run the automated simulation test suite locally:

```bash
python3 Code/tests/test_brick_logic.py
```

All 7 core operational specifications pass automatically:
- Test 1: Minimal 2-byte packet protocol
- Test 2: RFID tag scan lights Green, sounds buzzer, and triggers ERM haptic pulse
- Test 3: Close to all active neighbors lights on-board LED, chimes, and double-buzzes
- Test 4: Neighbor count sequence (Blue pulses $N$ times then Red)
- Test 5: 4-face directional IR sensors with docking chime and haptics
- Test 6: Timeout eviction
- Test 7: MPU9250 Pitch, Roll, and Compass Heading mathematics

---

## 7. Troubleshooting & Calibration

- **MPU9250 Fails to Initialize (`[IMU] MPU9250 not detected at 0x68`)**:
  - Check **Pin 9 (NCS)**: Must be connected to **`3V3`**. If NCS is floating, the sensor enters SPI mode and ignores I2C commands!
  - Check **Pin 7 (AD0)**: Must be pulled to **`GND`** to set I2C address to `0x68`.
  - Check SDA (`GPIO 21`) and SCL (`GPIO 22`).
- **ERM Motor Does Not Vibrate**:
  - Check transistor pinout (E-B-C on 2N2222 or BC547). Emitter must connect to GND.
  - Verify diode polarity: Cathode (striped end) connects to 3.3V, Anode connects to motor negative.
- **IR Sensor Always Detected**:
  - Turn trimmer screw on IR board counter-clockwise until detection LED on module turns off in empty air.
- **RFID Reads 0x00 or 0xFF**:
  - Check SPI wiring: `MOSI=GPIO 23`, `MISO=GPIO 19`, `SCK=GPIO 18`, `SS=GPIO 5`, `RST=GPIO 4`. Ensure reader is powered by **3.3V**, not 5V.
