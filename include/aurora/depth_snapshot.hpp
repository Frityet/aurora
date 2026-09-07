#pragma once

#include <cstdint>

namespace aurora {
// A completed draw-sync callback reads the image captured by its own token.
// The selector is independent of Game/host allocation routing.
inline thread_local std::uint64_t selected_depth_snapshot = 0;

class ScopedDepthSnapshotRead final {
public:
  explicit ScopedDepthSnapshotRead(std::uint64_t id) noexcept
  : m_previous(selected_depth_snapshot) { selected_depth_snapshot = id; }
  ~ScopedDepthSnapshotRead() { selected_depth_snapshot = m_previous; }
  ScopedDepthSnapshotRead(const ScopedDepthSnapshotRead&) = delete;
  ScopedDepthSnapshotRead& operator=(const ScopedDepthSnapshotRead&) = delete;

private:
  std::uint64_t m_previous;
};
} // namespace aurora
