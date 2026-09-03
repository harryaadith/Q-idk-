# Multi-Agent Microbots — Historical Sketches & Prototypes Archive

This directory organizes all earlier experimental sketches, exploratory prototypes, and initial topology experiments developed prior to consolidation into the unified production firmware [`Code/brick.ino`](../brick.ino).

---

## 📁 Archived Sketches Overview

| Directory / File | Description & Scope | Key Hardware Tested | Replaced By |
| :--- | :--- | :--- | :--- |
| [`Prototypes/espNow_connection/`](Prototypes/espNow_connection/) | Initial 2-node ESP-NOW broadcast prototype. Verified wireless packet transmission without Wi-Fi AP association. | ESP32 Dev Modules (WiFi) | Consolidated into [`brick.ino`](../brick.ino) |
| [`Prototypes/brick_0.1/`](Prototypes/brick_0.1/) | Two-brick interaction test. Read local RFID UID, flashed Blue LED on packet reception, and illuminated Green LED when tag detected. | MFRC522 (VSPI), RGB LED | Consolidated into [`brick.ino`](../brick.ino) |
| [`Prototypes/Proximity_Mesh/`](Prototypes/Proximity_Mesh/) | Multi-node mesh using packet RSSI signal strength with Exponential Moving Average (EMA) smoothing and adaptive proximity thresholding. | ESP32 ESP-NOW RSSI, Dual LEDs | Consolidated into [`brick.ino`](../brick.ino) |
| [`smart_brick_topology/`](smart_brick_topology/) | Cardinal 4-face (North, East, South, West) docking topology matrix with neighbor table aging and timeout eviction. | ESP-NOW, MFRC522, RGB LED | Consolidated into [`brick.ino`](../brick.ino) |
| [`Prototypes/PROTOTYPE_INST.md`](Prototypes/PROTOTYPE_INST.md) | Original prototype instructions and color legends for the 2-brick experimental testbed. | Documentation | Superseded by [`INSTRUCTIONS.MD`](../INSTRUCTIONS.MD) |

---

## 🔬 Evolutionary Progression to Unified `brick.ino`

1. **Protocol Convergence**: Early sketches used divergent packet structures (`BrickMessage`, `Message`, `PingPacket`/`ReportPacket`, `BrickPacket`). In `brick.ino`, this is unified to a single, lightweight `BrickPacket` exchanging `sender_id` and `neighbor_count`.
2. **Proximity Sensing**: Borrowed the low-pass EMA filter and hysteresis model from `Proximity_Mesh` so that signal fluctuations do not cause rapid state toggling.
3. **Status Indication**: Combined the RFID Green detection from `brick_0.1`, the On-Board LED proximity indicator, and a sequential Blue-then-Red pulse sequence directly encoding the neighbor count on the external RGB LED.
4. **Future-Proofing**: Preserved face docking and sensor stub architecture from `smart_brick_topology` for the upcoming IR, IMU, and haptic actuator integrations.
