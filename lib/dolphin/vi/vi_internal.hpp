#pragma once

#include <dolphin/gx/GXStruct.h>

#include <cstdint>

#include <aurora/math.hpp>

namespace aurora::vi {
void configure(const GXRenderModeObj* rm) noexcept;
Vec2<uint32_t> configured_fb_size() noexcept;
Vec2<uint32_t> configured_efb_size() noexcept;
Vec2<uint32_t> locked_aspect_ratio() noexcept;
void set_locked_aspect_ratio(uint32_t width, uint32_t height) noexcept;
} // namespace aurora::vi
