#pragma once

#include <cstdint>
#include <exception>

namespace aurora::os {

enum class ProcessDestination { Restart, Reboot, Shutdown, Menu };

// Terminal SDK calls transfer control to the native application boundary. They
// never reboot or power down the host machine, and never return to Game code.
class ProcessRequest final : public std::exception {
public:
  explicit ProcessRequest(ProcessDestination destination, std::uint32_t resetCode = 0) noexcept
  : destination(destination), resetCode(resetCode) {}
  const char* what() const noexcept override { return "Guest requested a process lifecycle transition"; }

  ProcessDestination destination;
  std::uint32_t resetCode;
};

// Native platform input enters through the same serialized interrupt context
// as other SDK device callbacks. No synthetic button state is inferred from a
// menu selection; original Game reset logic handles that separately.
// Reset delivery is edge-triggered; OSGetResetButtonState consumes its latch.
// SDK callbacks are one-shot and can re-register themselves during delivery.
void set_reset_button_pressed(bool pressed);
void notify_power_button_pressed();

} // namespace aurora::os
