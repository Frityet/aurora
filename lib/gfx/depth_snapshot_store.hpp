#pragma once

#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aurora::gfx::depth_peek::detail {

class SnapshotStore {
public:
  static constexpr size_t Capacity = 16;

  AuroraDepthSnapshotId create() noexcept {
    std::lock_guard lock{m_mutex};
    expire_one_if_full();

    AuroraDepthSnapshotId id;
    do {
      id = m_nextId++;
    } while (id == AURORA_INVALID_DEPTH_SNAPSHOT_ID || m_records.contains(id));
    m_order.push_back(id);
    m_records.emplace(id, Record{});
    return id;
  }

  bool set_info(AuroraDepthSnapshotId id, const AuroraDepthSnapshotInfo& info) noexcept {
    std::lock_guard lock{m_mutex};
    const auto it = m_records.find(id);
    if (it == m_records.end() || it->second.status != AURORA_DEPTH_SNAPSHOT_PENDING || it->second.infoSet) {
      return false;
    }
    it->second.info = info;
    it->second.info.id = id;
    it->second.infoSet = true;
    return true;
  }

  void complete(AuroraDepthSnapshotId id, std::vector<uint32_t> data) noexcept {
    std::lock_guard lock{m_mutex};
    const auto it = m_records.find(id);
    if (it == m_records.end() || it->second.status != AURORA_DEPTH_SNAPSHOT_PENDING) {
      return;
    }

    const auto width = static_cast<size_t>(it->second.info.width);
    const auto height = static_cast<size_t>(it->second.info.height);
    if (width == 0 || height == 0 || width > std::numeric_limits<size_t>::max() / height ||
        data.size() != width * height) {
      it->second.status = AURORA_DEPTH_SNAPSHOT_DROPPED;
      it->second.data.clear();
      return;
    }

    it->second.data = std::move(data);
    it->second.status = AURORA_DEPTH_SNAPSHOT_READY;
  }

  void drop(AuroraDepthSnapshotId id) noexcept {
    std::lock_guard lock{m_mutex};
    const auto it = m_records.find(id);
    if (it == m_records.end() || it->second.status != AURORA_DEPTH_SNAPSHOT_PENDING) {
      return;
    }
    it->second.status = AURORA_DEPTH_SNAPSHOT_DROPPED;
    it->second.data.clear();
  }

  AuroraDepthSnapshotStatus status(AuroraDepthSnapshotId id, AuroraDepthSnapshotInfo* info) const noexcept {
    std::lock_guard lock{m_mutex};
    const auto it = m_records.find(id);
    if (it == m_records.end()) {
      if (info != nullptr) {
        *info = {};
      }
      return AURORA_DEPTH_SNAPSHOT_UNKNOWN;
    }
    if (info != nullptr) {
      *info = it->second.info;
      info->id = id;
    }
    return it->second.status;
  }

  bool read(AuroraDepthSnapshotId id, uint16_t x, uint16_t y, uint32_t& z) const noexcept {
    std::lock_guard lock{m_mutex};
    const auto it = m_records.find(id);
    if (it == m_records.end() || it->second.status != AURORA_DEPTH_SNAPSHOT_READY || x >= it->second.info.width ||
        y >= it->second.info.height) {
      return false;
    }
    const auto idx = static_cast<size_t>(y) * it->second.info.width + x;
    if (idx >= it->second.data.size()) {
      return false;
    }
    z = it->second.data[idx] & 0x00ffffffu;
    return true;
  }

  void release(AuroraDepthSnapshotId id) noexcept {
    std::lock_guard lock{m_mutex};
    m_records.erase(id);
    std::erase(m_order, id);
  }

  void reset() noexcept {
    std::lock_guard lock{m_mutex};
    m_records.clear();
    m_order.clear();
    m_nextId = 1;
  }

private:
  struct Record {
    AuroraDepthSnapshotInfo info{};
    AuroraDepthSnapshotStatus status = AURORA_DEPTH_SNAPSHOT_PENDING;
    bool infoSet = false;
    std::vector<uint32_t> data;
  };

  void expire_one_if_full() noexcept {
    if (m_records.size() < Capacity) {
      return;
    }

    auto victim = m_order.begin();
    for (auto it = m_order.begin(); it != m_order.end(); ++it) {
      const auto record = m_records.find(*it);
      if (record != m_records.end() && record->second.status != AURORA_DEPTH_SNAPSHOT_PENDING) {
        victim = it;
        break;
      }
    }
    m_records.erase(*victim);
    m_order.erase(victim);
  }

  mutable std::mutex m_mutex;
  AuroraDepthSnapshotId m_nextId = 1;
  std::deque<AuroraDepthSnapshotId> m_order;
  std::unordered_map<AuroraDepthSnapshotId, Record> m_records;
};

} // namespace aurora::gfx::depth_peek::detail
