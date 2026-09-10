#include <aurora/wpad.hpp>
#include <revolution/kpad.h>

#include <gtest/gtest.h>

#include <cstddef>
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

class WpadProbeTest : public testing::Test {
protected:
  void SetUp() override { aurora::wpad_service().clear(); }
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

} // namespace
