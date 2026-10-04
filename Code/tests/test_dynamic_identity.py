from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
s=(root/'Code/brick/brick.ino').read_text()
# Run the actual peer registry and identity functions with a host queue adapter.
a=s[s.index('uint8_t displayNumber(uint64_t uid) {'):s.index('// Broadcast local state packet to all peers')]
source='''#include <cstdint>
#include <cstring>
#include <deque>
#include <cassert>
#define TOTAL_SWARM_BRICKS 4
typedef struct __attribute__((packed)) {
  uint32_t magic;          // Reject old/incompatible packets
  uint64_t sender_uid;     // Full hardware MAC identity; no manual assignment
  uint8_t neighbor_count;
  uint8_t face_mask;       // Body-frame N/E/S/W occupancy, never peer identity
  uint8_t heading_valid;  // Fresh aligned-start relative gyro yaw
  uint8_t orientation_state; // 0=no IMU, 1=calibrating, 2=ready, 3=tracking lost
  uint32_t alignment_age_ms;
  uint8_t command;         // 0=telemetry, 1=restart shared alignment
  float heading;
  char instrument_uid[21]; // Last scanned instrument token, not continuous presence
  uint8_t recent_scan;
} BrickPacket;
struct PeerInfo { BrickPacket telemetry={}; uint64_t uid=0; bool active=false; uint8_t reported_neighbor_count=0; };
struct ReceivedPacket { BrickPacket packet; int8_t rssi; };
PeerInfo peers[5]; uint64_t nodeUid=200; uint8_t BRICK_ID=1;
const uint32_t PACKET_MAGIC=0x54524132;
int resets=0;struct Yaw {void reset(){resets++;}} gyroYaw;
struct Imu {float heading=0;} imu;
std::deque<ReceivedPacket> queue;
void* receivedPackets=&queue;
#define pdTRUE 1
struct Rx { int8_t rssi; };
struct esp_now_recv_info_t { Rx* rx_ctrl; };
void xQueueSend(void*, ReceivedPacket* p, int) { queue.push_back(*p); }
int xQueueReceive(void*, ReceivedPacket* p, int) { if(queue.empty()) return 0; *p=queue.front(); queue.pop_front(); return 1; }
void evaluateProximity(uint8_t slot, int8_t) { peers[slot].active=true; }
'''+a+'''
void send(uint64_t uid, uint32_t magic=PACKET_MAGIC, uint8_t command=0) {
 BrickPacket p={}; p.magic=magic; p.sender_uid=uid; p.neighbor_count=2; p.command=command; onDataRecv(nullptr,(uint8_t*)&p,sizeof(p)); processReceivedPackets();
}
int main() {
 send(100,0x54524131); assert(resets==0); // Previous protocol must be rejected.
 send(200); send(0); send(100,0); assert(queue.empty()); assert(BRICK_ID==1);
 send(300); send(100); assert(BRICK_ID==2); assert(displayNumber(100)==1); assert(displayNumber(300)==3);
 send(100); int count=0; for(auto p:peers) count+=p.active; assert(count==2);
 for(auto &p:peers) if(p.uid==100) p.active=false;
 processReceivedPackets(); assert(BRICK_ID==1); // Timeout removes a lower-ranked peer.
 send(100); assert(BRICK_ID==2); // Rejoin keeps hardware identity.
 send(400); send(500); send(600); // Full registry must never overrun.
 assert(displayNumber(nodeUid)==2);
 imu.heading=90;send(100,PACKET_MAGIC,1);assert(resets==1);assert(imu.heading==0);
 send(100,PACKET_MAGIC,2);assert(resets==1); // Invalid command is rejected.
}
'''
import tempfile, os
with tempfile.TemporaryDirectory() as folder:
    os.chdir(folder)
    Path('work').mkdir()
    Path('work/identity_test.cpp').write_text(source)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','work/identity_test.cpp','-o','work/identity_test'],check=True)
    subprocess.run(['work/identity_test'],check=True)
print('PASS: actual firmware functions reject incompatible/self packets, handle arrival order, duplicates, timeout/rejoin and full capacity.')
