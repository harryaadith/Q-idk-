# Multi-Agent Microbots (Smart Bricks) — Embedded Firmware

This repository contains the complete embedded firmware and test suite for decentralized, multi-agent **Smart Bricks (Microbots)** running on **ESP32 Dev Modules**.

---

## 🚀 Project Overview

Each microbot ("brick") operates as an autonomous node with no master controller or central server. Nodes detect one another over low-latency wireless communication, estimate neighbor proximity via signal strength, read local RFID tokens, and dynamically signal swarm states through on-board and external RGB LEDs.

### Key Capabilities
1. **Peer-to-Peer Swarm Broadcast**: Each ESP32 exchanges its unique ID and neighbor count with all other bricks in the swarm using connectionless ESP-NOW broadcasts.
2. **Signal-Strength Proximity Sensing**: Real-time RSSI measurement smoothed by an Exponential Moving Average (EMA) filter with hysteresis to reliably determine physical proximity.
3. **On-Board Proximity LED**: The built-in blue LED (`GPIO 2`) illuminates whenever a brick is in close physical proximity to all its active neighbors.
4. **Neighbor Count Sequencing**: When not close to all neighbors, the external RGB LED executes an animated pulse sequence: blinking **Blue** $N$ times (where $N$ = neighbor count) followed by a **Red** pulse (or solid **Red** when 0 neighbors are detected).
5. **RFID Neighbor Detection**: When an RFID tag from a neighbor is scanned by the MFRC522 reader, the RGB LED immediately illuminates **Green** for 2 seconds.
6. **Future Expansion Ready**: Code architecture includes pinouts, configuration flags, and function stubs for 4-face Directional IR sensors, 9-DoF IMU/Magnetometer orientation, and a haptic vibrator motor actuator.

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
│   ├── sim_topology.py     <-- Visual interactive swarm topology simulator
│   └── test_topology.py    <-- Automated assertion verification tests
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

| Peripheral | Function / Pin | ESP32 GPIO |
| :--- | :--- | :--- |
| **MFRC522 RFID** | SCK, MISO, MOSI | `GPIO 18`, `GPIO 19`, `GPIO 23` |
| | SDA / SS, RST | `GPIO 5`, `GPIO 4` |
| | VCC, GND | `3V3`, `GND` |
| **Common-Cathode RGB LED** | RED, GREEN, BLUE | `GPIO 16`, `GPIO 17`, `GPIO 25` (via 220Ω resistors) |
| | Cathode | `GND` |
| **On-Board LED** | Proximity State | `GPIO 2` |

See [`INSTRUCTIONS.MD`](INSTRUCTIONS.MD) for full circuit diagrams and the future expansion pin map (`GPIO 34, 35, 36, 39` for IR; `GPIO 21, 22` for I2C IMU; `GPIO 32` for Vibrator).

---

## 🚦 Status Indicators & Color Legend

| State | On-Board LED (`GPIO 2`) | External RGB LED (`GPIO 16, 17, 25`) |
| :--- | :---: | :--- |
| **RFID Tag Read** | Normal | 🟢 **Solid GREEN** (2.0s hold) |
| **Close to All Neighbors** | 💡 **ON** | 🔵 **Solid BLUE** |
| **0 Neighbors (Standalone)** | ⚫ OFF | 🔴 **Solid RED** |
| **1 Neighbor (Not Close)** | ⚫ OFF | 🔵 **1 Blue Pulse** ➔ 🔴 **1 Red Pulse** |
| **2 Neighbors (Not Close)** | ⚫ OFF | 🔵 **2 Blue Pulses** ➔ 🔴 **1 Red Pulse** |
| **3 Neighbors (Not Close)** | ⚫ OFF | 🔵 **3 Blue Pulses** ➔ 🔴 **1 Red Pulse** |

---

## 🧪 Simulation & Automated Tests

To test multi-agent neighbor discovery, docking, and timeout logic locally on your computer:

```bash
# Run automated topology assertions (Grid, Strip, Timeout)
python3 tests/test_topology.py

# Run interactive ASCII visual cluster simulator
python3 tests/sim_topology.py
```