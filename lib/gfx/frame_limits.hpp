#pragma once
#include <cstddef>
namespace aurora::gfx::detail {
inline constexpr size_t FrameSlotCount = 2;
inline constexpr size_t StagingBufferCount = FrameSlotCount + 3;
// Each frame owns one unsubmitted encoder. Prefix replacement may briefly
// construct its successor before releasing the preceding packet's marker.
inline constexpr size_t SubmissionSlotCount = FrameSlotCount + 1;
}
