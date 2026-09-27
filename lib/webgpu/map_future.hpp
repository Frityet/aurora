#pragma once

#include "gpu.hpp"
#include "../internal.hpp"

#include <aurora/allocation.hpp>

#include <algorithm>
#include <limits>
#include <mutex>
#include <vector>

namespace aurora::webgpu {

inline constexpr uint64_t kFutureWaitQuantumNs = 1'000'000;

// WaitAny completes the callback as well as the GPU map. Slot state written
// inside a callback alone does not prove that callback has finished returning.
inline bool complete_future_for(wgpu::Future& future, uint64_t timeoutNs) noexcept {
  if (future.id == 0) {
    return true;
  }
  const allocation::HostAllocationScope hostAllocations;
  static constexpr Module Log{"aurora::webgpu::map_future"};
  AURORA_ASSERT(g_instance, "GPU callback retirement requires a live WebGPU instance");
  const auto status = g_instance.WaitAny(future, timeoutNs);
  AURORA_ASSERT(status != wgpu::WaitStatus::Error, "Failed to retire WebGPU callback");
  if (status == wgpu::WaitStatus::Success) {
    future = {};
    return true;
  }
  AURORA_ASSERT(timeoutNs != std::numeric_limits<uint64_t>::max(),
                "Blocking WebGPU callback retirement timed out");
  return false;
}

inline bool complete_future(wgpu::Future& future, bool wait) noexcept {
  return complete_future_for(future, wait ? std::numeric_limits<uint64_t>::max() : 0);
}

// Producers must be quiescent before drain(). In particular, a render-worker
// join must precede teardown. Never run callbacks while holding this mutex or
// an owner's slot mutex: completion may acquire the owner's slot mutex itself.
class MapFutureTracker final {
public:
  void add(wgpu::Future future) {
    const allocation::HostAllocationScope hostAllocations;
    std::lock_guard lock{mutex_};
    futures_.push_back(future);
  }

  bool retire_ready() { return retire(0); }

  // Wait briefly on one pending future, then sweep the rest without blocking.
  // The wait happens after releasing mutex_; callbacks may acquire owner locks.
  bool wait_for_one(uint64_t timeoutNs = kFutureWaitQuantumNs) { return retire(timeoutNs); }

  void drain() { retire(std::numeric_limits<uint64_t>::max()); }

private:
  bool retire(uint64_t timeoutNs) {
    const allocation::HostAllocationScope hostAllocations;
    std::vector<wgpu::Future> pending;
    {
      std::lock_guard lock{mutex_};
      pending.swap(futures_);
    }
    bool waitNext = timeoutNs != 0;
    for (auto& future : pending) {
      const bool hasFuture = future.id != 0;
      const bool waitThisFuture = timeoutNs == std::numeric_limits<uint64_t>::max() || waitNext;
      const auto futureTimeout = waitThisFuture ? timeoutNs : 0;
      complete_future_for(future, futureTimeout);
      if (hasFuture && timeoutNs != std::numeric_limits<uint64_t>::max()) waitNext = false;
    }
    std::erase_if(pending, [](const wgpu::Future& future) { return future.id == 0; });
    if (!pending.empty()) {
      std::lock_guard lock{mutex_};
      futures_.insert(futures_.end(), pending.begin(), pending.end());
    }
    return pending.empty();
  }

  std::mutex mutex_;
  std::vector<wgpu::Future> futures_;
};

} // namespace aurora::webgpu
