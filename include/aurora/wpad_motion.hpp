#pragma once

#include <aurora/wpad.hpp>

#include <array>
#include <cstddef>

namespace aurora {

// A virtual upright controller sampled at the KPAD 60 Hz input clock. A key
// press produces a 200 ms lateral shake with zero net lateral impulse and a
// peak of 2 g. Gravity remains +1 g on KPAD Z throughout the gesture.
class WpadShakeGesture final {
public:
  [[nodiscard]] WpadVec3State sample(bool pressed) noexcept {
    if (pressed && !m_pressed) {
      m_sample = 0;
    }
    m_pressed = pressed;
    const float lateral = m_sample < kLateralAcceleration.size() ? kLateralAcceleration[m_sample++] : 0.0F;
    return {lateral, 0.0F, 1.0F};
  }

private:
  static constexpr std::array<float, 12> kLateralAcceleration{
      1.0F, 1.5F, 2.0F, 1.5F, 1.0F, 0.0F, -1.0F, -1.5F, -2.0F, -1.5F, -1.0F, 0.0F};
  std::size_t m_sample = kLateralAcceleration.size();
  bool m_pressed = false;
};

} // namespace aurora
