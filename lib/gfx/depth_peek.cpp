#include "depth_peek.hpp"
#include "depth_snapshot_store.hpp"

#include "../dolphin/vi/vi_internal.hpp"
#include "../gx/gx.hpp"
#include "../gfx/render_worker.hpp"
#include "../webgpu/gpu.hpp"
#include "../webgpu/gpu_prof.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <string>

#include <magic_enum.hpp>
#include <tracy/Tracy.hpp>

namespace aurora::gfx::depth_peek {
namespace {
Module Log("aurora::gfx::depth_peek");

using webgpu::g_device;
using webgpu::g_instance;
using webgpu::g_queue;

using Clock = std::chrono::steady_clock;

constexpr size_t SlotCount = 3;
constexpr uint32_t WorkgroupSizeX = 8;
constexpr uint32_t WorkgroupSizeY = 8;
constexpr uint32_t DepthPeekSnapshotHz = 30;
constexpr auto SnapshotInterval = std::chrono::nanoseconds{1'000'000'000 / DepthPeekSnapshotHz};

struct Params {
  uint32_t dstWidth = 0;
  uint32_t dstHeight = 0;
  uint32_t srcWidth = 0;
  uint32_t srcHeight = 0;
  float offsetX = 0.f;
  float offsetY = 0.f;
  float scaleX = 1.f;
  float scaleY = 1.f;
};
static_assert(sizeof(Params) == 32);

struct LatestSnapshot {
  uint64_t sequence = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint32_t> data;
};

enum class SlotState : uint8_t {
  Available,
  CopySubmitted,
  MapPending,
};

struct Slot {
  wgpu::Buffer storageBuffer;
  wgpu::Buffer readbackBuffer;
  wgpu::Buffer paramsBuffer;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t byteSize = 0;
  AuroraDepthSnapshotId snapshotId = AURORA_INVALID_DEPTH_SNAPSHOT_ID;
  uint64_t legacySequence = 0;
  SlotState state = SlotState::Available;
};

struct PendingMap {
  size_t slotIdx = 0;
  wgpu::Buffer readbackBuffer;
  uint64_t byteSize = 0;
  AuroraDepthSnapshotId snapshotId = AURORA_INVALID_DEPTH_SNAPSHOT_ID;
  uint64_t legacySequence = 0;
};

bool g_enabled = false;
std::array<Slot, SlotCount> g_slots;
size_t g_nextSlot = 0;
wgpu::BindGroupLayout g_bindGroupLayout;
wgpu::ComputePipeline g_pipeline;
bool g_snapshotRequested = false;
Clock::time_point g_nextSnapshotTime;
uint64_t g_nextLegacySequence = 1;
LatestSnapshot g_latest;
detail::SnapshotStore g_snapshots;
std::mutex g_mutex;

constexpr std::string_view ShaderPreamble = R"(
struct Params {
    dstSize: vec2u,
    srcSize: vec2u,
    offset: vec2f,
    scale: vec2f,
};

@group(0) @binding(1) var<storage, read_write> out_z: array<u32>;
@group(0) @binding(2) var<uniform> params: Params;
)"sv;

constexpr std::string_view ReversedZBody = R"(
fn gx_z24(depth: f32) -> u32 {
    return min(u32(clamp(1.0 - depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)"sv;

constexpr std::string_view ForwardZBody = R"(
fn gx_z24(depth: f32) -> u32 {
    return min(u32(clamp(depth, 0.0, 1.0) * 16777215.0 + 0.5), 0x00ffffffu);
}
)"sv;

constexpr std::string_view ShaderMain = R"(
@group(0) @binding(0) var src: texture_depth_2d;

fn load_depth(coord: vec2i) -> f32 {
    return textureLoad(src, coord, 0);
}

@compute @workgroup_size(8, 8, 1)
fn cs_main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= params.dstSize.x || id.y >= params.dstSize.y) {
        return;
    }

    let dstCenter = vec2f(vec2u(id.xy)) + vec2f(0.5, 0.5);
    let srcPixel = clamp(vec2i(floor(params.offset + dstCenter * params.scale)), vec2i(0, 0),
                         vec2i(params.srcSize) - vec2i(1, 1));
    let depth = load_depth(srcPixel);
    out_z[id.y * params.dstSize.x + id.x] = gx_z24(depth);
}
)"sv;

std::string build_shader_source() {
  std::string source;
  source.reserve(ShaderPreamble.size() + ReversedZBody.size() + ShaderMain.size());
  source += ShaderPreamble;
  source += gx::UseReversedZ ? ReversedZBody : ForwardZBody;
  source += ShaderMain;
  return source;
}

wgpu::ComputePipeline create_pipeline(const wgpu::BindGroupLayout& bindGroupLayout, const char* label) {
  const auto shaderSource = build_shader_source();
  const wgpu::ShaderSourceWGSL wgslSource{wgpu::ShaderSourceWGSL::Init{
      .code = shaderSource.c_str(),
  }};
  const wgpu::ShaderModuleDescriptor moduleDescriptor{
      .nextInChain = &wgslSource,
      .label = label,
  };
  const auto module = g_device.CreateShaderModule(&moduleDescriptor);

  const wgpu::PipelineLayoutDescriptor layoutDescriptor{
      .bindGroupLayoutCount = 1,
      .bindGroupLayouts = &bindGroupLayout,
  };
  const auto pipelineLayout = g_device.CreatePipelineLayout(&layoutDescriptor);
  const wgpu::ComputePipelineDescriptor pipelineDescriptor{
      .label = label,
      .layout = pipelineLayout,
      .compute =
          wgpu::ComputeState{
              .module = module,
              .entryPoint = "cs_main",
          },
  };
  return g_device.CreateComputePipeline(&pipelineDescriptor);
}

wgpu::BindGroupLayout create_bind_group_layout(const char* label) {
  constexpr std::array entries{
      wgpu::BindGroupLayoutEntry{
          .binding = 0,
          .visibility = wgpu::ShaderStage::Compute,
          .texture =
              wgpu::TextureBindingLayout{
                  .sampleType = wgpu::TextureSampleType::Depth,
                  .viewDimension = wgpu::TextureViewDimension::e2D,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 1,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Storage,
              },
      },
      wgpu::BindGroupLayoutEntry{
          .binding = 2,
          .visibility = wgpu::ShaderStage::Compute,
          .buffer =
              wgpu::BufferBindingLayout{
                  .type = wgpu::BufferBindingType::Uniform,
              },
      },
  };
  const wgpu::BindGroupLayoutDescriptor descriptor{
      .label = label,
      .entryCount = entries.size(),
      .entries = entries.data(),
  };
  return g_device.CreateBindGroupLayout(&descriptor);
}

Params make_params(wgpu::Extent3D sourceSize, Vec2<uint32_t> dstSize, AuroraViewportPolicy viewportPolicy) noexcept {
  Params params{
      .dstWidth = dstSize.x,
      .dstHeight = dstSize.y,
      .srcWidth = sourceSize.width,
      .srcHeight = sourceSize.height,
  };

  if (viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return params;
  }

  if (dstSize.x == 0 || dstSize.y == 0 || sourceSize.width == 0 || sourceSize.height == 0) {
    return params;
  }

  const bool stretch = viewportPolicy == AURORA_VIEWPORT_STRETCH;
  const float scaleX = static_cast<float>(sourceSize.width) / static_cast<float>(dstSize.x);
  const float scaleY = static_cast<float>(sourceSize.height) / static_cast<float>(dstSize.y);
  const float scale = std::min(scaleX, scaleY);
  params.scaleX = stretch ? scaleX : scale;
  params.scaleY = stretch ? scaleY : scale;
  params.offsetX =
      stretch ? 0.f : (static_cast<float>(sourceSize.width) - static_cast<float>(dstSize.x) * scale) * 0.5f;
  params.offsetY =
      stretch ? 0.f : (static_cast<float>(sourceSize.height) - static_cast<float>(dstSize.y) * scale) * 0.5f;
  return params;
}

bool ensure_slot(Slot& slot, uint32_t width, uint32_t height) {
  const uint64_t byteSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * sizeof(uint32_t);
  if (slot.storageBuffer && slot.width == width && slot.height == height && slot.byteSize == byteSize) {
    return true;
  }

  slot.storageBuffer = {};
  slot.readbackBuffer = {};
  slot.paramsBuffer = {};
  slot.width = width;
  slot.height = height;
  slot.byteSize = byteSize;

  if (byteSize == 0 || byteSize > UINT32_MAX) {
    return false;
  }

  const wgpu::BufferDescriptor storageDescriptor{
      .label = "Depth Peek Storage Buffer",
      .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc,
      .size = byteSize,
  };
  slot.storageBuffer = g_device.CreateBuffer(&storageDescriptor);

  const wgpu::BufferDescriptor readbackDescriptor{
      .label = "Depth Peek Readback Buffer",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = byteSize,
  };
  slot.readbackBuffer = g_device.CreateBuffer(&readbackDescriptor);

  const wgpu::BufferDescriptor paramsDescriptor{
      .label = "Depth Peek Params Buffer",
      .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
      .size = sizeof(Params),
  };
  slot.paramsBuffer = g_device.CreateBuffer(&paramsDescriptor);
  return slot.storageBuffer && slot.readbackBuffer && slot.paramsBuffer;
}

Slot* find_available_slot(uint32_t width, uint32_t height) {
  for (size_t i = 0; i < g_slots.size(); ++i) {
    const size_t idx = (g_nextSlot + i) % g_slots.size();
    auto& slot = g_slots[idx];
    if (slot.state != SlotState::Available) {
      continue;
    }
    if (!ensure_slot(slot, width, height)) {
      continue;
    }
    g_nextSlot = (idx + 1) % g_slots.size();
    return &slot;
  }
  return nullptr;
}

void complete_slot(size_t slotIdx, AuroraDepthSnapshotId expectedSnapshotId, uint64_t expectedLegacySequence,
                   wgpu::MapAsyncStatus status, wgpu::StringView message) {
  std::vector<uint32_t> data;
  bool succeeded = false;
  {
    std::lock_guard lock{g_mutex};
    auto& slot = g_slots[slotIdx];
    if (slot.state != SlotState::MapPending || slot.snapshotId != expectedSnapshotId ||
        slot.legacySequence != expectedLegacySequence) {
      return;
    }

    if (status == wgpu::MapAsyncStatus::Success) {
      const auto valueCount = static_cast<size_t>(slot.width) * static_cast<size_t>(slot.height);
      const auto* mapped =
          static_cast<const uint32_t*>(slot.readbackBuffer.GetConstMappedRange(0, valueCount * sizeof(uint32_t)));
      if (mapped != nullptr) {
        data.assign(mapped, mapped + valueCount);
        succeeded = true;
      }
      slot.readbackBuffer.Unmap();
    } else if (status != wgpu::MapAsyncStatus::CallbackCancelled && status != wgpu::MapAsyncStatus::Aborted) {
      Log.warn("Depth Peek readback mapping failed {}: {}", magic_enum::enum_name(status), message);
    }

    if (succeeded && expectedSnapshotId == AURORA_INVALID_DEPTH_SNAPSHOT_ID &&
        expectedLegacySequence > g_latest.sequence) {
      g_latest.sequence = expectedLegacySequence;
      g_latest.width = slot.width;
      g_latest.height = slot.height;
      g_latest.data = data;
    }
    slot.snapshotId = AURORA_INVALID_DEPTH_SNAPSHOT_ID;
    slot.legacySequence = 0;
    slot.state = SlotState::Available;
  }

  if (expectedSnapshotId != AURORA_INVALID_DEPTH_SNAPSHOT_ID) {
    if (succeeded) {
      g_snapshots.complete(expectedSnapshotId, std::move(data));
    } else {
      g_snapshots.drop(expectedSnapshotId);
    }
  }
}
} // namespace

