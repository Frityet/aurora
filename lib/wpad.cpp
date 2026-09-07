#include <aurora/wpad.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace aurora {
namespace {
WpadService g_wpadService;

constexpr float kSubStickDirectionThreshold = 0.2F;

[[nodiscard]] std::uint32_t sub_stick_direction_flags(float x, float y) {
  auto flags = static_cast<std::uint32_t>(WpadStickNone);
  if (x > kSubStickDirectionThreshold) {
    flags |= WpadStickRight;
  }
  if (x < -kSubStickDirectionThreshold) {
    flags |= WpadStickLeft;
  }
  if (y > kSubStickDirectionThreshold) {
    flags |= WpadStickUp;
  }
  if (y < -kSubStickDirectionThreshold) {
    flags |= WpadStickDown;
  }
  return flags;
}
} // namespace

void WpadService::clear() { m_channels = {}; }

void WpadService::begin_frame() {
  for (auto& channel : m_channels) {
    channel.previous_hold = channel.hold;
    channel.previous_sub_stick_hold = channel.sub_stick_hold;
    channel.previous_core_swing = channel.core_swing;
    channel.previous_sub_swing = channel.sub_swing;
    channel.trigger = 0U;
    channel.release = 0U;
    channel.repeat = 0U;
    channel.sub_stick_trigger = WpadStickNone;
    channel.sub_stick_release = WpadStickNone;
    if (channel.hold != 0U) {
      ++channel.hold_frame_count;
      // begin_frame advances the original 60 Hz input clock. KPAD callers
      // configure repeat delay and pulse in seconds, independently per port.
      const auto delay = channel.button_repeat_parameters[0];
      const auto pulse = channel.button_repeat_parameters[1];
      if (std::isfinite(delay) && std::isfinite(pulse) && delay >= 0.0F && pulse > 0.0F) {
        const auto delayFrames = static_cast<std::uint64_t>(std::min<double>(std::numeric_limits<std::uint32_t>::max(), std::round(static_cast<double>(delay) * 60.0)));
        const auto pulseFrames = std::max<std::uint64_t>(1U, static_cast<std::uint64_t>(std::min<double>(std::numeric_limits<std::uint32_t>::max(), std::round(static_cast<double>(pulse) * 60.0))));
        const auto elapsedFrames = channel.hold_frame_count - 1U;
        if (elapsedFrames >= delayFrames && (elapsedFrames - delayFrames) % pulseFrames == 0U) {
          channel.repeat = channel.hold;
        }
      }
    } else {
      channel.hold_frame_count = 0U;
    }
    if (!channel.connected) {
      channel.hold = 0U;
      channel.sub_stick = {};
      channel.previous_sub_stick_hold = WpadStickNone;
      channel.sub_stick_hold = WpadStickNone;
      channel.pointer.valid = false;
      channel.pointer_history_count = 0U;
    }
  }
}

void WpadService::set_connected(s32 channel, bool connected) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = connected;
  if (!connected) {
    state->hold = 0U;
    state->trigger = 0U;
    state->release = 0U;
    state->repeat = 0U;
    state->hold_frame_count = 0U;
    state->sub_stick = {};
    state->previous_sub_stick_hold = WpadStickNone;
    state->sub_stick_hold = WpadStickNone;
    state->sub_stick_trigger = WpadStickNone;
    state->sub_stick_release = WpadStickNone;
    state->pointer.valid = false;
    state->pointer_history_count = 0U;
  }
}

void WpadService::set_button_mask(s32 channel, std::uint32_t hold) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->hold = hold;
  state->trigger = hold & ~state->previous_hold;
  state->release = state->previous_hold & ~hold;
  if (hold != state->previous_hold) {
    state->hold_frame_count = hold == 0U ? 0U : 1U;
    state->repeat = state->trigger;
  }
}

void WpadService::set_pointer(s32 channel, float x, float y, bool valid, float horizon_x, float horizon_y) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->pointer = WpadPointerState{
      .x = x,
      .y = y,
      .valid = valid,
      .horizon_x = horizon_x,
      .horizon_y = horizon_y,
  };
  for (auto i = state->pointer_history.size() - 1U; i > 0U; --i) {
    state->pointer_history[i] = state->pointer_history[i - 1U];
  }
  state->pointer_history[0U] = state->pointer;
  state->pointer_history_count = std::min<std::uint32_t>(static_cast<std::uint32_t>(state->pointer_history.size()),
                                                         state->pointer_history_count + 1U);
}

void WpadService::set_pointer_resolution(s32 channel, float width, float height) {
  if (auto* state = mutable_channel_state(channel); state && width > 0.0F && height > 0.0F) {
    state->pointer_width = width;
    state->pointer_height = height;
  }
}

