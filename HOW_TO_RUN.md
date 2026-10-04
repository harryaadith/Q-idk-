# HOW TO RUN — Smart Surgery Tray

This is the complete entry point for setting up the current project from an unconfigured laptop and wired ESP32 boards through normal wireless operation. Use the same firmware on every brick. There is no per-brick source edit, laptop serial connection during normal operation, router, cloud service, or separate dashboard server.

**Current validation status:** host checks have passed for dynamic identities, gateway election and dashboard rendering. A full ESP32 board build and hardware operation have not yet been verified. The first compilation and the hardware acceptance checks below are required before treating this setup as working.

## Contents

1. [What you will get](#1-what-you-will-get)
2. [Parts and software](#2-parts-and-software)
3. [Find the project and the correct files](#3-find-the-project-and-the-correct-files)
4. [Wire each brick](#4-wire-each-brick)
5. [Install the Arduino environment](#5-install-the-arduino-environment)
6. [Generate and compile](#6-generate-and-compile)
7. [Upload to every brick](#7-upload-to-every-brick)
8. [Check and calibrate each brick](#8-check-and-calibrate-each-brick)
9. [Start the complete tray](#9-start-the-complete-tray)
10. [Understand the dashboard](#10-understand-the-dashboard)
11. [Run acceptance checks](#11-run-acceptance-checks)
12. [Troubleshooting](#12-troubleshooting)
    - [Serial sensor debug mode](#serial-sensor-debug-mode)
    - [IMU/magnetometer diagnostic checklist](#imumagnetometer-diagnostic-checklist)
13. [Edit and update the project](#13-edit-and-update-the-project)
14. [Optional command-line build and tests](#14-optional-command-line-build-and-tests)
15. [Shutdown, restart and known limitations](#15-shutdown-restart-and-known-limitations)

## 1. What you will get

The intended application is a modular surgery-tray prototype: independently powered bricks lie flat and come together to form a tray. RFID readers point upward toward tagged instruments; four IR obstacle sensors point out through the side faces.

Each brick broadcasts telemetry over ESP-NOW on Wi-Fi channel 1. After discovery settles, the brick with the lowest live hardware identity hosts a Wi-Fi access point and serves the dashboard. Your laptop connects to that host, which reports its own data and the other bricks it can directly hear. The browser polls telemetry every 500 ms.

The current dashboard shows brick identities, automatic display numbers, occupied faces, calibrated magnetic heading, and the last scanned RFID UID. **It does not reconstruct the actual physical tray arrangement.** IR sensors detect objects but cannot identify the adjacent brick. Heading can constrain possible connections without resolving every pairing. The individual diagram positions on screen are not physical coordinates.

## 2. Parts and software

For the intended four-brick setup, obtain four classic ESP32 Dev Modules/DevKit boards, with their own suitable regulated power supplies. This sketch targets the original ESP32; ESP32-C3/S2/S3 or different pin maps need a separate compatibility check.

Per brick:

- One MFRC522/RC522 RFID reader and suitable RFID instrument tags.
- Four 3.3 V compatible, active-low digital IR obstacle sensors.
- One MPU9250 breakout including its AK8963 magnetometer. A different MPU device does not provide equivalent magnetometer support.
- One common-cathode RGB LED and three 220–330 ohm resistors.
- Buzzer module with a 3.3 V compatible signal input, or an appropriate driver for the buzzer used.
- ERM vibration motor with a transistor/MOSFET driver and flyback diode, if haptics are installed.
- Wiring, a stable mount/enclosure, and a shared ground within each brick.

For the laptop:

- Arduino IDE 2, ESP32 board support, MFRC522 library, a data-capable USB cable, and Wi-Fi.
- A browser with JavaScript enabled. Web Serial support is not required.
- Internet during initial software installation; Internet is unnecessary during tray operation.
- Python 3 only if regenerating dashboard assets or running Python tests. Node.js and a C++ compiler are only needed for host tests, not the live dashboard.

Power the ESP32 through the development board's documented USB/VIN input or a correctly regulated input appropriate to that board. Do not connect a raw battery to the 3V3 pin. Use your board and peripheral ratings when choosing the supply; radio, buzzer and motor current peaks must be supported. Disconnect power before rewiring. Sensor signals entering the ESP32 must be 3.3 V compatible.

## 3. Find the project and the correct files

On this system:

```bash
cd /home/harry/programming/ESW/esw-m26-22_q-idk-if-i-m-getting-an-internship
git branch --show-current
```

The expected branch is `main`. This guide does not require editing or checking out `Bricks_Changes`. On another machine, open your local copy of this repository and run subsequent terminal commands from its root.

Use these files:

| File | Purpose |
| --- | --- |
| `Code/brick/brick.ino` | The single firmware sketch to upload to every board |
| `Code/brick/DebugCommand.h` | Serial debug command parser; keep beside the sketch |
| `Code/brick/DashboardPage.h` | Generated web assets; must stay beside the sketch |
| `Code/Dashboard/dashboard.html` | Editable dashboard page and styles |
| `Code/Dashboard/dashboard.js` | Editable dashboard behavior |
| `Code/Dashboard/embed.py` | Generates the firmware's web-assets header |
| `Code/tests/` | Host checks and historical simulations |

Only `Code/brick/brick.ino` is a production upload target. Obsolete numbered sketches, prototypes and old protocol simulations have been removed from this checkout; earlier versions remain in Git history.

## 4. Wire each brick

Repeat the same wiring on every board. N/E/S/W are **brick-local side labels**, not permanent directions on the table. Mark the local N face on each enclosure and mount all IMUs with a consistent axis orientation relative to that mark. Keep the RFID antenna on top, away from the side docking faces.

| Device pin / function | ESP32 connection |
| --- | --- |
| RC522 VCC, GND | 3V3, GND |
| RC522 SCK, MISO, MOSI | GPIO 18, 19, 23 respectively |
| RC522 SDA/SS, RST | GPIO 5, 4 respectively |
| RGB red, green, blue anodes | GPIO 16, 17, 25 respectively, each through its own resistor |
| RGB common cathode | GND |
| IR local North OUT | GPIO 34 |
| IR local East OUT | GPIO 35 |
| IR local South OUT | GPIO 32 |
| IR local West OUT | GPIO 33 |
| All IR VCC, GND | 3V3, GND; verify the actual modules work at 3.3 V |
| Buzzer module SIG | GPIO 27 |
| MPU9250 VCC, GND | 3V3, GND |
| MPU9250 SDA, SCL | GPIO 21, 22 respectively |
| MPU9250 AD0, FSYNC | GND, GND |
| MPU9250 NCS | 3V3, enabling I2C mode |
| MPU9250 EDA, ECL, INT | Leave unconnected for this firmware |
| Motor driver gate/base signal | GPIO 26 through the appropriate driver circuitry |
| On-board status LED | GPIO 2, where supported by the selected board |

The RC522 pin labelled SDA is its SPI chip-select here; it does not go on the IMU's I2C SDA bus. MPU9250 address is expected to be 0x68 and AK8963 address 0x0C. GPIO 34/35 are input-only and need valid externally driven digital signals from the IR modules.

For a discrete NPN motor driver: GPIO 26 → 1 kohm resistor → base; emitter → GND; collector → motor negative; motor positive → a supply rated for that motor. Put a flyback diode across the motor, cathode/band to positive and anode to negative. Join motor supply ground to the brick ground. Do not drive the motor directly from GPIO 26. Check module-specific buzzer and motor supply requirements. The motor-driver diagram below complements the pin table.

### Motor-driver wiring diagram

```text
                     Motor-rated supply (+)
                          |           |
                       Motor (+)   Diode cathode (band)
                       Motor (-)   Diode anode
                          |           |
                          +-----------+
                          |
                    NPN collector
GPIO 26 -- 1 kohm -- NPN base
                    NPN emitter
                          |
                     Shared GND
```

Confirm the actual transistor's pinout from its datasheet; E/B/C positions vary by part and package. The motor-rated supply ground and ESP32 ground must be connected. If using a complete driver module, use its signal and power requirements rather than duplicating a driver already on the board.

## 5. Install the Arduino environment

1. Install Arduino IDE 2 from [Arduino's software page](https://www.arduino.cc/en/software).
2. Open IDE Preferences/Settings and add this **Additional Boards Manager URL**:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Open Boards Manager, search `esp32`, and install **esp32 by Espressif Systems**, version **3.x**. Use the same version throughout the project; record the exact version you install. The current callback signature requires this API generation.
4. Select **Tools → Board → esp32 → ESP32 Dev Module** for the classic ESP32 boards used here.
5. Open Library Manager, search `MFRC522`, and install the library maintained by GithubCommunity/miguelbalboa. Record its version too.
6. WiFi, ESP-NOW, WebServer, Preferences, SPI, Wire and FreeRTOS are supplied by the ESP32 core; do not install unrelated replacements for them.

See [Espressif's installation instructions](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html) for the official board-package procedure. If your exact board is different, select its actual board profile and check the pin map before upload.

## 6. Generate and compile

The repository already contains `DashboardPage.h`. If Python 3 is available, regenerate it before the first build to ensure it matches the web sources:

```bash
python3 Code/Dashboard/embed.py
```

On Windows, use `py -3 Code/Dashboard/embed.py` or your installed Python launcher. The script updates only `Code/brick/DashboardPage.h`; it creates no numbered sketches.

1. In Arduino IDE choose **File → Open** and select `Code/brick/brick.ino`.
2. Confirm these three files remain together:

   ```text
   Code/brick/
   ├── brick.ino
   ├── DashboardPage.h
   └── DebugCommand.h
   ```

   Do not move the `.ino` alone into a different directory. `embed.py` generates `DashboardPage.h`; `DebugCommand.h` is a maintained source file and must also be present.
3. Confirm the ESP32 board profile and installed core version.
4. Click **Verify** (checkmark). Do this before connecting and uploading to every board.
5. Continue only after compilation succeeds. If it fails, retain the compiler output and use the troubleshooting table below. A successful host test is not a substitute for this build.

Do not change `BRICK_ID`: it is now a runtime display number. The source must remain identical across boards. Keep `WIFI_CHANNEL` identical, currently 1. The default project is four bricks; changing scale needs capacity and radio testing, not only a label change.

## 7. Upload to every brick

1. Connect one brick to the laptop using a **data-capable** USB cable. During flashing, power it according to the board's instructions; do not combine external and USB supplies in a way the board does not support.
2. In **Tools → Port**, select the port that appeared when this board was connected. Linux commonly uses `/dev/ttyUSB0` or `/dev/ttyACM0`; Windows uses a COM port; macOS commonly uses a `/dev/cu.*` device. Select the actual port, not an example blindly.
3. Close other programs that hold this port, including other serial monitors.
4. Click **Upload** and wait for a successful completion message.
5. If upload stalls while connecting, use your board's BOOT/EN procedure; commonly hold BOOT during the connection attempt and release once writing begins.
6. Open Serial Monitor at **115200 baud**, press EN/reset, and inspect the startup log.
7. Check for IMU/magnetometer detection, RFID initialization, ESP-NOW readiness, and eventually the dashboard access message. If IMU or magnetometer detection fails, enable [serial debug](#serial-sensor-debug-mode) before continuing to compass calibration.
8. Repeat with the exact same sketch and settings for every board, checking the correct port each time.

All boards must run the new version. Earlier firmware has a different radio packet format and will not join this version correctly. Uploading updates program memory; normal independent-power operation starts after you disconnect the laptop and supply suitable power to each board.

## 8. Check and calibrate each brick

Perform this procedure with **only one brick powered at a time**, so that brick becomes the dashboard host.

1. Power the brick and wait for initialization plus the four-second election settling period. Allow roughly 10 seconds before troubleshooting a missing hotspot.
2. On your laptop join Wi-Fi **SmartSurgeryTray** using password **smarttray22**. If the operating system reports “No Internet”, choose to stay connected.
3. Open **http://192.168.4.1** explicitly using HTTP. The page is served by the brick. Opening the HTML file directly from disk does not connect it to the tray API.
4. Check for one live card marked **gateway** and note its hardware UID. When running alone, every brick may display “Brick 1”; that does not mean their hardware identities collide.
5. Test one IR face at a time by moving an object into and out of its range. Check that the matching orange edge appears and clears. Adjust the sensor's sensitivity for the intended docking distance. Reflections, ambient light, enclosure surfaces and nearby objects affect detection.
6. Present a compatible RFID instrument tag to the top reader. Check that **Last instrument UID** changes and **Just scanned** appears briefly. The green indicator is held for about two seconds. Removing a tag does not clear the stored last UID.
7. On the dashboard click **Calibrate gateway compass**.
8. Keep the brick flat, rotate it smoothly through a full 360-degree circle, and continue rotating for the full **20 seconds**. Keep motors and nearby magnetic objects still or away during calibration.
9. A usable heading should appear after successful calibration. If it remains unknown, repeat with a complete rotation and inspect magnetometer detection. Both measured axes must span sufficient range; the code requires a half-range greater than 10 microtesla on each axis.
10. Rotate the brick approximately 90 degrees and check that the displayed heading changes sensibly. Compare the marked local N side with the diagram; verify sensor-axis mounting and sign conventions physically. Calibration does not automatically determine an enclosure mounting offset.
11. Power-cycle the brick and check that its calibration persists. Ordinary uploads normally preserve Preferences/NVS; erasing flash removes calibration and requires repeating it.
12. Power this brick off and repeat for the next one.

The button calibrates only the current gateway. Powering boards individually ensures all four receive their own calibration. This is basic flat-table offset/scale calibration, not full 3D fusion or immunity to magnetic interference.

## 9. Start the complete tray

1. Place all calibrated bricks flat on the table within direct radio range of one another.
2. Power them independently using their own supplies.
3. Wait about 10 seconds for boot, discovery and election to settle.
4. Join **SmartSurgeryTray**, password **smarttray22**.
5. Open **http://192.168.4.1** and confirm the live count matches the number powered, normally four.
6. There should be one gateway in a stable mutually visible group. Record hardware UIDs if you need to identify enclosures permanently.
7. Move or rotate bricks and place RFID-tagged instruments on top to observe telemetry.

No USB connection is required during this process. The laptop communicates with the elected host; the other boards send ESP-NOW telemetry to the group. No `npm install`, local Python server, cloud account or Internet connection is needed. The header embeds the page and JavaScript into the flashed firmware.

## 10. Understand the dashboard

| Display | Meaning |
| --- | --- |
| Brick number | Rank among currently visible hardware identities; may change after joins/leaves |
| Hardware UID | Stable identity derived from this ESP32's hardware MAC |
| Gateway | Board currently serving this browser |
| Orange edge / occupied face | Local IR sensor detects an object; not an identified connection |
| Heading | Fresh calibrated magnetic angle; sensor mounting must match the diagram convention |
| Unknown / calibrate | Calibration unavailable/in progress, magnetometer unavailable, or stale sample |
| Last instrument UID | Most recently read top-facing RFID tag |
| Just scanned | Recent scan indication, not a continuous instrument-presence measurement |
| Faded card | Offline/stale peer or lost connection |
| Physical arrangement unresolved | No verified face-to-peer connections or reconstructed physical coordinates |

The UI retains offline cards as faded records. Display numbers are not permanent instrument IDs. The firmware does not map UIDs to instrument names, count tray completeness, or verify instrument removal. The on-board/RGB proximity consensus indicates radio proximity to active peers; it is not proof that a complete tray is assembled.

## 11. Run acceptance checks

After all boards are flashed, verify:

1. **Independent boot:** each board alone starts its hotspot and renders one live card.
2. **Unique hardware identities:** all four cards have different UIDs. Numbers alone do not establish uniqueness.
3. **Group discovery:** all four appear in one stable, mutually visible group; all headings appear after calibration.
4. **Face signals:** each board's four face indicators respond to the correct local side and clear after objects are removed.
5. **RFID:** each board reports its own last instrument tag correctly; understand that removal is not continuously detected.
6. **Rotation:** rotate one brick flat; its heading changes while its IR labels remain attached to its local faces.
7. **Peer loss:** power off a non-gateway brick. After approximately 2.5 seconds plus browser refresh time, the online count falls and its old card fades.
8. **Rejoin:** power it back on. Its UID is unchanged; display numbers may be reassigned. A lower-UID arrival can also change the gateway.
9. **Gateway loss:** power off the gateway. Expect the browser to disconnect. Another board should host after peer timeout plus roughly four seconds of settling; allow additional boot/radio time. Reconnect Wi-Fi and reload the page manually if needed.
10. **Cold restart:** power everything off and back on; confirm discovery and saved compass calibration return.

A replacement host has its own browser state and only the telemetry it receives directly. Offline cards from the previous host are not a persistent event history. Do not infer that failover is broken solely because the laptop has not rejoined the new access point.

## 12. Troubleshooting

### Serial sensor debug mode

Debug mode is enabled at runtime on the same `Code/brick/brick.ino`; do not add a second `.ino` to the `brick` folder because Arduino combines sketch files into one program.

1. Regenerate assets if needed, compile, and upload the updated sketch to the brick being diagnosed.
2. Connect that brick by a USB data cable and open Serial Monitor at **115200 baud**.
3. Set the line ending to **Newline**, **Carriage return**, or **Both NL & CR**. Wait for startup to finish if opening the monitor resets the board.
4. Send `--debug`. Every 500 ms, detailed sensor snapshots appear until disabled or rebooted. The initial response also probes the I2C bus.
5. Send `--no-debug` (or `--debug-off`) to stop detailed output. Existing normal event and two-second status logs remain.
6. Send `--debug-i2c` for a one-shot I2C report without enabling periodic debug.
7. After checking wiring with power disconnected, power the board again. If an already-powered sensor was absent at initialization, `--debug-reinit` explicitly retries IMU/magnetometer setup; it does not change the configured I2C address. Recheck with `--debug`.

These are serial commands, not arguments passed to Arduino IDE or Arduino CLI. A reboot returns to debug-off by default. For automatic output after startup, change `BRICK_DEBUG_DEFAULT` from 0 to 1 in the sketch and recompile/upload; this does not require a separate debug sketch. Firmware remains the same across boards.

Output includes raw and converted accelerometer/gyro/magnetometer XYZ values, raw temperature, pitch/roll/heading, sample age, calibration/freshness state, IMU/magnetometer transfer status and byte count, all four raw and debounced IR signals, RFID reader version/last UID/recent-read state, and peer RSSI. Buzzer, motor and LEDs are actuators, not additional sensors. RFID debug does not reread cards and does not establish continuous presence.

A reading is cached from the normal sensor loop. `sample_age_ms=-1` means no valid sample has been received; cached zero values in that case are not measurements. Large sample ages mean stale data. `boot_detected` reports initialization success, not a guarantee the device is still responding. `last_tx_status=-1` means no transfer attempted; nonzero is an I2C error. Magnetometer ST1/ST2 are register snapshots: inspect `ST1_tx_status` and `ST1_read_bytes` alongside ST1 because a failed register read returns 0xFF. A usable ST1 read has status 0 and one byte; the driver does not treat a failed status read as a ready magnetic sample.

### IMU/magnetometer diagnostic checklist

Diagnose one brick at a time:

1. Connect USB, open Serial Monitor at 115200 baud, select Newline and wait for startup.
2. Send `--debug-i2c` and save the complete probe/register report.
3. Send `--debug` and capture at least three consecutive snapshots. Gently rotate the brick flat and observe whether raw values change and sample ages remain low.
4. Use the table below to distinguish missing I2C devices, failed sample transfers and missing compass calibration.
5. Disconnect power before changing wiring. Reconnect power and repeat the probes. Use `--debug-reinit` only for an explicit retry of sensor initialization; it does not change wiring or auto-select another address.
6. Once the sensor produces fresh data, power only that brick and follow [compass calibration](#8-check-and-calibrate-each-brick). Calibration cannot make an absent I2C device respond.
7. Send `--no-debug` when finished. Repeat for each board, identifying it by hardware UID rather than its temporary display number.

For investigation, save the startup log, the one-shot I2C report, three debug snapshots, board/core/library versions, and the module's printed model marking. Compare reports across boards to identify common wiring or module differences.

Interpret the probe and reading fields as follows:

| Debug observation | Interpretation / next check |
| --- | --- |
| Neither 0x68 nor 0x69 responds | Check sensor power, GND, SDA=21, SCL=22, I2C mode/NCS and wiring; probe status alone cannot prove the exact fault |
| 0x69 responds but 0x68 does not | Check AD0; current firmware expects AD0=GND/address 0x68. Reinit does not switch to 0x69 |
| 0x68 responds | Inspect WHO_AM_I; an ACK alone does not identify the part or guarantee it has a magnetometer |
| 0x68 responds but 0x0C does not | Inspect MPU identity, USER_CTRL, INT_PIN_CFG bypass and actual module type. The magnetometer is accessed through bypass; a generic gyro module may not include AK8963 |
| MPU sample age is -1 or growing | Check transfer status and whether a full 14-byte read succeeds |
| Magnetometer sample age is -1 or growing | Check ST1 read errors/data-ready state, 7-byte reads, ST2 overflow and mode register |
| Sensor readings are fresh but heading_valid=0, calibrated=0 | Hardware may be reading correctly; perform compass calibration with that brick alone |
| calibrated=1 but heading_valid=0 | Check freshness, calibration-in-progress state and magnetometer initialization |
| RFID version is 0x00 or 0xFF | The normal firmware treats this as a reader warning; check power/SPI connections |

I2C reports probe 0x68, 0x69 and 0x0C and read identity, power, bypass and magnetometer mode registers. They avoid draining the live magnetic sample registers. Status 0 means an acknowledged transaction; failed register reads are printed as unavailable rather than fabricated identity values. SDA/SCL digital pin levels are momentary snapshots, not a complete bus integrity test. Sensor absence is reported; the debug feature does not claim to repair it.


| Symptom | What to check |
| --- | --- |
| Board/port absent | Use a known data cable and another USB port; identify the board's USB-UART chip and install its official driver if needed |
| Linux “Permission denied” | Check serial-port group membership; see the Linux steps below |
| Port busy | Close Serial Monitor, other IDE instances, terminal serial tools and services holding the port |
| Upload stuck at connecting | Confirm board and port; try BOOT/EN sequence; reduce upload speed to 115200 if available |
| `MFRC522.h` missing | Install MFRC522 through Library Manager |
| `DashboardPage.h` missing | Regenerate assets and keep the header beside `brick.ino` |
| `DebugCommand.h` missing | Restore `Code/brick/DebugCommand.h` beside the sketch; the dashboard generator does not create it |
| `--debug` produces no detailed output | Confirm the updated firmware is flashed, 115200 baud is selected, startup has completed, and a newline/CR is sent; type the exact command without quotes |
| ESP-NOW receive-callback type error | Confirm Espressif ESP32 core 3.x; do not silently mix old callback APIs |
| Other compile errors | Verify target is the intended classic ESP32, preserve the complete compiler error and installed core/library versions |
| No hotspot after boot | Test one board alone; allow initialization/election time; inspect the serial log, power stability and ESP-NOW initialization |
| Several hotspots with the same name | Bricks may not hear one another; bring them within direct radio range, check identical firmware/channel and power stability |
| Connected to Wi-Fi but no page | Use `http://192.168.4.1`, stay on the no-Internet network, check laptop network address/routing and disable a conflicting VPN route for this local connection |
| Browser upgrades HTTP to HTTPS | Enter the HTTP URL explicitly or allow HTTP for this local address |
| Cards missing | Reflash all boards, check shared channel 1 and direct radio visibility; old packets are incompatible |
| Dashboard reports disconnected | Check whether the host lost power or a lower-UID board caused a gateway change; rejoin hotspot and reload |
| Number changed | Expected: display numbering depends on the active group; identify boards using hardware UIDs |
| Heading unknown | Calibrate that board while it is alone; inspect MPU9250 and AK8963 wiring/detection, freshness and rotation range |
| Heading distorted or consistently offset | Check sensor mounting/axis convention, complete calibration, nearby magnets/metals/motor currents; no mounting-offset control exists in the current UI |
| IMU missing at 0x68 | Check SDA=21, SCL=22, AD0=GND, NCS=3V3, power and common ground |
| MPU detected but magnetometer missing | Verify the module actually contains AK8963; inspect its I2C/bypass operation |
| RFID reader reports warning | Check 3.3 V power, SPI wiring, SS=5, RST=4 and antenna/tag compatibility |
| IR edge stuck on/off | Check active-low polarity, sensor supply/output level, sensitivity and enclosure obstruction; verify local face wiring |
| Reset when radio/motor activates | Check supply, wiring resistance, decoupling and motor driver; the present sketch disables the brownout detector, which does not fix inadequate power |
| Instruments disappear but UID remains | Expected: the field stores the last scan; removal tracking is not implemented |
| Diagram order differs from physical tray | Expected: actual topology reconstruction is unresolved with these obstacle sensors |

For Linux port permissions, first inspect the actual port owner/group, for example:

```bash
ls -l /dev/ttyUSB0
groups
```

Replace the device path with your port. If its group is `dialout` and your user is not a member, Arduino documents this remedy:

```bash
sudo usermod -a -G dialout "$USER"
```

Log out and back in before retrying. Some distributions use a different group or need udev rules; follow [Arduino's Linux port-access guide](https://support.arduino.cc/hc/en-us/articles/360016495679-Fix-port-access-on-Linux) and [udev guidance](https://support.arduino.cc/hc/en-us/articles/9005041052444-Fix-udev-rules-on-Linux). These are optional troubleshooting commands for your machine, not changes required to run the wireless dashboard.

For API diagnostics, while connected to the tray visit **http://192.168.4.1/api/state** in the browser, or run:

```bash
curl --max-time 5 http://192.168.4.1/api/state
```

Expect a JSON object with `topology: "unresolved"` and a `bricks` list containing UID, number, age, gateway status, face mask, heading or null, instrument UID and recent-scan state. Face-mask bits are N=1, E=2, S=4, W=8; combinations add together. A POST to `/api/calibrate` starts calibration of the hosting brick only; using the dashboard button is simpler.

## 13. Edit and update the project

1. Edit firmware only in `Code/brick/brick.ino`.
2. Edit dashboard sources in `Code/Dashboard/dashboard.html` and `dashboard.js`.
3. If dashboard sources change, regenerate the header:

   ```bash
   python3 Code/Dashboard/embed.py
   ```

4. Compile in Arduino IDE, run relevant host checks if available, then upload the updated firmware to **every** brick.
5. Reopen the browser page after updates. Recalibrate if flash/NVS was erased, sensor mounting changed, or magnetic conditions changed significantly.

The running dashboard is stored in flash. Editing files on the laptop does not update an already-flashed board. Changing hotspot credentials or channel means updating the shared firmware constants and reflashing all boards. Defaults are `TRAY_SSID`, `TRAY_PASSWORD` and `WIFI_CHANNEL` in the sketch. Keep the same source and radio packet layout on all peers. Work on `main` for this setup; `Bricks_Changes` is not involved.

## 14. Optional command-line build and tests

Arduino IDE is the primary route. If Arduino CLI is already installed, the following are alternative commands from the repository root; initial core/library installation requires Internet and writes to Arduino's configured data directories:

```bash
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "MFRC522@1.4.12"
python3 Code/Dashboard/embed.py
arduino-cli compile --fqbn esp32:esp32:esp32 Code/brick
arduino-cli board list
```

The pinned core 3.3.11 and MFRC522 1.4.12 are reproducibility targets, not a claim that this firmware has been successfully built with them. If using another supported 3.x core, record the exact version and compile it explicitly. Only upload after a successful build. Replace the example port with the actual port from `board list`:

```bash
arduino-cli upload --port /dev/ttyUSB0 --fqbn esp32:esp32:esp32 Code/brick
```

Repeat upload for every board. See [Arduino CLI's command reference](https://arduino.github.io/arduino-cli/latest/commands/arduino-cli/) for your installed CLI version.

For host checks, install Python 3, Node.js and a C++17-capable `g++` accessible on PATH. Run from the root:

```bash
python3 Code/tests/test_dynamic_identity.py
python3 Code/tests/test_gateway_election.py
python3 Code/tests/test_sensor_debug.py
node Code/tests/test_dashboard.js
node --check Code/Dashboard/dashboard.js
```

The Python checks compile extracted firmware functions using desktop queue/network adapters. `test_sensor_debug.py` additionally checks command parsing, enable/disable timing and diagnostic distinctions between absent, stale and uncalibrated sensor data. The JavaScript check uses a mocked DOM. They check logic, not the ESP32 hardware build, real radio, power, sensors or visual browser layout. On Windows, use a Python launcher and a working C++ toolchain, or run host checks in a configured Linux/WSL environment; none of these tools is needed for ordinary tray operation.

## 15. Shutdown, restart and known limitations

Close the browser and turn off each board's supply. Saved calibration persists; last-read UID and peer membership are runtime state and reset on reboot. Restart by powering the boards, waiting for discovery/election, joining the hotspot, and opening the dashboard again. A browser view is not a saved inventory.

Current limits:

- Automatic physical tray coordinates are not available; identifiable face communication or another positioning mechanism is needed for reliable reconstruction.
- The design assumes flat-table operation and consistently aligned sensor axes.
- ESP-NOW is used for direct single-hop broadcasts; there is no multi-hop routing or robust distributed consensus under partitions.
- Gateway changes can interrupt laptop connectivity and require manual Wi-Fi reconnection.
- Display numbers may change; hardware UID is the stable identity.
- Instrument names, continuous presence/removal detection, completeness checking and persistent inventory are not implemented.
- The wireless/password defaults and prototype hardware require assessment before any actual clinical use; this repository does not establish surgical safety or sterilization suitability.
- Real ESP32 compilation and hardware acceptance remain outstanding until someone performs the build and checks described above.

Codex assisted with the wireless firmware, dashboard, cleanup and this guide. Setup commands were checked against the cited official documentation; code-specific behavior was checked against the current repository source. Documentation does not substitute for the pending physical tests.
