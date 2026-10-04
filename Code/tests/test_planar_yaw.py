from pathlib import Path
import subprocess, tempfile
code=Path(__file__).resolve().parents[1]
source=r'''
#include <cassert>
#include <cmath>
#include "PlanarYaw.h"
void calibrate(PlanarYaw &yaw, uint32_t start=100, float bias=.5f, float z=1) {
 for(uint32_t t=0;t<=3000;t+=100) yaw.sample(start+t,0,0,z,0,0,bias);
 assert(yaw.state()==PlanarYaw::Ready);
}
int main() {
 PlanarYaw yaw;assert(!yaw.valid(0));calibrate(yaw);assert(std::fabs(yaw.bias()-.5f)<.001);
 for(uint32_t t=3200;t<=4100;t+=100) yaw.sample(t,0,0,1,0,0,-89.5);
 assert(std::fabs(yaw.yaw()-90)<.01);assert(yaw.valid(4100));assert(!yaw.valid(4700));
 yaw.sample(4800,0,0,1,0,0,.5);assert(yaw.state()==PlanarYaw::TrackingLost);
 yaw.sample(4900,0,0,1,0,0,.5);assert(!yaw.valid(4900));
 yaw.reset();calibrate(yaw);yaw.sample(3200,.4,0,.9,0,0,.5);assert(yaw.state()==PlanarYaw::TrackingLost);
 yaw.reset();calibrate(yaw);yaw.sample(3200,0,0,1,0,0,245);assert(yaw.state()==PlanarYaw::TrackingLost);
 yaw.reset();calibrate(yaw,100,.5,-1);
 for(uint32_t t=3200;t<=4100;t+=100) yaw.sample(t,0,0,-1,0,0,90.5);
 assert(std::fabs(yaw.yaw()-90)<.01); // Upside-down sensor, same clockwise physical rotation.
 yaw.reset();for(uint32_t t=100;t<3100;t+=100) yaw.sample(t,0,0,1,0,0,10);
 assert(yaw.state()==PlanarYaw::Calibrating);calibrate(yaw,3200);
 yaw.reset();calibrate(yaw,0xFFFFFF00u);assert(yaw.valid(uint32_t(0xFFFFFF00u+3000u))); // millis wrap
 yaw.reset();calibrate(yaw);yaw.sample(3200,0,0,1,0,0,NAN);assert(yaw.state()==PlanarYaw::TrackingLost);
 yaw.reset();calibrate(yaw);for(uint32_t t=3200;t<=8100;t+=100) yaw.sample(t,0,0,1,0,0,-89.5);
 assert(std::fabs(yaw.yaw()-90)<.02); // More than one complete turn wraps.
}
'''
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp);(path/'yaw.cpp').write_text(source)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(code/'brick'),str(path/'yaw.cpp'),'-o',str(path/'yaw')],check=True)
 subprocess.run([str(path/'yaw')],check=True)
print('PASS: stationary bias, relative yaw, inverted mount, drift freshness, gaps/tilt/saturation, explicit reset and timer wrap.')
