#pragma once
#include <cmath>
#include <cstdint>

// No magnetic reference: clockwise yaw relative to a physically aligned start.
class PlanarYaw {
public:
  enum State : uint8_t { Calibrating = 1, Ready = 2, TrackingLost = 3 };
private:
  State state_ = Calibrating;
  bool haveSample_ = false, haveWindow_ = false;
  uint32_t last_ = 0, windowStart_ = 0, alignedAt_ = 0;
  unsigned count_ = 0;
  double sum_ = 0, sumSquares_ = 0;
  float bias_ = 0, yaw_ = 0, zSign_ = 1;
  void clearWindow() { haveWindow_ = false; count_ = 0; sum_ = 0; sumSquares_ = 0; }
public:
  void reset() { *this = PlanarYaw(); }
  State state() const { return state_; }
  float yaw() const { return yaw_; }
  float bias() const { return bias_; }
  uint32_t alignmentAge(uint32_t now) const { return state_ == Ready ? now - alignedAt_ : 0; }
  bool valid(uint32_t now) const { return state_ == Ready && haveSample_ && now - last_ <= 500; }
  void sample(uint32_t now, float ax, float ay, float az, float gx, float gy, float gz) {
    if (state_ == TrackingLost) return; // Explicit realignment required; never guess lost turns.
    bool finite = std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az) &&
                  std::isfinite(gx) && std::isfinite(gy) && std::isfinite(gz);
    bool flat = finite && std::fabs(ax) < .15f && std::fabs(ay) < .15f &&
                std::fabs(az) > .85f && std::fabs(az) < 1.15f;
    uint32_t dt = haveSample_ ? now - last_ : 0;
    bool gap = haveSample_ && dt > 500;
    last_ = now; haveSample_ = true;
    if (state_ == Calibrating) {
      bool still = flat && std::fabs(gx) < 3 && std::fabs(gy) < 3 && std::fabs(gz) < 3;
      if (!still || gap) { clearWindow(); return; }
      if (!haveWindow_) { clearWindow(); windowStart_ = now; haveWindow_ = true; }
      count_++; sum_ += gz; sumSquares_ += double(gz) * gz;
      if (now - windowStart_ >= 3000 && count_ >= 20) {
        double mean = sum_ / count_;
        double variance = sumSquares_ / count_ - mean * mean;
        if (variance > .0225) { clearWindow(); return; } // 0.15 deg/s standard deviation.
        bias_ = float(mean); zSign_ = az > 0 ? -1.f : 1.f;
        yaw_ = 0; alignedAt_ = now; state_ = Ready;
      }
      return;
    }
    if (!flat || gap || std::fabs(gz) >= 240 || (az > 0 ? -1.f : 1.f) != zSign_) {
      state_ = TrackingLost; return;
    }
    yaw_ = std::fmod(yaw_ + zSign_ * (gz - bias_) * (dt / 1000.f), 360.f);
    if (yaw_ < 0) yaw_ += 360.f;
  }
};