void initialize() {
  g_enabled = false;
  if (!webgpu::g_hasCoreFeatures) {
    return;
  }
  g_bindGroupLayout = create_bind_group_layout("Depth Peek Bind Group Layout");
  g_pipeline = create_pipeline(g_bindGroupLayout, "Depth Peek Pipeline");
  g_enabled = g_bindGroupLayout && g_pipeline;
}

void shutdown() {
  g_enabled = false;
  testing::reset();
  g_pipeline = {};
  g_bindGroupLayout = {};
  for (auto& slot : g_slots) {
    slot = {};
  }
}

AuroraDepthSnapshotId create_snapshot() noexcept { return g_snapshots.create(); }

bool set_snapshot_info(AuroraDepthSnapshotId id, const AuroraDepthSnapshotInfo& info) noexcept {
  return g_snapshots.set_info(id, info);
}

AuroraDepthSnapshotStatus get_snapshot_info(AuroraDepthSnapshotId id, AuroraDepthSnapshotInfo* info) noexcept {
  return g_snapshots.status(id, info);
}

bool read_snapshot(AuroraDepthSnapshotId id, uint16_t x, uint16_t y, uint32_t& z) noexcept {
  return g_snapshots.read(id, x, y, z);
}

void release_snapshot(AuroraDepthSnapshotId id) noexcept { g_snapshots.release(id); }

