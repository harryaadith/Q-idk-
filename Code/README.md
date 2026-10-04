# Multi-Agent Microbots (Smart Bricks) — Embedded Firmware

Start with the complete [HOW TO RUN guide](../HOW_TO_RUN.md) for setup from the beginning.

Upload the same [`brick/brick.ino`](brick/brick.ino) to every board; identities and display numbers are automatic. See the [wireless dashboard guide](Dashboard/README.md) for current setup and validation limitations.

This repository contains the complete embedded firmware and test suite for decentralized, multi-agent **Smart Bricks (Microbots)** running on **ESP32 Dev Modules** (compatible with both 30-pin and 38-pin boards).

---

## 🚀 Project Overview

Each microbot ("brick") operates as an autonomous node with no master controller or central server. Nodes detect one another over low-latency wireless communication, estimate neighbor proximity via signal strength, track physical docking via cardinal IR sensors, read local RFID tokens, measure 9-DoF self-orientation via MPU9250, provide tactile feedback via an ERM vibration motor, and dynamically signal swarm states through on-board LEDs, external RGB LEDs, and an audio buzzer.

### Key Capabilities
1. **Peer-to-Peer Swarm Broadcast**: Each ESP32 exchanges its unique ID and neighbor count with all other bricks in the swarm using connectionless ESP-NOW broadcasts.
2. **Signal-Strength Proximity Sensing**: Real-time RSSI measurement smoothed by an Exponential Moving Average (EMA) filter with hysteresis to reliably determine physical proximity.
3. **4-Face Physical Docking (Directional IR)**: 4 infrared sensors on North (`GPIO 34`), East (`GPIO 35`), South (`GPIO 32`), and West (`GPIO 33`) faces provide immediate physical docking and undocking detection.
4. **9-DoF Motion Tracking (MPU9250 & AK8963)**: Interfaced over I2C (`SDA=21, SCL=22`), calculating 3-axis accelerometer tilt (Pitch/Roll) and 3-axis magnetometer compass heading (Yaw relative to Magnetic North).
5. **Tactile Haptic Feedback (ERM Motor)**: Driven on `GPIO 26` through a transistor driver, providing tactile pulses for RFID confirmations, face docking snaps, and swarm consensus alerts.
6. **Audible Acoustic Feedback (Buzzer)**: Audio feedback on `GPIO 27` for RFID confirmation chirps, face docking/undocking chimes, proximity consensus alerts, and boot chime.
7. **On-Board Proximity LED**: The built-in blue LED (`GPIO 2`) illuminates whenever a brick is in close physical proximity to all its active neighbors.
8. **Neighbor Count Sequencing**: When not close to all neighbors, the external RGB LED executes an animated pulse sequence: blinking **Blue** $N$ times (where $N$ = neighbor count) followed by a **Red** pulse (or solid **Red** when 0 neighbors are detected).
9. **RFID Neighbor Detection**: When an RFID tag from a neighbor is scanned by the MFRC522 reader, the RGB LED immediately illuminates **Green** for 2 seconds alongside an acoustic confirmation chirp and haptic pulse.
10. **30-Pin Board Optimized (All Pins $\le 35$)**: Completely eliminates dependency on GPIO 36 and above, making it 100% compatible with popular 30-pin ESP32 DevKit boards.

---

## 📂 Repository Structure

```
Code/
├── brick/
│   ├── brick.ino           <-- Single production sketch for every board
│   └── DashboardPage.h     <-- Generated dashboard assets
├── Dashboard/             <-- HTML, JavaScript, embedding script & wireless guide
├── INSTRUCTIONS.MD         <-- Complete wiring diagrams, circuit pinouts & user guide
├── setup.md                <-- Quickstart and flashing guide
├── README.md               <-- Project overview & documentation guidelines
├── tests/
│   ├── test_brick_logic.py <-- Automated 7-point verification suite (IR, Buzzer, Haptics, IMU, ESP-NOW)
│   ├── sim_topology.py     <-- Visual interactive swarm topology simulator
│   └── test_topology.py    <-- Automated topology assertion verification tests
└── Archive/                <-- Historical prototypes & early experiments
    ├── README.md           <-- History and design evolution documentation
    ├── Prototypes/
    │   ├── espNow_connection/
    │   ├── brick_0.1/
    │   ├── Proximity_Mesh/
    │   └── PROTOTYPE_INST.md
    └── smart_brick_topology/
```

