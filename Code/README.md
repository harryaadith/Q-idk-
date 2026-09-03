# Multi-Agent Microbots (Smart Bricks) — Embedded Firmware

This repository contains the complete embedded firmware and test suite for decentralized, multi-agent **Smart Bricks (Microbots)** running on **ESP32 Dev Modules** (compatible with both 30-pin and 38-pin boards).

---

## 🚀 Project Overview

Each microbot ("brick") operates as an autonomous node with no master controller or central server. Nodes detect one another over low-latency wireless communication, estimate neighbor proximity via signal strength, track physical docking via cardinal IR sensors, read local RFID tokens, and dynamically signal swarm states through on-board LEDs, external RGB LEDs, and an audio buzzer.

### Key Capabilities
1. **Peer-to-Peer Swarm Broadcast**: Each ESP32 exchanges its unique ID and neighbor count with all other bricks in the swarm using connectionless ESP-NOW broadcasts.
2. **Signal-Strength Proximity Sensing**: Real-time RSSI measurement smoothed by an Exponential Moving Average (EMA) filter with hysteresis to reliably determine physical proximity.
3. **4-Face Physical Docking (Directional IR)**: 4 infrared sensors on North (`GPIO 34`), East (`GPIO 35`), South (`GPIO 32`), and West (`GPIO 33`) faces provide immediate physical docking and undocking detection.
4. **Audible Acoustic Feedback (Buzzer)**: Audio feedback on `GPIO 27` for RFID confirmation chirps, face docking/undocking chimes, proximity consensus alerts, and boot chime.
5. **On-Board Proximity LED**: The built-in blue LED (`GPIO 2`) illuminates whenever a brick is in close physical proximity to all its active neighbors.
6. **Neighbor Count Sequencing**: When not close to all neighbors, the external RGB LED executes an animated pulse sequence: blinking **Blue** $N$ times (where $N$ = neighbor count) followed by a **Red** pulse (or solid **Red** when 0 neighbors are detected).
7. **RFID Neighbor Detection**: When an RFID tag from a neighbor is scanned by the MFRC522 reader, the RGB LED immediately illuminates **Green** for 2 seconds alongside an acoustic confirmation chirp.
8. **30-Pin Board Optimized (All Pins $\le 35$)**: Completely eliminates dependency on GPIO 36 and above, making it 100% compatible with popular 30-pin ESP32 DevKit boards.
9. **Future Expansion Ready**: Code architecture includes reserved pinouts and function stubs for 9-DoF IMU/Magnetometer orientation (`GPIO 21, 22`) and a haptic vibrator motor actuator (`GPIO 26`).

---

## 📂 Repository Structure

```
Code/
├── brick.ino               <-- Primary consolidated production sketch
├── brick/
│   └── brick.ino           <-- Arduino IDE compatible sketch folder
├── INSTRUCTIONS.MD         <-- Complete wiring diagrams, circuit pinouts & user guide
├── setup.md                <-- Quickstart and flashing guide
├── README.md               <-- Project overview & documentation guidelines
├── tests/
│   ├── test_brick_logic.py <-- Automated 6-point verification suite (IR, Buzzer, ESP-NOW)
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

See [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD) for full circuit schematics, resistor values, and calibration steps.

---

## 🚦 Status Indicators & Sensory Legend

| State / Event | On-Board LED (`GPIO 2`) | RGB LED (`GPIO 16, 17, 25`) | Buzzer (`GPIO 27`) |
| :--- | :---: | :--- | :--- |
| **RFID Tag Read** | Normal | 🟢 **Solid GREEN** (2.0s hold) | 🔔 Cheerful double-chirp |
| **IR Face Docks** | Normal | Normal | 🔔 Ascending double-beep |
| **IR Face Undocks** | Normal | Normal | ⚠️ Single alert tone |
| **Close to All Neighbors** | 💡 **ON** | 🔵 **Solid BLUE** | 🔔 Consensus chime on entry |
| **0 Neighbors (Standalone)** | ⚫ OFF | 🔴 **Solid RED** | Silent |
| **1 Neighbor (Not Close)** | ⚫ OFF | 🔵 **1 Blue Pulse** ➔ 🔴 **1 Red Pulse** | Silent |
| **2 Neighbors (Not Close)** | ⚫ OFF | 🔵 **2 Blue Pulses** ➔ 🔴 **1 Red Pulse** | Silent |
| **3 Neighbors (Not Close)** | ⚫ OFF | 🔵 **3 Blue Pulses** ➔ 🔴 **1 Red Pulse** | Silent |

---

## 🧪 Simulation & Automated Tests

To test multi-agent neighbor discovery, IR face docking, and timeout logic locally:

```bash
# Run automated verification suite (ESP-NOW, RFID, IR, Buzzer, Timeout)
python3 tests/test_brick_logic.py

# Run automated topology assertions (Grid, Strip, Timeout)
python3 tests/test_topology.py

# Run interactive ASCII visual cluster simulator
python3 tests/sim_topology.py
```