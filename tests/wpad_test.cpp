#include <aurora/wpad.hpp>
#include <revolution/kpad.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>
#include <type_traits>

namespace {

static_assert(WPAD_ERR_NONE == 0 && WPAD_ERR_NO_CONTROLLER == -1 && WPAD_ERR_BUSY == -2 && WPAD_ERR_TRANSFER == -3);
static_assert(std::is_standard_layout_v<KPADStatus> && std::is_trivially_copyable_v<KPADStatus>);
static_assert(std::is_standard_layout_v<KPADEXStatus> && std::is_trivially_copyable_v<KPADEXStatus>);
static_assert(sizeof(KPADStatus) == 0x84 && alignof(KPADStatus) == 4);
static_assert(sizeof(KPADEXStatus) == 0x24 && alignof(KPADEXStatus) == 4);
static_assert(offsetof(KPADStatus, hold) == 0x00 && offsetof(KPADStatus, trig) == 0x04);
static_assert(offsetof(KPADStatus, release) == 0x08 && offsetof(KPADStatus, acc) == 0x0C);
static_assert(offsetof(KPADStatus, acc_value) == 0x18 && offsetof(KPADStatus, acc_speed) == 0x1C);
static_assert(offsetof(KPADStatus, pos) == 0x20 && offsetof(KPADStatus, vec) == 0x28);
static_assert(offsetof(KPADStatus, speed) == 0x30 && offsetof(KPADStatus, horizon) == 0x34);
static_assert(offsetof(KPADStatus, hori_vec) == 0x3C && offsetof(KPADStatus, hori_speed) == 0x44);
static_assert(offsetof(KPADStatus, dist) == 0x48 && offsetof(KPADStatus, dist_vec) == 0x4C);
static_assert(offsetof(KPADStatus, dist_speed) == 0x50 && offsetof(KPADStatus, acc_vertical) == 0x54);
static_assert(offsetof(KPADStatus, dev_type) == 0x5C && offsetof(KPADStatus, wpad_err) == 0x5D);
static_assert(offsetof(KPADStatus, dpd_valid_fg) == 0x5E && offsetof(KPADStatus, data_format) == 0x5F);
static_assert(offsetof(KPADStatus, ex_status) == 0x60);
static_assert(offsetof(KPADEXStatus, fs.stick) == 0x00 && offsetof(KPADEXStatus, fs.acc) == 0x08);
static_assert(offsetof(KPADEXStatus, fs.acc_value) == 0x14 && offsetof(KPADEXStatus, fs.acc_speed) == 0x18);
static_assert(offsetof(KPADEXStatus, cl.hold) == 0x00 && offsetof(KPADEXStatus, cl.trig) == 0x04);
static_assert(offsetof(KPADEXStatus, cl.release) == 0x08 && offsetof(KPADEXStatus, cl.lstick) == 0x0C);
static_assert(offsetof(KPADEXStatus, cl.rstick) == 0x14 && offsetof(KPADEXStatus, cl.ltrigger) == 0x1C);
static_assert(offsetof(KPADEXStatus, cl.rtrigger) == 0x20);
static_assert(std::is_same_v<decltype(KPADStatus::dev_type), u8>);
static_assert(std::is_same_v<decltype(KPADStatus::wpad_err), s8>);
static_assert(std::is_same_v<decltype(KPADStatus::dpd_valid_fg), s8>);
static_assert(std::is_same_v<decltype(KPADStatus::data_format), u8>);

static_assert(std::is_standard_layout_v<WPADInfo> && std::is_trivially_copyable_v<WPADInfo>);
static_assert(sizeof(WPADInfo) == 0x18 && alignof(WPADInfo) == 4);
static_assert(offsetof(WPADInfo, dpd) == 0 && offsetof(WPADInfo, speaker) == 4);
static_assert(offsetof(WPADInfo, attach) == 8 && offsetof(WPADInfo, lowBat) == 12);
static_assert(offsetof(WPADInfo, nearempty) == 16 && offsetof(WPADInfo, battery) == 20);
static_assert(offsetof(WPADInfo, led) == 21 && offsetof(WPADInfo, protocol) == 22 && offsetof(WPADInfo, firmware) == 23);

class WpadProbeTest : public testing::Test {
protected:
  void SetUp() override { aurora::wpad_service() = {}; }
  void TearDown() override { aurora::wpad_service() = m_saved; }

private:
  aurora::WpadService m_saved = aurora::wpad_service();
};

TEST_F(WpadProbeTest, DisconnectedChannelsReturnNoControllerAndNotFoundType) {
  for (s32 channel = 0; channel < WPAD_MAX_CONTROLLERS; ++channel) {
    u32 type = WPAD_DEV_CORE;
    EXPECT_EQ(WPADProbe(channel, &type), WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);
    EXPECT_EQ(WPADProbe(channel, nullptr), WPAD_ERR_NO_CONTROLLER);
  }
}

TEST_F(WpadProbeTest, ConnectedChannelsReturnZeroAndCoreType) {
  for (s32 channel = 0; channel < WPAD_MAX_CONTROLLERS; ++channel) {
    aurora::wpad_service().set_connected(channel, true);
    u32 type = WPAD_DEV_NOT_FOUND;
    EXPECT_EQ(WPADProbe(channel, &type), WPAD_ERR_NONE);
    EXPECT_EQ(type, WPAD_DEV_CORE);
    EXPECT_EQ(WPADProbe(channel, nullptr), WPAD_ERR_NONE);
  }
}

TEST_F(WpadProbeTest, DisconnectAndNativeInputReconnectionUpdateResult) {
  auto& service = aurora::wpad_service();
  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_A);
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, nullptr), WPAD_ERR_NONE);

  WPADDisconnect(WPAD_CHAN0);
  u32 type = WPAD_DEV_CORE;
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NO_CONTROLLER);
  EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);

  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_B);
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
  EXPECT_EQ(type, WPAD_DEV_CORE);
  EXPECT_TRUE(service.is_button_held(WPAD_CHAN0, WPAD_BUTTON_B));
}