void drop_snapshot(AuroraDepthSnapshotId id) noexcept { g_snapshots.drop(id); }

void request_snapshot() noexcept {
  if (!g_enabled) {
    return;
  }
  std::lock_guard lock{g_mutex};
  g_snapshotRequested = true;
}

bool snapshot_requested() noexcept {
  std::lock_guard lock{g_mutex};
  return g_enabled && g_snapshotRequested;
}

bool read_latest(uint16_t x, uint16_t y, uint32_t& z) noexcept {
  std::lock_guard lock{g_mutex};
  if (x >= g_latest.width || y >= g_latest.height || g_latest.data.empty()) {
    return false;
  }
  z = g_latest.data[static_cast<size_t>(y) * g_latest.width + x] & 0x00ffffffu;
  return true;
}

static void encode_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                            wgpu::Extent3D sourceSize, uint32_t msaaSamples, Vec2<uint32_t> dstSize,
                            AuroraViewportPolicy viewportPolicy, AuroraDepthSnapshotId snapshotId,
                            uint64_t legacySequence) noexcept {
  ZoneScoped;
  const auto drop = [snapshotId] {
    if (snapshotId != AURORA_INVALID_DEPTH_SNAPSHOT_ID) {
      g_snapshots.drop(snapshotId);
    }
  };
  if (!g_enabled || !depthView || dstSize.x == 0 || dstSize.y == 0 || sourceSize.width == 0 || sourceSize.height == 0) {
    drop();
    return;
  }
  if (msaaSamples > 1) {
    Log.warn("Dropping depth snapshot {}: multisampled EFB targets are unsupported", snapshotId);
    drop();
    return;
  }

  const Params params = make_params(sourceSize, dstSize, viewportPolicy);
  wgpu::Buffer storageBuffer;
  wgpu::Buffer readbackBuffer;
  wgpu::Buffer paramsBuffer;
  uint64_t byteSize = 0;
  size_t slotIdx = 0;
  {
    std::lock_guard lock{g_mutex};
    auto* slot = find_available_slot(dstSize.x, dstSize.y);
    if (slot == nullptr) {
      drop();
      return;
    }
    slotIdx = static_cast<size_t>(slot - g_slots.data());
    slot->state = SlotState::CopySubmitted;
    slot->snapshotId = snapshotId;
    slot->legacySequence = legacySequence;
    storageBuffer = slot->storageBuffer;
    readbackBuffer = slot->readbackBuffer;
    paramsBuffer = slot->paramsBuffer;
    byteSize = slot->byteSize;
  }

  AURORA_ASSERT(render_worker::is_worker_thread(), "Depth peek queue write must run on the render worker");
  g_queue.WriteBuffer(paramsBuffer, 0, &params, sizeof(params));

  const std::array bindGroupEntries{
      wgpu::BindGroupEntry{
          .binding = 0,
          .textureView = depthView,
      },
      wgpu::BindGroupEntry{
          .binding = 1,
          .buffer = storageBuffer,
          .size = byteSize,
      },
      wgpu::BindGroupEntry{
          .binding = 2,
          .buffer = paramsBuffer,
          .size = sizeof(Params),
      },
  };
  const wgpu::BindGroupDescriptor bindGroupDescriptor{
      .label = "Depth Peek Bind Group",
      .layout = g_bindGroupLayout,
      .entryCount = bindGroupEntries.size(),
      .entries = bindGroupEntries.data(),
  };
  const auto bindGroup = g_device.CreateBindGroup(&bindGroupDescriptor);
  if (!bindGroup) {
    {
      std::lock_guard lock{g_mutex};
      auto& slot = g_slots[slotIdx];
      if (slot.snapshotId == snapshotId && slot.legacySequence == legacySequence) {
        slot.snapshotId = AURORA_INVALID_DEPTH_SNAPSHOT_ID;
        slot.legacySequence = 0;
        slot.state = SlotState::Available;
      }
    }
    drop();
    return;
  }

  const wgpu::ComputePassDescriptor passDescriptor{
      .label = "Depth Peek Compute Pass",
      .timestampWrites = webgpu::gpu_prof::pass_writes("Depth peek"),
  };
  const auto pass = cmd.BeginComputePass(&passDescriptor);
  pass.SetPipeline(g_pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.DispatchWorkgroups((dstSize.x + WorkgroupSizeX - 1) / WorkgroupSizeX,
                          (dstSize.y + WorkgroupSizeY - 1) / WorkgroupSizeY);
  pass.End();

  cmd.CopyBufferToBuffer(storageBuffer, 0, readbackBuffer, 0, byteSize);
}

void encode_frame_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                           wgpu::Extent3D sourceSize, uint32_t msaaSamples) noexcept {
  if (!g_enabled) {
    return;
  }

  uint64_t legacySequence = 0;
  const auto now = Clock::now();
  {
    std::lock_guard lock{g_mutex};
    if (!g_snapshotRequested || now < g_nextSnapshotTime) {
      return;
    }
    g_snapshotRequested = false;
    g_nextSnapshotTime = now + SnapshotInterval;
    legacySequence = g_nextLegacySequence++;
  }

  auto dstSize = vi::configured_fb_size();
  const auto viewportPolicy = gx::g_gxState.viewportPolicy;
  if (viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    dstSize = {sourceSize.width, sourceSize.height};
  }
  encode_snapshot(cmd, depthView, sourceSize, msaaSamples, dstSize, viewportPolicy, AURORA_INVALID_DEPTH_SNAPSHOT_ID,
                  legacySequence);
}

