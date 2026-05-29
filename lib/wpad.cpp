#include <aurora/wpad.hpp>

#include <algorithm>

namespace aurora {
namespace {
WpadService g_wpadService;
} // namespace

void WpadService::clear() { m_channels = {}; }

void WpadService::begin_frame() {
  for (auto& channel : m_channels) {
    channel.previous_hold = channel.hold;
    channel.previous_core_swing = channel.core_swing;
    channel.previous_sub_swing = channel.sub_swing;
    channel.trigger = 0U;
    channel.release = 0U;
    channel.repeat = 0U;
    if (channel.hold != 0U) {
      ++channel.hold_frame_count;
      if (channel.hold_frame_count == 1U || (channel.hold_frame_count > 30U && (channel.hold_frame_count % 8U) == 0U)) {
        channel.repeat = channel.hold;
      }
    } else {
      channel.hold_frame_count = 0U;
    }
    if (!channel.connected) {
      channel.hold = 0U;
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

void WpadService::set_pointer(s32 channel, float x, float y, bool valid) {
  auto* state = mutable_channel_state(channel);
  if (state == nullptr) {
    return;
  }

  state->connected = true;
  state->pointer = WpadPointerState{
      .x = x,
      .y = y,
      .valid = valid,
  };
  for (auto i = state->pointer_history.size() - 1U; i > 0U; --i) {
    state->pointer_history[i] = state->pointer_history[i - 1U];
  }
  state->pointer_history[0U] = state->pointer;
  state->pointer_history_count = std::min<std::uint32_t>(static_cast<std::uint32_t>(state->pointer_history.size()),
                                                         state->pointer_history_count + 1U);
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
  return state == nullptr ? WpadStickState{} : state->sub_stick;
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
      .x = state->pointer.x,
      .y = state->pointer.y,
  };
  sampling_bufs[0].dist = state->distance_to_display;
  sampling_bufs[0].wpad_err = WPAD_ERR_NONE;
  sampling_bufs[0].dpd_valid_fg = state->pointer.valid ? 1 : 0;
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

extern "C" void WPADSetVRes(s32, u32, u32) {}

extern "C" void WPADSetAutoSamplingBuf(s32, void*, u32) {}

extern "C" void WPADControlSpeaker(s32, s32, void*) {}

extern "C" void WPADStartFastSimpleSync() {}

extern "C" void WPADStopSimpleSync() {}

extern "C" void WPADSetConnectCallback(s32, void*) {}

extern "C" void WPADSetExtensionCallback(s32, void*) {}