TEST_F(WpadProbeTest, ProbeDoesNotConsumeButtonsPointerOrStick) {
  auto& service = aurora::wpad_service();
  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_A | WPAD_BUTTON_Z);
  service.set_sub_stick(WPAD_CHAN0, 0.75F, -0.5F);
  service.set_pointer(WPAD_CHAN0, 123.0F, 234.0F, true);
  KPADStatus before;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &before, 1), 1);

  for (int read = 0; read < 3; ++read) {
    u32 type = WPAD_DEV_NOT_FOUND;
    EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
    EXPECT_EQ(type, WPAD_DEV_CORE);
  }

  KPADStatus after;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &after, 1), 1);
  EXPECT_EQ(after.hold, before.hold);
  EXPECT_EQ(after.trig, before.trig);
  EXPECT_EQ(after.release, before.release);
  EXPECT_FLOAT_EQ(after.pos.x, before.pos.x);
  EXPECT_FLOAT_EQ(after.pos.y, before.pos.y);
  EXPECT_EQ(after.dpd_valid_fg, before.dpd_valid_fg);
  EXPECT_FLOAT_EQ(service.sub_stick(WPAD_CHAN0).x, 0.75F);
  EXPECT_FLOAT_EQ(service.sub_stick(WPAD_CHAN0).y, -0.5F);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickDown);
}

TEST_F(WpadProbeTest, InvalidNativeChannelsCannotAliasConnectedPorts) {
  aurora::wpad_service().set_connected(WPAD_CHAN0, true);
  for (const s32 channel : {-1, WPAD_MAX_CONTROLLERS}) {
    u32 type = WPAD_DEV_CORE;
    EXPECT_EQ(WPADProbe(channel, &type), WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);
  }
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, nullptr), WPAD_ERR_NONE);
}

TEST_F(WpadProbeTest, CoreCapabilityDoesNotImplicitlyAcquireAnExtension) {
  auto& service = aurora::wpad_service();
  service.set_sub_stick(WPAD_CHAN0, 0.75F, -0.5F);
  service.set_sub_acceleration(WPAD_CHAN0, 3.0F, 4.0F, 0.0F);

  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  u32 type = WPAD_DEV_NOT_FOUND;
  ASSERT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
  EXPECT_EQ(type, WPAD_DEV_CORE);
  EXPECT_EQ(status.dev_type, type);
  EXPECT_EQ(status.data_format, WPAD_FMT_CORE_ACC_DPD);
  EXPECT_EQ(status.wpad_err, WPAD_ERR_NONE);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.y, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc_value, 0.0F);
}

TEST_F(WpadProbeTest, CapabilitySelectionIsPerChannelAndDoesNotConnect) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN1, aurora::WpadDeviceType::Freestyle);
  u32 type = WPAD_DEV_CORE;
  EXPECT_EQ(WPADProbe(WPAD_CHAN1, &type), WPAD_ERR_NO_CONTROLLER);
  EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);

  service.set_connected(WPAD_CHAN0, true);
  service.set_connected(WPAD_CHAN1, true);
  for (const s32 channel : {WPAD_CHAN0, WPAD_CHAN1}) {
    KPADStatus status;
    ASSERT_EQ(KPADRead(channel, &status, 1), 1);
    ASSERT_EQ(WPADProbe(channel, &type), WPAD_ERR_NONE);
    EXPECT_EQ(type, channel == WPAD_CHAN0 ? WPAD_DEV_CORE : WPAD_DEV_FREESTYLE);
    EXPECT_EQ(status.dev_type, type);
  }

  service.clear();
  service.set_connected(WPAD_CHAN1, true);
  EXPECT_EQ(WPADProbe(WPAD_CHAN1, &type), WPAD_ERR_NONE);
  EXPECT_EQ(type, WPAD_DEV_CORE);
}

TEST_F(WpadProbeTest, FreestylePublishesSignedDiagonalAndNeutralStickSamples) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  const KPADVec2 samples[] = {{1.0F, 0.0F}, {-1.0F, 0.0F}, {0.0F, 1.0F}, {0.0F, -1.0F},
                             {0.70710677F, -0.70710677F}, {0.0F, 0.0F}};
  for (const auto& sample : samples) {
    service.begin_frame();
    service.set_sub_stick(WPAD_CHAN0, sample.x, sample.y);
    KPADStatus status;
    ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
    u32 type = WPAD_DEV_NOT_FOUND;
    ASSERT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
    EXPECT_EQ(type, WPAD_DEV_FREESTYLE);
    EXPECT_EQ(status.dev_type, type);
    EXPECT_EQ(status.data_format, WPAD_FMT_FREESTYLE_ACC_DPD);
    EXPECT_EQ(status.wpad_err, WPAD_ERR_NONE);
    EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, sample.x);
    EXPECT_FLOAT_EQ(status.ex_status.fs.stick.y, sample.y);
  }
}

