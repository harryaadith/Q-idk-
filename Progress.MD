# Multi-Agent Microbots (Smart Bricks) — Project Progress & Hardware Tracker

**Project:** Autonomous Multi-Agent Smart Bricks (Microbots)  
**Team:** Team 22 (*Q-idk-if-i-m-getting-an-internship*) — Electronic System Workshop (ESW)  
**Firmware File:** [`Code/brick/brick.ino`](Code/brick/brick.ino)
**Target Platform:** ESP32 Dev Module (30-Pin & 38-Pin Form Factors, all pins strictly $\le 35$)

---

## 📊 Status Dashboard

| Subsystem / Peripheral | Status | Pin / Bus | Function / Notes |
| :--- | :---: | :---: | :--- |
| **ESP32 Core Swarm Mesh** | ✅ **COMPLETE** | ESP-NOW (2.4 GHz) | Peer-to-peer 2-byte packet broadcast (`sender_id`, `neighbor_count`) |
| **MFRC522 RFID Reader** | ✅ **COMPLETE** | VSPI (`18, 19, 23, 5, 4`) | Local neighbor token & instrument tag identification |
| **Common-Cathode RGB LED** | ✅ **COMPLETE** | `GPIO 16, 17, 25` | Green on RFID (2s hold); Blue pulses $N$ times then Red in order of neighbors |
| **On-Board Blue LED** | ✅ **COMPLETE** | `GPIO 2` | Solid ON when close to all active neighbors |
| **4x Directional IR Sensors** | ✅ **COMPLETE** | `GPIO 34, 35, 32, 33` | 4-Face physical docking detection (North, East, South, West) |
| **Audio Buzzer Module** | ✅ **COMPLETE** | `GPIO 27` | Melodic boot, RFID chirps, face docking/undocking chimes, consensus alert |
| **9-DoF IMU (MPU9250)** | ✅ **INTEGRATED** | I2C (`SDA=21, SCL=22`) | 3-axis accel, 3-axis gyro, AK8963 3-axis compass (Pitch, Roll, Yaw heading) |
| **ERM Vibration Motor** | ✅ **INTEGRATED** | `GPIO 26` | Tactile haptic feedback on RFID reads, IR face docks, and proximity consensus |

---

## 1. Current Progress: Completed Baseline

### A. ESP32 Dev Module (30-Pin Compatible)
All assigned pins are strictly **$\le 35$**, preventing any issues on 30-pin ESP32 boards where pins 36–39 (VP/VN) are missing or non-configurable.

### B. MFRC522 RFID Reader (SPI / VSPI)
- **Connections:**
  - `SCK` ➔ `GPIO 18` (D18)
  - `MISO` ➔ `GPIO 19` (D19)
  - `MOSI` ➔ `GPIO 23` (D23)
  - `SDA / SS` ➔ `GPIO 5` (D5)
  - `RST` ➔ `GPIO 4` (D4)
  - `3.3V` ➔ `3V3` (*Warning: Never connect to 5V*)
  - `GND` ➔ `GND`
- **Behavior:** On card detection, sounds double-chirp, triggers haptic pulse, and holds RGB LED solid **Green** for 2.0s.

### C. Common-Cathode RGB LED & On-Board LED
- **Connections:**
  - `Red Anode` ➔ `GPIO 16` (via 220Ω resistor)
  - `Green Anode` ➔ `GPIO 17` (via 220Ω resistor)
  - `Blue Anode` ➔ `GPIO 25` (via 220Ω resistor)
  - `Common Cathode` ➔ `GND`
  - `On-Board LED` ➔ Built-in `GPIO 2`
- **Behavior:**
  - Standalone (0 peers): Solid **Red**.
  - Neighbor sequencing: Pulses **Blue** $N$ times (where $N$ = neighbor count) followed by a **Red** pulse.
  - Proximity consensus: Solid **Blue** on RGB LED + On-Board LED turns **Solid ON**.