void WpadService::set_sampling_parameter(s32 channel, SamplingParameter parameter, float first, float second) {
  auto* state = mutable_channel_state(channel);
  if (!state) return;
  auto* values = &state->position_parameters;
  switch (parameter) {
  case SamplingParameter::Position: break;
  case SamplingParameter::Horizon: values = &state->horizon_parameters; break;
  case SamplingParameter::Distance: values = &state->distance_parameters; break;
  case SamplingParameter::Acceleration: values = &state->acceleration_parameters; break;
  case SamplingParameter::ButtonRepeat: values = &state->button_repeat_parameters; break;
  }
  *values = {first, second};
}

void WpadService::set_sensor_height(s32 channel, float height) {
  if (auto* state = mutable_channel_state(channel)) state->sensor_height = height;
}

void WpadService::set_sub_stick(s32 channel, float x, float y) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->sub_stick = WpadStickState{
      .x = x,
      .y = y,
  };
  state->sub_stick_hold = sub_stick_direction_flags(x, y);
  state->sub_stick_trigger = state->sub_stick_hold & ~state->previous_sub_stick_hold;
  state->sub_stick_release = state->previous_sub_stick_hold & ~state->sub_stick_hold;
}

void WpadService::set_core_acceleration(s32 channel, float x, float y, float z) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->core_acceleration = WpadVec3State{
      .x = x,
      .y = y,
      .z = z,
  };
}

void WpadService::set_sub_acceleration(s32 channel, float x, float y, float z) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->sub_acceleration = WpadVec3State{
      .x = x,
      .y = y,
      .z = z,
  };
}

void WpadService::set_swing(s32 channel, bool core_swing, bool sub_swing) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->core_swing = core_swing;
  state->sub_swing = sub_swing;
}

void WpadService::set_distance_to_display(s32 channel, float distance) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->distance_to_display = distance;
}

bool WpadService::is_connected(s32 channel) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected;
}

bool WpadService::is_button_held(s32 channel, std::uint32_t button_mask) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && (state->hold & button_mask) != 0U;
}

bool WpadService::is_button_triggered(s32 channel, std::uint32_t button_mask) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && (state->trigger & button_mask) != 0U;
}

bool WpadService::is_button_released(s32 channel, std::uint32_t button_mask) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && (state->release & button_mask) != 0U;
}

bool WpadService::is_button_repeated(s32 channel, std::uint32_t button_mask) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && (state->repeat & button_mask) != 0U;
}

WpadPointerState WpadService::pointer(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr ? WpadPointerState{} : state->pointer;
}

WpadPointerState WpadService::past_pointer(s32 channel, std::uint32_t index) const {
  const auto* state = channel_state(channel);
  if (state == nullptr || index >= state->pointer_history_count || index >= state->pointer_history.size()) {
    return {};
  }

  return state->pointer_history[index];
}

std::uint32_t WpadService::pointer_history_count(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr ? 0U : state->pointer_history_count;
}

WpadStickState WpadService::sub_stick(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr || !state->connected ? WpadStickState{} : state->sub_stick;
}

std::uint32_t WpadService::sub_stick_hold(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr || !state->connected ? WpadStickNone : state->sub_stick_hold;
}

std::uint32_t WpadService::sub_stick_trigger(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr || !state->connected ? WpadStickNone : state->sub_stick_trigger;
}

std::uint32_t WpadService::sub_stick_release(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr || !state->connected ? WpadStickNone : state->sub_stick_release;
}

WpadVec3State WpadService::core_acceleration(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr ? WpadVec3State{} : state->core_acceleration;
}

WpadVec3State WpadService::sub_acceleration(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr ? WpadVec3State{} : state->sub_acceleration;
}

bool WpadService::is_core_swing(s32 channel) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && state->core_swing;
}

bool WpadService::is_core_swing_triggered(s32 channel) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && state->core_swing && !state->previous_core_swing;
}

bool WpadService::is_sub_swing(s32 channel) const {
  const auto* state = channel_state(channel);
  return state != nullptr && state->connected && state->sub_swing;
}

float WpadService::distance_to_display(s32 channel) const {
  const auto* state = channel_state(channel);
  return state == nullptr ? 0.0F : state->distance_to_display;
}

const WpadChannelState* WpadService::channel_state(s32 channel) const {
  if (channel < 0 || channel >= static_cast<s32>(m_channels.size())) {
    return nullptr;
  }

  return &m_channels[static_cast<std::size_t>(channel)];
}

WpadChannelState* WpadService::mutable_channel_state(s32 channel) {
  if (channel < 0 || channel >= static_cast<s32>(m_channels.size())) {
    return nullptr;
  }

  return &m_channels[static_cast<std::size_t>(channel)];
}

WpadService& wpad_service() { return g_wpadService; }

} // namespace aurora

