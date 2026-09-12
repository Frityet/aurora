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

// Use only around a blocking native wait that cannot execute guest code on
// this thread. Let SDK threads and callbacks run while the host worker finishes,
// then restore this caller's CPU ownership, nesting and interrupt state.
class GuestThreadWaitScope final {
public:
  GuestThreadWaitScope();
  ~GuestThreadWaitScope() noexcept(false);
  GuestThreadWaitScope(const GuestThreadWaitScope&) = delete;
  GuestThreadWaitScope& operator=(const GuestThreadWaitScope&) = delete;

private:
  bool owned_;
};

} // namespace aurora::os
