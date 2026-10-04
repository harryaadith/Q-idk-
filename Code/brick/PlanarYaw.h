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
  double sums_[3] = {}, squares_[3] = {}, accelSums_[3] = {}, accelSquares_[3] = {};
  float biases_[3] = {}, gravity_[3] = {}, yaw_ = 0;
  void clearWindow() {
    haveWindow_ = false; count_ = 0;
    for (int i=0; i<3; ++i) sums_[i] = squares_[i] = accelSums_[i] = accelSquares_[i] = 0;
  }
public:
  void reset() { *this = PlanarYaw(); }
  State state() const { return state_; }
  float yaw() const { return yaw_; }
  float bias() const { return biases_[2]; }
  uint32_t alignmentAge(uint32_t now) const { return state_ == Ready ? now - alignedAt_ : 0; }
  bool valid(uint32_t now) const { return state_ == Ready && haveSample_ && now - last_ <= 500; }
  void sample(uint32_t now, float ax, float ay, float az, float gx, float gy, float gz) {
    if (state_ == TrackingLost) return; // Explicit realignment required; never guess lost turns.
    bool finite = std::isfinite(ax) && std::isfinite(ay) && std::isfinite(az) &&
                  std::isfinite(gx) && std::isfinite(gy) && std::isfinite(gz);
    const float accel[3] = {ax, ay, az}, gyro[3] = {gx, gy, gz};
    const float magnitude = std::sqrt(ax*ax + ay*ay + az*az);
    const bool gravityLike = finite && magnitude > .75f && magnitude < 1.25f;
    uint32_t dt = haveSample_ ? now - last_ : 0;
    bool gap = haveSample_ && dt > 500;
    last_ = now; haveSample_ = true;
    if (state_ == Calibrating) {
      bool still = gravityLike && std::fabs(gx) < 20 && std::fabs(gy) < 20 && std::fabs(gz) < 20;
      if (!still || gap) { clearWindow(); return; }
      if (!haveWindow_) { clearWindow(); windowStart_ = now; haveWindow_ = true; }
      count_++;
      for (int i=0; i<3; ++i) {
        sums_[i] += gyro[i]; squares_[i] += double(gyro[i])*gyro[i];
        accelSums_[i] += accel[i]; accelSquares_[i] += double(accel[i])*accel[i];
      }
      if (now - windowStart_ >= 3000 && count_ >= 20) {
        double accelVariance = 0;
        for (int i=0; i<3; ++i) {
          const double mean = sums_[i]/count_, amean = accelSums_[i]/count_;
          if (squares_[i]/count_ - mean*mean > .36) { clearWindow(); return; }
          accelVariance += accelSquares_[i]/count_ - amean*amean;
        }
        if (accelVariance > .01) { clearWindow(); return; } // Stable gravity, any mounting angle.
        float lengthSquared = 0;
        for (int i=0; i<3; ++i) {
          biases_[i] = float(sums_[i]/count_);
          gravity_[i] = float(accelSums_[i]/count_);
          lengthSquared += gravity_[i]*gravity_[i];
        }
        const float length = std::sqrt(lengthSquared);
        if (length < .75f) { clearWindow(); return; }
        for (int i=0; i<3; ++i) gravity_[i] /= length;
        yaw_ = 0; alignedAt_ = now; state_ = Ready;
      }
      return;
    }
    // Accept up to 20 degrees of tilt from the calibrated pose, including tilted sensor mounts.
    const float dot = gravityLike ? (ax*gravity_[0]+ay*gravity_[1]+az*gravity_[2])/magnitude : 0;
    if (!gravityLike || gap || dot < .9396926f ||
        std::fabs(gx) >= 240 || std::fabs(gy) >= 240 || std::fabs(gz) >= 240) {
      state_ = TrackingLost; return;
    }
    // Project the corrected three-axis angular rate onto measured vertical; clockwise positive.
    float rate = 0;
    for (int i=0; i<3; ++i) rate -= (gyro[i]-biases_[i])*accel[i]/magnitude;
    yaw_ = std::fmod(yaw_ + rate * (dt / 1000.f), 360.f);
    if (yaw_ < 0) yaw_ += 360.f;
  }
};
