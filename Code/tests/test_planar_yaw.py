from pathlib import Path
import subprocess, tempfile
code=Path(__file__).resolve().parents[1]
source=r'''
#include <cassert>
#include <cmath>
#include <initializer_list>
#include "PlanarYaw.h"
void calibrate(PlanarYaw &yaw, uint32_t start=100, float bias=.5f, float z=1) {
 for(uint32_t t=0;t<=3000;t+=100) yaw.sample(start+t,0,0,z,0,0,bias);
 assert(yaw.state()==PlanarYaw::Ready);
}
int main() {
 PlanarYaw yaw;assert(!yaw.valid(0));calibrate(yaw);assert(std::fabs(yaw.bias()-.5f)<.001);
 for(uint32_t t=3200;t<=4100;t+=100) yaw.sample(t,0,0,1,0,0,-89.5);
 assert(std::fabs(yaw.yaw()-90)<.01);assert(yaw.valid(4100));assert(!yaw.valid(4700));
 yaw.sample(5600,0,0,1,0,0,.5);assert(yaw.state()==PlanarYaw::Ready); // 1.5s gap tolerated
 yaw.sample(8300,0,0,1,0,0,.5);assert(yaw.state()==PlanarYaw::TrackingLost); // >2.5s gap causes TrackingLost
 yaw.sample(8400,0,0,1,0,0,.5);assert(!yaw.valid(8400));
 yaw.reset();calibrate(yaw);yaw.sample(3200,.8,0,.6,0,0,.5);assert(yaw.state()==PlanarYaw::TrackingLost); // >45 deg tilt
 yaw.reset();calibrate(yaw);yaw.sample(3200,0,0,1,0,0,245);assert(yaw.state()==PlanarYaw::TrackingLost);
 yaw.reset();calibrate(yaw,100,.5,-1);
 for(uint32_t t=3200;t<=4100;t+=100) yaw.sample(t,0,0,-1,0,0,90.5);
 assert(std::fabs(yaw.yaw()-90)<.01); // Upside-down sensor, same clockwise physical rotation.
 yaw.reset();for(uint32_t t=100;t<3100;t+=100) yaw.sample(t,0,0,1,0,0,25);
 assert(yaw.state()==PlanarYaw::Calibrating);calibrate(yaw,3200);
 // Real-world hardware with resting factory bias (gx=7.2, gy=4.5, gz=0.7) calibrates and tracks:
 {
  yaw.reset();
  const float ax = -0.133f, ay = 0.025f, az = 1.092f;
  const float mag = std::sqrt(ax*ax + ay*ay + az*az);
  for(uint32_t t=100;t<=3100;t+=100) yaw.sample(t,ax,ay,az,7.214f,4.5f,0.7f);
  assert(yaw.state()==PlanarYaw::Ready);
  for(uint32_t t=3200;t<=4100;t+=100)
   yaw.sample(t,ax,ay,az,7.214f - 90.f*ax/mag, 4.5f - 90.f*ay/mag, 0.7f - 90.f*az/mag);
  assert(std::fabs(yaw.yaw()-90)<.02);
 }
 // A tilted or sideways IMU mount calibrates and tracks the physical vertical axis.
 for (float tilt : {0.1745329f, .7853982f, 1.5707963f}) {
  yaw.reset();float x=std::sin(tilt), z=std::cos(tilt);
  for(uint32_t t=100;t<=3100;t+=100) yaw.sample(t,x,0,z,.4,-.3,.5);
  assert(yaw.state()==PlanarYaw::Ready);
  for(uint32_t t=3200;t<=4100;t+=100) yaw.sample(t,x,0,z,.4-90*x,-.3,.5-90*z);
  assert(std::fabs(yaw.yaw()-90)<.02);
 }
 yaw.reset();calibrate(yaw);yaw.sample(3200,.173648f,0,.984808f,0,0,.5);
 assert(yaw.state()==PlanarYaw::Ready); // 10-degree tilt is tolerated.
 yaw.sample(3300,.5f,0,.866f,0,0,.5);
 assert(yaw.state()==PlanarYaw::Ready); // 30-degree tilt is tolerated.
 yaw.reset();for(uint32_t t=100;t<=4000;t+=100) yaw.sample(t,0,0,0,0,0,0);
 assert(yaw.state()==PlanarYaw::Calibrating); // Free fall is not a valid reference.
 yaw.reset();for(uint32_t t=100;t<=4000;t+=100) yaw.sample(t,0,0,1,t%200 ? 1 : -1,0,0);
 assert(yaw.state()==PlanarYaw::Calibrating); // Noise on X is checked too.
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
print('PASS: stationary bias, relative yaw, inverted/tilted/sideways mounts, drift freshness, gaps/tilt/saturation, explicit reset and timer wrap.')
