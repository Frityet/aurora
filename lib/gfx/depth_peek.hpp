#pragma once

#include "types.hpp"

#include <dolphin/gx/GXAurora.h>

#include <vector>

namespace aurora::gfx::depth_peek {

void initialize();
void shutdown();

struct SnapshotCapture {
  AuroraDepthSnapshotInfo info{};
  AuroraViewportPolicy viewportPolicy = AURORA_VIEWPORT_FIT;
};

AuroraDepthSnapshotId create_snapshot() noexcept;
bool set_snapshot_info(AuroraDepthSnapshotId id, const AuroraDepthSnapshotInfo& info) noexcept;
AuroraDepthSnapshotStatus get_snapshot_info(AuroraDepthSnapshotId id, AuroraDepthSnapshotInfo* info) noexcept;
bool read_snapshot(AuroraDepthSnapshotId id, uint16_t x, uint16_t y, uint32_t& z) noexcept;
void release_snapshot(AuroraDepthSnapshotId id) noexcept;
void drop_snapshot(AuroraDepthSnapshotId id) noexcept;

// Legacy GXPeekZ/latest-snapshot path. Tagged requests do not use this flag or
// its 30 Hz throttle.
void request_snapshot() noexcept;
bool snapshot_requested() noexcept;
bool read_latest(uint16_t x, uint16_t y, uint32_t& z) noexcept;

void encode_frame_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                           wgpu::Extent3D sourceSize, uint32_t msaaSamples) noexcept;
void encode_tagged_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                            wgpu::Extent3D sourceSize, uint32_t msaaSamples, const SnapshotCapture& capture) noexcept;
void after_submit() noexcept;

namespace testing {
void reset() noexcept;
bool snapshot_requested() noexcept;
void set_latest(uint32_t width, uint32_t height, const std::vector<uint32_t>& data);
void complete_snapshot(AuroraDepthSnapshotId id, const AuroraDepthSnapshotInfo& info,
                       std::vector<uint32_t> data) noexcept;
} // namespace testing

} // namespace aurora::gfx::depth_peek