void encode_tagged_snapshot(const wgpu::CommandEncoder& cmd, const wgpu::TextureView& depthView,
                            wgpu::Extent3D sourceSize, uint32_t msaaSamples, const SnapshotCapture& capture) noexcept {
  if (capture.info.id == AURORA_INVALID_DEPTH_SNAPSHOT_ID ||
      get_snapshot_info(capture.info.id, nullptr) != AURORA_DEPTH_SNAPSHOT_PENDING) {
    return;
  }
  encode_snapshot(cmd, depthView, sourceSize, msaaSamples, {capture.info.width, capture.info.height},
                  capture.viewportPolicy, capture.info.id, 0);
}

void after_submit() noexcept {
  if (!g_enabled) {
    return;
  }

  std::vector<PendingMap> pendingMaps;
  {
    std::lock_guard lock{g_mutex};
    for (size_t i = 0; i < g_slots.size(); ++i) {
      auto& slot = g_slots[i];
      if (slot.state != SlotState::CopySubmitted) {
        continue;
      }
      slot.state = SlotState::MapPending;
      pendingMaps.push_back({
          .slotIdx = i,
          .readbackBuffer = slot.readbackBuffer,
          .byteSize = slot.byteSize,
          .snapshotId = slot.snapshotId,
          .legacySequence = slot.legacySequence,
      });
    }
  }

  for (const auto& pending : pendingMaps) {
    pending.readbackBuffer.MapAsync(
        wgpu::MapMode::Read, 0, pending.byteSize, wgpu::CallbackMode::AllowSpontaneous,
        [slotIdx = pending.slotIdx, snapshotId = pending.snapshotId,
         legacySequence = pending.legacySequence](wgpu::MapAsyncStatus status, wgpu::StringView message) {
          complete_slot(slotIdx, snapshotId, legacySequence, status, message);
        });
  }
}

namespace testing {
void reset() noexcept {
  {
    std::lock_guard lock{g_mutex};
    g_snapshotRequested = false;
    g_nextSlot = 0;
    g_nextSnapshotTime = {};
    g_nextLegacySequence = 1;
    g_latest = {};
    for (auto& slot : g_slots) {
      slot.snapshotId = AURORA_INVALID_DEPTH_SNAPSHOT_ID;
      slot.legacySequence = 0;
      slot.state = SlotState::Available;
    }
  }
  g_snapshots.reset();
}

bool snapshot_requested() noexcept {
  std::lock_guard lock{g_mutex};
  return g_snapshotRequested;
}

void set_latest(uint32_t width, uint32_t height, const std::vector<uint32_t>& data) {
  std::lock_guard lock{g_mutex};
  g_latest.sequence = g_nextLegacySequence++;
  g_latest.width = width;
  g_latest.height = height;
  g_latest.data = data;
}

void complete_snapshot(AuroraDepthSnapshotId id, const AuroraDepthSnapshotInfo& info,
                       std::vector<uint32_t> data) noexcept {
  if (g_snapshots.set_info(id, info)) {
    g_snapshots.complete(id, std::move(data));
  }
}
} // namespace testing

} // namespace aurora::gfx::depth_peek
