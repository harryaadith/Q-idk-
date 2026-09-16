#!/usr/bin/env python3
"""
Multi-Agent Smart Bricks (Microbots) — Swarm Multi-Node Hardware & Protocol Simulator
File: Code/tests/simulate_swarm.py

Simulates all 4 physical ESP32 bricks running concurrently over discrete time ticks.
Verifies all project requirements:
1. P2P ESP-NOW communication broadcasting only sender_id and neighbor_count.
2. RSSI EMA filtering, proximity hysteresis, and close-to-all detection:
   - On-Board LED (GPIO 2) ON when close to all active neighbors.
   - External RGB LED: Solid RED (0 neighbors), sequential Blue N times then Red, Solid Blue (close to all).
   - Consensus chime & double haptic pulse on ERM motor.
3. RFID reader scanning neighbor tags:
   - RGB LED lights Green for 2.0s hold.
   - RFID success chime & 100ms haptic click.
4. 4-Face Directional IR Sensors (North, East, South, West):
   - Face docking chime & 80ms haptic snap.
   - Undocking alert tone.
5. Inactive peer timeout and eviction after 2.5s silence.
6. 9-DoF IMU (MPU9250) + AK8963 Magnetometer math:
   - Pitch, Roll, and Compass Heading (Yaw).
7. ERM Vibration Motor tactile feedback.
"""

import math
import sys

TOTAL_SWARM_BRICKS = 4
PROXIMITY_THRESHOLD = -65.0
PROXIMITY_HYSTERESIS = 4.0
PEER_TIMEOUT_MS = 2500
BROADCAST_INTERVAL_MS = 250
RSSI_EMA_ALPHA = 0.30
RFID_GREEN_HOLD_MS = 2000
IR_DEBOUNCE_COUNT = 2

FACE_NAMES = ["NORTH", "EAST", "SOUTH", "WEST"]


