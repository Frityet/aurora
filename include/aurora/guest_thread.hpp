#pragma once

namespace aurora::os {

// Serialize host-to-guest execution with OS-created guest threads. SDK waits
// and explicit yields release this ownership while retaining the caller's
// scope, interrupt state, and allocation routing. This does not mask host
// signals or asynchronously stop a host thread.
class GuestThreadExecutionScope final {
public:
  GuestThreadExecutionScope();
  ~GuestThreadExecutionScope();
  GuestThreadExecutionScope(const GuestThreadExecutionScope&) = delete;
  GuestThreadExecutionScope& operator=(const GuestThreadExecutionScope&) = delete;
};

} // namespace aurora::os