TEST_F(WpadProbeTest, CoreAndFreestyleAccelerationsHaveIndependentMagnitudesAndFrameDeltas) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.begin_frame();
  service.set_core_acceleration(WPAD_CHAN0, 0.0F, 0.0F, -2.0F);
  service.set_sub_acceleration(WPAD_CHAN0, -3.0F, 4.0F, 0.0F);
  for (int read = 0; read < 2; ++read) {
    KPADStatus status;
    ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
    EXPECT_FLOAT_EQ(status.acc.z, -2.0F);
    EXPECT_FLOAT_EQ(status.acc_value, 2.0F);
    EXPECT_FLOAT_EQ(status.acc_speed, 2.0F);
    EXPECT_FLOAT_EQ(status.ex_status.fs.acc.x, -3.0F);
    EXPECT_FLOAT_EQ(status.ex_status.fs.acc.y, 4.0F);
    EXPECT_FLOAT_EQ(status.ex_status.fs.acc.z, 0.0F);
    EXPECT_FLOAT_EQ(status.ex_status.fs.acc_value, 5.0F);
    EXPECT_FLOAT_EQ(status.ex_status.fs.acc_speed, 5.0F);
  }

  service.begin_frame();
  service.set_core_acceleration(WPAD_CHAN0, 0.0F, 0.0F, 2.0F);
  service.set_sub_acceleration(WPAD_CHAN0, 0.0F, 8.0F, 0.0F);
  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_FLOAT_EQ(status.acc_value, 2.0F);
  EXPECT_FLOAT_EQ(status.acc_speed, 4.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc_value, 8.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc_speed, 5.0F);

  service.begin_frame();
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_FLOAT_EQ(status.acc_speed, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc_speed, 0.0F);
}

TEST_F(WpadProbeTest, ExtensionPublicationPreservesPointerAndButtonTransitions) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_A | WPAD_BUTTON_C | WPAD_BUTTON_Z);
  service.set_pointer_resolution(WPAD_CHAN0, 800.0F, 600.0F);
  service.set_pointer(WPAD_CHAN0, 600.0F, 150.0F, true, 0.0F, 1.0F);
  service.set_distance_to_display(WPAD_CHAN0, 1.25F);
  service.set_sub_stick(WPAD_CHAN0, -0.5F, 0.25F);
  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold & KPAD_BUTTON_MASK, WPAD_BUTTON_A | WPAD_BUTTON_C | WPAD_BUTTON_Z);
  EXPECT_EQ(status.trig, WPAD_BUTTON_A | WPAD_BUTTON_C | WPAD_BUTTON_Z);
  EXPECT_FLOAT_EQ(status.pos.x, 0.5F);
  EXPECT_FLOAT_EQ(status.pos.y, -0.5F);
  EXPECT_FLOAT_EQ(status.horizon.x, 0.0F);
  EXPECT_FLOAT_EQ(status.horizon.y, 1.0F);
  EXPECT_FLOAT_EQ(status.dist, 1.25F);
  EXPECT_EQ(status.dpd_valid_fg, 2);

  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, 0U);
  service.set_pointer(WPAD_CHAN0, 400.0F, 300.0F, true);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, 0U);
  EXPECT_EQ(status.trig, 0U);
  EXPECT_EQ(status.release, WPAD_BUTTON_A | WPAD_BUTTON_C | WPAD_BUTTON_Z);
  EXPECT_FLOAT_EQ(status.vec.x, -0.5F);
  EXPECT_FLOAT_EQ(status.vec.y, 0.5F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, -0.5F);
}

TEST_F(WpadProbeTest, RemovingExtensionImmediatelyChangesProbeAndSampleMetadata) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.set_sub_stick(WPAD_CHAN0, 0.5F, 0.75F);
  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  ASSERT_EQ(status.dev_type, WPAD_DEV_FREESTYLE);

  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Core);
  u32 type = WPAD_DEV_NOT_FOUND;
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
  EXPECT_EQ(type, WPAD_DEV_CORE);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.dev_type, type);
  EXPECT_EQ(status.data_format, WPAD_FMT_CORE_ACC_DPD);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.y, 0.0F);
}

TEST_F(WpadProbeTest, CoreFiltersExtensionButtonEdgesWithoutChangingNativeState) {
  auto& service = aurora::wpad_service();
  constexpr u32 buttons = WPAD_BUTTON_A | WPAD_BUTTON_C | WPAD_BUTTON_Z;
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, buttons);
  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold & KPAD_BUTTON_MASK, buttons);
  EXPECT_EQ(status.trig, buttons);

  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Core);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, WPAD_BUTTON_A | KPAD_BUTTON_RPT);
  EXPECT_EQ(status.trig, WPAD_BUTTON_A);
  EXPECT_EQ(status.release, 0U);
  EXPECT_EQ(service.channel_state(WPAD_CHAN0)->hold, buttons);
  EXPECT_EQ(service.channel_state(WPAD_CHAN0)->trigger, buttons);

  service.begin_frame();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.set_button_mask(WPAD_CHAN0, buttons);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, buttons);
  EXPECT_EQ(status.trig, 0U);
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Core);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, WPAD_BUTTON_A);
  EXPECT_EQ(status.trig, 0U);
  EXPECT_EQ(service.channel_state(WPAD_CHAN0)->hold, buttons);

  service.begin_frame();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.set_button_mask(WPAD_CHAN0, 0U);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.release, buttons);
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Core);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, 0U);
  EXPECT_EQ(status.trig, 0U);
  EXPECT_EQ(status.release, WPAD_BUTTON_A);
  EXPECT_EQ(service.channel_state(WPAD_CHAN0)->release, buttons);

  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.release, buttons);
}

