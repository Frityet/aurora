#pragma once

#include <revolution.h>

#include <array>
#include <cstdint>

namespace aurora {

struct WpadPointerState {
  float x = 0.0F;
  float y = 0.0F;
  bool valid = false;
  // Mouse input has an explicit upright orientation; callers supplying another
  // pointing device may publish its measured horizon with the same sample.
  float horizon_x = 1.0F;
  float horizon_y = 0.0F;
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

enum WpadStickFlag : std::uint32_t {
  WpadStickNone = 0U,
  WpadStickUp = 1U,
  WpadStickDown = 2U,
  WpadStickRight = 4U,
  WpadStickLeft = 8U,
};

// Only capabilities backed by native service inputs can be selected. The SDK
// Classic record is available for ABI compatibility, without a virtual device.
enum class WpadDeviceType : u8 {
  Core = WPAD_DEV_CORE,
  Freestyle = WPAD_DEV_FREESTYLE,
};

struct WpadChannelState {
  bool connected = false;
  WpadDeviceType device_type = WpadDeviceType::Core;
  std::uint32_t previous_hold = 0U;
  std::uint32_t hold = 0U;
  std::uint32_t trigger = 0U;
  std::uint32_t release = 0U;
  std::uint32_t repeat = 0U;
  std::uint32_t hold_frame_count = 0U;
  WpadPointerState pointer{};
  float pointer_width = 640.0F;
  float pointer_height = 480.0F;
  std::array<float, 2> position_parameters{};
  std::array<float, 2> horizon_parameters{};
  std::array<float, 2> distance_parameters{};
  std::array<float, 2> acceleration_parameters{};
  std::array<float, 2> button_repeat_parameters{0.5F, 8.0F / 60.0F};
  float sensor_height = 0.0F;
  std::array<WpadPointerState, 16U> pointer_history{};
  std::uint32_t pointer_history_count = 0U;
  WpadVec3State core_acceleration{};
  WpadVec3State sub_acceleration{};
  WpadVec3State previous_core_acceleration{};
  WpadVec3State previous_sub_acceleration{};
  WpadStickState sub_stick{};
  std::uint32_t previous_sub_stick_hold = WpadStickNone;
  std::uint32_t sub_stick_hold = WpadStickNone;
  std::uint32_t sub_stick_trigger = WpadStickNone;
  std::uint32_t sub_stick_release = WpadStickNone;
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
  // Selection does not connect the channel and remains configured on disconnect.
  void set_device_type(s32 channel, WpadDeviceType device_type);
  void set_button_mask(s32 channel, std::uint32_t hold);
  void set_pointer(s32 channel, float x, float y, bool valid, float horizon_x = 1.0F, float horizon_y = 0.0F);
  void set_pointer_resolution(s32 channel, float width, float height);
  enum class SamplingParameter { Position, Horizon, Distance, Acceleration, ButtonRepeat };
  void set_sampling_parameter(s32 channel, SamplingParameter parameter, float first, float second);
  void set_sensor_height(s32 channel, float height);
  void set_sub_stick(s32 channel, float x, float y);
  // Native samples are already in KPAD coordinates and acceleration units.
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
  [[nodiscard]] std::uint32_t sub_stick_hold(s32 channel) const;
  [[nodiscard]] std::uint32_t sub_stick_trigger(s32 channel) const;
  [[nodiscard]] std::uint32_t sub_stick_release(s32 channel) const;
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
