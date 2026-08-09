#include "destruction_state.hpp"

namespace aurora::gx {
namespace {
struct DestructionState {
  std::mutex mutex;
  bool commandsEnabled = false;
};

DestructionState& state() noexcept {
  // Resource-owning globals can run after other translation units have begun
  // static destruction. Keep this tiny guard alive until process teardown so
  // those late destructors can still take the disabled path safely.
  static auto* value = new DestructionState;
  return *value;
}
} // namespace

void initialize_destruction_state() noexcept {
  auto& destructionState = state();
  std::lock_guard lock{destructionState.mutex};
  destructionState.commandsEnabled = true;
}

void shutdown_destruction_state() noexcept {
  auto& destructionState = state();
  std::lock_guard lock{destructionState.mutex};
  destructionState.commandsEnabled = false;
}

bool destruction_commands_enabled() noexcept {
  auto& destructionState = state();
  std::lock_guard lock{destructionState.mutex};
  return destructionState.commandsEnabled;
}

namespace detail {
std::mutex& destruction_state_mutex() noexcept { return state().mutex; }

bool destruction_commands_enabled_locked() noexcept { return state().commandsEnabled; }
} // namespace detail

} // namespace aurora::gx
