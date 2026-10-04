# Wireless Smart Surgery Tray

Start with the complete [HOW TO RUN guide](../../HOW_TO_RUN.md) for setup from the beginning.

The single production sketch is `Code/brick/brick.ino`. Open it in Arduino IDE and upload the same version to every ESP32. Install the MFRC522 library and use ESP32 Arduino core 3.x (the receive callback uses its API). DashboardPage.h must remain beside the sketch. Old two-byte packet firmware is incompatible; reflash all bricks together. Firmware has not yet been fully board-compiled or tested on hardware.

## Connecting

Power the bricks independently and wait for discovery and the four-second election settling period. Join Wi-Fi `SmartSurgeryTray`, password `smarttray22`, and visit `http://192.168.4.1`. No laptop cable, router, Internet, external JavaScript library or font is required. The dashboard polls `/api/state` every 500ms with request timeouts. All nodes broadcast their telemetry over ESP-NOW; the lowest live hardware UID hosts the browser interface. Bricks must share channel 1 and directly hear one another. This is single-hop discovery, not multi-hop mesh routing. Separate radio groups may create separate hotspots with the same name. Gateway loss can require manually reconnecting Wi-Fi and reloading the dashboard after peer timeout plus election settling. Election assumes a stable, mutually visible group; it does not guarantee consensus during packet loss or partitions.

## Identity and instruments

The hardware MAC is the stable identity. Display numbers are ranks of the currently visible identities and can change after a join or departure; never use those numbers as permanent instrument associations. Peer records use bounded slots, independent of the hardware UID. RFID points upward and records the last scanned instrument UID. A recent read is not continuous presence or reliable removal detection. Instruments require an actual UID-to-instrument registry before names or completeness can be reported.

## Orientation and physical layout

The dashboard displays individual top-down brick diagrams with orange occupied faces. Positions of those diagrams are NOT physical tray positions. Obstacle IR sensors cannot associate a face with a peer's identity. Heading plus face occupancy can constrain possible layouts but may leave multiple configurations, and obstacles can also trigger IR. Automatic reliable topology needs face-level identity sensing, docking contacts, directional communications or another positioning mechanism.

The compass supports flat-table 2D offset/scale calibration, saved separately on each board. Power one brick at a time so it becomes gateway, click Calibrate gateway compass, and rotate it flat through a full circle for 20 seconds. Both axes need sufficient range; successful fresh calibrated measurements appear as headings. This is basic planar calibration, not full 3D sensor fusion or proof of heading accuracy. Sensor X/Y axes must align consistently with brick-local face labels; mounting offsets and magnetic interference from motors/magnets must be checked physically. Heading is magnetic, not a position measurement. No continuous instrument presence, surgical safety, or tray completeness assertion is made by this prototype.

## Editing and verification

Edit `Code/Dashboard/dashboard.html` and `dashboard.js`, then run `python3 Code/Dashboard/embed.py` to regenerate the embedded headers before reflashing. Edit firmware only in `Code/brick/brick.ino`; the generator writes its embedded header to `Code/brick/DashboardPage.h`. Host checks covered identity arrival order, duplicate packets, self/incompatible-packet rejection, timeout/rejoin, registry capacity, gateway election/failover, and dashboard rendering/renumbering/stale states. Full firmware compilation, radio coexistence, real face signals, heading mounting, compass calibration, hotspot failover, and instrument scans still need hardware verification.

## AI usage

Codex assisted with inspecting the existing serial mismatch and implementing dynamic identities, instrument-token semantics, queued radio reception, gateway election, HTTP telemetry, the dashboard and planar calibration. User requested independent power, identical firmware, dynamic numbering, rotation support and an automatically elected hotspot. Physical topology remains unresolved with the present sensors.