extern "C" s32 KPADRead(s32 channel, KPADStatus sampling_bufs[], u32 length) {
  if (sampling_bufs == nullptr || length == 0U) {
    return 0;
  }

  sampling_bufs[0] = KPADStatus{};
  const auto* state = aurora::wpad_service().channel_state(channel);
  if (state == nullptr || !state->connected) {
    return 0;
  }

  sampling_bufs[0].hold = state->hold | (state->repeat != 0U ? KPAD_BUTTON_RPT : 0U);
  sampling_bufs[0].trig = state->trigger;
  sampling_bufs[0].release = state->release;
  sampling_bufs[0].acc = KPADVec3{
      .x = state->core_acceleration.x,
      .y = state->core_acceleration.y,
      .z = state->core_acceleration.z,
  };
  sampling_bufs[0].pos = KPADVec2{
      .x = state->pointer.x * 2.0F / state->pointer_width - 1.0F,
      .y = state->pointer.y * 2.0F / state->pointer_height - 1.0F,
  };
  sampling_bufs[0].horizon = {state->pointer.horizon_x, state->pointer.horizon_y};
  if (state->pointer_history_count > 1 && state->pointer.valid && state->pointer_history[1].valid) {
    const auto& previous = state->pointer_history[1];
    sampling_bufs[0].vec = {(state->pointer.x - previous.x) * 2.0F / state->pointer_width,
                            (state->pointer.y - previous.y) * 2.0F / state->pointer_height};
    sampling_bufs[0].speed = std::hypot(sampling_bufs[0].vec.x, sampling_bufs[0].vec.y);
    sampling_bufs[0].hori_vec = {state->pointer.horizon_x - previous.horizon_x, state->pointer.horizon_y - previous.horizon_y};
    sampling_bufs[0].hori_speed = std::hypot(sampling_bufs[0].hori_vec.x, sampling_bufs[0].hori_vec.y);
  }
  sampling_bufs[0].dist = state->distance_to_display;
  sampling_bufs[0].wpad_err = WPAD_ERR_NONE;
  sampling_bufs[0].dpd_valid_fg = state->pointer.valid ? 2 : 0;
  return 1;
}

extern "C" BOOL WPADProbe(s32 channel, u32* type) {
  if (type != nullptr) {
    *type = 0U;
  }
  return aurora::wpad_service().is_connected(channel) ? TRUE : FALSE;
}

extern "C" void WPADDisconnect(s32 channel) { aurora::wpad_service().set_connected(channel, false); }

extern "C" void WPADEnableURCC(BOOL) {}

extern "C" void WPADSetDataFormat(s32, s32) {}

extern "C" void WPADSetVRes(s32 channel, u32 width, u32 height) {
  aurora::wpad_service().set_pointer_resolution(channel, static_cast<float>(width), static_cast<float>(height));
}

extern "C" void WPADSetAutoSamplingBuf(s32, void*, u32) {}

extern "C" void WPADControlMotor(s32 channel, u32 command) {
  if (channel < 0 || channel >= PAD_CHANMAX) {
    return;
  }
  PADControlMotor(static_cast<u32>(channel), command == WPAD_MOTOR_RUMBLE ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
}

extern "C" BOOL WPADSupportsRumble(s32 channel) {
  return channel >= 0 && channel < PAD_CHANMAX ? PADSupportsRumble(static_cast<u32>(channel)) : FALSE;
}

extern "C" void WPADControlSpeaker(s32, s32, void*) {}

extern "C" void WPADStartFastSimpleSync() {}

extern "C" void WPADStopSimpleSync() {}

extern "C" void WPADSetConnectCallback(s32, void*) {}

extern "C" void WPADSetExtensionCallback(s32, void*) {}

extern "C" void KPADSetPosParam(s32 channel, f32 first, f32 second) {
  aurora::wpad_service().set_sampling_parameter(channel, aurora::WpadService::SamplingParameter::Position, first, second);
}

extern "C" void KPADSetHoriParam(s32 channel, f32 first, f32 second) {
  aurora::wpad_service().set_sampling_parameter(channel, aurora::WpadService::SamplingParameter::Horizon, first, second);
}

extern "C" void KPADSetDistParam(s32 channel, f32 first, f32 second) {
  aurora::wpad_service().set_sampling_parameter(channel, aurora::WpadService::SamplingParameter::Distance, first, second);
}

extern "C" void KPADSetAccParam(s32 channel, f32 first, f32 second) {
  aurora::wpad_service().set_sampling_parameter(channel, aurora::WpadService::SamplingParameter::Acceleration, first, second);
}

extern "C" void KPADSetBtnRepeat(s32 channel, f32 delay, f32 pulse) {
  aurora::wpad_service().set_sampling_parameter(channel, aurora::WpadService::SamplingParameter::ButtonRepeat, delay, pulse);
}
extern "C" void KPADSetSensorHeight(s32 channel, f32 height) {
  aurora::wpad_service().set_sensor_height(channel, height);
}