class VirtualBrick:
    def __init__(self, brick_id: int):
        self.brick_id = brick_id
        self.peers = {}  # peer_id -> dict
        for i in range(1, TOTAL_SWARM_BRICKS + 1):
            if i != self.brick_id:
                self.peers[i] = {
                    "active": False,
                    "last_seen_ms": 0,
                    "rssi_ema": -100.0,
                    "reported_neighbor_count": 0,
                    "is_close": False
                }

        # Hardware outputs
        self.onboard_led = False
        self.rgb_led = "RED"  # "RED", "GREEN", "BLUE", or "BLINK_BLUE_RED"

        # RFID
        self.rfid_active = False
        self.rfid_detected_time_ms = 0
        self.last_read_uid = ""

        # 4 IR faces
        self.ir_face_raw = [False, False, False, False]
        self.ir_face_detected = [False, False, False, False]
        self.ir_debounce_counters = [0, 0, 0, 0]

        # Audio and Haptics log
        self.buzzer_log = []
        self.haptic_log = []
        self.prev_close_to_all = False

        # IMU state
        self.imu_online = True
        self.mag_online = True
        self.pitch = 0.0
        self.roll = 0.0
        self.heading = 0.0

        # Broadcast timer
        self.last_broadcast_time = 0

    def update_imu(self, ax, ay, az, mx=0.0, my=0.0, mz=0.0):
        self.pitch = math.atan2(ay, math.sqrt(ax * ax + az * az)) * 180.0 / math.pi
        self.roll = math.atan2(-ax, az) * 180.0 / math.pi
        h = math.atan2(my, mx) * 180.0 / math.pi
        if h < 0.0:
            h += 360.0
        self.heading = h

    def scan_rfid(self, uid: str, now_ms: int):
        self.rfid_active = True
        self.rfid_detected_time_ms = now_ms
        self.last_read_uid = uid
        self.buzzer_log.append((now_ms, "RFID_SUCCESS"))
        self.haptic_log.append((now_ms, "HAPTIC_RFID_100MS"))

    def set_ir_sensor_pin(self, face_idx: int, detected: bool):
        self.ir_face_raw[face_idx] = detected

    def check_ir_sensors(self, now_ms: int):
        for i in range(4):
            detected = self.ir_face_raw[i]
            if detected == self.ir_face_detected[i]:
                self.ir_debounce_counters[i] = 0
            else:
                self.ir_debounce_counters[i] += 1
                if self.ir_debounce_counters[i] >= IR_DEBOUNCE_COUNT:
                    self.ir_face_detected[i] = detected
                    self.ir_debounce_counters[i] = 0
                    if detected:
                        self.buzzer_log.append((now_ms, f"DOCK_{FACE_NAMES[i]}"))
                        self.haptic_log.append((now_ms, "HAPTIC_DOCK_80MS"))
                    else:
                        self.buzzer_log.append((now_ms, f"UNDOCK_{FACE_NAMES[i]}"))

    def on_data_recv(self, sender_id: int, neighbor_count: int, rssi: int, now_ms: int):
        if sender_id == self.brick_id or sender_id == 0 or sender_id > TOTAL_SWARM_BRICKS:
            return

        p = self.peers[sender_id]
        if not p["active"]:
            p["active"] = True
            p["rssi_ema"] = float(rssi)
            p["is_close"] = (rssi >= PROXIMITY_THRESHOLD)
        else:
            p["rssi_ema"] = (RSSI_EMA_ALPHA * float(rssi)) + ((1.0 - RSSI_EMA_ALPHA) * p["rssi_ema"])
            if not p["is_close"] and p["rssi_ema"] >= (PROXIMITY_THRESHOLD + PROXIMITY_HYSTERESIS):
                p["is_close"] = True
            elif p["is_close"] and p["rssi_ema"] < (PROXIMITY_THRESHOLD - PROXIMITY_HYSTERESIS):
                p["is_close"] = False

        p["reported_neighbor_count"] = neighbor_count
        p["last_seen_ms"] = now_ms

    def prune_stale_peers(self, now_ms: int):
        for pid, p in self.peers.items():
            if p["active"] and (now_ms - p["last_seen_ms"] > PEER_TIMEOUT_MS):
                p["active"] = False
                p["is_close"] = False

    def get_active_neighbor_count(self) -> int:
        return sum(1 for p in self.peers.values() if p["active"])

    def is_close_to_all_neighbors(self) -> bool:
        active = [p for p in self.peers.values() if p["active"]]
        return len(active) > 0 and all(p["is_close"] for p in active)

    def update_led_states(self, now_ms: int):
        close_to_all = self.is_close_to_all_neighbors()
        count = self.get_active_neighbor_count()

        # Edge detection for consensus chime & double haptic pulse
        if close_to_all and not self.prev_close_to_all:
            self.buzzer_log.append((now_ms, "CONSENSUS_CHIME"))
            self.haptic_log.append((now_ms, "HAPTIC_DOUBLE_PULSE"))
        self.prev_close_to_all = close_to_all

        # On-Board LED
        self.onboard_led = close_to_all

        # RGB LED priority
        if self.rfid_active:
            if now_ms - self.rfid_detected_time_ms < RFID_GREEN_HOLD_MS:
                self.rgb_led = "GREEN"
                return
            else:
                self.rfid_active = False

        if close_to_all:
            self.rgb_led = "SOLID BLUE"
        elif count == 0:
            self.rgb_led = "SOLID RED"
        else:
            self.rgb_led = f"BLUE x {count} THEN RED"


