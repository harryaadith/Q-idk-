# Smart Surgery Tray

Team 22 — (Q)idk if i'm getting an internship — Electronic System Workshop (ESW).

Independently powered ESP32 bricks form a modular tray prototype. Top-facing RFID readers scan instrument tags; side IR sensors detect occupied faces; an MPU9250/AK8963 supplies flat-table orientation. Bricks exchange telemetry over ESP-NOW and automatically elect one to host the Wi-Fi dashboard.

Start with [HOW_TO_RUN.md](HOW_TO_RUN.md) for wiring, software installation, compilation, upload, calibration, wireless startup, serial debug and troubleshooting.

## Current implementation

Upload the same [brick.ino](Code/brick/brick.ino) to every board. Hardware identities and display numbers are automatic. Join `SmartSurgeryTray` with password `smarttray22`, then open `http://192.168.4.1`.

The dashboard reports individual brick orientation, face occupancy and the last RFID scan. Actual physical arrangement reconstruction and continuous instrument-presence detection remain unresolved. Host checks pass; full ESP32 compilation and physical operation still require verification.

For serial sensor diagnostics, open Serial Monitor at 115200 baud with a newline and send `--debug`. Details and other commands are in [the run guide](HOW_TO_RUN.md#serial-sensor-debug-mode).

## Repository layout

| Location | Contents |
| --- | --- |
| [HOW_TO_RUN.md](HOW_TO_RUN.md) | Single setup, wiring, operation and troubleshooting guide |
| [Code/brick/](Code/brick/) | Production sketch, debug command parser and generated dashboard header |
| [Code/Dashboard/](Code/Dashboard/) | Dashboard HTML/JavaScript sources and embedding script |
| [Code/tests/](Code/tests/) | Current identity, gateway, sensor-debug and dashboard host checks |
| [Demos/](Demos/) | Demonstration materials |
| [Resources/](Resources/) | Supporting references |
| [Presentation/](Presentation/) | Presentation PDFs and editable slide source |

After dashboard edits, run `python3 Code/Dashboard/embed.py`, compile, and reflash every board. Keep both header files beside the sketch. Historical prototypes and obsolete documentation are available in Git history.

## Verification

From the repository root, with Python 3, Node.js and a C++17-capable `g++`:

```bash
python3 Code/tests/test_dynamic_identity.py
python3 Code/tests/test_gateway_election.py
python3 Code/tests/test_sensor_debug.py
node Code/tests/test_dashboard.js
```

These checks use host adapters and do not replace an ESP32 build or hardware acceptance tests. See the run guide for those steps.

## Submission and AI disclosure

Keep project code in `Code/`, demonstration material in `Demos/`, references in `Resources/`, and presentation deliverables in `Presentation/`. Document dependencies, reproducible setup, results and external sources before submission.

Codex assisted with repository inspection, dynamic identity and wireless dashboard implementation, serial sensor diagnostics, cleanup and documentation. The requested scope was independently powered, identically flashed bricks with automatic numbering, rotation telemetry, an elected hotspot and opt-in sensor debugging. Current limitations and validation status are recorded in the run guide; no public chat link is available.
