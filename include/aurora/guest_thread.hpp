#pragma once

#include <dolphin/os/OSContext.h>

struct OSThread;

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

// Native delivery of a nonblocking SDK interrupt. The interrupted guest keeps
// its thread/context identity; the callback uses a separate current context.
// Scheduler and interrupt state are restored when the callback returns. A
// blocking SDK wait inside this scope is invalid, as in the alarm ISR.
class GuestInterruptExecutionScope final {
public:
  GuestInterruptExecutionScope();
  ~GuestInterruptExecutionScope();
  GuestInterruptExecutionScope(const GuestInterruptExecutionScope&) = delete;
  GuestInterruptExecutionScope& operator=(const GuestInterruptExecutionScope&) = delete;

  [[nodiscard]] OSContext* interrupted_context() const noexcept { return interruptedContext_; }

private:
  OSThread* previousThread_;
  OSContext* previousContext_;
  OSContext* interruptedContext_;
  OSContext interruptContext_{};
  bool previousInterrupts_;
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