TEST_F(WpadProbeTest, ExtensionOnlyRepeatCannotAppearOnCore) {
  auto& service = aurora::wpad_service();
  constexpr u32 buttons = WPAD_BUTTON_C | WPAD_BUTTON_Z;
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, buttons);
  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, buttons | KPAD_BUTTON_RPT);
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Core);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.hold, 0U);
  EXPECT_EQ(status.trig, 0U);
  EXPECT_EQ(service.channel_state(WPAD_CHAN0)->repeat, buttons);

  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, 0U);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.release, 0U);
  EXPECT_EQ(service.channel_state(WPAD_CHAN0)->release, buttons);
}

TEST_F(WpadProbeTest, DisconnectRetainsConfigurationButCannotRepublishStaleSamples) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.set_sub_stick(WPAD_CHAN0, 1.0F, -1.0F);
  service.set_core_acceleration(WPAD_CHAN0, 1.0F, 2.0F, 3.0F);
  service.set_sub_acceleration(WPAD_CHAN0, 4.0F, 5.0F, 6.0F);
  KPADStatus status;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);

  WPADDisconnect(WPAD_CHAN0);
  u32 type = WPAD_DEV_CORE;
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NO_CONTROLLER);
  EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);
  EXPECT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 0);
  EXPECT_EQ(status.wpad_err, WPAD_ERR_NO_CONTROLLER);
  EXPECT_EQ(status.dev_type, type);

  service.set_connected(WPAD_CHAN0, true);
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
  EXPECT_EQ(type, WPAD_DEV_FREESTYLE);
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &status, 1), 1);
  EXPECT_EQ(status.dev_type, type);
  EXPECT_FLOAT_EQ(status.acc_value, 0.0F);
  EXPECT_FLOAT_EQ(status.acc_speed, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.x, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.stick.y, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc_value, 0.0F);
  EXPECT_FLOAT_EQ(status.ex_status.fs.acc_speed, 0.0F);
}

TEST_F(WpadProbeTest, BoundedReadsDoNotOverwriteFollowingRecords) {
  auto& service = aurora::wpad_service();
  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.set_sub_stick(WPAD_CHAN0, 0.25F, -0.75F);
  KPADStatus samples[2];
  samples[1].hold = 0xDEADBEEFU;
  samples[1].ex_status.fs.acc_speed = 123.0F;
  EXPECT_EQ(KPADRead(WPAD_CHAN0, samples, 2), 1);
  EXPECT_EQ(samples[1].hold, 0xDEADBEEFU);
  EXPECT_FLOAT_EQ(samples[1].ex_status.fs.acc_speed, 123.0F);
  EXPECT_EQ(KPADRead(WPAD_CHAN0, samples, 0), 0);
  EXPECT_FLOAT_EQ(samples[0].ex_status.fs.stick.x, 0.25F);
  EXPECT_EQ(KPADRead(WPAD_CHAN0, nullptr, 1), 0);
  for (const s32 channel : {-1, WPAD_MAX_CONTROLLERS}) {
    EXPECT_EQ(KPADRead(channel, samples, 2), 0);
    EXPECT_EQ(samples[0].dev_type, WPAD_DEV_NOT_FOUND);
    EXPECT_EQ(samples[0].wpad_err, WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(samples[1].hold, 0xDEADBEEFU);
  }
}

TEST(WpadService, SubStickUsesRetailDirectionalThreshold) {
  auto service = aurora::WpadService{};
  service.set_connected(WPAD_CHAN0, true);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 0.2F, -0.2F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);

  service.set_sub_stick(WPAD_CHAN0, 0.2001F, -0.2001F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickNone);
}

TEST(WpadService, SubStickTriggerAndReleaseArePreviousFrameEdges) {
  auto service = aurora::WpadService{};
  service.set_connected(WPAD_CHAN0, true);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 0.75F, 0.5F);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickUp);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 1.0F, 0.25F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickUp);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickNone);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, -0.75F, -0.5F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickLeft | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickLeft | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickUp);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 0.0F, 0.0F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickLeft | aurora::WpadStickDown);
}

TEST(WpadService, DisconnectionClearsSubStickAndDirectionalEdges) {
  auto service = aurora::WpadService{};
  service.set_connected(WPAD_CHAN0, true);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 1.0F, -1.0F);
  service.set_connected(WPAD_CHAN0, false);

  EXPECT_EQ(service.sub_stick(WPAD_CHAN0).x, 0.0F);
  EXPECT_EQ(service.sub_stick(WPAD_CHAN0).y, 0.0F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickNone);
}


struct WpadCallbackEvent {
  enum Kind { Connect, Extension, Info };
  Kind kind;
  s32 channel;
  s32 result;
  bool operator==(const WpadCallbackEvent&) const = default;
};

