from pathlib import Path
import subprocess, tempfile
code=Path(__file__).resolve().parents[1]
sketch=(code/'brick/brick.ino').read_text()
init=sketch[sketch.index('bool initMpu9250()'):sketch.index('void readMpu9250()')]
source=r"""
#include <cassert>
#include <cstdint>
#include "PlanarYaw.h"
#define MPU9250_I2C_ADDR 0x68
#define AK8963_I2C_ADDR 0x0C
PlanarYaw gyroYaw;
struct {bool mpu_detected=false,mag_detected=false;} imu;
struct {void println(const char*){} template<class... A> void printf(const char*,A...){} } Serial;
uint8_t mpuId=0x71,magId=0x48,failReg=0xFF;
int lastI2cReadError=0,lastI2cReadBytes=1;
bool busPresent=true,magPresent=true;
struct {
 uint8_t address=0;
 void beginTransmission(uint8_t a){address=a;}
 int endTransmission(){return (address==0x68 ? busPresent : magPresent) ? 0 : 2;}
} Wire;
void delay(unsigned){}
uint8_t i2cReadByte(uint8_t address,uint8_t){return address==0x68 ? mpuId : magId;}
bool i2cWriteByte(uint8_t,uint8_t reg,uint8_t){return reg!=failReg;}
"""+init+r"""
int main(){
 assert(initMpu9250() && imu.mpu_detected && imu.mag_detected);
 mpuId=0x70;assert(initMpu9250() && imu.mpu_detected && !imu.mag_detected);
 mpuId=0x71;magPresent=false;assert(initMpu9250() && !imu.mag_detected);
 magPresent=true;magId=0x12;assert(initMpu9250() && !imu.mag_detected);
 magId=0x48;failReg=0x0A;assert(initMpu9250() && !imu.mag_detected);
 failReg=0x6B;assert(!initMpu9250() && !imu.mpu_detected);
 failReg=0x1B;assert(!initMpu9250());
 failReg=0x1C;assert(!initMpu9250());
 failReg=0xFF;mpuId=0x12;assert(!initMpu9250());
 mpuId=0x71;lastI2cReadBytes=0;assert(!initMpu9250());
 lastI2cReadBytes=1;lastI2cReadError=2;assert(!initMpu9250());
 lastI2cReadError=0;busPresent=false;assert(!initMpu9250());
}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(code/'brick'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual IMU initialization rejects missing/unsupported/failed setup, accepts MPU6500 and validates optional magnetometer.')
