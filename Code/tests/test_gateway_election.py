from pathlib import Path
import tempfile, subprocess
root=Path(__file__).resolve().parents[1]
s=(root/'brick/brick.ino').read_text()
function=s[s.index('void serviceDashboard() {'):s.index('// Periodic serial console diagnostics')]
prefix='#include <cstdint>\n#include <cassert>\n#define TOTAL_SWARM_BRICKS 4\n#define PEER_TIMEOUT_MS 2500\n#define WIFI_CHANNEL 1\n#define WIFI_STA 1\n#define WIFI_AP_STA 3\nuint32_t now=100; uint32_t millis(){return now;}\nuint64_t nodeUid=200, electionCandidate=0; uint32_t candidateSince=0; bool dashboardHost=false;\nconst char* TRAY_SSID="tray"; const char* TRAY_PASSWORD="password";\nstruct Peer { bool active=false; uint32_t last_seen_ms=0; uint64_t uid=0; } peers[5];\nstruct Wifi { void mode(int){} void softAPdisconnect(bool){} bool softAP(const char*,const char*,int){return true;} } WiFi;\nstruct Server { void stop(){} void begin(){} void handleClient(){} } dashboardServer;\nstruct Log { void println(const char*){} } Serial;\n'
checks='int main(){\n serviceDashboard(); assert(!dashboardHost); now=4200; serviceDashboard(); assert(dashboardHost);\n peers[1]={true,now,100}; serviceDashboard(); assert(!dashboardHost);\n now=8000; serviceDashboard(); assert(!dashboardHost); now=12100; serviceDashboard(); assert(dashboardHost);\n peers[1]={true,now,300}; serviceDashboard(); assert(dashboardHost);\n}'
with tempfile.TemporaryDirectory() as folder:
    source=Path(folder)/'election.cpp'
    binary=Path(folder)/'election'
    source.write_text(prefix+function+checks)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: actual election function settling, lower UID takeover, timeout failover, higher UID arrival.')
