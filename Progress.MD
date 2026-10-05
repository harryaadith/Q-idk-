## IR-only possible layout

The dashboard now ignores gyro readiness and measured directions when positioning bricks. It infers rotations from local IR masks and displays the first possible connected-grid layout automatically. Every two-brick single-face combination is tested, including N/S with unavailable gyro. Drawing directions are arbitrary; physical face identity remains unresolved.

## Automatic dashboard layout

The dashboard automatically displays the first deterministic fitting configuration, without candidate selection or confirmation. Multiple fits remain labelled uncertain. Unresolved bricks are drawn separately with dashed outlines and position-unknown labels; display spacing is not a physical arrangement. Updated embedded firmware assets and dashboard regression checks.

## Hardware-validated gyro calibration & tracking robustness update

- **Factory Zero-Rate Bias Accommodation:** Relaxed stationary gyro rate threshold during calibration from `< 3 deg/s` to `< 20 deg/s` (`std::fabs(gx) < 20 && std::fabs(gy) < 20 && std::fabs(gz) < 20`). MPU-6500 datasheet room-temperature Zero-Rate Output (ZRO) tolerance is typ $\pm 5^\circ/\text{s}$, max $\pm 20^\circ/\text{s}$. Verified against real hardware serial telemetry exhibiting resting offsets up to $+7.21^\circ/\text{s}$ ($gx$), $+4.94^\circ/\text{s}$ ($gy$), and $+0.82^\circ/\text{s}$ ($gz$).
- **Electrical & RF Noise Margin:** Relaxed calibration gyro variance threshold from $0.0225\text{ (deg/s)}^2$ ($\sigma = 0.15^\circ/\text{s}$) to $0.36\text{ (deg/s)}^2$ ($\sigma = 0.60^\circ/\text{s}$), and accelerometer variance from $0.0025\text{ g}^2$ to $0.01\text{ g}^2$, tolerating ESP32 Wi-Fi/ESP-NOW RF power surges and breadboard supply ripple without resetting the 3-second calibration timer.
- **Gravity Magnitude Range:** Widened from $[0.85, 1.15]\text{ g}$ to $[0.75, 1.25]\text{ g}$, accommodating uncalibrated factory accelerometer scale tolerances and resting gravity vectors ($1.098\text{ g}$ on test hardware).
- **Wi-Fi & HTTP Latency Tolerance:** Increased tracking gap timeout from $500\text{ ms}$ to $2500\text{ ms}$. Web telemetry polling (`/api/state`) and serial debug dumps can briefly pause `loop()` for 300–800 ms; the wider 2.5s window prevents transient Wi-Fi/HTTP delays from falsely triggering `TrackingLost`.
- **Handling & Tilt Tolerance:** Relaxed tracking tilt limit from $20^\circ$ ($\cos 20^\circ = 0.9397$) to $45^\circ$ ($\cos 45^\circ = 0.7071$). Bricks can be picked up, slid, or handled on the tray up to $45^\circ$ tilt without losing tracking reference.
- **Regression Test Suite:** Updated `Code/tests/test_planar_yaw.py` with the 2.5s gap threshold, 45° tilt limit, and explicit real-hardware bias test vector ($gx=7.214, gy=4.5, gz=0.7, ax=-0.133, ay=0.025, az=1.092$). All unit tests pass.

## Firmware review fixes

Replaced blocking buzzer/motor delays with bounded nonblocking patterns, preventing simultaneous docking events from causing gyro sample gaps. Validated optional magnetic identity/setup, accepted RC522 v1 and v2 diagnostic identities, treated IR readings as occupancy rather than identity failures, and restored brownout protection. GPIO assignments remain unchanged. Added feedback and IMU initialization regression checks; full ESP32 build/hardware validation remains outstanding.

## Gyro mounting tolerance update

Calibration now learns the gravity direction and biases on all three gyro axes, supporting tilted and sideways IMU mounts. Relative yaw uses the corrected angular rate projected onto gravity. The stationary window remains three continuous seconds; tracking tolerates up to 45° of tilt from the calibrated pose. Reflash every brick to use this change. This supersedes historical Z-axis-only calibration descriptions below.

# Modular Smart Surgery Tray (Microbots / Smart Bricks) — Universal Progress & Implementation Tracker

