#!/usr/bin/env python3
"""
Multi-Agent Smart Bricks — Visual Topology Simulator
Team 22 (ESW Project)

Simulates 4 autonomous ESP32 bricks communicating peer-to-peer via simulated
ESP-NOW broadcast packets and renders real-time 2D grid topologies and LED colors.
"""

import time
import sys

# 4 Cardinal Faces matching firmware
FACE_NORTH = 0  # Top
FACE_EAST  = 1  # Right
FACE_SOUTH = 2  # Bottom
FACE_WEST  = 3  # Left
FACE_NAMES = ["NORTH", "EAST", "SOUTH", "WEST"]
OPPOSITE_FACE = {0: 2, 1: 3, 2: 0, 3: 1}

def get_led_color(neighbor_count, has_tool=True):
    if not has_tool:
        return "\033[95m[BLINK MAGENTA - TOOL MISSING]\033[0m"
    colors = {
        0: "\033[91mRED (Standalone - 0 Neighbors)\033[0m",
        1: "\033[93mYELLOW (1 Neighbor)\033[0m",
        2: "\033[94mBLUE (2 Neighbors)\033[0m",
        3: "\033[96mCYAN (3 Neighbors)\033[0m",
        4: "\033[92mGREEN (4 Neighbors - Fully Docked)\033[0m"
    }
    return colors.get(neighbor_count, "\033[95mMAGENTA\033[0m")


class SimBrick:
    """Represents an autonomous smart brick node running firmware logic."""
    def __init__(self, brick_id, tool_name="Scalpel"):
        self.id = brick_id
        self.tool_name = tool_name
        self.has_tool = True
        self.physical_mating = [0, 0, 0, 0]  # [N, E, S, W] physical contacts
        self.face_table = [0, 0, 0, 0]       # Discovered neighbors via packets
        self.last_seen = [0.0, 0.0, 0.0, 0.0]

    def reset_connections(self):
        self.physical_mating = [0, 0, 0, 0]
        self.face_table = [0, 0, 0, 0]
        self.last_seen = [0.0, 0.0, 0.0, 0.0]

    def dock_to(self, my_face, other_brick, other_face):
        """Simulate physical snapping of two brick faces."""
        self.physical_mating[my_face] = other_brick.id
        other_brick.physical_mating[other_face] = self.id

    def undock_face(self, my_face, other_brick=None):
        """Simulate disconnecting a face."""
        self.physical_mating[my_face] = 0
        if other_brick:
            opp = OPPOSITE_FACE[my_face]
            other_brick.physical_mating[opp] = 0

    def generate_packet(self):
        """Generates the ESP-NOW broadcast packet."""
        return {
            "sender_id": self.id,
            "has_tool": self.has_tool,
            "neighbors": list(self.physical_mating)
        }

    def receive_packet(self, packet, current_time):
        """Process incoming packet according to smart_brick_topology.ino."""
        sender = packet["sender_id"]
        if sender == self.id:
            return

        for remote_face in range(4):
            if packet["neighbors"][remote_face] == self.id:
                my_face = OPPOSITE_FACE[remote_face]
                self.face_table[my_face] = sender
                self.last_seen[my_face] = current_time

    def check_timeouts(self, current_time, timeout=1.5):
        """Purges neighbors not heard from within timeout period."""
        for f in range(4):
            if self.face_table[f] != 0:
                if current_time - self.last_seen[f] > timeout:
                    self.face_table[f] = 0

    @property
    def neighbor_count(self):
        return sum(1 for n in self.face_table if n != 0)

    @property
    def led_status(self):
        return get_led_color(self.neighbor_count, self.has_tool)


def run_packet_cycle(bricks, current_time):
    """Simulate one round of wireless ESP-NOW broadcast exchange."""
    packets = [b.generate_packet() for b in bricks.values()]
    for b in bricks.values():
        for pkt in packets:
            b.receive_packet(pkt, current_time)
        b.check_timeouts(current_time)


