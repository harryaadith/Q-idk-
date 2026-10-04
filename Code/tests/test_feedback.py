from pathlib import Path
import subprocess, tempfile
code = Path(__file__).resolve().parents[1]
sketch = (code / "brick/brick.ino").read_text()
wrappers = sketch[sketch.index("PulseOutput motorFeedback"):sketch.index("// 5. 9-DoF")]
source = r"""
#include <cassert>
#include <cstdint>
#define HIGH 1
#define LOW 0
#define PIN_ERM_MOTOR 26
#define PIN_BUZZER 27
#define BUZZER_ENABLED true
uint32_t now=0;
int levels[36]={};
uint32_t millis(){return now;}
void digitalWrite(uint8_t pin,int value){levels[pin]=value;}
#include "PulseOutput.h"
""" + wrappers + r"""
int main() {
 buzzRfidSuccess();triggerErmHaptic(100);
 assert(now==0 && levels[27]==HIGH && levels[26]==HIGH);
 now=60;serviceFeedback();assert(levels[27]==LOW && levels[26]==HIGH);
 now=100;serviceFeedback();assert(levels[27]==HIGH && levels[26]==LOW);
 now=200;serviceFeedback();assert(levels[27]==LOW);
 // Multiple simultaneous face events never delay the sensor loop.
 for(int i=0;i<4;i++){buzzDockChime();triggerErmHaptic(80);}
 assert(now==200);now=400;serviceFeedback();assert(levels[26]==LOW && levels[27]==LOW);
 now=0xFFFFFFF0u;triggerErmDoublePulse();now+=50;serviceFeedback();assert(levels[26]==LOW);
 now+=40;serviceFeedback();assert(levels[26]==HIGH);
 now+=70;serviceFeedback();assert(levels[26]==LOW);
 buzzBootChime();now+=500;serviceFeedback();assert(levels[27]==LOW); // Skipped deadlines finish safely.
 triggerErmHaptic(100);triggerErmHaptic(0);assert(levels[26]==LOW);
}
"""
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp);(path/'test.cpp').write_text(source)
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(code/'brick'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
print('PASS: actual feedback wrappers, concurrent outputs, docking burst without delay, replacement, expiry and timer wrap.')
