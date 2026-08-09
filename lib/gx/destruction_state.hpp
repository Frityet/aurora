#pragma once

#include <mutex>
#include <utility>

namespace aurora::gx {

void initialize_destruction_state() noexcept;
void shutdown_destruction_state() noexcept;
[[nodiscard]] bool destruction_commands_enabled() noexcept;

namespace detail {
[[nodiscard]] std::mutex& destruction_state_mutex() noexcept;
[[nodiscard]] bool destruction_commands_enabled_locked() noexcept;
} // namespace detail

template <typename Callback>
void with_destruction_commands_enabled(Callback&& callback) noexcept(noexcept(std::forward<Callback>(callback)())) {
  std::lock_guard lock{detail::destruction_state_mutex()};
  if (detail::destruction_commands_enabled_locked()) {
    std::forward<Callback>(callback)();
  }
}

} // namespace aurora::gx
