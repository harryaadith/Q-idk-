from pathlib import Path
import subprocess
import tempfile

code = Path(__file__).resolve().parents[1]
sketch = (code / "brick/brick.ino").read_text()
age = sketch[sketch.index("long sampleAge("):sketch.index("void debugRegister(")]
printer = sketch[sketch.index("void printSensorDebug()"):sketch.index("void applyDebugCommand(")]
commands = sketch[sketch.index("void applyDebugCommand("):sketch.index("// 11. ARDUINO SETUP")]
prefix = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include "DebugCommand.h"
#include "PlanarYaw.h"
PlanarYaw gyroYaw;
uint32_t now=2000;
uint32_t millis(){return now;}
struct SerialMock {
 std::string output, input;
 int available(){return input.size();}
 int read(){char value=input.front();input.erase(0,1);return value;}
 template<class... A> void printf(const char* format,A... args){char buffer[1024];std::snprintf(buffer,sizeof(buffer),format,args...);output+=buffer;}
 void println(const char* text){output+=text;output+='\n';}
} Serial;
struct Text : std::string { using std::string::string; };
Text lastReadUid;
std::string uidText(uint64_t){return "000000000123";}
uint64_t nodeUid=123; uint8_t BRICK_ID=1;
struct Imu {
 bool mpu_detected=false,mag_detected=false;
 float accel_x=0,accel_y=0,accel_z=0,gyro_x=0,gyro_y=0,gyro_z=0;
 float mag_x=0,mag_y=0,mag_z=0,pitch=0,roll=0,heading=0;
} imu;
uint32_t lastAccelSample=0,lastMagSample=0,rfidDetectedTime=0;
bool debugEnabled=false;uint32_t lastSensorDebugPrint=0;int probeCount=0,reinitCount=0;
bool whoamiEnabled=false;uint32_t lastWhoAmIPrint=0;
#define MPU9250_I2C_ADDR 0x68
#define AK8963_I2C_ADDR 0x0C
uint8_t i2cReadByte(uint8_t dev, uint8_t reg) {
  if (dev == 0x68 && reg == 0x75) return 0x71;
  if (dev == 0x0C && reg == 0x00) return 0x48;
  return 0xFF;
}
void printDebugBus(){probeCount++;}
bool initMpu9250(){reinitCount++;return false;}
int imuReadError=-1,imuReadBytes=0,magReadError=-1,magReadBytes=0,magStatusReadError=-1,magStatusReadBytes=0;
int16_t rawAccel[3]={},rawGyro[3]={},rawMag[3]={},rawTemperature=0;
uint8_t magStatus1=0,magStatus2=0;
bool compassCalibrated=false,compassCalibrating=false,rfidActive=false;
bool headingIsValid(){return imu.mpu_detected && gyroYaw.valid(now);}
const char* FACE_NAMES[]={"NORTH","EAST","SOUTH","WEST"};
const uint8_t IR_PINS[]={34,35,32,33};
bool irFaceDetected[4]={}; uint8_t irDebounceCounters[4]={};
int digitalRead(uint8_t pin){return pin==35 ? 0 : 1;}
struct RFID { int VersionReg=0; int PCD_ReadRegister(int){return 0x92;} } rfid;
#define TOTAL_SWARM_BRICKS 4
struct Peer {bool active=false;uint64_t uid=0;float rssi_ema=0;bool is_close=false;uint32_t last_seen_ms=0;} peers[5];
"""
checks = r"""
DebugCommand line(DebugCommandParser &parser,const char* text){
 DebugCommand result=DebugCommand::None;
 while(*text){auto command=parser.feed(*text++);if(command!=DebugCommand::None) result=command;}
 return result;
}
int main(){
 DebugCommandParser parser;
 assert(line(parser,"--deb")==DebugCommand::None);
 assert(line(parser,"ug\r")==DebugCommand::Enable);
 assert(line(parser,"\n")==DebugCommand::None);
 assert(line(parser,"--no-debug\n")==DebugCommand::Disable);
 assert(line(parser,"--debug-off\n")==DebugCommand::Disable);
 assert(line(parser,"--debug-i2c\n")==DebugCommand::Probe);
 assert(line(parser,"--debug-reinit\n")==DebugCommand::Reinitialize);
 assert(line(parser,"--whoami\n")==DebugCommand::WhoAmI);
 assert(line(parser,"--kill-whoami\n")==DebugCommand::KillWhoAmI);
 assert(line(parser,"--help\n")==DebugCommand::Help);
 assert(line(parser,"--debugging\n")==DebugCommand::Unknown);
 assert(line(parser,"--debugXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n")==DebugCommand::Unknown);
 assert(line(parser,"--debug\n")==DebugCommand::Enable); // recover after overflow
 printSensorDebug();
 assert(Serial.output.find("sample_age_ms=-1")!=std::string::npos);
 assert(Serial.output.find("face=NORTH gpio=34 raw=1")!=std::string::npos);
 assert(Serial.output.find("face=EAST gpio=35 raw=0")!=std::string::npos);
 assert(Serial.output.find("reader_version=0x92")!=std::string::npos);
 assert(Serial.output.find("heading_valid=0 calibrated=0")!=std::string::npos);
 Serial.output.clear(); imu.mpu_detected=true;imu.mag_detected=true;
 lastAccelSample=1900;lastMagSample=1900;rawAccel[2]=16384;imu.accel_z=1;
 rawMag[0]=100;imu.mag_x=15;lastReadUid="ABC123";rfidDetectedTime=1800;rfidActive=true;
 printSensorDebug();
 assert(Serial.output.find("boot_detected=1 sample_age_ms=100")!=std::string::npos);
 assert(Serial.output.find("raw_xyz=0,0,16384")!=std::string::npos);
 assert(Serial.output.find("last_instrument_uid=ABC123 scan_age_ms=200")!=std::string::npos);
 assert(Serial.output.find("heading_valid=0 calibrated=0")!=std::string::npos); // sensor present, uncalibrated
 for(uint32_t t=100;t<=3100;t+=100) gyroYaw.sample(t,0,0,1,0,0,0);
 now=3200;Serial.output.clear();printSensorDebug();
 assert(Serial.output.find("heading_valid=1 calibrated=1")!=std::string::npos);
 now=4000;Serial.output.clear();printSensorDebug();
 assert(Serial.output.find("heading_valid=0 calibrated=1")!=std::string::npos); // stale, calibrated
 Serial.output.clear();serviceSerialDebug();assert(Serial.output.empty());
 Serial.input="--debug\n";serviceSerialDebug();assert(debugEnabled);assert(probeCount==1);
 Serial.output.clear();now+=499;serviceSerialDebug();assert(Serial.output.empty());
 now++;serviceSerialDebug();assert(!Serial.output.empty());
 Serial.input="--no-debug\n";serviceSerialDebug();assert(!debugEnabled);
 Serial.output.clear();now+=1000;serviceSerialDebug();assert(Serial.output.empty());
 Serial.input="--debug-i2c\n";serviceSerialDebug();assert(probeCount==2);assert(!debugEnabled);
 Serial.input="--debug-reinit\n";serviceSerialDebug();assert(reinitCount==1);assert(lastMagSample==0);
 Serial.output.clear();
 Serial.input="--whoami\n";serviceSerialDebug();assert(whoamiEnabled);
 assert(Serial.output.find("[WHOAMI]")!=std::string::npos);
 assert(Serial.output.find("MPU6500/9250")!=std::string::npos);
 assert(Serial.output.find("AK8963")!=std::string::npos);
 assert(Serial.output.find("RFID MFRC522")!=std::string::npos);
 assert(Serial.output.find("IR NORTH")!=std::string::npos);
 Serial.output.clear();
 Serial.input="--kill-whoami\n";serviceSerialDebug();assert(!whoamiEnabled);
 assert(Serial.output.find("killed")!=std::string::npos);
 Serial.output.clear();
 Serial.input="--help\n";serviceSerialDebug();
 assert(Serial.output.find("AVAILABLE SERIAL COMMANDS")!=std::string::npos);
}
"""
with tempfile.TemporaryDirectory() as directory:
    source=Path(directory)/"debug.cpp"
    binary=Path(directory)/"debug"
    source.write_text(prefix+age+printer+commands+checks)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(code/"brick"),str(source),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("PASS: debug commands, split/CRLF input, overflow recovery, actual sensor output, missing/stale samples and calibration distinctions.")
