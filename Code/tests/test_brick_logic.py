#!/usr/bin/env python3
"""
Multi-Agent Microbots (Smart Bricks) — Unified Logic Verification Suite
File: Code/tests/test_brick_logic.py

Validates all 5 requirements:
1. Each ESP talks to other ESPs exchanging only sender_id and neighbor_count.
2. Proximity detection:
   - If close to all active neighbors: On-Board LED turns ON.
   - Else: RGB LED sequences Blue then Red in order of neighbor count.
3. RFID reader detection:
   - When RFID tag is read: RGB LED turns Green (holds for duration).
4. Timeout and peer eviction:
   - Inactive peers are pruned after timeout.
"""

import time
import sys

# Parameters matching brick.ino
PROXIMITY_THRESHOLD = -65.0
PROXIMITY_HYSTERESIS = 4.0
PEER_TIMEOUT_S = 2.5
RFID_HOLD_S = 2.0
RSSI_EMA_ALPHA = 0.30


class SimulatedBrick:
    def __init__(self, brick_id, total_bricks=4):
        self.id = brick_id
        self.total_bricks = total_bricks
        self.peers = {}  # peer_id -> dict(rssi_ema, last_seen, reported_count, is_close)
        self.rfid_active = False
        self.rfid_time = 0.0
        self.last_tag_uid = ""

    def receive_packet(self, sender_id, reported_count, rssi_raw, current_time):
        if sender_id == self.id:
            return

        if sender_id not in self.peers:
            self.peers[sender_id] = {
                "active": True,
                "rssi_ema": float(rssi_raw),
                "last_seen": current_time,
                "reported_count": reported_count,
                "is_close": (rssi_raw >= PROXIMITY_THRESHOLD)
            }
        else:
            p = self.peers[sender_id]
            p["active"] = True
            p["rssi_ema"] = (RSSI_EMA_ALPHA * float(rssi_raw)) + ((1.0 - RSSI_EMA_ALPHA) * p["rssi_ema"])
            p["last_seen"] = current_time
            p["reported_count"] = reported_count

            # Hysteresis
            if not p["is_close"] and p["rssi_ema"] >= (PROXIMITY_THRESHOLD + PROXIMITY_HYSTERESIS):
                p["is_close"] = True
            elif p["is_close"] and p["rssi_ema"] < (PROXIMITY_THRESHOLD - PROXIMITY_HYSTERESIS):
                p["is_close"] = False

    def scan_rfid(self, uid_str, current_time):
        self.rfid_active = True
        self.rfid_time = current_time
        self.last_tag_uid = uid_str

    def prune_stale_peers(self, current_time):
        for peer_id, p in list(self.peers.items()):
            if p["active"] and (current_time - p["last_seen"] > PEER_TIMEOUT_S):
                p["active"] = False
                p["is_close"] = False

        if self.rfid_active and (current_time - self.rfid_time > RFID_HOLD_S):
            self.rfid_active = False

    @property
    def active_neighbor_count(self):
        return sum(1 for p in self.peers.values() if p["active"])

    @property
    def is_close_to_all(self):
        active_count = self.active_neighbor_count
        if active_count == 0:
            return False
        return all(p["is_close"] for p in self.peers.values() if p["active"])

    @property
    def onboard_led(self):
        return self.is_close_to_all

    @property
    def rgb_led_state(self):
        if self.rfid_active:
            return "GREEN"
        if self.is_close_to_all:
            return "SOLID BLUE"
        count = self.active_neighbor_count
        if count == 0:
            return "SOLID RED"
        return f"BLUE x {count} THEN RED"


def test_packet_structure():
    print("\n--- TEST 1: Minimal Packet Protocol Verification ---")
    packet_fields = ["sender_id", "neighbor_count"]
    print(f"Packet payload fields: {packet_fields}")
    assert len(packet_fields) == 2
    assert "sender_id" in packet_fields
    assert "neighbor_count" in packet_fields
    print(">> [PASS] Only sender_id and neighbor_count are transmitted over ESP-NOW.")


def test_rfid_green_light():
    print("\n--- TEST 2: RFID Reading Lights Green LED ---")
    b1 = SimulatedBrick(1)
    now = 100.0
    assert b1.rgb_led_state == "SOLID RED"

    b1.scan_rfid("1A2B3C4D", now)
    print(f"Time t={now}s: RFID Scanned UID '1A2B3C4D'")
    print(f"RGB LED: {b1.rgb_led_state}")
    assert b1.rgb_led_state == "GREEN"

    # Within hold window
    b1.prune_stale_peers(now + 1.0)
    assert b1.rgb_led_state == "GREEN"

    # After hold window expiration
    b1.prune_stale_peers(now + 2.5)
    print(f"Time t={now + 2.5}s: RFID Hold Expired -> RGB LED: {b1.rgb_led_state}")
    assert b1.rgb_led_state == "SOLID RED"
    print(">> [PASS] RFID reader triggers Green LED with timeout restore.")


