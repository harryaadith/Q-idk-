#pragma once
#include <cstdint>

// One bounded, nonblocking alternating HIGH/LOW pattern per actuator.
// A new event replaces an unfinished pattern instead of building a blocking backlog.
class PulseOutput {
  uint8_t pin_, count_ = 0, index_ = 0;
  uint16_t durations_[5] = {};
  uint32_t started_ = 0;
public:
  explicit PulseOutput(uint8_t pin) : pin_(pin) {}
  void start(uint32_t now, uint16_t first, uint16_t gap=0, uint16_t second=0,
             uint16_t gap2=0, uint16_t third=0) {
    durations_[0]=first; durations_[1]=gap; durations_[2]=second;
    durations_[3]=gap2; durations_[4]=third;
    count_=third ? 5 : second ? 3 : first ? 1 : 0;
    index_=0; started_=now; digitalWrite(pin_, count_ ? HIGH : LOW);
  }
  void service(uint32_t now) {
    while (count_ && now-started_ >= durations_[index_]) {
      started_ += durations_[index_];
      if (++index_ == count_) { count_=0; digitalWrite(pin_,LOW); return; }
      digitalWrite(pin_, index_%2 ? LOW : HIGH);
    }
  }
};
