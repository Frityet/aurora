#pragma once

#include "hash.hpp"
#include "../internal.hpp"
#include "../webgpu/gpu.hpp"

#include <aurora/gfx.h>
#include <aurora/gfx.hpp>
#include <aurora/math.hpp>
#include <dolphin/gx/GXEnum.h>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace aurora::gfx {
using BindGroupRef = HashType;
using PipelineRef = HashType;
using SamplerRef = HashType;
using ShaderRef = HashType;

struct ClipRect {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;

  bool operator==(const ClipRect& rhs) const { return memcmp(this, &rhs, sizeof(*this)) == 0; }
  bool operator!=(const ClipRect& rhs) const { return !(*this == rhs); }
};

struct CopyFilter {
  std::array<uint32_t, 3> coefficients{0, 64, 0};
  bool clampTop = false;
  bool clampBottom = false;

  [[nodiscard]] bool has_effect() const noexcept { return coefficients != std::array<uint32_t, 3>{0, 64, 0}; }
};

using webgpu::Viewport;

struct TextureRef;
using TextureHandle = std::shared_ptr<TextureRef>;
using AfterSubmitCallback = std::function<void()>;
using EndFrameCallback = std::function<void(wgpu::CommandEncoder&, std::vector<AfterSubmitCallback>)>;
} // namespace aurora::gfx