### D. 4 Directional IR Obstacle Sensors (Face Docking)
- **Connections:**
  - **North Face:** OUT ➔ `GPIO 34` (D34)
  - **East Face:** OUT ➔ `GPIO 35` (D35)
  - **South Face:** OUT ➔ `GPIO 32` (D32)
  - **West Face:** OUT ➔ `GPIO 33` (D33)
  - Power: `3V3` and `GND` (shared bus)
- **Behavior:** Active-LOW detection when another brick face touches (2–4 cm range). Triggers ascending dock chime + haptic click on dock; alert tone on undock.

### E. Audio Buzzer Module
- **Connections:**
  - `I/O` or `Positive (+)` ➔ `GPIO 27` (D27)
  - `GND` / `Negative (-)` ➔ `GND`
  - (If 3-pin module: `VCC` ➔ `3V3`)
- **Behavior:** Acoustic tone sequences for boot, RFID read confirmation, IR face dock/undock, and consensus.

---

## 2. New Hardware Integration (Todo Right Now)

### A. MPU9250 9-DoF MotionTracking Module (10-Pin Breakout)

The MPU9250 integrates two silicon dies: an **MPU-6500** (3-axis accelerometer + 3-axis gyroscope) at I2C address `0x68`, and an **AK8963** (3-axis magnetometer) at I2C address `0x0C`.

#### Complete 10-Pin Connection Guide:

| Pin # | MPU9250 Pin | ESP32 Connection | Function & Wiring Instructions |
| :---: | :--- | :--- | :--- |
| **1** | **VCC** | `3V3` | Power supply (3.3V) |
| **2** | **GND** | `GND` | Common Ground |
| **3** | **SCL** | `GPIO 22` (`D22`) | I2C Serial Clock |
| **4** | **SDA** | `GPIO 21` (`D21`) | I2C Serial Data |
| **5** | **EDA** | *Leave NC* | Auxiliary I2C Data (Not connected) |
| **6** | **ECL** | *Leave NC* | Auxiliary I2C Clock (Not connected) |
| **7** | **AD0** | `GND` | **I2C Address Select**: Pull to GND for address `0x68` (If 3.3V, address is `0x69`) |
| **8** | **INT** | *Leave NC* (or `D14`) | Interrupt output (Firmware uses non-blocking 10Hz I2C polling) |
| **9** | **NCS** | **`3V3`** | **CRITICAL: Chip Select**. Connect to **3.3V** to force I2C mode! *(If grounded, MPU enters SPI mode)* |
| **10** | **FSYNC** | `GND` | Frame Synchronization digital input (Tie to GND) |

> [!IMPORTANT]
> **NCS Pin Wiring**: On GY-9250 / MPU9250 10-pin breakout boards, **Pin 9 (NCS)** MUST be connected to **`3V3`**. If NCS is left floating or tied to GND, the MPU9250 boots in SPI mode and will NOT respond on the I2C bus!

#### Software Implementation:
1. Standard `<Wire.h>` driver with zero external library bloat.
2. Wakes up MPU-6500 by clearing `PWR_MGMT_1` (register `0x6B`).
3. Enables **I2C Bypass Mode** via `INT_PIN_CFG` (register `0x37 = 0x02`), granting the ESP32 direct access to the internal AK8963 magnetometer on the same I2C lines (`GPIO 21, 22`).
4. Configures AK8963 for continuous 16-bit 100Hz mode (`0x0A = 0x16`).
5. Real-time mathematical orientation pipeline:
   - **Pitch:** $\arctan2(A_y, \sqrt{A_x^2 + A_z^2}) \cdot \frac{180}{\pi}$
   - **Roll:** $\arctan2(-A_x, A_z) \cdot \frac{180}{\pi}$
   - **Compass Heading (Yaw):** $\arctan2(M_y, M_x) \cdot \frac{180}{\pi}$ (normalized to $0^\circ \dots 360^\circ$ relative to Magnetic North).

