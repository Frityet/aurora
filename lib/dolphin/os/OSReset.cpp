#include <dolphin/os.h>
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <aurora/process.hpp>

#include <charconv>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace {
struct Callback {
  void (*function)() = nullptr;
  aurora::allocation::RoutingState routing;
};
Callback resetCallback;
Callback powerCallback;
bool resetPressed = false;
bool resetDown = false;

void invoke(const Callback& callback) {
  if (!callback.function) return;
  const aurora::allocation::ClientAllocationScope allocations(callback.routing);
  callback.function();
}
}

OSResetCallback OSSetResetCallback(OSResetCallback callback) {
  const aurora::os::GuestThreadExecutionScope execution;
  const BOOL enabled = OSDisableInterrupts();
  auto* previous = resetCallback.function;
  resetCallback = {callback, aurora::allocation::routing_state};
  OSRestoreInterrupts(enabled);
  return previous;
}

OSPowerCallback OSSetPowerCallback(OSPowerCallback callback) {
  const aurora::os::GuestThreadExecutionScope execution;
  const BOOL enabled = OSDisableInterrupts();
  auto* previous = powerCallback.function;
  powerCallback = {callback, aurora::allocation::routing_state};
  OSRestoreInterrupts(enabled);
  return previous;
}

BOOL OSGetResetButtonState() {
  const aurora::os::GuestThreadExecutionScope execution;
  const BOOL enabled = OSDisableInterrupts();
  // Wii OSStateTM consumes ResetDown on every read. Holding the button does
  // not manufacture more presses, and releasing it must not lose an event.
  const bool state = std::exchange(resetDown, false);
  OSRestoreInterrupts(enabled);
  return state;
}

namespace aurora::os {
void set_reset_button_pressed(bool pressed) {
  const GuestInterruptExecutionScope execution;
  const bool triggered = pressed && !resetPressed;
  resetPressed = pressed;
  if (triggered) {
    resetDown = true;
    // STM replaces the handler with its default before dispatch. A callback
    // may install its successor, including itself, without being overwritten.
    const auto callback = std::exchange(resetCallback, Callback{});
    invoke(callback);
  }
}
void notify_power_button_pressed() {
  const GuestInterruptExecutionScope execution;
  const auto callback = std::exchange(powerCallback, Callback{});
  invoke(callback);
}
}

void OSRestart(u32 resetCode) {
  throw aurora::os::ProcessRequest(aurora::os::ProcessDestination::Restart, resetCode);
}
void OSRebootSystem() {
  throw aurora::os::ProcessRequest(aurora::os::ProcessDestination::Reboot);
}
void OSShutdownSystem() {
  throw aurora::os::ProcessRequest(aurora::os::ProcessDestination::Shutdown);
}
void OSReturnToMenu() {
  throw aurora::os::ProcessRequest(aurora::os::ProcessDestination::Menu);
}

u32 OSGetResetCode() {
  const char* inherited = std::getenv("AURORA_PROCESS_RESET_CODE");
  if (!inherited) return 0;
  const std::string_view text(inherited);
  u32 code = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), code, 10);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return 0;
  return OS_RESETCODE_RESTART | code;
}