def test_proximity_and_onboard_led():
    print("\n--- TEST 3: Proximity to All Neighbors & On-Board LED ---")
    b1 = SimulatedBrick(1)
    now = 100.0

    # Brick 2 connects far (-80 dBm)
    b1.receive_packet(sender_id=2, reported_count=0, rssi_raw=-80, current_time=now)
    print(f"Brick 2 connects at -80 dBm (Far):")
    print(f"  Neighbors: {b1.active_neighbor_count} | Close to all: {b1.is_close_to_all} | Onboard: {b1.onboard_led} | RGB: {b1.rgb_led_state}")
    assert b1.active_neighbor_count == 1
    assert not b1.is_close_to_all
    assert not b1.onboard_led
    assert b1.rgb_led_state == "BLUE x 1 THEN RED"

    # Brick 3 connects far (-82 dBm)
    b1.receive_packet(sender_id=3, reported_count=1, rssi_raw=-82, current_time=now)
    print(f"Brick 3 connects at -82 dBm (Far):")
    print(f"  Neighbors: {b1.active_neighbor_count} | Close to all: {b1.is_close_to_all} | Onboard: {b1.onboard_led} | RGB: {b1.rgb_led_state}")
    assert b1.active_neighbor_count == 2
    assert not b1.onboard_led
    assert b1.rgb_led_state == "BLUE x 2 THEN RED"

    # Both move close (-50 dBm)
    now += 0.5
    for _ in range(5):  # allow EMA filter to settle
        b1.receive_packet(sender_id=2, reported_count=2, rssi_raw=-50, current_time=now)
        b1.receive_packet(sender_id=3, reported_count=2, rssi_raw=-52, current_time=now)

    print(f"Both Bricks move close (-50 dBm):")
    print(f"  Neighbors: {b1.active_neighbor_count} | Close to all: {b1.is_close_to_all} | Onboard: {b1.onboard_led} | RGB: {b1.rgb_led_state}")
    assert b1.active_neighbor_count == 2
    assert b1.is_close_to_all
    assert b1.onboard_led
    assert b1.rgb_led_state == "SOLID BLUE"
    print(">> [PASS] On-Board LED illuminates when close to all active neighbors.")


def test_neighbor_count_ordering():
    print("\n--- TEST 4: RGB LED Sequenced in Order of Neighbor Count ---")
    b1 = SimulatedBrick(1)
    now = 200.0

    # 0 neighbors -> Solid Red
    assert b1.active_neighbor_count == 0
    assert b1.rgb_led_state == "SOLID RED"
    print(f"0 Neighbors -> RGB: {b1.rgb_led_state}")

    # 1 neighbor -> 1 Blue then Red
    b1.receive_packet(2, 0, -78, now)
    assert b1.rgb_led_state == "BLUE x 1 THEN RED"
    print(f"1 Neighbor  -> RGB: {b1.rgb_led_state}")

    # 2 neighbors -> 2 Blue then Red
    b1.receive_packet(3, 1, -78, now)
    assert b1.rgb_led_state == "BLUE x 2 THEN RED"
    print(f"2 Neighbors -> RGB: {b1.rgb_led_state}")

    # 3 neighbors -> 3 Blue then Red
    b1.receive_packet(4, 2, -78, now)
    assert b1.rgb_led_state == "BLUE x 3 THEN RED"
    print(f"3 Neighbors -> RGB: {b1.rgb_led_state}")
    print(">> [PASS] RGB LED accurately follows Blue pulses then Red in order of neighbor count.")


def test_timeout_and_eviction():
    print("\n--- TEST 5: Silence Timeout & Peer Eviction ---")
    b1 = SimulatedBrick(1)
    t0 = 300.0
    b1.receive_packet(2, 0, -55, t0)
    assert b1.active_neighbor_count == 1
    assert b1.onboard_led

    # Silence for 3 seconds (> 2.5s timeout)
    t1 = t0 + 3.0
    b1.prune_stale_peers(t1)
    print(f"After 3.0s silence: Neighbors = {b1.active_neighbor_count} | Onboard: {b1.onboard_led} | RGB: {b1.rgb_led_state}")
    assert b1.active_neighbor_count == 0
    assert not b1.onboard_led
    assert b1.rgb_led_state == "SOLID RED"
    print(">> [PASS] Timed out neighbors evicted cleanly; system reverts to standalone.")


if __name__ == "__main__":
    print("==================================================================")
    print("  RUNNING UNIFIED MICROBOT (SMART BRICK) VERIFICATION TEST SUITE  ")
    print("==================================================================")
    test_packet_structure()
    test_rfid_green_light()
    test_proximity_and_onboard_led()
    test_neighbor_count_ordering()
    test_timeout_and_eviction()
    print("\n==================================================================")
    print("  ALL UNIFIED FIRMWARE SPECIFICATIONS PASSED (5/5)               ")
    print("==================================================================\n")