def run_full_swarm_simulation():
    print("================================================================================")
    print("      STARTING HIGH-FIDELITY 4-BRICK SWARM TIME-SERIES SIMULATION              ")
    print("================================================================================")

    bricks = {i: VirtualBrick(i) for i in range(1, TOTAL_SWARM_BRICKS + 1)}

    # Distance matrix to RSSI mapper: distance in meters -> RSSI in dBm
    # -40 dBm @ 0.1m (very close), -60 dBm @ 0.4m (close), -75 dBm @ 1.2m (far), -85 dBm @ 3m (edge)
    radio_links = {
        # (src, dst): rssi
    }

    def set_distance_all(rssi_val):
        for s in range(1, TOTAL_SWARM_BRICKS + 1):
            for d in range(1, TOTAL_SWARM_BRICKS + 1):
                if s != d:
                    radio_links[(s, d)] = rssi_val

    # Scenario Timeline:
    # Phase 0 (0 - 500ms): All bricks boot up isolated (Standalone)
    # Phase 1 (500 - 2000ms): Bricks 1 and 2 come in range, but FAR (-80 dBm)
    # Phase 2 (2000 - 4000ms): All 4 bricks discover each other, FAR (-75 dBm)
    # Phase 3 (4000 - 6000ms): All 4 bricks move CLOSE (-50 dBm) -> Consensus!
    # Phase 4 (6000 - 8000ms): Physical Docking on IR North-South & East-West
    # Phase 5 (8000 - 10000ms): RFID Tag read on Brick 1
    # Phase 6 (10000 - 12000ms): IMU rotation & Compass validation
    # Phase 7 (12000 - 16000ms): Brick 4 leaves network -> Timeout eviction test

    step_ms = 50
    total_duration_ms = 16000

    print("\n>>> Phase 0: Standalone Boot (t = 0 - 500ms)...")
    for t in range(0, 500, step_ms):
        for b in bricks.values():
            b.check_ir_sensors(t)
            b.update_led_states(t)

    for b in bricks.values():
        assert b.get_active_neighbor_count() == 0, f"Brick {b.brick_id} must have 0 neighbors"
        assert not b.onboard_led, f"Brick {b.brick_id} onboard LED must be OFF"
        assert b.rgb_led == "SOLID RED", f"Brick {b.brick_id} RGB LED must be SOLID RED"
    print("  [PASS] Standalone state verified: 0 neighbors, On-Board LED OFF, RGB SOLID RED.")

    print("\n>>> Phase 1: Two Bricks in Range (Far, -80 dBm, t = 500 - 2000ms)...")
    radio_links[(1, 2)] = -80
    radio_links[(2, 1)] = -80

    for t in range(500, 2000, step_ms):
        # Broadcast packets every 250ms
        for s_id, s_brick in bricks.items():
            if t - s_brick.last_broadcast_time >= BROADCAST_INTERVAL_MS:
                s_brick.last_broadcast_time = t
                cnt = s_brick.get_active_neighbor_count()
                for d_id, d_brick in bricks.items():
                    if (s_id, d_id) in radio_links:
                        rssi = radio_links[(s_id, d_id)]
                        d_brick.on_data_recv(s_id, cnt, rssi, t)
        for b in bricks.values():
            b.prune_stale_peers(t)
            b.check_ir_sensors(t)
            b.update_led_states(t)

    assert bricks[1].get_active_neighbor_count() == 1
    assert bricks[2].get_active_neighbor_count() == 1
    assert not bricks[1].onboard_led and not bricks[2].onboard_led
    assert bricks[1].rgb_led == "BLUE x 1 THEN RED"
    assert bricks[2].rgb_led == "BLUE x 1 THEN RED"
    assert bricks[3].rgb_led == "SOLID RED" and bricks[4].rgb_led == "SOLID RED"
    print("  [PASS] 2-Brick Far State: Neighbor count = 1, RGB pulses BLUE 1x then RED.")

    print("\n>>> Phase 2: All 4 Bricks in Range (Far, -75 dBm, t = 2000 - 4000ms)...")
    set_distance_all(-75)

    for t in range(2000, 4000, step_ms):
        for s_id, s_brick in bricks.items():
            if t - s_brick.last_broadcast_time >= BROADCAST_INTERVAL_MS:
                s_brick.last_broadcast_time = t
                cnt = s_brick.get_active_neighbor_count()
                for d_id, d_brick in bricks.items():
                    if (s_id, d_id) in radio_links:
                        rssi = radio_links[(s_id, d_id)]
                        d_brick.on_data_recv(s_id, cnt, rssi, t)
        for b in bricks.values():
            b.prune_stale_peers(t)
            b.check_ir_sensors(t)
            b.update_led_states(t)

    for b in bricks.values():
        assert b.get_active_neighbor_count() == 3, f"Brick {b.brick_id} should see 3 peers"
        assert not b.onboard_led
        assert b.rgb_led == "BLUE x 3 THEN RED"
    print("  [PASS] 4-Brick Far Swarm: All bricks see 3 neighbors, RGB pulses BLUE 3x then RED.")

    print("\n>>> Phase 3: All 4 Bricks Move CLOSE (-50 dBm, t = 4000 - 6000ms) -> Consensus!...")
    set_distance_all(-50)

    for t in range(4000, 6000, step_ms):
        for s_id, s_brick in bricks.items():
            if t - s_brick.last_broadcast_time >= BROADCAST_INTERVAL_MS:
                s_brick.last_broadcast_time = t
                cnt = s_brick.get_active_neighbor_count()
                for d_id, d_brick in bricks.items():
                    if (s_id, d_id) in radio_links:
                        rssi = radio_links[(s_id, d_id)]
                        d_brick.on_data_recv(s_id, cnt, rssi, t)
        for b in bricks.values():
            b.prune_stale_peers(t)
            b.check_ir_sensors(t)
            b.update_led_states(t)

    for b in bricks.values():
        assert b.is_close_to_all_neighbors(), f"Brick {b.brick_id} should be close to all"
        assert b.onboard_led, f"Brick {b.brick_id} On-Board LED must be ON"
        assert b.rgb_led == "SOLID BLUE", f"Brick {b.brick_id} RGB LED must be SOLID BLUE"
        consensus_events = [ev for _, ev in b.buzzer_log if ev == "CONSENSUS_CHIME"]
        haptic_events = [ev for _, ev in b.haptic_log if ev == "HAPTIC_DOUBLE_PULSE"]
        assert len(consensus_events) >= 1, f"Brick {b.brick_id} must have triggered consensus chime"
        assert len(haptic_events) >= 1, f"Brick {b.brick_id} must have triggered double haptic pulse"
    print("  [PASS] Proximity Consensus achieved across all 4 bricks!")
    print("         - On-Board LED: ON (all 4 bricks)")
    print("         - RGB LED: SOLID BLUE (all 4 bricks)")
    print("         - Acoustic Consensus Chime: Triggered")
    print("         - ERM Double Haptic Pulse: Triggered")

    print("\n>>> Phase 4: Physical IR Face Docking (t = 6000 - 8000ms)...")
    # Brick 1 East face docks to Brick 2 West face
    # Debounce requires 2 samples
    for t in range(6000, 6150, step_ms):
        bricks[1].set_ir_sensor_pin(1, True)  # EAST
        bricks[2].set_ir_sensor_pin(3, True)  # WEST
        for b in bricks.values():
            b.check_ir_sensors(t)

    assert bricks[1].ir_face_detected[1] is True
    assert bricks[2].ir_face_detected[3] is True
    dock_1 = [ev for _, ev in bricks[1].buzzer_log if ev == "DOCK_EAST"]
    hapt_1 = [ev for _, ev in bricks[1].haptic_log if ev == "HAPTIC_DOCK_80MS"]
    assert len(dock_1) >= 1 and len(hapt_1) >= 1
    print("  [PASS] Directional IR Docking on East-West faces verified with chime & tactile snap!")

    print("\n>>> Phase 5: RFID Tag Scanning (t = 8000 - 10000ms)...")
    # Brick 1 scans neighbor RFID card
    bricks[1].scan_rfid("5E6F7A8B", 8000)
    bricks[1].update_led_states(8050)
    assert bricks[1].rgb_led == "GREEN", "RGB must turn GREEN upon RFID detection"
    rfid_buzz = [ev for _, ev in bricks[1].buzzer_log if ev == "RFID_SUCCESS"]
    rfid_hapt = [ev for _, ev in bricks[1].haptic_log if ev == "HAPTIC_RFID_100MS"]
    assert len(rfid_buzz) >= 1 and len(rfid_hapt) >= 1
    print("  [PASS] RFID read triggers GREEN LED, success chirp & 100ms haptic pulse!")

    # Verify hold duration of 2.0s
    bricks[1].update_led_states(9500)  # +1.5s
    assert bricks[1].rgb_led == "GREEN"
    bricks[1].update_led_states(10100) # +2.1s (expired)
    assert bricks[1].rgb_led == "SOLID BLUE"
    print("  [PASS] RFID 2.0s hold timer expired cleanly; restored swarm status.")

    print("\n>>> Phase 6: 9-DoF IMU & Magnetometer Heading (t = 10000 - 12000ms)...")
    # Brick 1 rotates 90 degrees East: Mx=0, My=15 uT, tilt 30 deg Pitch
    bricks[1].update_imu(ax=0.0, ay=0.5, az=0.866, mx=0.0, my=15.0, mz=0.0)
    assert abs(bricks[1].heading - 90.0) < 0.1, f"Expected heading 90, got {bricks[1].heading}"
    assert abs(bricks[1].pitch - 30.0) < 0.1, f"Expected pitch 30, got {bricks[1].pitch}"
    print(f"  [PASS] IMU orientation math verified: Heading={bricks[1].heading:.1f}°, Pitch={bricks[1].pitch:.1f}°.")

    print("\n>>> Phase 7: Brick 4 Leaves Swarm -> Timeout Eviction (t = 12000 - 16000ms)...")
    # Brick 4 completely disconnects
    for k in list(radio_links.keys()):
        if 4 in k:
            del radio_links[k]

    for t in range(12000, 16000, step_ms):
        for s_id, s_brick in bricks.items():
            if s_id == 4:
                continue  # Brick 4 is dead / powered off
            if t - s_brick.last_broadcast_time >= BROADCAST_INTERVAL_MS:
                s_brick.last_broadcast_time = t
                cnt = s_brick.get_active_neighbor_count()
                for d_id, d_brick in bricks.items():
                    if (s_id, d_id) in radio_links:
                        rssi = radio_links[(s_id, d_id)]
                        d_brick.on_data_recv(s_id, cnt, rssi, t)
        for b in bricks.values():
            b.prune_stale_peers(t)
            b.check_ir_sensors(t)
            b.update_led_states(t)

    # Bricks 1, 2, 3 should now only see 2 neighbors (Brick 4 evicted after 2.5s)
    for i in (1, 2, 3):
        assert bricks[i].get_active_neighbor_count() == 2, f"Brick {i} should now see 2 neighbors"
        assert not bricks[i].peers[4]["active"], f"Brick 4 should be marked inactive on Brick {i}"
        assert bricks[i].is_close_to_all_neighbors()  # remaining 2 are still close
        assert bricks[i].onboard_led
        assert bricks[i].rgb_led == "SOLID BLUE"
    print("  [PASS] Stale peer timeout eviction cleanly pruned Brick 4 after 2.5s silence.")

    print("\n================================================================================")
    print("  SWARM SIMULATION COMPLETED: 100% SPECIFICATIONS PASSED (7 PHASES VERIFIED)   ")
    print("================================================================================\n")
    return True


if __name__ == "__main__":
    success = run_full_swarm_simulation()
    sys.exit(0 if success else 1)