**Project:** Autonomous Multi-Agent Smart Bricks (Smart Surgery Tray)  
**Team:** Team 22 (*(Q)idk if i'm getting an internship*) — Electronic System Workshop (ESW)  
**Production Firmware:** [`Code/brick/brick.ino`](Code/brick/brick.ino) *(Single universal firmware flashed to all boards)*  
**Target Platform:** ESP32 Dev Module (Compatible with 30-Pin and 38-Pin form factors; all GPIOs $\le 35$)  
**Operating Guide:** [`HOW_TO_RUN.md`](HOW_TO_RUN.md)

---

## Current revision: magnetometer-independent 2D layout

- MPU6500 (WHO_AM_I 0x70) and MPU9250 (0x71) are accepted for accel/gyro operation. A magnetometer is optional.
- Flat, stationary aligned startup calibrates gyro bias over three seconds. Relative yaw is integrated from gyro Z; it drifts and is not magnetic north.
- The dashboard solves connected square-grid configurations from 90-degree relative rotations and complementary occupied faces. It displays alternative identity layouts and supports local operator confirmation rather than claiming ambiguous data is unique.
- The group realignment button calls `/api/align`; peers receive repeated alignment commands over ESP-NOW. All physical N marks must be aligned first, and every card must become ready.
- Gaps over 2.5 s, tilt beyond 45° or gyro saturation invalidate tracking. The solver rejects stale, off-grid and contradictory inputs. The packet format is TRA2; every board must be reflashed.
- Host tests cover gyro bias/integration, tracking loss, rotated grid, strip ambiguity, independent exhaustive layout checks, SVG rendering and confirmation invalidation. Full board compilation and physical testing remain pending.
- The entries below are an earlier implementation snapshot. Their magnetic-heading and persistent-compass-calibration descriptions are superseded by this revision; consult HOW_TO_RUN.md for current operation.

---

## 📊 Subsystem Status Dashboard

| Subsystem / Peripheral | Status | Bus / Pin(s) | Operational Function |
| :--- | :---: | :---: | :--- |
| **ESP32 Swarm Mesh (ESP-NOW)** | ✅ **OPERATIONAL** | Wi-Fi Ch 1 (2.4 GHz) | Peer-to-peer telemetry broadcast; decentralized neighbor tracking |
| **Dynamic Gateway & Hotspot** | ✅ **OPERATIONAL** | SoftAP `SmartSurgeryTray` | Node with lowest hardware UID auto-elects as AP gateway (`192.168.4.1`) |
| **Embedded Web Dashboard** | ✅ **OPERATIONAL** | HTTP Port 80 (`/api/state`) | Real-time browser UI for live brick cards, heading, IR docking, and RFID tags |
| **MFRC522 RFID Top Reader** | ✅ **OPERATIONAL** | VSPI (`SCK:18, MISO:19, MOSI:23, SS:5, RST:4`) | Top-facing surgical instrument identification & tag tracking |
| **4x Directional IR Sensors** | ✅ **OPERATIONAL** | GPIO `34, 35, 32, 33` | 4-Face physical docking & edge occupancy detection (N, E, S, W) |
| **Common-Cathode RGB LED** | ✅ **OPERATIONAL** | GPIO `16, 17, 25` | Green on RFID scan (2s hold); Blue pulses $N$ times then Red; Solid Blue when close |
| **On-Board Status LED** | ✅ **OPERATIONAL** | GPIO `2` | Solid ON upon proximity consensus (all active neighbors close) |
| **Audio Buzzer Module** | ✅ **OPERATIONAL** | GPIO `27` | Melodic boot chime, RFID chirps, face docking/undocking tones, consensus alert |
| **ERM Vibration Motor** | ✅ **OPERATIONAL** | GPIO `26` (via NPN driver) | Haptic pulse on RFID read, physical face dock snap, consensus double-buzz |
| **9-DoF IMU (MPU9250 / AK8963)** | ✅ **OPERATIONAL** | I2C (`SDA:21, SCL:22`) | 3-axis accel/gyro + AK8963 3-axis compass (Pitch, Roll, Magnetic Heading) |
| **Serial Debug CLI & WHO_AM_I** | ✅ **OPERATIONAL** | Serial UART (115200 baud) | Interactive runtime CLI: `--debug`, `--whoami`, `--kill-whoami`, `--help` |

---

## 1. Web Dashboard Subsystem (`Code/Dashboard`)

The tray includes an integrated web telemetry dashboard that operates completely offline without external routers, internet access, Node.js servers, or cloud dependencies.

### A. How the Dashboard Works
1. **Dynamic Gateway Election:** All bricks communicate over ESP-NOW. The brick with the lowest unique 64-bit hardware MAC/UID automatically elects itself as the Wi-Fi Gateway.
2. **Access Point:** The gateway starts a Wi-Fi Access Point:
   - **SSID:** `SmartSurgeryTray`
   - **Password:** `smarttray22`
   - **IP Address:** `http://192.168.4.1`
3. **Embedded Web Assets:** The HTML and JavaScript files in [`Code/Dashboard/`](Code/Dashboard/) (`dashboard.html` and `dashboard.js`) are converted into C++ raw string literals in [`Code/brick/DashboardPage.h`](Code/brick/DashboardPage.h) using [`Code/Dashboard/embed.py`](Code/Dashboard/embed.py), allowing the ESP32's internal flash to serve them directly via its asynchronous `WebServer`.
4. **Telemetry API (`/api/state`):** The browser polls `http://192.168.4.1/api/state` every 500ms. The gateway aggregates its own sensor data and remote telemetry packets received from peer bricks over ESP-NOW.

### B. Instructions on How to Use the Dashboard
1. **Power on the Bricks:** Power on all assembled bricks. Wait 5–10 seconds for ESP-NOW discovery and gateway election to settle.
2. **Connect Laptop/Phone Wi-Fi:**
   - Scan for Wi-Fi networks on your laptop or smartphone.
   - Connect to `SmartSurgeryTray` using password `smarttray22`.
3. **Open the Dashboard in Browser:**
   - Open any standard browser (Chrome, Firefox, Safari, Edge).
   - Navigate to: **`http://192.168.4.1`**
4. **Live Telemetry Display:**
   - **Connection Banner:** Displays connection status and total count of online bricks.
   - **Individual Brick Cards:**
     - **Brick Number & Gateway Badge:** Displays assigned logical number and indicates which brick is acting as the Wi-Fi gateway.
     - **Hardware UID:** Unique 64-bit hex hardware identifier.
     - **Rotating Compass Visualizer:** Displays the brick's orientation relative to Magnetic North.
     - **Occupied Faces:** Highlights in orange any face where an adjacent brick or obstacle is detected by the directional IR sensors (N, E, S, W).
     - **Last Instrument UID:** Displays the RFID tag UID of the most recently scanned surgical instrument.
     - **Scan Status:** Indicates "Just scanned", "No recent scan", or "Offline / stale".
5. **Compass Calibration via Web UI:**
   - Click the **"Calibrate gateway compass"** button on the dashboard.
   - While calibration is active, rotate the gateway brick in a flat figure-8 motion for 15 seconds to calibrate the AK8963 magnetometer offsets.
   - Calibration parameters are saved persistently to ESP32 non-volatile storage (`Preferences`).

### C. Regenerating Dashboard Assets (If Modifying Web UI)
If you make changes to `Code/Dashboard/dashboard.html` or `Code/Dashboard/dashboard.js`, regenerate the firmware header before compiling:
```bash
python3 Code/Dashboard/embed.py
```
This regenerates [`Code/brick/DashboardPage.h`](Code/brick/DashboardPage.h).

---

## 2. Interactive Serial Debug CLI & Sensor WHO_AM_I Verification

The firmware includes a runtime serial command processor (`Code/brick/DebugCommand.h`) that allows real-time diagnostics and hardware validation over Serial Monitor at **115200 baud** (with Newline enabled).

### A. Available Serial Commands

| Command Flag | Description |
| :--- | :--- |
| **`--whoami`** | Performs a full hardware register audit on all sensors (MPU9250, AK8963, RFID, 4x IR). Compares readings against hardcoded expected values and outputs Boolean matches. Runs every 1000ms until killed. |
| **`--kill-whoami`** | Immediately terminates the periodic WHO_AM_I verification stream. |
| **`--debug`** | Enables continuous high-speed sensor telemetry streaming every 500ms (raw/converted accel, gyro, mag, IR pins, RFID age, peer RSSI). |
| **`--no-debug`** / **`--debug-off`** | Disables high-speed telemetry streaming; returns to normal 2-second summary logs. |
| **`--debug-i2c`** | Executes a one-shot I2C bus scan (probes `0x68`, `0x69`, `0x0C`) and dumps critical register states. |
| **`--debug-reinit`** | Explicitly retries I2C sensor initialization without needing to reboot the microcontroller. |
| **`--help`** | Displays the complete interactive serial command reference menu. |

### B. Sensor WHO_AM_I Register Specifications & Bootleg Detection

The `--whoami` command verifies the physical hardware identity of every connected sensor:

| Sensor Subsystem | Bus / Pin | Register / Read | Expected Value | Purpose / Bootleg Diagnostic |
| :--- | :---: | :---: | :---: | :--- |
| **MPU9250 (Accel/Gyro)** | I2C `0x68` | Register `0x75` (`WHO_AM_I`) | **`0x71`** | **Genuine MPU9250 returns `0x71`.** If `0x70` is received, the module contains an **MPU-6500** silicon die (common counterfeit/bootleg lacking a functional AK8963 magnetometer). If `0xFF` or `0x00`, verify SDA/SCL and ensure NCS is tied to 3.3V. |
| **AK8963 Magnetometer** | I2C `0x0C` | Register `0x00` (`WIA`) | **`0x48`** | Device identification for the AK8963 compass die. Must respond with `0x48` when MPU I2C bypass mode is active (`INT_PIN_CFG = 0x02`). |
| **RFID MFRC522** | VSPI | Register `0x37` (`VersionReg`) | **`0x92`** | Standard NXP MFRC522 v2.0 silicon returns `0x92` (v1.0 returns `0x91`). `0x00` or `0xFF` indicates SPI wiring failure. |
| **IR North Face** | GPIO `34` | Digital Read | **`1` (HIGH)** | Active-LOW LM393 sensor. Reads `1` when unobstructed in open air; reads `0` when docked/blocked. |
| **IR East Face** | GPIO `35` | Digital Read | **`1` (HIGH)** | Active-LOW LM393 sensor. Reads `1` when unobstructed in open air; reads `0` when docked/blocked. |
| **IR South Face** | GPIO `32` | Digital Read | **`1` (HIGH)** | Active-LOW LM393 sensor. Reads `1` when unobstructed in open air; reads `0` when docked/blocked. |
| **IR West Face** | GPIO `33` | Digital Read | **`1` (HIGH)** | Active-LOW LM393 sensor. Reads `1` when unobstructed in open air; reads `0` when docked/blocked. |

### C. Sample `--whoami` Serial Output
```text
==================== SENSOR WHO_AM_I VERIFICATION (UID: 000000000123) ====================
[WHOAMI] MPU9250 (0x68 Reg 0x75)     : Expected=0x71, Received=0x71 -> Match: TRUE (Boolean: 1)
[WHOAMI] AK8963 Mag (0x0C Reg 0x00)  : Expected=0x48, Received=0x48 -> Match: TRUE (Boolean: 1)
[WHOAMI] RFID MFRC522 (Reg 0x37)     : Expected=0x92, Received=0x92 -> Match: TRUE (Boolean: 1)
[WHOAMI] IR NORTH (GPIO 34)          : Expected=1, Received=1 -> Match: TRUE (Boolean: 1)
[WHOAMI] IR EAST (GPIO 35)           : Expected=1, Received=1 -> Match: TRUE (Boolean: 1)
[WHOAMI] IR SOUTH (GPIO 32)          : Expected=1, Received=1 -> Match: TRUE (Boolean: 1)
[WHOAMI] IR WEST (GPIO 33)           : Expected=1, Received=1 -> Match: TRUE (Boolean: 1)
---------------------------------------------------------------------------------
[WHOAMI] OVERALL STATUS: ALL_MATCH (Boolean: 1)
=================================================================================
```

---

## 3. Hardware Connections & Pinout Matrix (All Pins $\le 35$)

Every brick uses the exact same wiring scheme, fully compatible with both 30-pin and 38-pin ESP32 boards:

```text
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

| ESP32 Pin | Label | Target Peripheral Pin | Signal Function | Electrical Notes |
| :---: | :---: | :--- | :---: | :--- |
| **GPIO 2** | `D2` | Built-in Blue LED | Output | On-Board LED; solid ON when close to all active neighbors |
| **GPIO 4** | `D4` | MFRC522 `RST` | Output | Active-LOW hardware reset for RFID reader |
| **GPIO 5** | `D5` | MFRC522 `SDA / SS` | Output | SPI Slave Select (Chip Select) |
| **GPIO 16** | `D16` | RGB Red Anode | Output | Driven through 220Ω–330Ω current-limiting resistor |
| **GPIO 17** | `D17` | RGB Green Anode | Output | Driven through 220Ω–330Ω current-limiting resistor |
| **GPIO 18** | `D18` | MFRC522 `SCK` | Output | VSPI Serial Clock |
| **GPIO 19** | `D19` | MFRC522 `MISO` | Input | VSPI Master In / Slave Out |
| **GPIO 21** | `D21` | MPU9250 `SDA` | Bidirectional | I2C Serial Data line |
| **GPIO 22** | `D22` | MPU9250 `SCL` | Output | I2C Serial Clock line |
| **GPIO 23** | `D23` | MFRC522 `MOSI` | Output | VSPI Master Out / Slave In |
| **GPIO 25** | `D25` | RGB Blue Anode | Output | Driven through 220Ω–330Ω current-limiting resistor |
| **GPIO 26** | `D26` | ERM Motor Driver | Output | Driven through 1kΩ resistor to NPN base / MOSFET gate |
| **GPIO 27** | `D27` | Audio Buzzer `SIG` | Output | Acoustic sound alerts and feedback |
| **GPIO 32** | `D32` | IR SOUTH Face `OUT` | Input | Active-LOW digital comparator signal |
| **GPIO 33** | `D33` | IR WEST Face `OUT` | Input | Active-LOW digital comparator signal |
| **GPIO 34** | `D34` | IR NORTH Face `OUT` | Input | Active-LOW digital comparator signal (Input-only pin) |
| **GPIO 35** | `D35` | IR EAST Face `OUT` | Input | Active-LOW digital comparator signal (Input-only pin) |

### Important Peripheral Wiring Notes:
- **MPU9250 Pin 9 (NCS):** MUST be connected to **`3V3`**. Tying NCS to 3.3V forces the chip into I2C mode. If left floating or grounded, it defaults to SPI and will not respond on I2C.
- **MPU9250 AD0 & FSYNC:** Tie both to **`GND`** (sets primary I2C address `0x68`).
- **MFRC522 Power:** Connect VCC strictly to **`3V3`**. Connecting 5V will destroy the MFRC522 chip.
- **ERM Vibration Motor:** Do not connect directly to a GPIO pin. Drive via an NPN transistor (2N2222/BC547) or MOSFET with a 1N4148/1N4001 flyback diode across the motor terminals.

---

## 4. Sensory Feedback & State Indicator Matrix

| State / Trigger Event | On-Board LED (GPIO 2) | RGB LED (GPIO 16, 17, 25) | Audio Buzzer (GPIO 27) | ERM Haptic Motor (GPIO 26) |
| :--- | :---: | :---: | :---: | :---: |
| **System Boot / Power-On** | OFF | Solid RED | 🎵 Melodic 3-tone boot chime | ⚡ 1x 50ms confirmation pulse |
| **Standalone (0 Neighbors)** | OFF | Solid RED | Silent | Silent |
| **Mesh Active (N Neighbors, Far)** | OFF | Pulses BLUE $N$ times, then RED | Silent | Silent |
| **Proximity Consensus (All Close)**| **Solid ON** | **Solid BLUE** | 🔔 Ascending chime | ⚡ Double-pulse (50ms ON, 40ms OFF, 70ms ON) |
| **RFID Instrument Token Scanned** | Unchanged | **Solid GREEN (2.0s hold)** | 🔔 Double-chirp | ⚡ 1x 100ms tactile click |
| **Directional IR Face Docked** | Unchanged | Unchanged | 🔔 Dock chime | ⚡ 1x 80ms docking snap pulse |
| **Directional IR Face Cleared** | Unchanged | Unchanged | 🔔 Short undock alert | Silent |

---

## 5. Host Verification & Testing Suite

All firmware logic, state machines, serial parsers, and command interfaces are verified by automated host tests:

```bash
python3 Code/tests/test_sensor_debug.py
```
**Verification Scope:**
- Runtime command parsing (`--debug`, `--no-debug`, `--debug-i2c`, `--debug-reinit`, `--whoami`, `--kill-whoami`, `--help`)
- Buffer overflow recovery and split CRLF line handling
- Sensor WHO_AM_I comparison and Boolean return validation
- Cached vs stale telemetry distinguishing (`sample_age_ms`)
- AK8963 calibration state tracking and heading validation

All tests pass cleanly under C++17 compilation (`-Wall -Wextra -Werror`).