class WpadCallbackTest : public WpadProbeTest {
protected:
  void SetUp() override {
    WpadProbeTest::SetUp();
    s_current = this;
  }
  void TearDown() override {
    s_current = nullptr;
    WpadProbeTest::TearDown();
  }
  static void connect(s32 channel, s32 result) {
    s_current->events.push_back({WpadCallbackEvent::Connect, channel, result});
    if (result == WPAD_ERR_NONE) {
      EXPECT_EQ(KPADRead(channel, &s_current->callback_sample, 1), 1);
      WPADSetExtensionCallback(channel, extension);
    }
  }
  static void extension(s32 channel, s32 result) {
    s_current->events.push_back({WpadCallbackEvent::Extension, channel, result});
  }
  static void info(s32 channel, s32 result) {
    s_current->events.push_back({WpadCallbackEvent::Info, channel, result});
  }
  static void requeue_info(s32 channel, s32 result) {
    info(channel, result);
    EXPECT_EQ(WPADGetInfoAsync(channel, s_current->next_info, info), WPAD_ERR_NONE);
  }
  static void disconnect_on_connect(s32 channel, s32 result) {
    connect(channel, result);
    if (result == WPAD_ERR_NONE) WPADDisconnect(channel);
  }
  static void retire_client(WpadCallbackEvent::Kind kind, s32 channel, s32 result) {
    s_current->events.push_back({kind, channel, result});
    aurora::wpad_service().exchange_client({});
    s_current->retired_info.reset();
  }
  static void retire_on_connect(s32 channel, s32 result) { retire_client(WpadCallbackEvent::Connect, channel, result); }
  static void retire_on_extension(s32 channel, s32 result) { retire_client(WpadCallbackEvent::Extension, channel, result); }
  static void retire_on_info(s32 channel, s32 result) { retire_client(WpadCallbackEvent::Info, channel, result); }
  static void temporary_client_on_connect(s32 channel, s32 result) {
    connect(channel, result);
    WPADInfo temporary{};
    const aurora::WpadClientScope nested_client;
    EXPECT_EQ(WPADGetInfoAsync(channel, &temporary, info), WPAD_ERR_NONE);
  }
  static std::array<unsigned char, sizeof(WPADInfo)> bytes(const WPADInfo& value) {
    std::array<unsigned char, sizeof(WPADInfo)> result;
    std::memcpy(result.data(), &value, result.size());
    return result;
  }
  std::vector<WpadCallbackEvent> events;
  KPADStatus callback_sample;
  WPADInfo* next_info = nullptr;
  std::unique_ptr<WPADInfo> retired_info;

private:
  static inline WpadCallbackTest* s_current = nullptr;
};

TEST_F(WpadCallbackTest, ConnectionAndExtensionCallbacksObservePublishedInputAndOnlyTransitions) {
  auto& service = aurora::wpad_service();
  EXPECT_EQ(WPADSetConnectCallback(WPAD_CHAN0, connect), nullptr);
  EXPECT_TRUE(events.empty());
  service.dispatch_callbacks();
  EXPECT_TRUE(events.empty());

  service.set_device_type(WPAD_CHAN0, aurora::WpadDeviceType::Freestyle);
  service.set_sub_stick(WPAD_CHAN0, -0.75F, 0.5F);
  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_A);
  EXPECT_TRUE(events.empty());
  service.dispatch_callbacks();
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Connect, 0, WPAD_ERR_NONE},
                                                  {WpadCallbackEvent::Extension, 0, WPAD_DEV_FREESTYLE}}));
  EXPECT_EQ(callback_sample.hold & ~KPAD_BUTTON_RPT, WPAD_BUTTON_A);
  EXPECT_FLOAT_EQ(callback_sample.ex_status.fs.stick.x, -0.75F);
  EXPECT_FLOAT_EQ(callback_sample.ex_status.fs.stick.y, 0.5F);
  service.dispatch_callbacks();
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(WPADSetConnectCallback(0, connect), connect);
  EXPECT_EQ(WPADSetExtensionCallback(0, extension), extension);
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 2U);

  service.set_device_type(0, aurora::WpadDeviceType::Core);
  service.dispatch_callbacks();
  EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Extension, 0, WPAD_DEV_CORE}));
  WPADDisconnect(0);
  ASSERT_EQ(events.size(), 3U);
  service.dispatch_callbacks();
  EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Connect, 0, WPAD_ERR_NO_CONTROLLER}));
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 4U);
  service.set_device_type(0, aurora::WpadDeviceType::Freestyle);
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 4U);
  service.set_connected(0, true);
  service.dispatch_callbacks();
  ASSERT_EQ(events.size(), 6U);
  EXPECT_EQ(events[4], (WpadCallbackEvent{WpadCallbackEvent::Connect, 0, WPAD_ERR_NONE}));
  EXPECT_EQ(events[5], (WpadCallbackEvent{WpadCallbackEvent::Extension, 0, WPAD_DEV_FREESTYLE}));
}

TEST_F(WpadCallbackTest, ConnectCallbackCanDisconnectBeforeExtensionPublication) {
  auto& service = aurora::wpad_service();
  WPADSetConnectCallback(0, disconnect_on_connect);
  service.set_connected(0, true);
  service.dispatch_callbacks();
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Connect, 0, WPAD_ERR_NONE}}));
  EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NO_CONTROLLER);
  service.dispatch_callbacks();
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Connect, 0, WPAD_ERR_NO_CONTROLLER}));
}