---

### B. ERM Vibration Motor (2 Wires: + and -)
(ignore for now)
An **Eccentric Rotating Mass (ERM)** motor creates tactile vibration feedback. DC vibration coin/cylinder motors draw **60mA to 90mA** at 3V. Because an ESP32 GPIO pin is rated for a maximum of **20mA to 40mA**, the motor **must be driven through a transistor or driver module** to avoid burning the microcontroller pin and protect against inductive back-EMF voltage spikes.

#### Method 1: Discrete Transistor Circuit (Standard 2-Wire Motor)

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

#### Connection Table (Method 1):
| Component Lead | Connects To | Notes |
| :--- | :--- | :--- |
| **Motor Positive (+) [Red Wire]** | `3V3` (or `VIN`) | Motor power rail |
| **Motor Negative (-) [Blue/Black Wire]** | Transistor **Collector (C)** | Switched ground |
| **Flyback Diode (1N4148 / 1N4001)** | Across Motor (+) and (-) | **Cathode** (stripe) to (+), **Anode** to (-) |
| **Transistor Base (B)** | `GPIO 26` (`D26`) via **1kΩ** resistor | Current limiting into base |
| **Transistor Emitter (E)** | `GND` | Common ground |
| *(Optional) 10kΩ Pull-Down* | From Base to GND | Ensures motor stays off during ESP32 boot |

#### Method 2: Using a 3-Pin Vibration Motor Breakout Module
If you have a 3-pin vibration motor module (which has the transistor and diode built onto the small PCB):
- `VCC` ➔ `3V3` (or `5V`)
- `GND` ➔ `GND`
- `IN` / `SIG` ➔ `GPIO 26` (`D26`)

#### Haptic Sensory Event Mapping:
| Trigger Event | Haptic Vibration Profile | Sensation |
| :--- | :--- | :--- |
| **System Boot** | 1x 50ms pulse | Short boot confirmation click |
| **RFID Card Scanned** | 1x 100ms pulse | Crisp tactile confirmation |
| **IR Face Docked** | 1x 80ms pulse | Physical "snap-in" tactile click |
| **Close-to-All Consensus** | Double-pulse (50ms ON, 40ms OFF, 70ms ON) | Affirmative double-buzz |

---

## 3. Master Hardware Pinout Matrix ($\le 35$)

Every peripheral in the entire Smart Brick design is mapped below:

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

| ESP32 Pin | Board Label | Connected Peripheral & Function | Direction | Electrical Notes |
| :---: | :---: | :--- | :---: | :--- |
| **GPIO 2** | `D2` | Native On-Board Blue LED | Output | Proximity consensus indicator |
| **GPIO 4** | `D4` | MFRC522 RFID Reset (`RST`) | Output | Active-LOW reset |
| **GPIO 5** | `D5` | MFRC522 RFID Slave Select (`SDA / SS`) | Output | SPI Chip Select |
| **GPIO 16** | `D16` / `RX2` | RGB LED — Red Anode | Output | Via 220Ω resistor |
| **GPIO 17** | `D17` / `TX2` | RGB LED — Green Anode | Output | Via 220Ω resistor |
| **GPIO 18** | `D18` | MFRC522 RFID Clock (`SCK`) | Output | VSPI Clock |
| **GPIO 19** | `D19` | MFRC522 RFID Master In (`MISO`) | Input | VSPI Data In |
| **GPIO 21** | `D21` | MPU9250 9-DoF IMU (`SDA`) | Bidirectional | I2C Serial Data |
| **GPIO 22** | `D22` | MPU9250 9-DoF IMU (`SCL`) | Output | I2C Serial Clock |
| **GPIO 23** | `D23` | MFRC522 RFID Master Out (`MOSI`) | Output | VSPI Data Out |
| **GPIO 25** | `D25` | RGB LED — Blue Anode | Output | Via 220Ω resistor |
| **GPIO 26** | `D26` | ERM Vibration Motor Gate/Base Driver | Output | Via 1kΩ base resistor |
| **GPIO 27** | `D27` | Audio Buzzer Module (`I/O` / `SIG`) | Output | Melodic alerts & chimes |
| **GPIO 32** | `D32` | IR Proximity Sensor — SOUTH Face | Input | Active LOW |
| **GPIO 33** | `D33` | IR Proximity Sensor — WEST Face | Input | Active LOW |
| **GPIO 34** | `D34` | IR Proximity Sensor — NORTH Face | Input | Active LOW (Input-only pin) |
| **GPIO 35** | `D35` | IR Proximity Sensor — EAST Face | Input | Active LOW (Input-only pin) |