---

## 🛠️ Hardware Requirements & Pinout Summary

| Peripheral | Function / Pin | ESP32 Pin | Label on Board | Description |
| :--- | :--- | :--- | :--- | :--- |
| **MFRC522 RFID** | SCK, MISO, MOSI | `GPIO 18`, `GPIO 19`, `GPIO 23` | `D18`, `D19`, `D23` | VSPI bus |
| | SDA / SS, RST | `GPIO 5`, `GPIO 4` | `D5`, `D4` | Chip select & reset |
| | VCC, GND | `3V3`, `GND` | `3V3`, `GND` | Power (**Must be 3.3V**) |
| **Common-Cathode RGB LED** | RED, GREEN, BLUE | `GPIO 16`, `GPIO 17`, `GPIO 25` | `D16`, `D17`, `D25` | Via 220Ω series resistors |
| | Cathode | `GND` | `GND` | Common ground |
| **On-Board LED** | Proximity State | `GPIO 2` | `D2` | Lights when close to all active neighbors |
| **4x Directional IR Sensors** | North Face OUT | `GPIO 34` | `D34` | Cardinal top face docking sensor |
| | East Face OUT | `GPIO 35` | `D35` | Cardinal right face docking sensor |
| | South Face OUT | `GPIO 32` | `D32` | Cardinal bottom face docking sensor |
| | West Face OUT | `GPIO 33` | `D33` | Cardinal left face docking sensor |
| **Audio Buzzer** | Positive / SIG | `GPIO 27` | `D27` | Acoustic alert & confirmation tones |
| **MPU9250 9-DoF IMU** | SDA, SCL | `GPIO 21`, `GPIO 22` | `D21`, `D22` | I2C Serial Data & Clock |
| | NCS, AD0 | `3V3`, `GND` | `3V3`, `GND` | I2C mode select & address (0x68) |
| **ERM Vibration Motor** | Gate / Base | `GPIO 26` | `D26` | Driven via transistor driver |

See [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD) for full circuit schematics, transistor circuits, and calibration steps.

---

## 🚦 Status Indicators & Sensory Legend

| State / Event | On-Board LED (`GPIO 2`) | RGB LED (`16, 17, 25`) | Buzzer (`GPIO 27`) | ERM Motor (`GPIO 26`) |
| :--- | :---: | :--- | :--- | :--- |
| **Boot Complete** | Pulse | Flash Red | 🎵 Startup melody | ⚡ 50ms startup buzz |
| **RFID Tag Read** | Normal | 🟢 **Solid GREEN** (2.0s hold) | 🔔 Cheerful double-chirp | ⚡ 100ms tactile click |
| **IR Face Docks** | Normal | Normal | 🔔 Ascending double-beep | ⚡ 80ms docking snap |
| **IR Face Undocks** | Normal | Normal | ⚠️ Single alert tone | Silent |
| **Close to All Neighbors** | 💡 **ON** | 🔵 **Solid BLUE** | 🔔 Consensus chime on entry | ⚡ Double-pulse buzz |
| **0 Neighbors (Standalone)** | ⚫ OFF | 🔴 **Solid RED** | Silent | Silent |
| **1 Neighbor (Not Close)** | ⚫ OFF | 🔵 **1 Blue Pulse** ➔ 🔴 **Red** | Silent | Silent |
| **2 Neighbors (Not Close)** | ⚫ OFF | 🔵 **2 Blue Pulses** ➔ 🔴 **1 Red** | Silent | Silent |
| **3 Neighbors (Not Close)** | ⚫ OFF | 🔵 **3 Blue Pulses** ➔ 🔴 **1 Red** | Silent | Silent |

---

## 🧪 Simulation & Automated Tests

To test multi-agent neighbor discovery, IR face docking, IMU mathematics, and timeout logic locally:

```bash
# Run automated verification suite (ESP-NOW, RFID, IR, Buzzer, IMU, ERM Haptics, Timeout)
python3 tests/test_brick_logic.py

# Run automated topology assertions (Grid, Strip, Timeout)
python3 tests/test_topology.py

# Run interactive ASCII visual cluster simulator
python3 tests/sim_topology.py
```