TEST_F(WpadCallbackTest, InfoRequestsAreDeferredAndBoundedWithImmediateBusyRejection) {
  auto& service = aurora::wpad_service();
  service.set_device_type(1, aurora::WpadDeviceType::Freestyle);
  service.set_connected(1, true);
  struct GuardedInfo { u32 before; WPADInfo value; u32 after; } record{0x12345678U, {}, 0x89ABCDEFU};
  std::memset(&record.value, 0xA5, sizeof(record.value));
  const auto untouched = bytes(record.value);
  WPADInfo rejected{};
  EXPECT_EQ(WPADGetInfoAsync(1, &record.value, info), WPAD_ERR_NONE);
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(bytes(record.value), untouched);
  EXPECT_EQ(WPADGetInfoAsync(1, &rejected, info), WPAD_ERR_BUSY);
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Info, 1, WPAD_ERR_BUSY}}));
  EXPECT_EQ(bytes(record.value), untouched);
  service.dispatch_callbacks();
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Info, 1, WPAD_ERR_NONE}));
  EXPECT_EQ(record.before, 0x12345678U);
  EXPECT_EQ(record.after, 0x89ABCDEFU);
  EXPECT_EQ(record.value.dpd, TRUE);
  EXPECT_EQ(record.value.speaker, FALSE);
  EXPECT_EQ(record.value.attach, TRUE);
  EXPECT_EQ(record.value.lowBat, FALSE);
  EXPECT_EQ(record.value.nearempty, FALSE);
  EXPECT_EQ(record.value.battery, 4);
  EXPECT_EQ(record.value.led, 2);
  EXPECT_EQ(record.value.protocol, 0);
  EXPECT_EQ(record.value.firmware, 0);
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 2U);
  EXPECT_EQ(rejected.battery, 0);

  service.set_device_type(1, aurora::WpadDeviceType::Core);
  EXPECT_EQ(WPADGetInfoAsync(1, &record.value, nullptr), WPAD_ERR_NONE);
  service.dispatch_callbacks();
  EXPECT_EQ(record.value.attach, FALSE);
  EXPECT_EQ(record.value.battery, 4);
  EXPECT_EQ(events.size(), 2U);
}

TEST_F(WpadCallbackTest, InfoRequestsRemainIndependentAndDisconnectDoesNotWriteDestination) {
  auto& service = aurora::wpad_service();
  service.set_connected(0, true);
  service.set_connected(1, true);
  WPADInfo disconnected;
  WPADInfo connected{};
  std::memset(&disconnected, 0x5A, sizeof(disconnected));
  const auto untouched = bytes(disconnected);
  EXPECT_EQ(WPADGetInfoAsync(0, &disconnected, info), WPAD_ERR_NONE);
  EXPECT_EQ(WPADGetInfoAsync(1, &connected, info), WPAD_ERR_NONE);
  WPADDisconnect(0);
  service.dispatch_callbacks();
  EXPECT_EQ(bytes(disconnected), untouched);
  EXPECT_EQ(connected.battery, 4);
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Info, 0, WPAD_ERR_NO_CONTROLLER},
                                                  {WpadCallbackEvent::Info, 1, WPAD_ERR_NONE}}));
  service.set_connected(0, true);
  EXPECT_EQ(WPADGetInfoAsync(0, &disconnected, info), WPAD_ERR_NONE);
  service.dispatch_callbacks();
  EXPECT_EQ(disconnected.battery, 4);
}

TEST_F(WpadCallbackTest, InfoCompletionCanQueueTheNextRequestForALaterDispatch) {
  auto& service = aurora::wpad_service();
  service.set_connected(0, true);
  WPADInfo first{};
  WPADInfo second{};
  next_info = &second;
  ASSERT_EQ(WPADGetInfoAsync(0, &first, requeue_info), WPAD_ERR_NONE);
  service.dispatch_callbacks();
  EXPECT_EQ(first.battery, 4);
  EXPECT_EQ(second.battery, 0);
  EXPECT_EQ(events.size(), 1U);
  service.dispatch_callbacks();
  EXPECT_EQ(second.battery, 4);
  EXPECT_EQ(events.size(), 2U);
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 2U);
}

TEST_F(WpadCallbackTest, ScopedClientSuspendsPendingWritesAndRestoresCallbacksWithoutRetiredDestinations) {
  auto& service = aurora::wpad_service();
  WPADSetConnectCallback(0, connect);
  service.set_connected(0, true);
  service.dispatch_callbacks();
  events.clear();
  WPADInfo outer{};
  ASSERT_EQ(WPADGetInfoAsync(0, &outer, info), WPAD_ERR_NONE);
  {
    const aurora::WpadClientScope inner_client;
    EXPECT_EQ(WPADSetConnectCallback(0, connect), nullptr);
    EXPECT_EQ(WPADSetExtensionCallback(0, extension), nullptr);
    service.dispatch_callbacks();
    EXPECT_EQ(outer.battery, 0);
    EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Connect, 0, WPAD_ERR_NONE},
                                                    {WpadCallbackEvent::Extension, 0, WPAD_DEV_CORE}}));
    auto retired = std::make_unique<WPADInfo>();
    ASSERT_EQ(WPADGetInfoAsync(0, retired.get(), info), WPAD_ERR_NONE);
  }
  events.clear();
  EXPECT_EQ(WPADSetConnectCallback(0, connect), connect);
  EXPECT_EQ(WPADSetExtensionCallback(0, extension), extension);
  service.dispatch_callbacks();
  EXPECT_EQ(outer.battery, 4);
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Info, 0, WPAD_ERR_NONE}}));
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 1U);
}