---

## 4. Expected Console & Hardware Outputs

### A. Power-Up / Boot Initialization
```text
=============================================================
     MULTI-AGENT SMART BRICKS (MICROBOTS) FIRMWARE           
     Node Identity: BRICK #1 of 4                          
     Pin Map: All GPIOs <= 35 (Compatible with 30-Pin ESP32) 
=============================================================
[IR] 4-Face Directional IR Sensors initialized (North:34, East:35, South:32, West:33)
[IMU] MPU9250 found! Device WHO_AM_I: 0x71
[IMU] AK8963 Magnetometer online! ID: 0x48
[RFID] MFRC522 Reader initialized. Hardware version: 0x92
[WIFI] MAC Address: AC:67:B2:1C:45:90
[WIFI] Channel locked to 1
[ESP-NOW] Broadcast peer registered successfully.
[INFO] Brick #1 initialization complete. Running swarm loop...
```
- **Sensory feedback:** 🎵 Melodic 3-tone boot chime on Buzzer + ⚡ 50ms tactile vibration buzz on ERM motor.

---

### B. Steady-State Swarm Operation (Periodic Diagnostics)
```text
-------------------------------------------------------------
BRICK #1 | Active Neighbors: 2 | Close To All: NO | On-Board LED: OFF
IR Faces Docked (0/4): [N:-- E:-- S:-- W:--]
IMU (MPU9250):    Heading: 142.5 deg | Pitch: -1.2 deg | Roll: 2.4 deg [ONLINE]
ERM Motor (D26):  [READY]
RGB LED State:    [BLUEx2 then RED] (Cycling Count Sequence)
  -> Peer #2: RSSI: -74.2 dBm [FAR] | Reports 2 neighbors
  -> Peer #3: RSSI: -76.8 dBm [FAR] | Reports 2 neighbors
-------------------------------------------------------------
```

---

### C. RFID Token Scan Event
```text
========================================
[RFID] Card Detected! UID: 5E6F7A8B
[RFID] Identified Neighbor Brick: #2
[LED] Lighting Green LED (RGB)
[BUZZER] Triggering RFID confirmation beep
[HAPTIC] Triggering ERM tactile pulse
========================================
```
- **Sensory feedback:** 🟢 Solid Green RGB LED for 2.0s + 🔔 Double-chirp + ⚡ 100ms tactile haptic click.

---

### D. Physical Face Docking (Directional IR)
```text
[IR] >>> FACE DOCKED: NORTH face detected adjacent brick! <<<
```
- **Sensory feedback:** 🔔 Ascending docking chime + ⚡ 80ms tactile click.

---

### E. Proximity Consensus (All Neighbors Close)
```text
-------------------------------------------------------------
BRICK #1 | Active Neighbors: 2 | Close To All: YES | On-Board LED: ON
IR Faces Docked (2/4): [N:YES E:YES S:-- W:--]
IMU (MPU9250):    Heading: 142.5 deg | Pitch: -0.8 deg | Roll: 0.2 deg [ONLINE]
ERM Motor (D26):  [READY]
RGB LED State:    [SOLID BLUE] (All Neighbors Close)
  -> Peer #2: RSSI: -54.1 dBm [CLOSE] | Reports 2 neighbors
  -> Peer #3: RSSI: -51.3 dBm [CLOSE] | Reports 2 neighbors
-------------------------------------------------------------
```
- **Sensory feedback:** 💡 On-Board Blue LED turns **Solid ON** + 🔵 RGB LED turns **Solid Blue** + 🔔 Consensus chime + ⚡ Double-haptic pulse.