def display_cluster_state(bricks, scenario_title):
    print("\n" + "=" * 65)
    print(f"  SCENARIO: {scenario_title}")
    print("=" * 65)
    for b_id in sorted(bricks.keys()):
        b = bricks[b_id]
        tool_str = f"{b.tool_name} (PRESENT)" if b.has_tool else "[MISSING]"
        print(f" Brick #{b.id:d} | Tool: {tool_str:<20} | LED: {b.led_status}")
        print(f"          Faces -> N:{b.face_table[0]}  E:{b.face_table[1]}  S:{b.face_table[2]}  W:{b.face_table[3]}")
    print("-" * 65)


def main():
    print("=================================================================")
    print("      MULTI-AGENT SMART BRICKS: TOPOLOGY & CLUSTER SIMULATOR     ")
    print("=================================================================")
    now = 100.0

    # Initialize 4 Bricks with surgical tools
    tools = {1: "Scalpel", 2: "Forceps", 3: "Hemostat", 4: "Retractor"}
    bricks = {i: SimBrick(i, tools[i]) for i in range(1, 5)}

    # -------------------------------------------------------------
    # Scenario 1: Isolated / Standalone (All on table separately)
    # -------------------------------------------------------------
    run_packet_cycle(bricks, now)
    display_cluster_state(bricks, "1. Isolated Bricks (0 Connections)")
    print(" ASCII Grid:  [Brick 1]   [Brick 2]   [Brick 3]   [Brick 4]")

    # -------------------------------------------------------------
    # Scenario 2: 1x4 Linear Surgical Strip
    # -------------------------------------------------------------
    for b in bricks.values():
        b.reset_connections()
    now += 2.0
    bricks[1].dock_to(FACE_EAST, bricks[2], FACE_WEST)
    bricks[2].dock_to(FACE_EAST, bricks[3], FACE_WEST)
    bricks[3].dock_to(FACE_EAST, bricks[4], FACE_WEST)
    run_packet_cycle(bricks, now)
    display_cluster_state(bricks, "2. 1x4 Linear Strip (1 <-> 2 <-> 3 <-> 4)")
    print(" ASCII Grid:  +---------+   +---------+   +---------+   +---------+")
    print("              | Brick 1 |<->| Brick 2 |<->| Brick 3 |<->| Brick 4 |")
    print("              +---------+   +---------+   +---------+   +---------+")

    # -------------------------------------------------------------
    # Scenario 3: 2x2 Modular Surgical Tray
    # -------------------------------------------------------------
    for b in bricks.values():
        b.reset_connections()
    now += 2.0

    bricks[1].dock_to(FACE_EAST, bricks[2], FACE_WEST)
    bricks[1].dock_to(FACE_SOUTH, bricks[3], FACE_NORTH)
    bricks[2].dock_to(FACE_SOUTH, bricks[4], FACE_NORTH)
    bricks[3].dock_to(FACE_EAST, bricks[4], FACE_WEST)
    run_packet_cycle(bricks, now)
    display_cluster_state(bricks, "3. 2x2 Modular Surgical Tray Grid")
    print(" ASCII Grid:")
    print("       +-----------+       +-----------+")
    print("       |  Brick 1  | <---> |  Brick 2  |")
    print("       | (Scalpel) |       | (Forceps) |")
    print("       +-----+-----+       +-----+-----+")
    print("             ^                   ^      ")
    print("             v                   v      ")
    print("       +-----+-----+       +-----+-----+")
    print("       |  Brick 3  | <---> |  Brick 4  |")
    print("       | (Hemostat)|       |(Retractor)|")
    print("       +-----------+       +-----------+")

    # -------------------------------------------------------------
    # Scenario 4: Tool Lifted from Brick 2 + Disconnect Brick 4
    # -------------------------------------------------------------
    bricks[2].has_tool = False  # Surgeon picks up Forceps
    # Undock Brick 4
    bricks[4].undock_face(FACE_NORTH, bricks[2])
    bricks[4].undock_face(FACE_WEST, bricks[3])

    # Advance time past timeout threshold for Brick 4
    now += 2.0
    run_packet_cycle(bricks, now)
    display_cluster_state(bricks, "4. Forceps Removed from Brick 2 & Brick 4 Detached")
    print("\n[SIMULATION COMPLETED SUCCESSFULLY]")


if __name__ == "__main__":
    main()