TEST_F(WpadCallbackTest, CallbackClientRetirementCannotWriteThroughRetiredRequests) {
  auto& service = aurora::wpad_service();
  for (const auto point : {WpadCallbackEvent::Connect, WpadCallbackEvent::Extension, WpadCallbackEvent::Info}) {
    SCOPED_TRACE(static_cast<int>(point));
    service = {};
    events.clear();
    service.set_connected(0, true);
    retired_info = std::make_unique<WPADInfo>();
    WPADInfo first{};
    if (point == WpadCallbackEvent::Info) {
      service.set_connected(1, true);
      ASSERT_EQ(WPADGetInfoAsync(0, &first, retire_on_info), WPAD_ERR_NONE);
      ASSERT_EQ(WPADGetInfoAsync(1, retired_info.get(), info), WPAD_ERR_NONE);
    } else {
      if (point == WpadCallbackEvent::Connect) WPADSetConnectCallback(0, retire_on_connect);
      else WPADSetExtensionCallback(0, retire_on_extension);
      ASSERT_EQ(WPADGetInfoAsync(0, retired_info.get(), info), WPAD_ERR_NONE);
    }
    service.dispatch_callbacks();
    EXPECT_EQ(retired_info, nullptr);
    EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{point, 0, WPAD_ERR_NONE}}));
    service.dispatch_callbacks();
    EXPECT_EQ(events.size(), 1U);
    if (point == WpadCallbackEvent::Info) EXPECT_EQ(first.battery, 4);
  }
}

TEST_F(WpadCallbackTest, TemporaryClientInsideCallbackDefersRestoredRequestToNextDispatch) {
  auto& service = aurora::wpad_service();
  service.set_connected(0, true);
  WPADSetConnectCallback(0, temporary_client_on_connect);
  WPADInfo original{};
  ASSERT_EQ(WPADGetInfoAsync(0, &original, info), WPAD_ERR_NONE);
  service.dispatch_callbacks();
  EXPECT_EQ(original.battery, 0);
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Connect, 0, WPAD_ERR_NONE}}));
  service.dispatch_callbacks();
  EXPECT_EQ(original.battery, 4);
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Connect, 0, WPAD_ERR_NONE},
                                                  {WpadCallbackEvent::Extension, 0, WPAD_DEV_CORE},
                                                  {WpadCallbackEvent::Info, 0, WPAD_ERR_NONE}}));
  service.dispatch_callbacks();
  EXPECT_EQ(events.size(), 3U);
}

TEST_F(WpadCallbackTest, InvalidRequestsReturnErrorsWithoutAliasingAnActiveChannel) {
  auto& service = aurora::wpad_service();
  service.set_connected(0, true);
  WPADSetConnectCallback(0, connect);
  WPADSetExtensionCallback(0, extension);
  WPADInfo record{};
  for (const s32 channel : {-1, WPAD_MAX_CONTROLLERS}) {
    EXPECT_EQ(WPADSetConnectCallback(channel, connect), nullptr);
    EXPECT_EQ(WPADSetExtensionCallback(channel, extension), nullptr);
    EXPECT_EQ(WPADGetInfoAsync(channel, &record, info), WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Info, channel, WPAD_ERR_NO_CONTROLLER}));
  }
  EXPECT_EQ(WPADGetInfoAsync(1, &record, info), WPAD_ERR_NO_CONTROLLER);
  EXPECT_EQ(WPADGetInfoAsync(0, nullptr, info), WPAD_ERR_INVALID);
  EXPECT_EQ(events.size(), 4U);
  EXPECT_EQ(record.battery, 0);
  EXPECT_EQ(WPADSetConnectCallback(0, connect), connect);
  EXPECT_EQ(WPADSetExtensionCallback(0, extension), extension);
  ASSERT_EQ(WPADGetInfoAsync(0, &record, info), WPAD_ERR_NONE);
  service.dispatch_callbacks();
  EXPECT_EQ(record.battery, 4);
}

TEST_F(WpadCallbackTest, SpeakerCapabilityErrorsCompleteSynchronouslyAndDisableSucceeds) {
  auto& service = aurora::wpad_service();
  service.set_connected(0, true);
  for (const u32 command : {1U, 2U, 0xFFFFFFFFU}) {
    const auto before = events.size();
    EXPECT_EQ(WPADControlSpeaker(0, command, info), WPAD_ERR_INVALID);
    ASSERT_EQ(events.size(), before + 1U);
    EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Info, 0, WPAD_ERR_INVALID}));
  }
  EXPECT_EQ(WPADControlSpeaker(0, 0, info), WPAD_ERR_NONE);
  EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Info, 0, WPAD_ERR_NONE}));
  const auto before = events.size();
  EXPECT_EQ(WPADControlSpeaker(0, 0, nullptr), WPAD_ERR_NONE);
  EXPECT_EQ(events.size(), before);
  for (const s32 channel : {-1, 1, WPAD_MAX_CONTROLLERS}) {
    EXPECT_EQ(WPADControlSpeaker(channel, 0, info), WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Info, channel, WPAD_ERR_NO_CONTROLLER}));
  }
}