---

## 5. Verification & Swarm Simulation Test Suite Status

The automated software test suite ([`Code/tests/test_brick_logic.py`](Code/tests/test_brick_logic.py)) and high-fidelity multi-node temporal simulator ([`Code/tests/simulate_swarm.py`](Code/tests/simulate_swarm.py)) validate all logic points:

```text
==================================================================
  RUNNING UNIFIED MICROBOT (SMART BRICK) VERIFICATION TEST SUITE  
==================================================================
--- TEST 1: Minimal Packet Protocol Verification               >> [PASS]
--- TEST 2: RFID Read Green LED, Buzzer & Haptic Pulse         >> [PASS]
--- TEST 3: Proximity to All, On-Board LED, Chime & Haptics    >> [PASS]
--- TEST 4: RGB LED Sequenced in Order of Neighbor Count       >> [PASS]
--- TEST 5: 4-Face Directional IR Sensors, Chime & Dock Haptics >> [PASS]
--- TEST 6: Silence Timeout & Peer Eviction                     >> [PASS]
--- TEST 7: 9-DoF IMU Orientation & Magnetometer Heading       >> [PASS]
==================================================================
  ALL UNIFIED FIRMWARE SPECIFICATIONS PASSED (7/7)               
==================================================================

================================================================================
  HIGH-FIDELITY 4-BRICK SWARM TIME-SERIES SIMULATION (simulate_swarm.py)
================================================================================
>>> Phase 0: Standalone Boot (t=0-500ms)                         >> [PASS]
>>> Phase 1: Two Bricks in Range (Far, -80 dBm, t=500-2000ms)   >> [PASS]
>>> Phase 2: All 4 Bricks in Range (Far, -75 dBm, t=2-4s)       >> [PASS]
>>> Phase 3: All 4 Bricks Move CLOSE (-50 dBm) -> Consensus!    >> [PASS]
>>> Phase 4: Physical IR Face Docking (East-West Snap)          >> [PASS]
>>> Phase 5: RFID Tag Scanning (Green LED + 2.0s Hold)          >> [PASS]
>>> Phase 6: 9-DoF IMU Heading (90 deg) & Pitch (30 deg)        >> [PASS]
>>> Phase 7: Peer Timeout Eviction after 2.5s Silence            >> [PASS]
================================================================================
  SWARM SIMULATION COMPLETED: 100% SPECIFICATIONS PASSED (7/7)   
================================================================================
```

### Bug & Consistency Fixes Applied to `brick.ino`:
1. **I2C Read Sequence Safety**: Replaced unsequenced `(Wire.read() << 8) | Wire.read()` calls in `readMpu9250()` with explicit sequential buffer reads to eliminate undefined C++ operand evaluation order that could swap high/low bytes on certain compilers.
2. **AK8963 Magnetometer Bypass Protection**: Added `i2cWriteByte(MPU9250_I2C_ADDR, 0x6A, 0x00)` to ensure `USER_CTRL` has I2C Master mode disabled before asserting `BYPASS_EN` in `INT_PIN_CFG` (register `0x37 = 0x02`), guaranteeing the ESP32 can communicate with the AK8963 at address `0x0C`.
3. **ESP-NOW Null Guard**: Added `info && info->rx_ctrl` pointer validation in `onDataRecv()`.

---

## 6. Git Tracking Status
- Per user directive, `Progress.MD` has been added to [`.gitignore`](.gitignore) and is excluded from git commits.
