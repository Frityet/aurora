#pragma once

#include <revolution.h>

#include <array>
#include <cstdint>

namespace aurora {

struct WpadPointerState {
  float x = 0.0F;
  float y = 0.0F;
  bool valid = false;
};

struct WpadVec3State {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct WpadStickState {
  float x = 0.0F;
  float y = 0.0F;
};

struct WpadChannelState {
  bool connected = false;
  std::uint32_t previous_hold = 0U;
  std::uint32_t hold = 0U;
  std::uint32_t trigger = 0U;
  std::uint32_t release = 0U;
  std::uint32_t repeat = 0U;
  std::uint32_t hold_frame_count = 0U;
  WpadPointerState pointer{};
  std::array<WpadPointerState, 16U> pointer_history{};
  std::uint32_t pointer_history_count = 0U;
  WpadVec3State core_acceleration{};
  WpadVec3State sub_acceleration{};
  WpadStickState sub_stick{};
  bool core_swing = false;
  bool previous_core_swing = false;
  bool sub_swing = false;
  bool previous_sub_swing = false;
  float distance_to_display = 0.0F;
};

class WpadService final {
public:
  void clear();
  void begin_frame();
  void set_connected(s32 channel, bool connected);
  void set_button_mask(s32 channel, std::uint32_t hold);
  void set_pointer(s32 channel, float x, float y, bool valid);
  void set_sub_stick(s32 channel, float x, float y);
  void set_core_acceleration(s32 channel, float x, float y, float z);
  void set_sub_acceleration(s32 channel, float x, float y, float z);
  void set_swing(s32 channel, bool core_swing, bool sub_swing);
  void set_distance_to_display(s32 channel, float distance);

  [[nodiscard]] bool is_connected(s32 channel) const;
  [[nodiscard]] bool is_button_held(s32 channel, std::uint32_t button_mask) const;
  [[nodiscard]] bool is_button_triggered(s32 channel, std::uint32_t button_mask) const;
  [[nodiscard]] bool is_button_released(s32 channel, std::uint32_t button_mask) const;
  [[nodiscard]] bool is_button_repeated(s32 channel, std::uint32_t button_mask) const;
  [[nodiscard]] WpadPointerState pointer(s32 channel) const;
  [[nodiscard]] WpadPointerState past_pointer(s32 channel, std::uint32_t index) const;
  [[nodiscard]] std::uint32_t pointer_history_count(s32 channel) const;
  [[nodiscard]] WpadStickState sub_stick(s32 channel) const;
  [[nodiscard]] WpadVec3State core_acceleration(s32 channel) const;
  [[nodiscard]] WpadVec3State sub_acceleration(s32 channel) const;
  [[nodiscard]] bool is_core_swing(s32 channel) const;
  [[nodiscard]] bool is_core_swing_triggered(s32 channel) const;
  [[nodiscard]] bool is_sub_swing(s32 channel) const;
  [[nodiscard]] float distance_to_display(s32 channel) const;
  [[nodiscard]] const WpadChannelState* channel_state(s32 channel) const;

private:
  [[nodiscard]] WpadChannelState* mutable_channel_state(s32 channel);

  std::array<WpadChannelState, WPAD_MAX_CONTROLLERS> m_channels{};
};

[[nodiscard]] WpadService& wpad_service();

} // namespace aurora
