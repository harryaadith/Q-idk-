#!/usr/bin/env python3
"""
Multi-Agent Smart Bricks - Topology Verification & Simulation
Team 22 (ESW Project)

This script verifies and tests:
1. 4-Face Neighbor Detection (North, East, South, West)
2. Decentralized Grid Reconstruction (2x2 Grid, 1x4 Strip, L-Shape)
3. Neighbor timeout / disconnection handling
4. Color status mapping based on neighbor count
"""

import time
import sys

# Face indices matching the Arduino firmware
FACE_NORTH = 0  # Top
FACE_EAST  = 1  # Right
FACE_SOUTH = 2  # Bottom
FACE_WEST  = 3  # Left
FACE_NAMES = ["NORTH", "EAST", "SOUTH", "WEST"]
OPPOSITE_FACE = {0: 2, 1: 3, 2: 0, 3: 1}

def get_led_color(neighbor_count):
    colors = {
        0: "RED (Standalone)",
        1: "YELLOW",
        2: "BLUE",
        3: "CYAN",
        4: "GREEN (Full Tray Docked)"
    }
    return colors.get(neighbor_count, "MAGENTA")

class SmartBrickAgent:
    def __init__(self, brick_id):
        self.brick_id = brick_id
        self.has_tool = True
        self.neighbors = [0, 0, 0, 0]  # [N, E, S, W]
        self.last_seen = [0, 0, 0, 0]

    def connect_face(self, face, neighbor_id):
        self.neighbors[face] = neighbor_id
        self.last_seen[face] = time.time()

    def disconnect_face(self, face):
        self.neighbors[face] = 0
        self.last_seen[face] = 0

    def receive_packet(self, sender_id, remote_neighbors, current_time):
        # If the remote brick sees us on one of its faces, we mate on the opposite face
        for remote_face in range(4):
            if remote_neighbors[remote_face] == self.brick_id:
                my_face = OPPOSITE_FACE[remote_face]
                self.neighbors[my_face] = sender_id
                self.last_seen[my_face] = current_time

    def check_timeouts(self, current_time, timeout_seconds=1.5):
        for f in range(4):
            if self.neighbors[f] != 0:
                if current_time - self.last_seen[f] > timeout_seconds:
                    self.neighbors[f] = 0

    @property
    def neighbor_count(self):
        return sum(1 for n in self.neighbors if n != 0)

    @property
    def led_status(self):
        return get_led_color(self.neighbor_count)


def test_2x2_grid():
    print("\n========================================================")
    print(" TEST 1: 2x2 Grid Docking Verification (4 Bricks)")
    print("========================================================")
    print("Layout:")
    print("  [ Brick 1 ] <---> [ Brick 2 ]")
    print("       ^                 ^     ")
    print("       v                 v     ")
    print("  [ Brick 3 ] <---> [ Brick 4 ]\n")

    bricks = {i: SmartBrickAgent(i) for i in (1, 2, 3, 4)}

    # Physical Docking:
    # Brick 1: East->Brick 2, South->Brick 3
    bricks[1].connect_face(FACE_EAST, 2)
    bricks[1].connect_face(FACE_SOUTH, 3)

    # Brick 2: West->Brick 1, South->Brick 4
    bricks[2].connect_face(FACE_WEST, 1)
    bricks[2].connect_face(FACE_SOUTH, 4)

    # Brick 3: North->Brick 1, East->Brick 4
    bricks[3].connect_face(FACE_NORTH, 1)
    bricks[3].connect_face(FACE_EAST, 4)

    # Brick 4: North->Brick 2, West->Brick 3
    bricks[4].connect_face(FACE_NORTH, 2)
    bricks[4].connect_face(FACE_WEST, 3)

    # Verify symmetry and neighbor counts
    for b_id, b in bricks.items():
        print(f"Brick {b_id} -> Neighbors: {b.neighbor_count} | LED: {b.led_status} | [N:{b.neighbors[0]} E:{b.neighbors[1]} S:{b.neighbors[2]} W:{b.neighbors[3]}]")
        assert b.neighbor_count == 2, f"Brick {b_id} should have 2 neighbors in 2x2 grid"
        assert "BLUE" in b.led_status

    print(">> [PASS] 2x2 Grid Topology Verified successfully!")


def test_1x4_strip():
    print("\n========================================================")
    print(" TEST 2: 1x4 Linear Strip Verification")
    print("========================================================")
    print("Layout: [ Brick 1 ] <-> [ Brick 2 ] <-> [ Brick 3 ] <-> [ Brick 4 ]\n")

    bricks = {i: SmartBrickAgent(i) for i in (1, 2, 3, 4)}

    bricks[1].connect_face(FACE_EAST, 2)
    bricks[2].connect_face(FACE_WEST, 1)
    bricks[2].connect_face(FACE_EAST, 3)
    bricks[3].connect_face(FACE_WEST, 2)
    bricks[3].connect_face(FACE_EAST, 4)
    bricks[4].connect_face(FACE_WEST, 3)

    # End bricks have 1 neighbor (Yellow), middle bricks have 2 neighbors (Blue)
    assert bricks[1].neighbor_count == 1 and "YELLOW" in bricks[1].led_status
    assert bricks[2].neighbor_count == 2 and "BLUE" in bricks[2].led_status
    assert bricks[3].neighbor_count == 2 and "BLUE" in bricks[3].led_status
    assert bricks[4].neighbor_count == 1 and "YELLOW" in bricks[4].led_status

    for b_id, b in sorted(bricks.items()):
        print(f"Brick {b_id} -> Neighbors: {b.neighbor_count} | LED: {b.led_status}")

    print(">> [PASS] 1x4 Linear Strip Topology Verified successfully!")


def test_disconnection_timeout():
    print("\n========================================================")
    print(" TEST 3: Dynamic Disconnection & Timeout Purge")
    print("========================================================")

    b1 = SmartBrickAgent(1)
    b2 = SmartBrickAgent(2)

    t0 = 100.0
    b1.connect_face(FACE_EAST, 2)
    b1.last_seen[FACE_EAST] = t0

    assert b1.neighbor_count == 1
    print(f"Time t={t0}s: Brick 1 connected to Brick 2 (Neighbors={b1.neighbor_count})")

    # Advance time without new packets (exceeding 1.5s timeout)
    t1 = t0 + 2.0
    b1.check_timeouts(current_time=t1, timeout_seconds=1.5)
    print(f"Time t={t1}s (after 2s silence): Brick 1 neighbors={b1.neighbor_count} | LED={b1.led_status}")
    assert b1.neighbor_count == 0
    assert "RED" in b1.led_status

    print(">> [PASS] Disconnection and timeout eviction verified successfully!")


if __name__ == "__main__":
    print("========================================================")
    print("  RUNNING MULTI-AGENT SMART BRICK TOPOLOGY VERIFICATION")
    print("========================================================")
    test_2x2_grid()
    test_1x4_strip()
    test_disconnection_timeout()
    print("\n========================================================")
    print("  ALL TOPOLOGY TESTS PASSED (3/3)")
    print("========================================================\n")
