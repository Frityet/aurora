#pragma once

namespace aurora::allocation {

// Allocation routing is local to an executing thread. The embedding client
// owns its allocator and selected heap; Aurora does not transfer that ownership
// when it starts workers or queues callbacks.
struct RoutingState {
  bool guest = false;
  bool callbackGuest = false;
};

inline thread_local RoutingState routing_state;

// Keep native resources, caches and their control blocks independent of a
// client's arena. Preserve the caller's routing for synchronous callbacks,
// including when several native entry points nest.
class HostAllocationScope final {
public:
  HostAllocationScope() noexcept : previous_(routing_state) { routing_state.guest = false; }
  ~HostAllocationScope() { routing_state = previous_; }
  HostAllocationScope(const HostAllocationScope&) = delete;
  HostAllocationScope& operator=(const HostAllocationScope&) = delete;

private:
  RoutingState previous_;
};

// Use only at an actual client callback. A newly started worker has its own
// default host state; this scope does not borrow another thread's guest heap.
class ClientAllocationScope final {
public:
  ClientAllocationScope() noexcept : previous_(routing_state) {
    routing_state.guest = routing_state.callbackGuest;
  }
  ~ClientAllocationScope() { routing_state = previous_; }
  ClientAllocationScope(const ClientAllocationScope&) = delete;
  ClientAllocationScope& operator=(const ClientAllocationScope&) = delete;

private:
  RoutingState previous_;
};

} // namespace aurora::allocation