TEST_F(WpadCallbackTest, SamplingResetPreservesDeviceConfigurationAndPendingClientCallbacks) {
  auto& service = aurora::wpad_service();
  WPADSetConnectCallback(0, connect);
  service.set_device_type(0, aurora::WpadDeviceType::Freestyle);
  service.set_button_mask(0, WPAD_BUTTON_A);
  service.set_sub_stick(0, 0.5F, -0.5F);
  service.set_core_acceleration(0, 1.0F, 2.0F, 3.0F);
  service.set_sub_acceleration(0, 4.0F, 5.0F, 6.0F);
  service.set_pointer(0, 100.0F, 200.0F, true);
  service.begin_frame();
  service.set_pointer(0, 110.0F, 210.0F, true);
  KPADSetBtnRepeat(0, 0.25F, 0.125F);
  KPADSetPosParam(0, 2.0F, 3.0F);
  KPADSetSensorHeight(0, 0.5F);
  service.dispatch_callbacks();
  events.clear();
  WPADInfo record{};
  ASSERT_EQ(WPADGetInfoAsync(0, &record, info), WPAD_ERR_NONE);
  ASSERT_GT(service.pointer_history_count(0), 0U);
  KPADReset();
  EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NONE);
  EXPECT_EQ(service.pointer_history_count(0), 0U);
  const auto* state = service.channel_state(0);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->hold_frame_count, 0U);
  EXPECT_EQ(state->repeat, 0U);
  EXPECT_EQ(state->position_parameters, (std::array{2.0F, 3.0F}));
  EXPECT_EQ(state->button_repeat_parameters, (std::array{0.25F, 0.125F}));
  EXPECT_FLOAT_EQ(state->sensor_height, 0.5F);
  KPADStatus sample;
  ASSERT_EQ(KPADRead(0, &sample, 1), 1);
  EXPECT_EQ(sample.hold, WPAD_BUTTON_A);
  EXPECT_EQ(sample.trig, WPAD_BUTTON_A);
  EXPECT_EQ(sample.release, 0U);
  EXPECT_EQ(sample.dev_type, WPAD_DEV_FREESTYLE);
  EXPECT_FLOAT_EQ(sample.ex_status.fs.stick.x, 0.5F);
  EXPECT_FLOAT_EQ(sample.acc_speed, 0.0F);
  EXPECT_FLOAT_EQ(sample.ex_status.fs.acc_speed, 0.0F);
  service.dispatch_callbacks();
  EXPECT_EQ(record.battery, 4);
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Info, 0, WPAD_ERR_NONE}}));
  service.set_device_type(0, aurora::WpadDeviceType::Core);
  service.dispatch_callbacks();
  EXPECT_EQ(events.back(), (WpadCallbackEvent{WpadCallbackEvent::Extension, 0, WPAD_DEV_CORE}));
}

TEST_F(WpadCallbackTest, SamplingInitializationRestoresParametersWithoutDisconnectingClient) {
  auto& service = aurora::wpad_service();
  WPADSetConnectCallback(0, connect);
  service.set_button_mask(0, WPAD_BUTTON_B);
  service.set_pointer(0, 50.0F, 80.0F, true);
  service.dispatch_callbacks();
  events.clear();
  KPADSetPosParam(0, 2.0F, 3.0F);
  KPADSetHoriParam(0, 4.0F, 5.0F);
  KPADSetDistParam(0, 6.0F, 7.0F);
  KPADSetAccParam(0, 8.0F, 9.0F);
  KPADSetBtnRepeat(0, 0.5F, 0.25F);
  KPADSetSensorHeight(0, 0.75F);
  KPADInit();
  EXPECT_EQ(WPADProbe(0, nullptr), WPAD_ERR_NONE);
  EXPECT_EQ(service.pointer_history_count(0), 0U);
  const auto* state = service.channel_state(0);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->position_parameters, (std::array{0.0F, 1.0F}));
  EXPECT_EQ(state->horizon_parameters, (std::array{0.0F, 1.0F}));
  EXPECT_EQ(state->distance_parameters, (std::array{0.0F, 1.0F}));
  EXPECT_EQ(state->acceleration_parameters, (std::array{0.0F, 1.0F}));
  EXPECT_EQ(state->button_repeat_parameters, (std::array{0.0F, 0.0F}));
  EXPECT_FLOAT_EQ(state->sensor_height, 0.0F);
  EXPECT_EQ(WPADSetConnectCallback(0, connect), connect);
  service.dispatch_callbacks();
  EXPECT_TRUE(events.empty());
  WPADDisconnect(0);
  service.dispatch_callbacks();
  EXPECT_EQ(events, (std::vector<WpadCallbackEvent>{{WpadCallbackEvent::Connect, 0, WPAD_ERR_NO_CONTROLLER}}));
}

TEST_F(WpadProbeTest, SensorBarAndAutoSleepConfigurationRemainExplicitAcrossSamplingReset) {
  auto& service = aurora::wpad_service();
  EXPECT_EQ(WPADGetSensorBarPosition(), WPAD_SENSOR_BAR_POS_TOP);
  service.set_sensor_bar_position(WPAD_SENSOR_BAR_POS_BOTTOM);
  EXPECT_EQ(WPADGetSensorBarPosition(), WPAD_SENSOR_BAR_POS_BOTTOM);
  service.set_sensor_bar_position(0xFF);
  EXPECT_EQ(WPADGetSensorBarPosition(), WPAD_SENSOR_BAR_POS_BOTTOM);
  WPADSetAutoSleepTime(15);
  EXPECT_EQ(service.auto_sleep_time(), 15);
  KPADReset();
  KPADInit();
  EXPECT_EQ(WPADGetSensorBarPosition(), WPAD_SENSOR_BAR_POS_BOTTOM);
  EXPECT_EQ(service.auto_sleep_time(), 15);
}

} // namespace
