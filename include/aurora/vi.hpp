#pragma once

namespace aurora::vi {

// Emulated video-output connection capability (VI DTV status bit 0), independent
// of both the selected scan mode and the user's stored progressive preference.
// Desktop output supports progressive presentation by default. Platform owners
// may change the connection state without rewriting the configured render mode.
void set_dtv_connected(bool connected) noexcept;
[[nodiscard]] bool dtv_connected() noexcept;

// Stop the emulated VI interrupt source and retire its borrowed callbacks.
// The next VIInit starts a fresh device lifetime.
void shutdown() noexcept;

struct ScanoutState {
  bool initialized;
  bool black;
  void* frame_buffer;
};
[[nodiscard]] ScanoutState scanout_state() noexcept;

} // namespace aurora::vi
